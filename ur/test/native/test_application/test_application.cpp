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

// Transporte perfeito, conteudo reprovado: e o caso da sensora que responde todo quadro no prazo
// e com CRC bom, mas se declara doente no proprio status.
void scriptStatus(FakeSensorLink& link, int16_t xDeci, int16_t yDeci, uint16_t status,
                  uint16_t beat) {
    SensorSample sample = FakeSensorLink::goodSample(xDeci, yDeci, beat);
    sample.status = status;
    link.replySample(sample, 18);
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

// Um ciclo BOM cujo finishCycle() cai EXATAMENTE em alvoMs (absoluto). O finish acontece 18 ms
// depois da abertura do ciclo - o round-trip tipico da base comum, que e o que scriptGood()
// programa - entao a abertura e alvoMs - 18. Existe para prender bordas de prazo ao milissegundo
// sem depender da granularidade de 50 ms do ciclo.
void cycleFinishingAt(Rig& rig, uint32_t alvoMs, uint16_t beat) {
    const uint32_t abrirEm = static_cast<uint32_t>(alvoMs - 18u);
    TEST_ASSERT_TRUE_MESSAGE(static_cast<int32_t>(abrirEm - rig.clock.nowMs()) >= 0,
                             "alvo no passado: o relogio de teste nao anda para tras");
    rig.clock.advanceMs(elapsedMs(rig.clock.nowMs(), abrirEm));
    scriptGood(rig.link, kQuietDeci, kQuietDeci, beat);
    cycle(rig.clock, rig.app);
}

// Uma entrada em falha completa: tres transacoes mudas declaram CommFault, e boas suficientes
// (contagem E permanencia) devolvem o enlace a Ok. E o EVENTO que A7 conta.
void oneFaultAndRecovery(Rig& rig, uint16_t beatBase) {
    rig.link.replySilence();
    cycles(rig.clock, rig.app, 3);
    for (uint16_t i = 0; i < 45u; ++i) {
        scriptGood(rig.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(beatBase + i));
        cycle(rig.clock, rig.app);
    }
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

static void test_a_QUARTA_boa_nao_recupera_com_a_permanencia_ja_vencida(void) {
    // ISOLA kGoodsToRecover = 5, e nada mais. O teste anterior fazia 5 ciclos bons (250 ms) e
    // afirmava "ainda CommFault" - o que era verdade com QUALQUER exigencia >= 1, porque os
    // 2000 ms de permanencia ainda nao tinham vencido - e depois 40 ciclos afirmando "agora Ok",
    // verdade com QUALQUER permanencia <= 2000 ms. Provado por mutacao: kGoodsToRecover 5 -> 1
    // e kFaultMinDwellMs 2000 -> 1000 passavam os dois. Aqui a permanencia vence PRIMEIRO, em
    // silencio, e o unico criterio que ainda pode segurar a recuperacao e a contagem.
    Rig rig;
    rig.power();
    settleClear(rig);

    rig.link.replySilence();
    cycles(rig.clock, rig.app, 3);
    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::CommFault);

    // 42 ciclos mudos = 2100 ms, acima dos 2000 ms de permanencia minima e sem UMA transacao boa.
    cycles(rig.clock, rig.app, 42);
    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::CommFault);

    // Da primeira a QUARTA boa o enlace continua em falha. O numero 4 esta escrito aqui, e o 5
    // que ele testa esta escrito na linha seguinte - nenhum dos dois vem da constante da classe.
    for (uint16_t i = 0; i < 4u; ++i) {
        scriptGood(rig.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(300u + i));
        cycle(rig.clock, rig.app);
        TEST_ASSERT_TRUE_MESSAGE(rig.app.link() == LinkHealth::CommFault,
                                 "menos de 5 boas nao recuperam nem com a permanencia vencida");
    }
    scriptGood(rig.link, kQuietDeci, kQuietDeci, 400);
    cycle(rig.clock, rig.app);
    TEST_ASSERT_TRUE_MESSAGE(rig.app.link() == LinkHealth::Ok, "a QUINTA boa recupera");
}

