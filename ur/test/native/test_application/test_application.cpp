// Testes da camada de aplicacao: app::Application, o ciclo de 50 ms que decide se o rele atua.
//
// POR QUE ESTA SUITE EXISTE. Ate a etapa 8 as 18 suites cobriam angulo, regra de limite,
// avaliador, escala analogica, filtro, parametros, editor, senha, as cinco telas e os oito
// fakes - e paravam exatamente na classe que comanda os quatro reles e as duas saidas. Todo o
// comportamento de seguranca da etapa 8 (maquina de saude do enlace com 3/5/2000 ms, retencao
// de ordem zero, recarga por queda do heartbeat, override de calibracao por eixo, latch de
// config perdida, fallback signalAll quando applyMask reprova, guarda dura de idade, batimento
// do watchdog por ultimo) estava sem rede. Os dois defeitos criticos da revisao adversarial
// passaram por ai: nenhum deles quebra o build e nenhum teste os tocava.
//
// O QUE ESTA PRESO AQUI, com a fonte de cada numero:
//  - DECISIONS.md 2.1 item 3: 3 transacoes invalidas consecutivas (150 ms) declaram falha; 5
//    boas consecutivas MAIS permanencia minima de 2000 ms saem dela; idade maxima do dado que
//    comanda rele = 50 + 21,3 = 71,3 ms, arredondada para kDataMaxAgeMs = 72 ms.
//  - decisao 4 item 3 / REQ-MEA-02: registrador 3 igual a kStsAcceptedExact EXATO. Quadro
//    integro com status reprovado e SensorFault, nao CommFault - sao defeitos diferentes e a
//    tela do operador diz coisas diferentes.
//  - A4 opcao A: angulo fora de +/-90,0 graus e amostra invalida.
//  - decisao 4 item 9: recarga do filtro por queda do registrador 7 (reinicio da sensora).
//  - A5 opcao A: na falha os QUATRO reles vao a alarme, inclusive os programados em Off.
//  - decisao 6 itens 2 e 7: o override da Auto Calibracao vale para UM eixo; o outro continua
//    rastreando o angulo real e os quatro reles seguem vivos durante todo o procedimento.
//  - decisao 5 item 30: guarda dura de idade de 250 ms, independente do contador de falhas.
//  - decisao 2 item 16: atraso de escalonamento ate 500 ms e tick perdido tolerado; acima
//    disso a falha de enlace e declarada na hora.
//  - 2.4: o codigo de falha 3932 do boot ate o primeiro quadro valido e em toda falha.
//
// O tempo vem do FakeClock canonico, que comeca em 0xFFFF0000: todo prazo desta suite
// atravessa o wrap de 2^32 ms, entao um prazo escrito como "a > b" em vez da subtracao
// unsigned de ports/i_clock.h reprova aqui.
#include <unity.h>

#include "app/application.h"
#include "fakes/fake_analog_output.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_relay_bank.h"
#include "fakes/fake_sensor_link.h"

using app::Application;
using app::LinkHealth;
using domain::Angle;
using domain::Axis;
using test::FakeAnalogOutput;
using test::FakeClock;
using test::FakeRelayBank;
using test::FakeSensorLink;

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr uint16_t kFaultCode = FakeAnalogOutput::kFaultCode;
constexpr int16_t kQuietDeci = 10;   // 1,0 grau: abaixo dos 5,0 graus da Tabela 2
constexpr int16_t kAlarmDeci = 200;  // 20,0 graus: acima dos 5,0 graus da Tabela 2
constexpr int16_t kStepDeci = 25;    // 2,5 graus: degrau de 1,5 grau sobre kQuietDeci, abaixo
                                     // da recarga por salto de 2,0 graus do proprio filtro

// Tabela 2: so os Limites 1 (X1) e 3 (Y1) tem operacao; 2 e 4 nascem em Off. Um angulo alem
// dos 5,0 graus programados sinaliza estes dois canais, e nao os quatro - os quatro so vao
// juntos por FALHA (A5), nunca por angulo.
constexpr RelayMask kMaskAngularX1Y1 = 0x05;

// Banco de reles em que applyMask() pode reprovar SEM que signalAll() reprove junto. O
// FakeRelayBank canonico so reprova por ready_ == false, e nesse estado os DOIS caminhos caem -
// util para o latch de banco morto, inutil para provar o fallback. Aqui a recusa e so do
// applyMask(), que e o caminho ordinario.
class PickyRelayBank : public FakeRelayBank {
public:
    explicit PickyRelayBank(const IClock& clock) : FakeRelayBank(clock, true), failMask_(false) {}

    Status applyMask(RelayMask wanted) override {
        if (failMask_) {
            return Status(Err::Io);
        }
        return FakeRelayBank::applyMask(wanted);
    }

