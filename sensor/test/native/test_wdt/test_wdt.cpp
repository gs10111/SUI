// O PORTAO DO WATCHDOG DA SENSORA, isolado do hardware e provado no host.
//
// POR QUE ESTE ARQUIVO EXISTE. Ate 2026-09-14 a sensora chutava o STWD100 por esp_timer, cujo
// callback roda na ESP_TIMER_TASK - que executa DE FLASH e PARA quando a cache e desabilitada,
// que e o que acontece em todo apagamento de setor. A UR ja tinha migrado para ISR de timer de
// hardware em IRAM pelo mesmo motivo, e o DECISIONS.md registra isso como "requisito de base,
// nao otimizacao". A sensora ficou para tras.
//
// Migrar o chute para IRAM, sozinho, cria um defeito PIOR: uma ISR que pulsa
// incondicionalmente alimenta o cachorro para sempre, e um firmware travado nunca mais reseta -
// o watchdog vira decoracao. Por isso a migracao vem com TOKEN DE LIVENESS, e e a regra do
// token que este arquivo prende.
//
// A funcao e pura de proposito: e aritmetica de tique que atravessa o wrap de 2^32 ms, e e
// exatamente o tipo de codigo que erra em silencio dentro de uma ISR onde ninguem consegue
// olhar. Aqui ela e exercitada nas fronteiras.
#include <unity.h>

#include "drivers/wdt_gate.h"

void setUp(void) {}
void tearDown(void) {}

namespace {
constexpr uint32_t kPrazo = wdt::kLivenessDeadlineTicks;  // 800
constexpr uint32_t kCarencia = wdt::kBootGraceTicks;      // 3000
}  // namespace

// --- carencia de boot: sem token, a ISR chuta, mas NAO para sempre -------------------------

static void test_sem_token_a_carencia_de_boot_deixa_chutar(void) {
    TEST_ASSERT_TRUE(wdt::gateOpen(0u, 0u, false));
    TEST_ASSERT_TRUE(wdt::gateOpen(kCarencia - 1u, 0u, false));
}

static void test_sem_token_a_carencia_VENCE(void) {
    // Esta e a diferenca entre watchdog e decoracao: passada a carencia sem um unico
    // heartbeat, o portao fecha e o STWD100 reseta a placa. Um firmware que trava depois do
    // begin() do watchdog e antes do primeiro heartbeat TEM de resetar.
    TEST_ASSERT_FALSE(wdt::gateOpen(kCarencia, 0u, false));
    TEST_ASSERT_FALSE(wdt::gateOpen(kCarencia + 1u, 0u, false));
    TEST_ASSERT_FALSE(wdt::gateOpen(0xFFFFFFFFu, 0u, false));
}

// --- com token: vale o prazo, e a fronteira e exata -----------------------------------------

static void test_com_token_o_prazo_e_exato(void) {
    const uint32_t batida = 10000u;
    TEST_ASSERT_TRUE(wdt::gateOpen(batida, batida, true));
    TEST_ASSERT_TRUE(wdt::gateOpen(batida + kPrazo - 1u, batida, true));
    TEST_ASSERT_FALSE(wdt::gateOpen(batida + kPrazo, batida, true));
    TEST_ASSERT_FALSE(wdt::gateOpen(batida + kPrazo + 1u, batida, true));
}

static void test_com_token_a_carencia_deixa_de_valer(void) {
    // Armado o token, a carencia de boot nao pode ressuscitar: um firmware que bateu uma vez e
    // travou em seguida, dentro dos 3000 ms de carencia, TEM de resetar pelo prazo de 800.
    TEST_ASSERT_FALSE(wdt::gateOpen(kPrazo + 1u, 0u, true));
    TEST_ASSERT_TRUE(wdt::gateOpen(kCarencia - 1u, kCarencia - 1u, true));
}

// --- wrap de 2^32: a sensora fica energizada meses ------------------------------------------

static void test_o_prazo_atravessa_o_wrap_de_2_elevado_a_32(void) {
    // 49,7 dias de uptime continuo e o tique volta a zero. Uma subtracao unsigned atravessa;
    // uma comparacao "tick > batida + prazo" nao. A sensora fica energizada meses.
    const uint32_t batida = 0xFFFFFF00u;
    TEST_ASSERT_TRUE(wdt::gateOpen(batida, batida, true));
    TEST_ASSERT_TRUE(wdt::gateOpen(0x000000FFu, batida, true));   // 255 tiques depois
    TEST_ASSERT_TRUE(wdt::gateOpen(batida + kPrazo - 1u, batida, true));
    TEST_ASSERT_FALSE(wdt::gateOpen(batida + kPrazo, batida, true));
}

static void test_a_carencia_tambem_atravessa_o_wrap(void) {
    // A carencia conta do tique ZERO da ISR, entao ela so importa no boot - mas se o tique
    // nascesse alto por algum motivo, o portao nao pode reabrir sozinho.
    TEST_ASSERT_FALSE(wdt::gateOpen(0xFFFFFFFFu, 0u, false));
}

// --- a margem que torna os numeros seguros --------------------------------------------------

static void test_o_prazo_mais_a_cadencia_cabem_sob_o_tWD_minimo(void) {
    // O ultimo pulso sai ate kKickPeriodTicks depois de o portao fechar, entao a latencia real
    // ate o reset e prazo + cadencia + tWD. Prazo + cadencia TEM de caber sob o tWD minimo de
    // 1120 ms, senao o cachorro morde antes de o firmware ser declarado morto.
    TEST_ASSERT_EQUAL_UINT32(800u, wdt::kLivenessDeadlineTicks);
    TEST_ASSERT_EQUAL_UINT32(250u, wdt::kKickPeriodTicks);
    TEST_ASSERT_TRUE(wdt::kLivenessDeadlineTicks + wdt::kKickPeriodTicks < 1120u);
    // E a cadencia tem de dar pelo menos tres chutes dentro do tWD minimo.
    TEST_ASSERT_TRUE(wdt::kKickPeriodTicks * 3u <= 1120u);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sem_token_a_carencia_de_boot_deixa_chutar);
    RUN_TEST(test_sem_token_a_carencia_VENCE);
    RUN_TEST(test_com_token_o_prazo_e_exato);
    RUN_TEST(test_com_token_a_carencia_deixa_de_valer);
    RUN_TEST(test_o_prazo_atravessa_o_wrap_de_2_elevado_a_32);
    RUN_TEST(test_a_carencia_tambem_atravessa_o_wrap);
    RUN_TEST(test_o_prazo_mais_a_cadencia_cabem_sob_o_tWD_minimo);
    return UNITY_END();
}
