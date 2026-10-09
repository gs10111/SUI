// REINICIO PERIODICO DO PAINEL (Decisao 20). O que estes testes protegem: o painel e reiniciado
// a cada kPeriodMs sem nunca piscar na cara de quem esta mexendo nele, e sem nunca ser reiniciado
// enquanto outra coisa e dona da tela (splash, atualizacao).
#include <unity.h>

#include "app/display_refresh.h"

void setUp(void) {}
void tearDown(void) {}

namespace {
constexpr uint32_t kT0 = 0xFFFFF000u;  // perto do wrap de 2^32, de proposito
constexpr uint32_t kPeriod = app::DisplayRefresh::kPeriodMs;
constexpr uint32_t kQuiet = app::DisplayRefresh::kQuietMs;
}  // namespace

static void test_constantes_da_decisao_20(void) {
    TEST_ASSERT_EQUAL_UINT32(300000u, kPeriod);
    TEST_ASSERT_EQUAL_UINT32(10000u, kQuiet);
}

static void test_nao_vence_antes_do_periodo_e_vence_nele(void) {
    app::DisplayRefresh r;
    r.start(kT0);
    TEST_ASSERT_FALSE(r.takeDue(kT0, false));
    TEST_ASSERT_FALSE(r.takeDue(kT0 + kPeriod - 1u, false));
    TEST_ASSERT_TRUE(r.takeDue(kT0 + kPeriod, false));
}

// takeDue() CONSOME: depois de vencer, a contagem recomeca do instante do reinicio, e nao do
// start(). Sem isso o painel seria reiniciado a 20 Hz depois do primeiro periodo.
static void test_vencer_rearma_o_periodo(void) {
    app::DisplayRefresh r;
    r.start(kT0);
    const uint32_t t1 = kT0 + kPeriod + 777u;
    TEST_ASSERT_TRUE(r.takeDue(t1, false));
    TEST_ASSERT_FALSE(r.takeDue(t1 + 50u, false));
    TEST_ASSERT_FALSE(r.takeDue(t1 + kPeriod - 1u, false));
    TEST_ASSERT_TRUE(r.takeDue(t1 + kPeriod, false));
}

// O operador esta no menu editando um limite: o reinicio cega as teclas por ~0,5 s e um aperto
// se perderia. Espera kQuietMs depois da ULTIMA tecla.
static void test_tecla_recente_adia_ate_ficar_quieto(void) {
    app::DisplayRefresh r;
    r.start(kT0);
    const uint32_t tecla = kT0 + kPeriod - 3000u;
    r.noteKey(tecla);
    TEST_ASSERT_FALSE(r.takeDue(kT0 + kPeriod, false));
    TEST_ASSERT_FALSE(r.takeDue(tecla + kQuiet - 1u, false));
    TEST_ASSERT_TRUE(r.takeDue(tecla + kQuiet, false));
}

// Tecla segurada ou apertos seguidos renovam a espera a cada passagem.
static void test_uso_continuo_segura_o_reinicio(void) {
    app::DisplayRefresh r;
    r.start(kT0);
    uint32_t t = kT0 + kPeriod;
    for (uint8_t i = 0; i < 100u; ++i, t += 1000u) {
        r.noteKey(t);
        TEST_ASSERT_FALSE(r.takeDue(t, false));
    }
    TEST_ASSERT_FALSE(r.takeDue(t - 1000u + kQuiet - 1u, false));
    TEST_ASSERT_TRUE(r.takeDue(t - 1000u + kQuiet, false));
}

// Tecla velha, longe do vencimento, nao adia nada.
static void test_tecla_antiga_nao_adia(void) {
    app::DisplayRefresh r;
    r.start(kT0);
    r.noteKey(kT0 + 1000u);
    TEST_ASSERT_TRUE(r.takeDue(kT0 + kPeriod, false));
}

// Splash, autoteste ou radio de atualizacao no ar: a tela e de outro dono. Adia enquanto durar
// e reinicia na primeira passagem livre.
static void test_painel_ocupado_adia(void) {
    app::DisplayRefresh r;
    r.start(kT0);
    TEST_ASSERT_FALSE(r.takeDue(kT0 + kPeriod, true));
    TEST_ASSERT_FALSE(r.takeDue(kT0 + 3u * kPeriod, true));
    TEST_ASSERT_TRUE(r.takeDue(kT0 + 3u * kPeriod + 50u, false));
}

// Uma tecla apertada uma unica vez e depois esquecida: depois de 2^32 ms (49,7 dias) a diferenca
// nowMs - lastKey da a volta e pareceria recente. A marca tem de ser esquecida assim que a
// quietude vence.
static void test_tecla_esquecida_nao_volta_apos_o_wrap(void) {
    app::DisplayRefresh r;
    r.start(kT0);
    r.noteKey(kT0);
    TEST_ASSERT_TRUE(r.takeDue(kT0 + kPeriod, false));
    // 2^32 + 5 ms depois da tecla: nowMs - lastKey da 5, que pareceria tecla recente. SEM start()
    // no meio - start() tambem esquece a tecla e esconderia a regressao. O periodo esta vencido
    // (o ultimo reinicio foi em kT0 + kPeriod, ~49,7 dias antes).
    const uint32_t volta = kT0 + 5u;  // = kT0 + 2^32 + 5 em aritmetica de 32 bits
    TEST_ASSERT_TRUE(r.takeDue(volta, false));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_constantes_da_decisao_20);
    RUN_TEST(test_nao_vence_antes_do_periodo_e_vence_nele);
    RUN_TEST(test_vencer_rearma_o_periodo);
    RUN_TEST(test_tecla_recente_adia_ate_ficar_quieto);
    RUN_TEST(test_uso_continuo_segura_o_reinicio);
    RUN_TEST(test_tecla_antiga_nao_adia);
    RUN_TEST(test_painel_ocupado_adia);
    RUN_TEST(test_tecla_esquecida_nao_volta_apos_o_wrap);
    return UNITY_END();
}