    // signalAll() do fake canonico chama applyMask() por despacho virtual e cairia na recusa
    // acima; aqui ele vai direto ao caminho que funciona, que e o do adaptador real.
    Status signalAll() override { return FakeRelayBank::applyMask(kRelayMaskAllSignalled); }

    void failApplyMask(bool fail) { failMask_ = fail; }

private:
    bool failMask_;
};

// Watchdog espiao: conta batimentos e guarda quantas escritas de rele e de DAC ja tinham
// acontecido NO INSTANTE em que o token de liveness foi renovado. E assim que se prova que o
// batimento e a ULTIMA acao do ciclo sem inspecionar a implementacao - renovar o token antes de
// escrever os reles diria ao STWD100 que o ciclo de seguranca fechou quando ele ainda nao fechou.
//
// POR QUE NAO O FakeWatchdog CANONICO AQUI: ele e SINGLETON de proposito (um unico WDI e um
// unico timer na placa, e a segunda instancia recebe Err::Busy). Esta suite monta varias URs
// completas, algumas no mesmo caso de teste, e a semantica da ISR de 1 kHz nao e o que ela
// exercita - o que a Application faz com a porta e chamar heartbeat(). A maquina do fake
// canonico continua presa pelo seu proprio teste de contrato em test/native/test_fakes_watchdog/.
class SpyWatchdog final : public IWatchdog {
public:
    SpyWatchdog() : relays_(nullptr), analog_(nullptr), beats_(0), relayWritesAtBeat_(0),
                    analogWritesAtBeat_(0) {}

    // Amarra o espiao aos dois observadores de escrita, para o teste de ordem do ciclo.
    void watchWrites(const FakeRelayBank& relays, const FakeAnalogOutput& analog) {
        relays_ = &relays;
        analog_ = &analog;
    }

    Status begin() override { return kOk; }

    void heartbeat() override {
        ++beats_;
        if (relays_ != nullptr) {
            relayWritesAtBeat_ = relays_->transitionCount();
        }
        if (analog_ != nullptr) {
            analogWritesAtBeat_ = analog_->writeCount();
        }
    }

    void kickNow() override {}
    Status rearmPin() override { return kOk; }
    bool kicking() const override { return true; }
    uint32_t kickPeriodMs() const override { return 250; }
    uint32_t heartbeatTimeoutMs() const override { return 800; }
    uint32_t minTimeoutMs() const override { return 1120; }
    uint32_t typTimeoutMs() const override { return 1600; }
    uint32_t kickCount() const override { return 0; }
    uint32_t heartbeatCount() const override { return beats_; }

    uint32_t relayWritesAtBeat() const { return relayWritesAtBeat_; }
    uint32_t analogWritesAtBeat() const { return analogWritesAtBeat_; }

private:
    const FakeRelayBank* relays_;
    const FakeAnalogOutput* analog_;
    uint32_t beats_;
    uint32_t relayWritesAtBeat_;
    uint32_t analogWritesAtBeat_;
};

// A UR inteira em memoria de teste, montada como o composition root monta: relogio, enlace,
// reles, saida analogica e watchdog por referencia; nada instanciado dentro da aplicacao.
struct Rig {
    FakeClock clock;
    FakeSensorLink link;
    FakeRelayBank relays;
    FakeAnalogOutput analog;
    SpyWatchdog watchdog;
    Application app;

    Rig() : clock(), link(clock), relays(clock, true), analog(), watchdog(),
            app(clock, link, relays, analog, watchdog) {}

    void power() {
        TEST_ASSERT_TRUE(link.begin().ok());
        TEST_ASSERT_TRUE(relays.begin().ok());
        TEST_ASSERT_TRUE(analog.begin().ok());
        TEST_ASSERT_TRUE(app.begin(domain::Parameters::factoryDefaults()).ok());
        analog.clearLog();
    }
};

// Um ciclo completo, na ordem exata em que a tarefa ctrl o executa (ver ctrlTask() em
// src/main.cpp): aplica o publicado, abre a transacao, cede a CPU ate o veredito, fecha o ciclo
// e latcha o quadro para a IHM. O relogio avanca ate completar o periodo de 50 ms.
void cycle(FakeClock& clock, Application& app, uint32_t finishDelayMs = 0) {
    const uint32_t t0 = clock.nowMs();
    app.applyPublished();
    app.startCycle();
    uint16_t guard = 0;
    while (!app.pollCycle()) {
        clock.advanceMs(1);
        ++guard;
        TEST_ASSERT_TRUE_MESSAGE(guard < 200u, "pollCycle nunca fechou");
    }
    if (finishDelayMs != 0u) {
        clock.advanceMs(finishDelayMs);
    }
    app.finishCycle();
    app.latchSnapshot();
    const uint32_t used = elapsedMs(t0, clock.nowMs());
    if (used < Application::kCyclePeriodMs) {
        clock.advanceMs(Application::kCyclePeriodMs - used);
    }
}