static void test_a_permanencia_minima_solta_em_2000_ms_e_nao_em_1999(void) {
    // ISOLA kFaultMinDwellMs = 2000, e nada mais: a contagem de boas ja esta satisfeita antes
    // da borda. Duas URs, uma de cada lado, porque o relogio de teste nao anda para tras. Os
    // numeros 1999 e 2000 estao escritos aqui, nao referenciados da classe.
    for (unsigned lado = 0; lado < 2u; ++lado) {
        const uint32_t alvoMs = (lado == 0u) ? 1999u : 2000u;
        Rig rig;
        rig.power();
        settleClear(rig);

        // noteStall() declara a falha num instante EXATO e conhecido (decisao 2 item 16: atraso
        // de escalonamento acima do orcamento do commit de NVS e falha de enlace na hora). Com
        // as tres transacoes mudas o instante ficaria escondido dentro do terceiro ciclo.
        rig.app.noteStall(600);
        const uint32_t faltaEm = rig.clock.nowMs();
        TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::CommFault);

        for (uint16_t i = 0; i < 5u; ++i) {
            scriptGood(rig.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(600u + i));
            cycle(rig.clock, rig.app);
        }
        TEST_ASSERT_TRUE_MESSAGE(rig.app.link() == LinkHealth::CommFault,
                                 "5 boas em 250 ms nao vencem a permanencia de 2000 ms");

        cycleFinishingAt(rig, faltaEm + alvoMs, 700);
        if (alvoMs == 1999u) {
            TEST_ASSERT_TRUE_MESSAGE(rig.app.link() == LinkHealth::CommFault,
                                     "1999 ms depois da falha o enlace ainda esta em falha");
        } else {
            TEST_ASSERT_TRUE_MESSAGE(rig.app.link() == LinkHealth::Ok,
                                     "2000 ms depois da falha o enlace recupera");
        }
    }
}

// --- A7: latch de flapping do enlace -----------------------------------------------------

static void test_A7_a_QUINTA_entrada_em_falha_em_60_s_trava_o_estado_de_falha(void) {
    // A7, APROVADA: "5 entradas em falha dentro de 60 s travam o estado de falha". A quarta NAO
    // trava: quatro numeros escritos aqui (4, 5, 60000 e 0), nenhum vindo da classe.
    Rig rig;
    rig.power();
    settleClear(rig);

    for (uint16_t evento = 0; evento < 4u; ++evento) {
        oneFaultAndRecovery(rig, static_cast<uint16_t>(1000u + 100u * evento));
        TEST_ASSERT_TRUE_MESSAGE(rig.app.link() == LinkHealth::Ok,
                                 "ate a quarta entrada o enlace ainda recupera sozinho");
        TEST_ASSERT_FALSE_MESSAGE(rig.app.snapshot().linkLatched,
                                  "quatro entradas em 60 s nao podem travar");
    }

    // A quinta entrada, ainda dentro da janela: as cinco couberam em cerca de 12 s.
    rig.link.replySilence();
    cycles(rig.clock, rig.app, 3);
    TEST_ASSERT_TRUE(rig.app.snapshot().linkLatched);
    TEST_ASSERT_EQUAL_UINT32(5u, rig.app.snapshot().faultEvents);

    // E travado significa TRAVADO. O que fica travado sao os QUATRO RELES e as DUAS SAIDAS; a
    // saude do enlace continua sendo medida com honestidade, e e por isso que a tela pode dizer
    // "enlace voltou, mas a falha esta travada - rearme no menu" (docs/ihm-estados.md B7). Um
    // minuto inteiro de enlace perfeito nao solta nada disso.
    for (uint16_t i = 0; i < 1200u; ++i) {
        scriptGood(rig.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(2000u + (i % 500u)));
        cycle(rig.clock, rig.app);
    }
    TEST_ASSERT_TRUE_MESSAGE(rig.app.link() == LinkHealth::Ok,
                             "o latch nao mente sobre a saude do enlace: o cabo voltou");
    TEST_ASSERT_TRUE_MESSAGE(rig.app.snapshot().linkLatched,
                             "latch de A7 nao se solta sozinho, nunca");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kRelayMaskAllSignalled, rig.app.snapshot().relayMask,
                                    "os quatro reles ficam em alarme ate o rearme humano");
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::X));
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::Y));
}

