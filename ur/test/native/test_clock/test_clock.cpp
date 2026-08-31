// Testes da aritmetica de prazo de ports/i_clock.h e do FakeClock.
//
// Vem antes de tudo porque todo prazo do produto depende dela: o hold de 3 s do MENU (L81),
// o timeout de 2 minutos (L96 e L127), a liberacao de 3000 ms e o teto de 600 s de A3, e o
// bloqueio de 60 s de A13. Se a comparacao de prazo quebra no wrap do contador, o equipamento
// falha uma vez a cada 49,7 dias de operacao continua - exatamente o defeito que ninguem
// reproduz na bancada.
#include <unity.h>

#include "fakes/fake_clock.h"
#include "ports/i_clock.h"

using test::FakeClock;

void setUp(void) {}
void tearDown(void) {}

static void test_prazo_conta_intervalo_simples(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, elapsedMs(1000u, 1000u));
    TEST_ASSERT_EQUAL_UINT32(250u, elapsedMs(1000u, 1250u));
    TEST_ASSERT_FALSE(deadlineReached(1000u, 1250u, 3000u));
    TEST_ASSERT_TRUE(deadlineReached(1000u, 4000u, 3000u));
}

static void test_prazo_vence_exatamente_no_limite(void) {
    // Fronteira: 3000 ms de hold tem de valer em 3000, nao so em 3001.
    TEST_ASSERT_FALSE(deadlineReached(100u, 3099u, 3000u));
    TEST_ASSERT_TRUE(deadlineReached(100u, 3100u, 3000u));
}

static void test_prazo_sobrevive_ao_wrap_do_contador(void) {
    // O contador envolve em 2^32 ms. Uma comparacao ingenua com "a > b" daria falso aqui e o
    // prazo nunca venceria - ou venceria na hora errada.
    const uint32_t antes = 0xFFFFFF00u;
    const uint32_t depois = 0x000000FFu;  // 256 ms ate o wrap, mais 255: 511 ms de intervalo
    TEST_ASSERT_EQUAL_UINT32(511u, elapsedMs(antes, depois));
    TEST_ASSERT_TRUE(deadlineReached(antes, depois, 500u));
    TEST_ASSERT_TRUE(deadlineReached(antes, depois, 511u));
    TEST_ASSERT_FALSE(deadlineReached(antes, depois, 512u));
}

static void test_prazo_longo_atravessando_o_wrap(void) {
    // 2 minutos de timeout do Modo Programacao, comecando pouco antes do wrap.
    const uint32_t inicio = 0xFFFFF000u;
    const uint32_t dois_minutos = 120000u;
    TEST_ASSERT_FALSE(deadlineReached(inicio, inicio + 119999u, dois_minutos));
    TEST_ASSERT_TRUE(deadlineReached(inicio, inicio + 120000u, dois_minutos));
}

static void test_fake_clock_so_anda_quando_mandado(void) {
    FakeClock relogio(1000u);
    TEST_ASSERT_EQUAL_UINT32(1000u, relogio.nowMs());
    TEST_ASSERT_EQUAL_UINT32(1000u, relogio.nowMs());

    relogio.advanceMs(3000u);
    TEST_ASSERT_EQUAL_UINT32(4000u, relogio.nowMs());
}

static void test_fake_clock_mantem_ms_e_us_coerentes(void) {
    FakeClock relogio(0u);
    relogio.advanceMs(5u);
    TEST_ASSERT_EQUAL_UINT32(5u, relogio.nowMs());
    TEST_ASSERT_EQUAL_UINT32(5000u, relogio.nowUs());

    relogio.advanceUs(2500u);
    TEST_ASSERT_EQUAL_UINT32(7u, relogio.nowMs());
    TEST_ASSERT_EQUAL_UINT32(7500u, relogio.nowUs());
}

static void test_fake_clock_comeca_perto_do_wrap_por_padrao(void) {
    // O padrao expoe erro de wrap sem que o teste precise pedir.
    FakeClock relogio;
    const uint32_t inicio = relogio.nowMs();
    relogio.advanceMs(0x20000u);
    TEST_ASSERT_TRUE(relogio.nowMs() < inicio);
    TEST_ASSERT_EQUAL_UINT32(0x20000u, elapsedMs(inicio, relogio.nowMs()));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_prazo_conta_intervalo_simples);
    RUN_TEST(test_prazo_vence_exatamente_no_limite);
    RUN_TEST(test_prazo_sobrevive_ao_wrap_do_contador);
    RUN_TEST(test_prazo_longo_atravessando_o_wrap);
    RUN_TEST(test_fake_clock_so_anda_quando_mandado);
    RUN_TEST(test_fake_clock_mantem_ms_e_us_coerentes);
    RUN_TEST(test_fake_clock_comeca_perto_do_wrap_por_padrao);
    return UNITY_END();
}