void cycles(FakeClock& clock, Application& app, uint16_t count) {
    for (uint16_t i = 0; i < count; ++i) {
        cycle(clock, app);
    }
}

// Amostra integra e saudavel, entregue 18 ms depois do primeiro byte (o tipico de 17,9 ms da
// base comum), dentro do timeout de 35 ms.
void scriptGood(FakeSensorLink& link, int16_t xDeci, int16_t yDeci, uint16_t beat) {
    link.replySample(FakeSensorLink::goodSample(xDeci, yDeci, beat), 18);
}

// Leva o enlace a Ok e o avaliador a sair do alarme de boot: a liberacao confirmada de A3 e de
// 3000 ms depois da primeira leitura valida.
void settleClear(Rig& rig, int16_t deci = kQuietDeci) {
    scriptGood(rig.link, deci, deci, 1);
    for (uint16_t i = 0; i < 80u; ++i) {
        scriptGood(rig.link, deci, deci, static_cast<uint16_t>(i + 1u));
        cycle(rig.clock, rig.app);
    }
    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::Ok);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, rig.app.snapshot().relayMask);
}

}  // namespace

// --- boot e estado seguro --------------------------------------------------------------

static void test_boot_nasce_aguardando_com_reles_em_alarme_e_saidas_em_3932(void) {
    // 2.4 e o cabecalho do LimitEvaluator: do passo 5 do boot ate o primeiro quadro valido a
    // saida e 3932 e os quatro reles estao em alarme. Nunca 0x8000 (0,00 V), que e a leitura
    // legitima mais provavel de estrutura nivelada.
    Rig rig;
    rig.power();

    const Application::Snapshot boot = rig.app.snapshot();
    TEST_ASSERT_TRUE(boot.link == LinkHealth::Awaiting);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, boot.relayMask);
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, boot.analogCode[0]);
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, boot.analogCode[1]);

    rig.link.replySilence();
    cycle(rig.clock, rig.app);

    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, rig.app.snapshot().relayMask);
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::X));
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::Y));
    TEST_ASSERT_FALSE(rig.analog.everWrote(FakeAnalogOutput::kZeroCode));
}

static void test_primeiro_quadro_valido_tira_do_aguardando_sem_esperar_cinco(void) {
    // A recuperacao de 5 boas mais 2000 ms rege a SAIDA da falha, nunca a saida do estado de
    // boot: exigir 2 s de permanencia em Awaiting atrasaria a primeira leitura boa por nada.
    Rig rig;
    rig.power();
    scriptGood(rig.link, kQuietDeci, kQuietDeci, 1);
    cycle(rig.clock, rig.app);
    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::Ok);
}

// --- maquina de saude do enlace --------------------------------------------------------

static void test_tres_transacoes_mudas_declaram_CommFault_e_nao_duas(void) {
    Rig rig;
    rig.power();
    settleClear(rig);

    rig.link.replySilence();
    cycle(rig.clock, rig.app);
    cycle(rig.clock, rig.app);
    TEST_ASSERT_TRUE_MESSAGE(rig.app.link() == LinkHealth::Ok, "duas falhas nao declaram nada");
    cycle(rig.clock, rig.app);
    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::CommFault);

    // A5: os QUATRO reles vao a alarme, inclusive os dois programados em Off na Tabela 2.
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, rig.app.snapshot().relayMask);
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::X));
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::Y));
}

static void test_status_diferente_do_exato_e_SensorFault_e_nao_CommFault(void) {
    // Quadro integro no fio com conteudo reprovado pela sensora e outro defeito, e a tela do
    // operador diz outra coisa. 0x0011 e o caso real: kStsDataValid junto com
    // kStsSclNotResponding sobre angulo congelado.
    Rig rig;
    rig.power();
    settleClear(rig);

    SensorSample doente = FakeSensorLink::goodSample(kQuietDeci, kQuietDeci, 9);
    doente.status = static_cast<uint16_t>(kStsDataValid | 0x0010u);
    rig.link.replySample(doente, 18);
    cycles(rig.clock, rig.app, 3);

    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::SensorFault);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, rig.app.snapshot().relayMask);
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::X));
}

static void test_angulo_fora_de_mais_menos_900_decimos_e_recusado(void) {
    // A4 opcao A, criterio estrito: 90,1 graus nao e leitura, e amostra invalida.
    Rig rig;
    rig.power();
    settleClear(rig);

    scriptGood(rig.link, 901, kQuietDeci, 20);
    cycles(rig.clock, rig.app, 3);
    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::SensorFault);

    // A borda exata de 90,0 graus continua sendo leitura.
    Rig limpo;
    limpo.power();
    scriptGood(limpo.link, 900, 900, 1);
    cycle(limpo.clock, limpo.app);
    TEST_ASSERT_TRUE(limpo.app.link() == LinkHealth::Ok);
}