static void test_A7_o_latch_leva_os_reles_a_alarme_NO_MESMO_ciclo(void) {
    // Mesma razao de ser da guarda dura de idade: sem o passe por cima, o alarme esperaria os 3
    // ciclos (150 ms) que o avaliador leva para declarar falha sozinho. Uma falha ja TRAVADA nao
    // pode pagar esses 150 ms de contatos dizendo "sem alarme".
    Rig rig;
    rig.power();
    settleClear(rig);
    for (uint16_t evento = 0; evento < 4u; ++evento) {
        oneFaultAndRecovery(rig, static_cast<uint16_t>(8000u + 100u * evento));
    }
    TEST_ASSERT_FALSE(rig.app.snapshot().linkLatched);
    // Os 3000 ms de liberacao de A3 depois da ultima recuperacao: e preciso partir de reles
    // LIVRES, senao o teste nao distingue "o latch levou a alarme" de "ja estava em alarme".
    for (uint16_t i = 0; i < 80u; ++i) {
        scriptGood(rig.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(8500u + i));
        cycle(rig.clock, rig.app);
    }
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, rig.app.snapshot().relayMask);

    // QUINTA entrada por noteStall(), que declara a falha NA HORA (decisao 2 item 16). O
    // avaliador ainda esta na primeira transacao invalida: faltam dois ciclos para o veredito
    // dele virar alarme sozinho.
    rig.app.noteStall(600);
    TEST_ASSERT_TRUE(rig.app.linkLatched());

    scriptGood(rig.link, kQuietDeci, kQuietDeci, 9000);
    cycle(rig.clock, rig.app);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kRelayMaskAllSignalled, rig.app.snapshot().relayMask,
                                    "o latch de A7 nao espera os 150 ms do avaliador");
    TEST_ASSERT_EQUAL_UINT16(kFaultCode, rig.analog.lastCode(AnalogAxis::X));
}

static void test_A7_cinco_entradas_ESPALHADAS_por_mais_de_60_s_nao_travam(void) {
    // A janela desliza: cinco eventos em uma hora sao um enlace ruim, nao um enlace piscando.
    // Cada evento e separado por 20 s, entao a quinta entrada ve a primeira a 80 s de distancia.
    Rig rig;
    rig.power();
    settleClear(rig);

    for (uint16_t evento = 0; evento < 5u; ++evento) {
        oneFaultAndRecovery(rig, static_cast<uint16_t>(3000u + 100u * evento));
        TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::Ok);
        // Enlace saudavel por 20 s entre um evento e o proximo (400 ciclos de 50 ms).
        for (uint16_t i = 0; i < 400u; ++i) {
            scriptGood(rig.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(4000u + (i % 500u)));
            cycle(rig.clock, rig.app);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(5u, rig.app.snapshot().faultEvents);
    TEST_ASSERT_FALSE_MESSAGE(rig.app.snapshot().linkLatched,
                              "5 entradas espalhadas por 80 s estao FORA da janela de 60 s");
    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::Ok);
}

static void test_A7_o_rearme_da_IHM_destrava_e_exige_cinco_eventos_novos(void) {
    // "rearme pela IHM atras da senha, sem cortar energia". Rearmar zera tambem a janela: se os
    // quatro carimbos velhos ficassem no anel, a proxima falha isolada travaria de novo na hora
    // e o operador rearmaria a cada evento - o ponteamento em campo que A11 descreve.
    Rig rig;
    rig.power();
    settleClear(rig);
    for (uint16_t evento = 0; evento < 5u; ++evento) {
        oneFaultAndRecovery(rig, static_cast<uint16_t>(5000u + 100u * evento));
    }
    TEST_ASSERT_TRUE(rig.app.snapshot().linkLatched);

    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, rig.app.snapshot().relayMask);

    // O rearme atravessa o nucleo: a IHM PUBLICA o pedido e quem apaga o latch e applyPublished(),
    // na tarefa ctrl, sob o mesmo portMUX em que updateHealth() o arma. Publicar e apagar direto
    // seria update perdido - um clear do loop() simultaneo a um set da ctrl faz um dos dois sumir.
    rig.app.clearLinkLatch();
    TEST_ASSERT_TRUE_MESSAGE(rig.app.linkLatched(),
                             "o pedido sozinho nao apaga: quem apaga e a tarefa ctrl");
    rig.app.applyPublished();
    TEST_ASSERT_FALSE(rig.app.linkLatched());

    // Rearmar solta os reles: com o enlace ja saudavel e o angulo abaixo dos 5,0 graus da
    // Tabela 2, os quatro canais voltam a Clear depois dos prazos de liberacao de A3.
    for (uint16_t i = 0; i < 100u; ++i) {
        scriptGood(rig.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(6000u + i));
        cycle(rig.clock, rig.app);
    }
    TEST_ASSERT_TRUE(rig.app.link() == LinkHealth::Ok);
    TEST_ASSERT_FALSE(rig.app.snapshot().linkLatched);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kRelayMaskAllClear, rig.app.snapshot().relayMask,
                                    "rearmado e com o enlace bom, os reles tem de soltar");

    // Uma unica entrada nova nao pode travar de novo: a janela foi zerada junto.
    oneFaultAndRecovery(rig, 7000);
    TEST_ASSERT_FALSE_MESSAGE(rig.app.snapshot().linkLatched,
                              "uma entrada apos o rearme nao pode ser tratada como a quinta");
}

