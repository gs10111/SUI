// Prova de contrato do FakeWatchdog (LSP) contra o adaptador real Stwd100Watchdog.
//
// O fake so vale se for substituivel pelo adaptador sem que o dominio perceba: mesmas
// pre-condicoes, mesma semantica de erro, mesmos prazos. Aqui ficam presos exatamente os
// pontos em que a revisao adversarial pegou o adaptador mentindo, para que nenhuma das duas
// implementacoes volte a divergir em silencio:
//
//  - instancia sem begin() bem-sucedido nao alimenta o cachorro nem inventa contador;
//  - a segunda instancia recebe Err::Busy (um pino WDI, um timer de hardware);
//  - begin() pode FALHAR (timer ausente, ISR nao instalada, ISR sem tique) e, falhando, nao
//    deixa nada meio armado - o oposto do fake que so sabe devolver kOk;
//  - a carencia de boot, que existe porque o token so nasce no passo 13, TEM FIM: sem
//    heartbeat nenhum, o chute para e a placa reseta;
//  - com o token vivo, o portao fecha no prazo, e o ultimo chute sai na CADENCIA (750 ms),
//    nao no prazo (800 ms);
//  - os prazos publicados pela porta cabem sob o tWD minimo do STWD100.
#include <unity.h>

#include "fakes/fake_watchdog.h"

using test::FakeWatchdog;

void setUp(void) {}
void tearDown(void) {}

static void test_wdt_sem_begin_nao_alimenta_o_cachorro(void) {
    // Objeto orfao mexendo no estado global do alvo manteria o STWD100 REAL alimentado.
    FakeWatchdog wdt;

    TEST_ASSERT_FALSE(wdt.ready());
    TEST_ASSERT_FALSE(wdt.kicking());

    wdt.heartbeat();
    wdt.kickNow();
    wdt.advanceMs(5000u);

    TEST_ASSERT_FALSE(wdt.livenessArmed());
    TEST_ASSERT_EQUAL_UINT32(0u, wdt.kickCount());
    TEST_ASSERT_EQUAL_UINT32(0u, wdt.heartbeatCount());
    TEST_ASSERT_EQUAL_UINT32(0u, wdt.isrTickMs());
    TEST_ASSERT_TRUE(wdt.rearmPin().err == Err::NotInit);
}

static void test_wdt_begin_conta_o_primeiro_pulso_e_abre_o_portao(void) {
    FakeWatchdog wdt;
    TEST_ASSERT_TRUE(wdt.begin().ok());

    TEST_ASSERT_TRUE(wdt.ready());
    TEST_ASSERT_TRUE(wdt.kicking());          // carencia de boot, ainda sem token
    TEST_ASSERT_FALSE(wdt.livenessArmed());
    TEST_ASSERT_EQUAL_UINT32(1u, wdt.kickCount());  // o pulso imediato do passo 1 do boot
    TEST_ASSERT_EQUAL_UINT32(0u, wdt.heartbeatCount());
    TEST_ASSERT_TRUE(wdt.rearmPin().ok());
}

static void test_wdt_segunda_instancia_recebe_busy(void) {
    FakeWatchdog primeira;
    TEST_ASSERT_TRUE(primeira.begin().ok());

    FakeWatchdog segunda;
    TEST_ASSERT_TRUE(segunda.begin().err == Err::Busy);
    TEST_ASSERT_FALSE(segunda.ready());
    TEST_ASSERT_FALSE(segunda.kicking());
    TEST_ASSERT_EQUAL_UINT32(0u, segunda.kickCount());
}

static void test_wdt_begin_que_falha_nao_fica_meio_armado(void) {
    // No alvo: timerBegin nulo, timer_isr_callback_add != ESP_OK, ou ISR que nao tiquetou
    // dentro dos 5 ms de prova. Quem devolve kOk sem ter conseguido poe a placa em boot loop.
    FakeWatchdog wdt;
    wdt.setBeginResult(Status(Err::HwFault));

    TEST_ASSERT_TRUE(wdt.begin().err == Err::HwFault);
    TEST_ASSERT_FALSE(wdt.ready());
    TEST_ASSERT_FALSE(wdt.kicking());
    TEST_ASSERT_EQUAL_UINT32(0u, wdt.kickCount());

    wdt.advanceMs(1000u);
    TEST_ASSERT_EQUAL_UINT32(0u, wdt.kickCount());

    // A posse nao foi tomada: outra instancia ainda consegue armar.
    FakeWatchdog outra;
    TEST_ASSERT_TRUE(outra.begin().ok());
}