static void test_amostra_com_mais_de_72_ms_de_idade_e_recusada(void) {
    // kDataMaxAgeMs = kPollPeriodMs + kRoundTripMax. Um quadro integro que so foi CONSUMIDO
    // tarde e dado velho, e dado velho nao comanda rele.
    Rig rig;
    rig.power();
    settleClear(rig);

    scriptGood(rig.link, kQuietDeci, kQuietDeci, 30);
    cycle(rig.clock, rig.app, 90);  // 90 ms entre a chegada do quadro e o fim do ciclo
    cycle(rig.clock, rig.app, 90);
    cycle(rig.clock, rig.app, 90);
    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::SensorFault);
}

static void test_recuperacao_exige_cinco_boas_E_dois_mil_ms_de_permanencia(void) {
    Rig rig;
    rig.power();
    settleClear(rig);

    rig.link.replySilence();
    cycles(rig.clock, rig.app, 3);
    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::CommFault);

    // Cinco boas seguidas = 250 ms. Nao basta: a permanencia minima anti-flapping e 2000 ms.
    for (uint16_t i = 0; i < 5u; ++i) {
        scriptGood(rig.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(100u + i));
        cycle(rig.clock, rig.app);
    }
    TEST_ASSERT_TRUE_MESSAGE(rig.app.link() == LinkHealth::CommFault,
                             "5 boas sem os 2000 ms nao podem recuperar");

    for (uint16_t i = 0; i < 40u; ++i) {
        scriptGood(rig.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(200u + i));
        cycle(rig.clock, rig.app);
    }
    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::Ok);
}

// --- retencao, recarga e cadeia de medicao ---------------------------------------------

static void test_queda_do_heartbeat_da_sensora_recarrega_o_filtro(void) {
    // decisao 4 item 9: reinicio da sensora, detectado pela QUEDA do registrador 7, e um dos
    // tres motivos da lista fechada de recarga. Sem a recarga, o EMA arrastaria o angulo velho
    // da sensora anterior por varios ciclos.
    Rig comQueda;
    comQueda.power();
    for (uint16_t i = 0; i < 10u; ++i) {
        scriptGood(comQueda.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(500u + i));
        cycle(comQueda.clock, comQueda.app);
    }
    // Degrau de 1,5 grau: ABAIXO de kJumpReloadDeci (2,0 graus), para que a unica recarga
    // possivel seja a da queda do heartbeat e nao a recarga por salto do proprio filtro.
    scriptGood(comQueda.link, kStepDeci, kStepDeci, 1);  // heartbeat CAIU: sensora reiniciou
    cycle(comQueda.clock, comQueda.app);
    const int16_t recarregado = comQueda.app.snapshot().reading[0].deciDegrees();

    Rig semQueda;
    semQueda.power();
    for (uint16_t i = 0; i < 10u; ++i) {
        scriptGood(semQueda.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(500u + i));
        cycle(semQueda.clock, semQueda.app);
    }
    scriptGood(semQueda.link, kStepDeci, kStepDeci, 600);  // heartbeat seguiu subindo
    cycle(semQueda.clock, semQueda.app);
    const int16_t filtrado = semQueda.app.snapshot().reading[0].deciDegrees();

    TEST_ASSERT_EQUAL_INT16(kStepDeci, recarregado);
    TEST_ASSERT_TRUE_MESSAGE(filtrado < kStepDeci, "sem recarga o EMA tem de atrasar o degrau");
}

static void test_transacao_invalida_retem_a_ultima_leitura_valida(void) {
    // Retencao de ordem zero: sem amostra nova o filtro nao inventa dado, mas tambem nao
    // perde o ultimo valido - quem declara a falha e a maquina de saude, nao o filtro.
    Rig rig;
    rig.power();
    settleClear(rig);
    const int16_t antes = rig.app.snapshot().reading[0].deciDegrees();

    rig.link.replySilence();
    cycle(rig.clock, rig.app);
    TEST_ASSERT_EQUAL_INT16(antes, rig.app.snapshot().reading[0].deciDegrees());
}

// --- reles ------------------------------------------------------------------------------

