// Prova de contrato do FakeRelayBank (LSP).
//
// O fake do banco de reles so vale se for substituivel pelo RelayBankGpio real sem que o dominio
// perceba: mesmo codigo de erro, mesma ORDEM de checagem, mesmos valores de fallback, mesma
// contagem de transicoes. Este arquivo prende exatamente os pontos em que o fake poderia ser
// generoso demais - e, quando o adaptador da placa mudar de semantica, e aqui que o build tem de
// quebrar, em vez de a divergencia aparecer com quatro bobinas energizadas num porto.
//
// Os casos vem, um a um, da revisao adversarial do adaptador reles:
//  - mascara acima de 0x0F e Err::Param (nao Range), e a checagem de Param vem ANTES da de
//    NotInit;
//  - applyMask()/set() antes de begin() sao Err::NotInit;
//  - RelayState fora da enumeracao e RECUSADO (Err::Param), nunca interpretado - senao o pino iria
//    ao nivel de alarme e o cache reportaria Clear, a direcao insegura no observador;
//  - canal fora de faixa: state() = Signalled, channelName() = "??";
//  - applyMask() dos quatro canais e UMA transicao (a porta promete escrita atomica);
//  - a UR nasce em alarme: mask() == kRelayMaskAllSignalled ja no construtor;
//  - begin() pode reprovar, e reprovando nao deixa a placa comandavel;
//  - feedbackAvailable() e sempre false (decisao 16).
#include <unity.h>

#include "fakes/fake_clock.h"
#include "fakes/fake_relay_bank.h"

using test::FakeClock;
using test::FakeRelayBank;

void setUp(void) {}
void tearDown(void) {}

static constexpr LimitChannel kBadChannel = static_cast<LimitChannel>(9);
static constexpr RelayState kBadState = static_cast<RelayState>(2);

static void test_nasce_em_alarme_e_nao_comandavel(void) {
    // A UR nao inicializada nao pode apresentar "sem alarme" em canal nenhum.
    FakeClock clock;
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, relays.mask());
    TEST_ASSERT_FALSE(relays.ready());
    TEST_ASSERT_EQUAL_UINT8(kLimitChannelCount, relays.count());
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        TEST_ASSERT_TRUE(relays.state(static_cast<LimitChannel>(i)) == RelayState::Signalled);
    }
}

static void test_escrita_antes_do_begin_e_notinit(void) {
    FakeClock clock;
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_TRUE(relays.applyMask(kRelayMaskAllClear).err == Err::NotInit);
    TEST_ASSERT_TRUE(relays.set(LimitChannel::Limit1, RelayState::Clear).err == Err::NotInit);
    TEST_ASSERT_TRUE(relays.signalAll().err == Err::NotInit);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, relays.mask());
}

static void test_mascara_suja_e_param_e_vem_antes_de_notinit(void) {
    // Ordem de checagem do alvo: placa nao inicializada MAIS mascara suja devolve Param.
    FakeClock clock;
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_TRUE(relays.applyMask(0x10).err == Err::Param);
    TEST_ASSERT_TRUE(relays.applyMask(0xFF).err == Err::Param);
    TEST_ASSERT_TRUE(relays.begin().ok());
    TEST_ASSERT_TRUE(relays.applyMask(0x10).err == Err::Param);
    // Recusada, a mascara suja nao altera o comando anterior.
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, relays.mask());
}

static void test_begin_carimba_a_janela_de_energizacao(void) {
    // Passo 2 da ordem de boot: e o instante que a medicao de inrush procura, e ele e registrado
    // mesmo quando a mascara ja era all-signalled desde o construtor.
    FakeClock clock(5000u);
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_TRUE(relays.begin().ok());
    TEST_ASSERT_TRUE(relays.ready());
    TEST_ASSERT_EQUAL_UINT8(1u, relays.recorded());
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, relays.transition(0).mask);
    TEST_ASSERT_EQUAL_UINT32(5000u, relays.transition(0).atMs);
}

static void test_applymask_dos_quatro_canais_e_uma_unica_transicao(void) {
    // A porta promete escrita ATOMICA: o observador nao pode ver dois canais ja mudados e dois
    // ainda nao. Se o alvo voltar a escrever canal a canal, este numero deixa de bater.
    FakeClock clock(1000u);
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_TRUE(relays.begin().ok());
    clock.advanceMs(50u);
    TEST_ASSERT_TRUE(relays.applyMask(kRelayMaskAllClear).ok());
    TEST_ASSERT_EQUAL_UINT8(2u, relays.recorded());
    TEST_ASSERT_EQUAL_UINT32(2u, relays.transitionCount());
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, relays.lastTransition().mask);
    TEST_ASSERT_EQUAL_UINT32(1050u, relays.lastChangeMs());
}