static void test_wdt_busy_vem_antes_de_hwfault(void) {
    // Ordem de checagem do alvo: Stwd100Watchdog::begin() testa a posse (g_armed) ANTES de
    // abrir o timer, entao a segunda instancia recebe Busy mesmo quando o hardware falharia.
    // Se o fake devolvesse HwFault aqui, o app trataria "ja tem dono" como "placa quebrada".
    FakeWatchdog primeira;
    TEST_ASSERT_TRUE(primeira.begin().ok());

    FakeWatchdog segunda;
    segunda.setBeginResult(Status(Err::HwFault));
    TEST_ASSERT_TRUE(segunda.begin().err == Err::Busy);
    TEST_ASSERT_FALSE(segunda.ready());
}

static void test_wdt_isr_chuta_na_cadencia_de_250ms(void) {
    FakeWatchdog wdt;
    TEST_ASSERT_TRUE(wdt.begin().ok());

    wdt.advanceMs(249u);
    TEST_ASSERT_EQUAL_UINT32(0u, wdt.isrKickCount());

    wdt.advanceMs(1u);
    TEST_ASSERT_EQUAL_UINT32(1u, wdt.isrKickCount());

    wdt.advanceMs(750u);
    TEST_ASSERT_EQUAL_UINT32(4u, wdt.isrKickCount());
    TEST_ASSERT_EQUAL_UINT32(5u, wdt.kickCount());
    TEST_ASSERT_EQUAL_UINT32(250u, wdt.maxKickGapMs());
    TEST_ASSERT_FALSE(wdt.wouldHaveReset());
}

static void test_wdt_kickNow_reancora_a_cadencia(void) {
    // Sem reancorar, um chute manual pode ser seguido de um chute da ISR poucos ms depois e
    // de um vazio de 250 ms logo atras.
    FakeWatchdog wdt;
    TEST_ASSERT_TRUE(wdt.begin().ok());

    wdt.advanceMs(200u);
    wdt.kickNow();
    TEST_ASSERT_EQUAL_UINT32(2u, wdt.manualKickCount());

    wdt.advanceMs(249u);
    TEST_ASSERT_EQUAL_UINT32(0u, wdt.isrKickCount());
    wdt.advanceMs(1u);
    TEST_ASSERT_EQUAL_UINT32(1u, wdt.isrKickCount());
}

static void test_wdt_carencia_de_boot_tem_fim(void) {
    // A tarefa ctrl nunca nasceu (xTaskCreate do passo 13 falhando por heap): sem prazo para
    // a carencia, o cachorro ficaria alimentado para sempre com o firmware sem dono.
    FakeWatchdog wdt;
    TEST_ASSERT_TRUE(wdt.begin().ok());

    wdt.advanceMs(FakeWatchdog::kBootGraceMs - 1u);
    TEST_ASSERT_TRUE(wdt.kicking());
    TEST_ASSERT_EQUAL_UINT32(11u, wdt.isrKickCount());  // 250 ms ate 2750 ms

    wdt.advanceMs(1u);
    TEST_ASSERT_FALSE(wdt.kicking());
    TEST_ASSERT_FALSE(wdt.wouldHaveReset());

    wdt.advanceMs(900u);
    TEST_ASSERT_EQUAL_UINT32(11u, wdt.isrKickCount());  // parou de chutar de vez
    TEST_ASSERT_TRUE(wdt.wouldHaveReset());
}