static void test_applyMask_que_reprova_cai_em_signalAll_e_conta_o_erro(void) {
    FakeClock clock;
    FakeSensorLink link(clock);
    PickyRelayBank relays(clock);
    FakeAnalogOutput analog;
    SpyWatchdog watchdog;
    Application app(clock, link, relays, analog, watchdog);

    TEST_ASSERT_TRUE(link.begin().ok());
    TEST_ASSERT_TRUE(relays.begin().ok());
    TEST_ASSERT_TRUE(analog.begin().ok());
    TEST_ASSERT_TRUE(app.begin(domain::Parameters::factoryDefaults()).ok());

    // Leva os reles a Clear com o enlace saudavel, para que a mascara desejada NAO seja a de
    // alarme e o fallback fique visivel.
    for (uint16_t i = 0; i < 80u; ++i) {
        link.replySample(FakeSensorLink::goodSample(kQuietDeci, kQuietDeci,
                                                    static_cast<uint16_t>(i + 1u)), 18);
        cycle(clock, app);
    }
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, relays.mask());
    TEST_ASSERT_EQUAL_UINT32(0u, app.snapshot().relayWriteErrors);

    relays.failApplyMask(true);
    link.replySample(FakeSensorLink::goodSample(kQuietDeci, kQuietDeci, 900), 18);
    cycle(clock, app);

    // Um caminho de rele que devolve sucesso sem ter escrito e defeito de seguranca: o ciclo
    // cai em signalAll() e o hardware vai ao estado seguro, com o erro contado.
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, relays.mask());
    TEST_ASSERT_EQUAL_UINT32(1u, app.snapshot().relayWriteErrors);
    TEST_ASSERT_FALSE(app.relayBankDead());
    TEST_ASSERT_TRUE_MESSAGE(watchdog.heartbeatCount() > 0u,
                             "com o fallback bem sucedido o token continua sendo renovado");
}

static void test_banco_de_reles_mudo_latcha_e_para_de_alimentar_o_cachorro(void) {
    // O unico caminho de falha real do adaptador e ready_ == false, que so acontece quando
    // RelayBankGpio::begin() nao conseguiu PROVAR que comanda os quatro pinos. Nesse estado
    // applyMask() E signalAll() devolvem NotInit para sempre e os quatro reles ficam
    // congelados. Nao ha para onde escalar dentro do firmware: a saida e parar de renovar o
    // token de liveness e deixar o STWD100 resetar a placa, que na polaridade fail-safe
    // desenergiza as quatro bobinas = alarme.
    FakeClock clock;
    FakeSensorLink link(clock);
    FakeRelayBank relays(clock, true);
    FakeAnalogOutput analog;
    SpyWatchdog watchdog;
    Application app(clock, link, relays, analog, watchdog);

    TEST_ASSERT_TRUE(link.begin().ok());
    relays.injectBeginFault(true);
    TEST_ASSERT_TRUE(relays.begin().err == Err::HwFault);
    TEST_ASSERT_TRUE(analog.begin().ok());
    TEST_ASSERT_TRUE(app.begin(domain::Parameters::factoryDefaults()).ok());

    const uint32_t antes = watchdog.heartbeatCount();
    link.replySample(FakeSensorLink::goodSample(kQuietDeci, kQuietDeci, 1), 18);
    cycle(clock, app);

    TEST_ASSERT_TRUE(app.relayBankDead());
    TEST_ASSERT_TRUE(app.snapshot().relayBankDead);
    TEST_ASSERT_EQUAL_UINT32(2u, app.snapshot().relayWriteErrors);
    TEST_ASSERT_EQUAL_UINT32(antes, watchdog.heartbeatCount());

    // E o defeito e LATCHADO: um banco que reprovou nao volta a ser confiavel sozinho.
    link.replySample(FakeSensorLink::goodSample(kQuietDeci, kQuietDeci, 2), 18);
    cycle(clock, app);
    TEST_ASSERT_EQUAL_UINT32(antes, watchdog.heartbeatCount());
}

static void test_o_snapshot_publica_a_mascara_ESCRITA_e_nao_a_desejada(void) {
    // Com o banco mudo, publicar evaluator_.mask() faria a IHM mostrar o que o dominio queria
    // enquanto o hardware esta parado em outro estado - painel mentindo sobre rele.
    FakeClock clock;
    FakeSensorLink link(clock);
    PickyRelayBank relays(clock);
    FakeAnalogOutput analog;
    SpyWatchdog watchdog;
    Application app(clock, link, relays, analog, watchdog);

    TEST_ASSERT_TRUE(link.begin().ok());
    TEST_ASSERT_TRUE(relays.begin().ok());
    TEST_ASSERT_TRUE(analog.begin().ok());
    TEST_ASSERT_TRUE(app.begin(domain::Parameters::factoryDefaults()).ok());

    for (uint16_t i = 0; i < 80u; ++i) {
        link.replySample(FakeSensorLink::goodSample(kQuietDeci, kQuietDeci,
                                                    static_cast<uint16_t>(i + 1u)), 18);
        cycle(clock, app);
    }
    relays.failApplyMask(true);
    link.replySample(FakeSensorLink::goodSample(kQuietDeci, kQuietDeci, 900), 18);
    cycle(clock, app);

    TEST_ASSERT_EQUAL_UINT8(relays.mask(), app.snapshot().relayMask);
}