static void test_reescrever_o_mesmo_comando_nao_e_transicao(void) {
    // Permanencia: o ciclo de controle reescreve a mesma mascara a cada 50 ms, e isso nao pode
    // aparecer como rele chaveando.
    FakeClock clock(1000u);
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_TRUE(relays.begin().ok());
    for (uint8_t i = 0; i < 20u; ++i) {
        clock.advanceMs(50u);
        TEST_ASSERT_TRUE(relays.applyMask(kRelayMaskAllSignalled).ok());
    }
    TEST_ASSERT_EQUAL_UINT8(1u, relays.recorded());
    TEST_ASSERT_EQUAL_UINT32(1000u, relays.lastChangeMs());
}

static void test_signalall_equivale_a_applymask_de_todos(void) {
    FakeClock clock(1000u);
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_TRUE(relays.begin().ok());
    TEST_ASSERT_TRUE(relays.applyMask(kRelayMaskAllClear).ok());
    clock.advanceMs(7u);
    TEST_ASSERT_TRUE(relays.signalAll().ok());
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, relays.mask());
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, relays.lastTransition().mask);
    TEST_ASSERT_EQUAL_UINT32(1007u, relays.lastChangeMs());
}

static void test_set_atualiza_um_canal_so(void) {
    FakeClock clock;
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_TRUE(relays.begin().ok());
    TEST_ASSERT_TRUE(relays.applyMask(kRelayMaskAllClear).ok());
    TEST_ASSERT_TRUE(relays.set(LimitChannel::Limit3, RelayState::Signalled).ok());
    TEST_ASSERT_EQUAL_UINT8(0x04, relays.mask());
    TEST_ASSERT_TRUE(relays.state(LimitChannel::Limit3) == RelayState::Signalled);
    TEST_ASSERT_TRUE(relays.state(LimitChannel::Limit1) == RelayState::Clear);
    TEST_ASSERT_TRUE(relays.set(LimitChannel::Limit3, RelayState::Clear).ok());
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, relays.mask());
}

static void test_set_recusa_estado_fora_da_enumeracao(void) {
    // A via de fabrica e a que recebe valor de console. Estado invalido tem de ser REPROVADO, nao
    // interpretado: interpretar levaria a bobina ao nivel de alarme com o cache reportando Clear.
    FakeClock clock;
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_TRUE(relays.begin().ok());
    TEST_ASSERT_TRUE(relays.applyMask(kRelayMaskAllClear).ok());
    const uint8_t antes = relays.recorded();
    TEST_ASSERT_TRUE(relays.set(LimitChannel::Limit2, kBadState).err == Err::Param);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, relays.mask());
    TEST_ASSERT_EQUAL_UINT8(antes, relays.recorded());
}

static void test_canal_fora_de_faixa_e_param_e_le_seguro(void) {
    FakeClock clock;
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_TRUE(relays.begin().ok());
    TEST_ASSERT_TRUE(relays.set(kBadChannel, RelayState::Clear).err == Err::Param);
    // Sem codigo de erro na leitura, a porta so pode errar para o lado seguro.
    TEST_ASSERT_TRUE(relays.state(kBadChannel) == RelayState::Signalled);
    TEST_ASSERT_EQUAL_STRING("??", relays.channelName(kBadChannel));
}

static void test_rotulos_sao_os_do_manual(void) {
    FakeClock clock;
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_EQUAL_STRING("X1", relays.channelName(LimitChannel::Limit1));
    TEST_ASSERT_EQUAL_STRING("X2", relays.channelName(LimitChannel::Limit2));
    TEST_ASSERT_EQUAL_STRING("Y1", relays.channelName(LimitChannel::Limit3));
    TEST_ASSERT_EQUAL_STRING("Y2", relays.channelName(LimitChannel::Limit4));
}

static void test_begin_reprovado_nao_deixa_a_placa_comandavel(void) {
    // O alvo so devolve kOk depois de reler o latch de saida dos quatro pinos. Reprovando, o
    // hardware fica no nivel de Signalled e nenhuma escrita e aceita.
    FakeClock clock(2000u);
    FakeRelayBank relays(clock, true);
    relays.injectBeginFault();
    TEST_ASSERT_TRUE(relays.begin().err == Err::HwFault);
    TEST_ASSERT_FALSE(relays.ready());
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, relays.mask());
    TEST_ASSERT_TRUE(relays.applyMask(kRelayMaskAllClear).err == Err::NotInit);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, relays.mask());
}