// --- saida analogica muda ----------------------------------------------------------------

static void test_saida_analogica_que_reprova_a_escrita_latcha_e_aparece_no_quadro(void) {
    // A saida analogica e, por 2.4, o unico numero que diz ao CLP que o angulo publicado nao
    // vale. begin() reprovado deixa o adaptador NAO PRONTO e toda writeBoth() devolve NotInit
    // para sempre, com as duas saidas encostadas no POR do DAC8562. Ate a etapa 8 isso so
    // incrementava um contador que ninguem lia, e a tela principal continuava dizendo
    // "rastreando" sobre um DAC mudo.
    FakeClock clock;
    FakeSensorLink link(clock);
    FakeRelayBank relays(clock, true);
    FakeAnalogOutput analog;
    SpyWatchdog watchdog;
    Application app(clock, link, relays, analog, watchdog);

    TEST_ASSERT_TRUE(link.begin().ok());
    TEST_ASSERT_TRUE(relays.begin().ok());
    // A saida analogica NAO abre: adaptador nao pronto, como no alvo com o SPI/DAC8562 mudo.
    analog.failNext(Err::HwFault);
    TEST_ASSERT_TRUE(analog.begin().failed());
    TEST_ASSERT_FALSE(analog.ready());
    TEST_ASSERT_TRUE(app.begin(domain::Parameters::factoryDefaults()).ok());

    TEST_ASSERT_FALSE(app.snapshot().analogDead);

    link.replySample(FakeSensorLink::goodSample(kQuietDeci, kQuietDeci, 1), 18);
    cycle(clock, app);

    TEST_ASSERT_TRUE_MESSAGE(app.snapshot().analogDead,
                             "escrita reprovada com o adaptador nao pronto e DAC morto");
    TEST_ASSERT_TRUE(app.snapshot().analogWriteErrors > 0u);

    // LATCH: um DAC que reprovou nao volta a ser confiavel, nem depois de mil ciclos bons.
    for (uint16_t i = 0; i < 100u; ++i) {
        link.replySample(FakeSensorLink::goodSample(kQuietDeci, kQuietDeci,
                                                    static_cast<uint16_t>(10u + i)), 18);
        cycle(clock, app);
    }
    TEST_ASSERT_TRUE(app.snapshot().analogDead);
}

static void test_grampo_de_faixa_da_saida_analogica_NAO_e_morte(void) {
    // Simetrico do anterior, e o que impede o latch de virar falso positivo: o adaptador
    // devolve Err::Range quando GRAMPEOU um codigo na faixa util e escreveu assim mesmo. A
    // escrita valeu, o DAC esta vivo e ready() continua true - latchar ali derrubaria o canal
    // analogico por um grampo legitimo.
    Rig rig;
    rig.power();
    settleClear(rig);

    // Override do assistente com um codigo abaixo da faixa util: o adaptador GRAMPEIA em
    // kCodeMin, escreve, e devolve Err::Range para dizer "escrevi outra coisa". Escrita valeu.
    const uint32_t errosAntes = rig.app.snapshot().analogWriteErrors;
    for (uint16_t i = 0; i < 20u; ++i) {
        rig.app.requestAnalogOverride(Axis::X, 100);
        scriptGood(rig.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(800u + i));
        cycle(rig.clock, rig.app);
    }
    TEST_ASSERT_TRUE(rig.app.snapshot().analogWriteErrors > errosAntes);
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kCodeMin, rig.analog.lastCode(AnalogAxis::X));
    TEST_ASSERT_TRUE(rig.analog.ready());
    TEST_ASSERT_FALSE_MESSAGE(rig.app.snapshot().analogDead,
                              "grampo de faixa nao pode ser lido como DAC morto");
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

