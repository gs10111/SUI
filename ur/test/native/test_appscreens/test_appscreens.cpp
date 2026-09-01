// As telas que a camada de aplicacao desenha: mensagem temporizada, CONFIG PERDIDA e a
// confirmacao do PSET.
//
// Todas as tres viviam dentro de src/main.cpp, que nao compila no env native. Nenhuma tinha
// teste, e uma delas tinha DEFEITO: renderMessage desenhava em fonte grande com a largura
// clampada em x=0, entao "FORA DA FAIXA +/-090,0" - 22 caracteres, 311 px em fonte grande num
// painel de 256 - saia da tela pela direita em silencio. IDisplay nao recorta e nenhuma
// afirmacao de conteudo enxerga isso: a string continua "aparecendo".
#include <string.h>
#include <unity.h>

#include "app/application.h"
#include "domain/parameters.h"
#include "domain/ui/preset_wizard.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_display.h"

using domain::Axis;
using domain::Parameters;
using domain::ui::PresetWizard;
using test::FakeClock;
using test::FakeDisplay;

namespace {

void conferirQuadro(const FakeDisplay& painel) {
    TEST_ASSERT_TRUE(painel.drawCount() > 0u);
    for (uint8_t i = 0; i < painel.drawCount(); ++i) {
        const FakeDisplay::Draw& d = painel.draw(i);
        const int32_t x1 =
            static_cast<int32_t>(d.x) + static_cast<int32_t>(painel.textWidthPx(d.font, d.text));
        const int32_t y1 =
            static_cast<int32_t>(d.y) + static_cast<int32_t>(painel.lineHeightPx(d.font));
        TEST_ASSERT_TRUE_MESSAGE(d.x >= 0, d.text);
        TEST_ASSERT_TRUE_MESSAGE(d.y >= 0, d.text);
        TEST_ASSERT_TRUE_MESSAGE(x1 <= static_cast<int32_t>(painel.widthPx()), d.text);
        TEST_ASSERT_TRUE_MESSAGE(y1 <= static_cast<int32_t>(painel.heightPx()), d.text);
        for (uint8_t j = static_cast<uint8_t>(i + 1u); j < painel.drawCount(); ++j) {
            const FakeDisplay::Draw& o = painel.draw(j);
            const int32_t ox1 = static_cast<int32_t>(o.x) +
                                static_cast<int32_t>(painel.textWidthPx(o.font, o.text));
            const int32_t oy1 = static_cast<int32_t>(o.y) +
                                static_cast<int32_t>(painel.lineHeightPx(o.font));
            TEST_ASSERT_FALSE_MESSAGE(d.x < ox1 && o.x < x1 && d.y < oy1 && o.y < y1, d.text);
        }
    }
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_mensagem_curta_usa_a_fonte_grande(void) {
    FakeDisplay tela;
    app::renderMessage(tela, "PSET aplicado!");

    TEST_ASSERT_TRUE(tela.showsExactly("PSET aplicado!"));
    TEST_ASSERT_TRUE(tela.fontOf("PSET aplicado!") == TextFont::Large);
    conferirQuadro(tela);
}

// O defeito real: 22 caracteres em fonte grande dao 330 px no painel de 256. Antes a linha
// saia pela direita; agora ela desce de fonte sozinha e cabe inteira.
static void test_mensagem_longa_desce_de_fonte_em_vez_de_vazar_da_tela(void) {
    FakeDisplay tela;
    app::renderMessage(tela, PresetWizard::kOutOfRangeText);

    TEST_ASSERT_TRUE(tela.showsExactly(PresetWizard::kOutOfRangeText));
    TEST_ASSERT_TRUE(tela.textWidthPx(TextFont::Large, PresetWizard::kOutOfRangeText) >
                     tela.widthPx());
    TEST_ASSERT_TRUE(tela.fontOf(PresetWizard::kOutOfRangeText) != TextFont::Large);
    conferirQuadro(tela);
}

static void test_todas_as_mensagens_do_produto_cabem_na_tela(void) {
    const char* mensagens[] = {
        "PSET aplicado!",
        PresetWizard::kRefusedNoDataText,
        PresetWizard::kRefusedUnstableText,
        PresetWizard::kOutOfRangeText,
    };
    for (unsigned i = 0; i < sizeof(mensagens) / sizeof(mensagens[0]); ++i) {
        FakeDisplay tela;
        app::renderMessage(tela, mensagens[i]);
        TEST_ASSERT_TRUE(tela.showsExactly(mensagens[i]));
        conferirQuadro(tela);
    }
}

static void test_config_perdida_traz_as_duas_linhas_e_cabe(void) {
    FakeDisplay tela;
    app::renderConfigLost(tela);

    TEST_ASSERT_TRUE(tela.showsExactly(app::kTextConfigLost));
    TEST_ASSERT_TRUE(tela.showsExactly(app::kTextConfigLostHint));
    conferirQuadro(tela);
}

static void test_confirmacao_de_pset_mostra_valor_e_gesto(void) {
    FakeClock relogio;
    FakeDisplay tela;
    PresetWizard preset(relogio);
    Parameters params = Parameters::factoryDefaults();

    // Deslocamento acima de kConfirmThresholdDeci (5,0 graus) e o que exige confirmacao.
    TEST_ASSERT_TRUE(preset.beginEdit(Axis::X, params));
    preset.cancelEdit();
    preset.onProgrammingExit();
    TEST_ASSERT_TRUE(preset.armed());
    for (uint8_t i = 0; i < PresetWizard::kStabilityWindow; ++i) {
        preset.sample(domain::Angle::fromDeciDegrees(300), domain::Angle::fromDeciDegrees(0));
    }

    TEST_ASSERT_TRUE(preset.requestPset(params) == domain::ui::PsetOutcome::NeedsConfirm);
    TEST_ASSERT_TRUE(preset.awaitingConfirm());

    app::renderPresetConfirm(tela, preset, Axis::X);

    TEST_ASSERT_TRUE(tela.showsExactly(PresetWizard::kConfirmHintText));
    conferirQuadro(tela);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_mensagem_curta_usa_a_fonte_grande);
    RUN_TEST(test_mensagem_longa_desce_de_fonte_em_vez_de_vazar_da_tela);
    RUN_TEST(test_todas_as_mensagens_do_produto_cabem_na_tela);
    RUN_TEST(test_config_perdida_traz_as_duas_linhas_e_cabe);
    RUN_TEST(test_confirmacao_de_pset_mostra_valor_e_gesto);
    return UNITY_END();
}