static void test_forcar_de_isr_ignora_ready_mas_nao_o_begin(void) {
    // Espelha RelayBankGpio::signalAllFromIsr(): no-op antes de os pinos existirem, e depois
    // funciona mesmo com o latch reprovado - e o caminho da janela de cache desligada da NVS.
    FakeClock clock(3000u);
    FakeRelayBank relays(clock, true);
    relays.forceSignalledFromIsr();
    TEST_ASSERT_EQUAL_UINT8(0u, relays.recorded());

    relays.injectBeginFault();
    TEST_ASSERT_TRUE(relays.begin().err == Err::HwFault);
    TEST_ASSERT_TRUE(relays.applyMask(kRelayMaskAllClear).err == Err::NotInit);
    relays.injectBeginFault(false);
    TEST_ASSERT_TRUE(relays.begin().ok());
    TEST_ASSERT_TRUE(relays.applyMask(kRelayMaskAllClear).ok());

    clock.advanceMs(10u);
    relays.forceSignalledFromIsr();
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, relays.mask());
    TEST_ASSERT_EQUAL_UINT32(3010u, relays.lastChangeMs());
}

static void test_sem_realimentacao_de_contato(void) {
    // Decisao 16: state()/mask() sao COMANDO. Nenhuma decisao pode depender de leitura de contato,
    // e o fake nao pode sugerir que ela existe.
    FakeClock clock;
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_FALSE(relays.feedbackAvailable());
    TEST_ASSERT_TRUE(relays.begin().ok());
    TEST_ASSERT_FALSE(relays.feedbackAvailable());
}

static void test_polaridade_e_do_construtor_nao_do_fake(void) {
    // A1 pendente: as duas polaridades tem de ser testaveis sem recompilar o dominio.
    FakeClock clock;
    FakeRelayBank failSafe(clock, true);
    FakeRelayBank manual(clock, false);
    TEST_ASSERT_TRUE(failSafe.failSafeCoil());
    TEST_ASSERT_FALSE(manual.failSafeCoil());
}

static void test_historico_cheio_conta_o_descarte(void) {
    // Transicao perdida em equipamento de seguranca nao pode ser silenciosa.
    FakeClock clock(0u);
    FakeRelayBank relays(clock, true);
    TEST_ASSERT_TRUE(relays.begin().ok());
    for (uint8_t i = 0; i < 2u * FakeRelayBank::kHistoryCap; ++i) {
        clock.advanceMs(50u);
        const RelayMask wanted = ((i & 1u) != 0) ? kRelayMaskAllSignalled : kRelayMaskAllClear;
        TEST_ASSERT_TRUE(relays.applyMask(wanted).ok());
    }
    TEST_ASSERT_EQUAL_UINT8(FakeRelayBank::kHistoryCap, relays.recorded());
    TEST_ASSERT_EQUAL_UINT32(1u + 2u * FakeRelayBank::kHistoryCap, relays.transitionCount());
    TEST_ASSERT_EQUAL_UINT32(1u + FakeRelayBank::kHistoryCap, relays.dropped());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_nasce_em_alarme_e_nao_comandavel);
    RUN_TEST(test_escrita_antes_do_begin_e_notinit);
    RUN_TEST(test_mascara_suja_e_param_e_vem_antes_de_notinit);
    RUN_TEST(test_begin_carimba_a_janela_de_energizacao);
    RUN_TEST(test_applymask_dos_quatro_canais_e_uma_unica_transicao);
    RUN_TEST(test_reescrever_o_mesmo_comando_nao_e_transicao);
    RUN_TEST(test_signalall_equivale_a_applymask_de_todos);
    RUN_TEST(test_set_atualiza_um_canal_so);
    RUN_TEST(test_set_recusa_estado_fora_da_enumeracao);
    RUN_TEST(test_canal_fora_de_faixa_e_param_e_le_seguro);
    RUN_TEST(test_rotulos_sao_os_do_manual);
    RUN_TEST(test_begin_reprovado_nao_deixa_a_placa_comandavel);
    RUN_TEST(test_forcar_de_isr_ignora_ready_mas_nao_o_begin);
    RUN_TEST(test_sem_realimentacao_de_contato);
    RUN_TEST(test_polaridade_e_do_construtor_nao_do_fake);
    RUN_TEST(test_historico_cheio_conta_o_descarte);
    return UNITY_END();
}