// A janela de commit de NVS e ANUNCIADA, e so ela e creditada.
//
// kWriteBudgetMs do adaptador de NVS e kHardStaleMs sao os dois 250 ms: margem zero. Sem o
// anuncio, gravar um setpoint congela a tarefa ctrl por exatamente o tempo que dispara a guarda
// dura de idade, e salvar um parametro levaria os quatro reles a alarme - alarme falso a cada
// gravacao. O credito vale porque a decisao 6 ja congela os reles nos modos em que a gravacao
// acontece; e vale UM ciclo, nunca mais que isso.
static void test_janela_de_commit_anunciada_nao_dispara_a_guarda_de_idade(void) {
    Rig rig;
    rig.power();
    settleClear(rig);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, rig.app.snapshot().relayMask);

    rig.app.noteCommitWindow(250);
    rig.clock.advanceMs(250);

    scriptGood(rig.link, kQuietDeci, kQuietDeci, 900);
    cycle(rig.clock, rig.app);

    TEST_ASSERT_FALSE_MESSAGE(rig.app.stale(), "bloqueio anunciado dentro do orcamento nao envelhece o dado");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kRelayMaskAllClear, rig.app.snapshot().relayMask,
                                    "gravar um setpoint nao pode alarmar os quatro reles");
}

static void test_o_credito_da_janela_de_commit_vale_um_unico_ciclo(void) {
    Rig rig;
    rig.power();
    settleClear(rig);

    rig.app.noteCommitWindow(250);
    rig.clock.advanceMs(250);
    scriptGood(rig.link, kQuietDeci, kQuietDeci, 901);
    cycle(rig.clock, rig.app);
    TEST_ASSERT_FALSE(rig.app.stale());

    // Segundo bloqueio, agora SEM anuncio: a guarda tem de voltar a valer inteira.
    rig.clock.advanceMs(300);
    scriptGood(rig.link, kQuietDeci, kQuietDeci, 902);
    cycle(rig.clock, rig.app);
    TEST_ASSERT_TRUE_MESSAGE(rig.app.stale(), "o credito nao pode ficar armado para o proximo bloqueio");
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, rig.app.snapshot().relayMask);
}

static void test_bloqueio_alem_do_orcamento_declarado_nao_e_creditado(void) {
    // 501 ms passou do que foi declarado: nao ha desculpa, a guarda trabalha.
    Rig rig;
    rig.power();
    settleClear(rig);

    rig.app.noteCommitWindow(501);
    rig.clock.advanceMs(501);
    scriptGood(rig.link, kQuietDeci, kQuietDeci, 903);
    cycle(rig.clock, rig.app);

    TEST_ASSERT_TRUE_MESSAGE(rig.app.stale(), "estouro do orcamento nao se credita");
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, rig.app.snapshot().relayMask);
}

// O latch de A8 tambem atravessa o nucleo publicado, pelo mesmo motivo do rearme de A7.
static void test_A8_latch_de_configuracao_atravessa_o_publish(void) {
    Rig rig;
    rig.power();
    settleClear(rig);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, rig.app.snapshot().relayMask);

    rig.app.setConfigLatched(true);
    TEST_ASSERT_FALSE_MESSAGE(rig.app.configLatched(),
                              "o pedido sozinho nao arma: quem arma e a tarefa ctrl");

    rig.app.applyPublished();
    TEST_ASSERT_TRUE(rig.app.configLatched());

    // Com o latch de A8 armado a amostra deixa de contar como fresca, e o avaliador exige as
    // tres passagens invalidas de A4 antes de alarmar - nao e um ciclo so.
    for (uint8_t i = 0; i < 3u; ++i) {
        scriptGood(rig.link, kQuietDeci, kQuietDeci, static_cast<uint16_t>(904u + i));
        cycle(rig.clock, rig.app);
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kRelayMaskAllSignalled, rig.app.snapshot().relayMask,
                                    "configuracao perdida mantem os quatro reles em alarme");
}