// --- guarda dura de idade (decisao 5 item 30) -------------------------------------------

static void test_guarda_dura_de_250_ms_leva_os_reles_a_alarme_apos_um_bloqueio(void) {
    // Cenario concreto: o loop() entra na NVS, a cache de flash e desabilitada e a tarefa ctrl
    // fica parada 300 ms. Nenhuma transacao e TENTADA, entao o contador de 3 falhas nao avanca;
    // os GPIOs de rele e o DAC8562 sao latches de hardware e mantem o ultimo estado PERMISSIVO
    // enquanto a estrutura inclina. Sem esta guarda, a ctrl volta, a sensora responde bem e
    // NENHUM alarme jamais e declarado.
    Rig rig;
    rig.power();
    settleClear(rig);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, rig.app.snapshot().relayMask);
    TEST_ASSERT_FALSE(rig.app.stale());

    rig.clock.advanceMs(300);  // bloqueio: nenhum ciclo roda

    scriptGood(rig.link, kQuietDeci, kQuietDeci, 777);
    cycle(rig.clock, rig.app);

    TEST_ASSERT_TRUE_MESSAGE(rig.app.stale(), "o ciclo que VOLTA do bloqueio tem de ver a idade");
    TEST_ASSERT_TRUE(rig.app.snapshot().stale);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, rig.app.snapshot().relayMask);
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::X));
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::Y));

    // Passado o susto, o proximo ciclo bom ja nao esta velho.
    scriptGood(rig.link, kQuietDeci, kQuietDeci, 778);
    cycle(rig.clock, rig.app);
    TEST_ASSERT_FALSE(rig.app.stale());
}

static void test_bloqueio_abaixo_de_250_ms_nao_dispara_a_guarda(void) {
    // 172 ms e o pior caso declarado do item 27 (72 ms de idade + 100 ms de cache-off). A
    // guarda tem 45 % de margem sobre ele de proposito: ela nao pode disparar em regime.
    Rig rig;
    rig.power();
    settleClear(rig);

    rig.clock.advanceMs(170);
    scriptGood(rig.link, kQuietDeci, kQuietDeci, 300);
    cycle(rig.clock, rig.app);

    TEST_ASSERT_FALSE(rig.app.stale());
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, rig.app.snapshot().relayMask);
}

static void test_noteStall_acima_do_orcamento_do_commit_declara_falha_na_hora(void) {
    // decisao 2 item 16: ate 500 ms o tick perdido NAO conta como transacao invalida; acima
    // disso a ctrl declara falha de enlace imediatamente.
    Rig tolerado;
    tolerado.power();
    settleClear(tolerado);
    tolerado.app.noteStall(400);
    TEST_ASSERT_TRUE(tolerado.app.link() == LinkHealth::Ok);

    Rig longo;
    longo.power();
    settleClear(longo);
    const uint32_t eventosAntes = longo.app.snapshot().faultEvents;

    // Bloqueio real: o relogio anda 600 ms sem que nenhum ciclo rode, e a tarefa ctrl so
    // percebe na volta, pelo delta do xTaskGetTickCount.
    longo.clock.advanceMs(600);
    longo.app.noteStall(600);
    TEST_ASSERT_TRUE(longo.app.link() == LinkHealth::CommFault);

    scriptGood(longo.link, kQuietDeci, kQuietDeci, 400);
    cycle(longo.clock, longo.app);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, longo.app.snapshot().relayMask);
    TEST_ASSERT_EQUAL_UINT32(eventosAntes + 1u, longo.app.snapshot().faultEvents);
}

// --- override da Auto Calibracao --------------------------------------------------------

static void test_override_de_um_eixo_nao_mexe_no_outro_e_os_reles_seguem_o_angulo(void) {
    // decisao 6 itens 2 e 7: o eixo nao calibrado continua rastreando o angulo real e os
    // quatro reles continuam vivos durante todo o procedimento.
    Rig rig;
    rig.power();
    settleClear(rig);

    const uint16_t simulado = 40000;
    rig.app.requestAnalogOverride(Axis::X, simulado);
    scriptGood(rig.link, kAlarmDeci, kAlarmDeci, 800);
    cycles(rig.clock, rig.app, 4);  // 100 ms de ataque confirmado, com folga

    const Application::Snapshot snap = rig.app.snapshot();
    TEST_ASSERT_TRUE(snap.overriding[0]);
    TEST_ASSERT_FALSE(snap.overriding[1]);
    TEST_ASSERT_EQUAL_UINT16(simulado, snap.analogCode[0]);
    TEST_ASSERT_TRUE(snap.analogCode[1] != simulado);
    TEST_ASSERT_TRUE(snap.analogCode[1] != kFaultCode);
    TEST_ASSERT_EQUAL_UINT8(kMaskAngularX1Y1, snap.relayMask);

    rig.app.clearAnalogOverride(Axis::X);
    cycle(rig.clock, rig.app);
    TEST_ASSERT_FALSE(rig.app.snapshot().overriding[0]);
}

