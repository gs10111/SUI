// A regra de fonte da IHM, isolada: use a MAIOR fonte em que a string inteira cabe.
//
// Existe porque escolher fonte tela por tela ja produziu duas vezes o mesmo defeito - linha
// legitima estourando a borda em silencio, porque IDisplay nao recorta. Com a regra medida, uma
// string que cresce amanha reencontra a fonte certa sem ninguem lembrar de revisar a tela.
//
// Metricas do FakeDisplay, medidas contra o u8g2 real: Small 7 px/glifo, Medium 9, Large 15.
#include <unity.h>

#include "domain/ui/text_fit.h"
#include "fakes/fake_display.h"

using domain::ui::fontThatFits;
using test::FakeDisplay;

void setUp(void) {}
void tearDown(void) {}

static void test_string_curta_sobe_para_medium(void) {
    FakeDisplay tela;
    // "Ajuste 0Vcc:0000" = 16 caracteres = 144 px em Medium, dentro dos 256
    TEST_ASSERT_TRUE(fontThatFits(tela, "Ajuste 0Vcc:0000", 256) == TextFont::Medium);
    TEST_ASSERT_TRUE(fontThatFits(tela, "CALIBRACAO BLOQUEADA", 256) == TextFont::Medium);
    TEST_ASSERT_TRUE(fontThatFits(tela, "Alteracao bem sucedida!", 256) == TextFont::Medium);
}

static void test_string_longa_desce_sozinha_para_small(void) {
    FakeDisplay tela;
    // 36 caracteres: 324 px em Medium, 252 em Small. So Small cabe nos 256.
    const char* longa = "Angulo fim de escala X(graus):+045,0";
    TEST_ASSERT_TRUE(fontThatFits(tela, longa, 256) == TextFont::Small);
    TEST_ASSERT_TRUE(tela.textWidthPx(TextFont::Small, longa) <= 256u);
    TEST_ASSERT_TRUE(tela.textWidthPx(TextFont::Medium, longa) > 256u);
    // a linha do editor de Valor Limite cai no mesmo ramo
    TEST_ASSERT_TRUE(fontThatFits(tela, "Valor Limite X1(graus):+000,0", 256) == TextFont::Small);
}

static void test_fonte_grande_so_quando_pedida_e_quando_cabe(void) {
    FakeDisplay tela;
    // Por padrao o teto e Medium: Large nunca aparece sem alguem pedir.
    TEST_ASSERT_TRUE(fontThatFits(tela, "X:+045,5", 256) == TextFont::Medium);
    TEST_ASSERT_TRUE(fontThatFits(tela, "X:+045,5", 256, TextFont::Large) == TextFont::Large);
    // 8 caracteres em Large = 120 px; com 119 disponiveis desce para Medium
    TEST_ASSERT_TRUE(fontThatFits(tela, "X:+045,5", 119, TextFont::Large) == TextFont::Medium);
}

static void test_teto_small_nunca_sobe(void) {
    FakeDisplay tela;
    TEST_ASSERT_TRUE(fontThatFits(tela, "ok", 256, TextFont::Small) == TextFont::Small);
}

static void test_entradas_degeneradas_caem_em_small_sem_estourar(void) {
    FakeDisplay tela;
    TEST_ASSERT_TRUE(fontThatFits(tela, nullptr, 256) == TextFont::Small);
    TEST_ASSERT_TRUE(fontThatFits(tela, "", 256) == TextFont::Small);
    TEST_ASSERT_TRUE(fontThatFits(tela, "algo", 0) == TextFont::Small);
    TEST_ASSERT_TRUE(fontThatFits(tela, "algo", -10) == TextFont::Small);
    // Nem Small cabe: devolve Small mesmo assim. Recortar nao e decisao desta camada - a linha
    // grande demais tem de aparecer grande demais no teste geometrico, e nao sumir aqui.
    TEST_ASSERT_TRUE(fontThatFits(tela, "qualquer coisa", 3) == TextFont::Small);
}

static void test_fronteira_exata_o_que_cabe_justo_ainda_sobe(void) {
    FakeDisplay tela;
    const char* texto = "12345678";                    // 8 glifos
    const uint16_t medium = tela.textWidthPx(TextFont::Medium, texto);  // 72
    TEST_ASSERT_TRUE(fontThatFits(tela, texto, static_cast<int16_t>(medium)) == TextFont::Medium);
    TEST_ASSERT_TRUE(fontThatFits(tela, texto, static_cast<int16_t>(medium - 1u)) ==
                     TextFont::Small);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_string_curta_sobe_para_medium);
    RUN_TEST(test_string_longa_desce_sozinha_para_small);
    RUN_TEST(test_fonte_grande_so_quando_pedida_e_quando_cabe);
    RUN_TEST(test_teto_small_nunca_sobe);
    RUN_TEST(test_entradas_degeneradas_caem_em_small_sem_estourar);
    RUN_TEST(test_fronteira_exata_o_que_cabe_justo_ainda_sobe);
    return UNITY_END();
}