// A TELA TEM DE NOMEAR A CAUSA CERTA.
//
// lastGoodMs_ so avanca em amostra ACEITA, e accept() exige status == 0x0001. Uma sensora que
// responde TODO quadro, no prazo e com CRC bom, mas se declara doente, deixa stale ligar em
// 250 ms. Ate este conserto, stale forcava CommFault na tela - "falha de comunicacao" - e mandava
// o operador procurar defeito num cabo perfeito. Aconteceu em bancada duas vezes.
//
// updateHealth() ja separa os dois casos pelo verdict_ do enlace. stale so pode impedir o "Ok".
// A LIGACAO ENTRE O SNAPSHOT E A TELA TEM DE SER EXIGIDA CAMPO A CAMPO.
//
// Este teste existe por causa de um defeito real: a Emenda 2 acrescentou "unqualified" em
// Snapshot e em NormalInput, os testes de tela continuaram verdes porque montam o NormalInput a
// mao, e a ligacao entre os dois ficou faltando no composition root. A leitura marcada nao
// chegava a tela nenhuma, e nenhuma suite reprovou. Campo novo sem travessia exigida e campo que
// nao existe na placa.
static void test_buildNormalInput_leva_todo_campo_do_snapshot_para_a_tela(void) {
    Rig rig;
    rig.power();
    settleClear(rig);

    for (uint8_t i = 0; i < 10u; ++i) {
        scriptStatus(rig.link, 495, 9, 0x0008u, static_cast<uint16_t>(950u + i));
        cycle(rig.clock, rig.app);
    }

    const app::Application::Snapshot snap = rig.app.snapshot();
    const domain::Parameters params = domain::Parameters::factoryDefaults();
    const domain::NormalInput in = app::buildNormalInput(snap, params);

    // O campo da Emenda 2: sem esta assercao o defeito volta em silencio.
    TEST_ASSERT_TRUE_MESSAGE(in.unqualified[0] == snap.unqualified[0],
                             "unqualified do eixo X tem de atravessar");
    TEST_ASSERT_TRUE_MESSAGE(in.unqualified[1] == snap.unqualified[1],
                             "unqualified do eixo Y tem de atravessar");
    TEST_ASSERT_TRUE_MESSAGE(in.unqualified[0].valid(),
                             "com quadro chegando tem de existir numero medido");
    TEST_ASSERT_EQUAL_INT16(495, in.unqualified[0].deciDegrees());

    // E os demais, para que a proxima adicao tambem seja pega.
    TEST_ASSERT_TRUE(in.reading[0] == snap.reading[0]);
    TEST_ASSERT_TRUE(in.reading[1] == snap.reading[1]);
    TEST_ASSERT_TRUE(in.linkLatched == snap.linkLatched);
    TEST_ASSERT_TRUE(in.link == app::mapLinkToScreen(snap.link, snap.stale));
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        TEST_ASSERT_TRUE(in.limit[i].state == snap.limitState[i]);
    }
}

static void test_sensora_respondendo_e_doente_mostra_falha_do_SENSOR_nao_do_cabo(void) {
    using domain::NormalLinkState;
    TEST_ASSERT_TRUE(app::mapLinkToScreen(LinkHealth::SensorFault, true) == NormalLinkState::SensorFault);
    TEST_ASSERT_TRUE(app::mapLinkToScreen(LinkHealth::SensorFault, false) == NormalLinkState::SensorFault);
}

static void test_enlace_mudo_continua_mostrando_falha_de_comunicacao(void) {
    using domain::NormalLinkState;
    TEST_ASSERT_TRUE(app::mapLinkToScreen(LinkHealth::CommFault, true) == NormalLinkState::CommFault);
    TEST_ASSERT_TRUE(app::mapLinkToScreen(LinkHealth::CommFault, false) == NormalLinkState::CommFault);
}