static void test_override_sem_renovacao_expira_em_2000_ms(void) {
    // Uma IHM travada com a Auto Calibracao aberta deixaria o DAC preso num valor SIMULADO
    // para sempre, com a tarefa ctrl viva e o cachorro alimentado - o mesmo modo de falha que
    // 2.4 rejeitou em "congelar o ponteiro", so que pior, porque nem o ultimo angulo valido a
    // saida mostra. O prazo devolve o eixo sozinho.
    Rig rig;
    rig.power();
    settleClear(rig);

    rig.app.requestAnalogOverride(Axis::X, 40000);
    cycle(rig.clock, rig.app);
    TEST_ASSERT_TRUE(rig.app.snapshot().overriding[0]);

    // A IHM parou de republicar. O enlace tambem cai, que e o caso realista de um travamento
    // do core 1 durante um procedimento: a saida tem de acabar em 3932, nao no valor de teste.
    rig.link.replySilence();
    cycles(rig.clock, rig.app, 45);  // 2250 ms

    TEST_ASSERT_FALSE(rig.app.snapshot().overriding[0]);
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::X));
}

static void test_override_renovado_a_cada_passagem_nao_expira(void) {
    // Enquanto a IHM vive, ela republica o override a cada 50 ms e o carimbo se renova
    // sozinho: o prazo nao pode atrapalhar um procedimento legitimo, que dura minutos.
    Rig rig;
    rig.power();
    settleClear(rig);

    for (uint16_t i = 0; i < 120u; ++i) {  // 6000 ms, tres vezes o prazo
        rig.app.requestAnalogOverride(Axis::X, 40000);
        scriptGood(rig.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(1000u + i));
        cycle(rig.clock, rig.app);
    }
    TEST_ASSERT_TRUE(rig.app.snapshot().overriding[0]);
    TEST_ASSERT_EQUAL_UINT16(40000u, rig.app.snapshot().analogCode[0]);
}

// --- config perdida ---------------------------------------------------------------------

static void test_configLatched_forca_alarme_e_3932_mesmo_com_enlace_Ok(void) {
    // A8 / decisao 2 item 10: sem bloco de parametros integro o equipamento NAO carrega a
    // Tabela 2 em silencio - trava em falha, com os quatro reles em alarme e as duas saidas em
    // 3932, e so o Reset Geral sai desse estado.
    Rig rig;
    rig.power();
    settleClear(rig);

    rig.app.setConfigLatched(true);
    scriptGood(rig.link, kQuietDeci, kQuietDeci, 900);
    cycles(rig.clock, rig.app, 4);

    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::Ok);
    TEST_ASSERT_TRUE(rig.app.snapshot().configLatched);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, rig.app.snapshot().relayMask);
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::X));
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::Y));
}

// --- travessia de nucleo: publicacao e snapshot -----------------------------------------