static void test_wdt_heartbeat_periodico_nunca_deixa_resetar(void) {
    // Tarefa ctrl viva a 50 ms: 10 s sem nenhuma lacuna acima do periodo de chute.
    FakeWatchdog wdt;
    TEST_ASSERT_TRUE(wdt.begin().ok());

    for (uint32_t i = 0; i < 200u; ++i) {
        wdt.advanceMs(50u);
        wdt.heartbeat();
    }

    TEST_ASSERT_TRUE(wdt.livenessArmed());
    TEST_ASSERT_TRUE(wdt.kicking());
    TEST_ASSERT_EQUAL_UINT32(200u, wdt.heartbeatCount());
    TEST_ASSERT_EQUAL_UINT32(0u, wdt.livenessAgeMs());
    TEST_ASSERT_EQUAL_UINT32(250u, wdt.maxKickGapMs());
    TEST_ASSERT_FALSE(wdt.wouldHaveReset());
}

static void test_wdt_ultimo_chute_sai_na_cadencia_e_o_portao_fecha_no_prazo(void) {
    FakeWatchdog wdt;
    TEST_ASSERT_TRUE(wdt.begin().ok());
    wdt.heartbeat();

    const uint32_t antes = wdt.isrKickCount();
    wdt.advanceMs(750u);
    TEST_ASSERT_EQUAL_UINT32(antes + 3u, wdt.isrKickCount());  // 250, 500 e 750 ms
    TEST_ASSERT_TRUE(wdt.kicking());

    wdt.advanceMs(50u);  // 800 ms: prazo vencido
    TEST_ASSERT_FALSE(wdt.kicking());
    TEST_ASSERT_EQUAL_UINT32(800u, wdt.livenessAgeMs());

    wdt.advanceMs(250u);  // tique de 1000 ms: o chute NAO sai
    TEST_ASSERT_EQUAL_UINT32(antes + 3u, wdt.isrKickCount());

    // Do ultimo pulso (750 ms) ate o tWD minimo: a placa reseta.
    wdt.advanceMs(FakeWatchdog::kMinTimeoutMs - 250u);
    TEST_ASSERT_TRUE(wdt.wouldHaveReset());
}

static void test_wdt_prazos_publicados_cabem_sob_o_tWD_minimo(void) {
    FakeWatchdog wdt;

    TEST_ASSERT_EQUAL_UINT32(250u, wdt.kickPeriodMs());
    TEST_ASSERT_EQUAL_UINT32(800u, wdt.heartbeatTimeoutMs());
    TEST_ASSERT_EQUAL_UINT32(1120u, wdt.minTimeoutMs());
    TEST_ASSERT_EQUAL_UINT32(1600u, wdt.typTimeoutMs());

    // Os "3 chutes de margem" que a porta anota, validos tanto para 750 quanto para 800 ms
    // (divergencia aberta, do dono da porta) - e a margem que importa, sob o tWD minimo.
    TEST_ASSERT_TRUE(wdt.kickPeriodMs() * 3u <= wdt.heartbeatTimeoutMs());
    TEST_ASSERT_TRUE(wdt.heartbeatTimeoutMs() + wdt.kickPeriodMs() < wdt.minTimeoutMs());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_wdt_sem_begin_nao_alimenta_o_cachorro);
    RUN_TEST(test_wdt_begin_conta_o_primeiro_pulso_e_abre_o_portao);
    RUN_TEST(test_wdt_segunda_instancia_recebe_busy);
    RUN_TEST(test_wdt_begin_que_falha_nao_fica_meio_armado);
    RUN_TEST(test_wdt_busy_vem_antes_de_hwfault);
    RUN_TEST(test_wdt_isr_chuta_na_cadencia_de_250ms);
    RUN_TEST(test_wdt_kickNow_reancora_a_cadencia);
    RUN_TEST(test_wdt_carencia_de_boot_tem_fim);
    RUN_TEST(test_wdt_heartbeat_periodico_nunca_deixa_resetar);
    RUN_TEST(test_wdt_ultimo_chute_sai_na_cadencia_e_o_portao_fecha_no_prazo);
    RUN_TEST(test_wdt_prazos_publicados_cabem_sob_o_tWD_minimo);
    return UNITY_END();
}