static void test_stale_nunca_deixa_a_tela_dizer_Ok(void) {
    // O ciclo que volta de um bloqueio: a saude ainda diz Ok porque nenhuma transacao reprovou,
    // mas os quatro reles ja estao em alarme pela guarda de idade. A tela nao pode dizer Ok.
    using domain::NormalLinkState;
    TEST_ASSERT_TRUE(app::mapLinkToScreen(LinkHealth::Ok, true) == NormalLinkState::CommFault);
    TEST_ASSERT_TRUE(app::mapLinkToScreen(LinkHealth::Awaiting, true) == NormalLinkState::CommFault);
    TEST_ASSERT_TRUE(app::mapLinkToScreen(LinkHealth::Ok, false) == NormalLinkState::Ok);
    TEST_ASSERT_TRUE(app::mapLinkToScreen(LinkHealth::Awaiting, false) == NormalLinkState::Awaiting);
}

// Ponta a ponta: sensora respondendo com status reprovado leva a saude a SensorFault, e nao a
// CommFault - e portanto a tela nomeia o sensor.
static void test_status_reprovado_com_quadro_chegando_classifica_como_SensorFault(void) {
    Rig rig;
    rig.power();
    settleClear(rig);

    for (uint8_t i = 0; i < 10u; ++i) {
        scriptStatus(rig.link, kQuietDeci, kQuietDeci, 0x0008u, static_cast<uint16_t>(910u + i));
        cycle(rig.clock, rig.app);
    }
    TEST_ASSERT_TRUE_MESSAGE(rig.app.link() == LinkHealth::SensorFault,
                             "quadro chegando e status reprovado e falha do SENSOR");
    TEST_ASSERT_TRUE(app::mapLinkToScreen(rig.app.link(), rig.app.stale()) ==
                     domain::NormalLinkState::SensorFault);
}

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
    RUN_TEST(test_a_QUARTA_boa_nao_recupera_com_a_permanencia_ja_vencida);
    RUN_TEST(test_a_permanencia_minima_solta_em_2000_ms_e_nao_em_1999);
    RUN_TEST(test_A7_a_QUINTA_entrada_em_falha_em_60_s_trava_o_estado_de_falha);
    RUN_TEST(test_A7_o_latch_leva_os_reles_a_alarme_NO_MESMO_ciclo);
    RUN_TEST(test_A7_cinco_entradas_ESPALHADAS_por_mais_de_60_s_nao_travam);
    RUN_TEST(test_A7_o_rearme_da_IHM_destrava_e_exige_cinco_eventos_novos);
    RUN_TEST(test_saida_analogica_que_reprova_a_escrita_latcha_e_aparece_no_quadro);
    RUN_TEST(test_grampo_de_faixa_da_saida_analogica_NAO_e_morte);
    RUN_TEST(test_queda_do_heartbeat_da_sensora_recarrega_o_filtro);
    RUN_TEST(test_transacao_invalida_retem_a_ultima_leitura_valida);
    RUN_TEST(test_applyMask_que_reprova_cai_em_signalAll_e_conta_o_erro);
    RUN_TEST(test_banco_de_reles_mudo_latcha_e_para_de_alimentar_o_cachorro);
    RUN_TEST(test_o_snapshot_publica_a_mascara_ESCRITA_e_nao_a_desejada);
    RUN_TEST(test_janela_de_commit_anunciada_nao_dispara_a_guarda_de_idade);
    RUN_TEST(test_o_credito_da_janela_de_commit_vale_um_unico_ciclo);
    RUN_TEST(test_bloqueio_alem_do_orcamento_declarado_nao_e_creditado);
    RUN_TEST(test_A8_latch_de_configuracao_atravessa_o_publish);
    RUN_TEST(test_buildNormalInput_leva_todo_campo_do_snapshot_para_a_tela);
    RUN_TEST(test_sensora_respondendo_e_doente_mostra_falha_do_SENSOR_nao_do_cabo);
    RUN_TEST(test_enlace_mudo_continua_mostrando_falha_de_comunicacao);
    RUN_TEST(test_stale_nunca_deixa_a_tela_dizer_Ok);
    RUN_TEST(test_status_reprovado_com_quadro_chegando_classifica_como_SensorFault);
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