static void test_o_conjunto_publicado_so_entra_em_applyPublished_e_entra_inteiro(void) {
    // decisao 1 item 18: troca do conjunto ativo INTEIRO no topo do tick, antes da avaliacao.
    // applyPublished() e publico e fica fora de startCycle() para que a tarefa ctrl possa
    // chama-lo sob o MESMO portMUX que a IHM toma para publicar.
    Rig rig;
    rig.power();

    domain::Parameters novo = domain::Parameters::factoryDefaults();
    TEST_ASSERT_TRUE(novo.setLimitValue(domain::LimitId::X1, Angle::fromDeciDegrees(123)).ok());
    TEST_ASSERT_TRUE(novo.setLimitOp(domain::LimitId::X2, domain::LimitOp::GreaterEqual).ok());
    TEST_ASSERT_TRUE(novo.setLimitValue(domain::LimitId::X2, Angle::fromDeciDegrees(456)).ok());

    rig.app.publishParameters(novo);
    TEST_ASSERT_EQUAL_INT16(50, rig.app.active().limitValue(domain::LimitId::X1).deciDegrees());

    rig.app.applyPublished();
    // Os dois campos entram JUNTOS: nunca um limite na referencia nova e o outro na velha.
    TEST_ASSERT_EQUAL_INT16(123, rig.app.active().limitValue(domain::LimitId::X1).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(456, rig.app.active().limitValue(domain::LimitId::X2).deciDegrees());
    TEST_ASSERT_TRUE(rig.app.active().limitOp(domain::LimitId::X2) ==
                     domain::LimitOp::GreaterEqual);
}

static void test_o_quadro_da_IHM_so_muda_em_latchSnapshot(void) {
    // snapshot() nao monta mais nada: devolve o buffer que a tarefa ctrl latchou sob a seccao
    // critica. E isso que impede o par (raw[0], raw[1]) lido pela IHM de misturar dois ticks -
    // e esse par alimenta a guarda de estabilidade de 5 decimos do PSET.
    Rig rig;
    rig.power();
    scriptGood(rig.link, kQuietDeci, kQuietDeci, 1);
    cycle(rig.clock, rig.app);
    const uint32_t ciclosLatchados = rig.app.snapshot().cycles;

    rig.app.applyPublished();
    rig.app.startCycle();
    while (!rig.app.pollCycle()) {
        rig.clock.advanceMs(1);
    }
    rig.app.finishCycle();
    TEST_ASSERT_EQUAL_UINT32(ciclosLatchados, rig.app.snapshot().cycles);

    rig.app.latchSnapshot();
    TEST_ASSERT_EQUAL_UINT32(ciclosLatchados + 1u, rig.app.snapshot().cycles);
}

// --- ordem do ciclo ---------------------------------------------------------------------

static void test_o_batimento_do_watchdog_e_a_ULTIMA_acao_do_ciclo(void) {
    // Renovar o token antes de escrever os reles diria ao STWD100 que o ciclo de seguranca
    // fechou quando ele ainda nao fechou.
    FakeClock clock;
    FakeSensorLink link(clock);
    FakeRelayBank relays(clock, true);
    FakeAnalogOutput analog;
    SpyWatchdog spy;
    spy.watchWrites(relays, analog);
    Application app(clock, link, relays, analog, spy);

    TEST_ASSERT_TRUE(link.begin().ok());
    TEST_ASSERT_TRUE(relays.begin().ok());
    TEST_ASSERT_TRUE(analog.begin().ok());
    TEST_ASSERT_TRUE(app.begin(domain::Parameters::factoryDefaults()).ok());

    link.replySample(FakeSensorLink::goodSample(kQuietDeci, kQuietDeci, 1), 18);
    cycle(clock, app);

    TEST_ASSERT_EQUAL_UINT32(1u, spy.heartbeatCount());
    TEST_ASSERT_EQUAL_UINT32(relays.transitionCount(), spy.relayWritesAtBeat());
    TEST_ASSERT_EQUAL_UINT32(analog.writeCount(), spy.analogWritesAtBeat());
    TEST_ASSERT_TRUE(spy.analogWritesAtBeat() > 0u);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_boot_nasce_aguardando_com_reles_em_alarme_e_saidas_em_3932);
    RUN_TEST(test_primeiro_quadro_valido_tira_do_aguardando_sem_esperar_cinco);
    RUN_TEST(test_tres_transacoes_mudas_declaram_CommFault_e_nao_duas);
    RUN_TEST(test_status_diferente_do_exato_e_SensorFault_e_nao_CommFault);
    RUN_TEST(test_angulo_fora_de_mais_menos_900_decimos_e_recusado);
    RUN_TEST(test_amostra_com_mais_de_72_ms_de_idade_e_recusada);
    RUN_TEST(test_recuperacao_exige_cinco_boas_E_dois_mil_ms_de_permanencia);
    RUN_TEST(test_queda_do_heartbeat_da_sensora_recarrega_o_filtro);
    RUN_TEST(test_transacao_invalida_retem_a_ultima_leitura_valida);
    RUN_TEST(test_applyMask_que_reprova_cai_em_signalAll_e_conta_o_erro);
    RUN_TEST(test_banco_de_reles_mudo_latcha_e_para_de_alimentar_o_cachorro);
    RUN_TEST(test_o_snapshot_publica_a_mascara_ESCRITA_e_nao_a_desejada);
    RUN_TEST(test_guarda_dura_de_250_ms_leva_os_reles_a_alarme_apos_um_bloqueio);
    RUN_TEST(test_bloqueio_abaixo_de_250_ms_nao_dispara_a_guarda);
    RUN_TEST(test_noteStall_acima_do_orcamento_do_commit_declara_falha_na_hora);
    RUN_TEST(test_override_de_um_eixo_nao_mexe_no_outro_e_os_reles_seguem_o_angulo);
    RUN_TEST(test_override_sem_renovacao_expira_em_2000_ms);
    RUN_TEST(test_override_renovado_a_cada_passagem_nao_expira);
    RUN_TEST(test_configLatched_forca_alarme_e_3932_mesmo_com_enlace_Ok);
    RUN_TEST(test_o_conjunto_publicado_so_entra_em_applyPublished_e_entra_inteiro);
    RUN_TEST(test_o_quadro_da_IHM_so_muda_em_latchSnapshot);
    RUN_TEST(test_o_batimento_do_watchdog_e_a_ULTIMA_acao_do_ciclo);
    return UNITY_END();
}
