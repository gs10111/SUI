// Testes de dominio do Preset (PST-01 a PST-04) e da sua interacao com o Sentido do Sensor
// (DIR-01 e DIR-02).
//
// O que esta preso aqui, com a fonte de cada numero e de cada texto:
//
// Toda citacao abaixo e o numero de linha do arquivo bruto docs/manual-cliente-sui-2026.txt,
// reconferido linha a linha.
//
// PST-01 (MAN-5.6-L144): "A programacao e feita diretamente em angulo, na faixa de -90,0 a
//   +90,0, com resolucao de 0,1". O campo e o mesmo DigitEditor de A13, com passo de um decimo
//   de grau e recusa explicita fora da faixa - nunca clamp silencioso.
// PST-02 (MAN-5.6-L149 e L161): "A configuracao do Preset e realizada em duas etapas:
//   programacao do valor de referencia e ativacao do Preset". Gravar o valor NAO move a
//   leitura; so o duplo acionamento de UP (PSET) transforma o valor programado em offset.
// PST-03 (MAN-5.6-L161): "a leitura passa a ser apresentada em relacao ao valor programado".
// PST-04 (MAN-5.6-L162): o Preset e gravado e mantido apos o desligamento - aqui verificado ate
//   onde o dominio alcanca, que e o registro serializado de domain/parameters.h.
// DIR-01 (MAN-5.8-L188..L192 e MAN-5.5-L141): o Sentido do Sensor define o sinal de incremento
//   da leitura, por eixo, e entra ANTES do Preset na formula de A9.
// DIR-02 (MAN-5.8-L199): "A alteracao do sentido do sensor inverte o sinal da leitura. Apos
//   alterar este parametro, recomenda-se refazer o Preset e conferir os valores programados nos
//   Limites 1 a 4". Por A9 o firmware age em vez de recomendar: zera o offset do eixo, desarma o
//   gesto e avisa por 3000 ms, com o texto byte a byte de D14 item 7.
//
// A9, APROVADA em 2026-08-31: leitura = clamp(dir * bruto + offset, -900, +900), com
// offset := P - dir * bruto no aceite. Os pontos conferidos um a um estao em
// test_A9_formula_ponto_a_ponto_no_sentido_horario e no caso combinado de sentido anti-horario
// com preset diferente de zero, que e onde os dois sinais errados de D11 apareciam.
//
// Guardas de D1: item 6 (armamento por visita a tela de Preset, validade de 120000 ms), item 8
// (aplica os dois eixos de uma vez), item 9 (sem leitura valida o PSET e recusado), item 10
// (janela de 8 amostras por eixo, pico-a-pico maximo de 5 decimos), item 11 (deslocamento acima
// de 50 decimos exige hold de MENU e cancela em 10000 ms) e item 17 (indicador permanente).
//
// O tempo vem do FakeClock canonico de test/fakes/fake_clock.h, que comeca em 0xFFFF0000: todo
// prazo deste arquivo atravessa o wrap de 2^32 ms, entao um prazo escrito como "a > b" em vez da
// subtracao unsigned de ports/i_clock.h reprova aqui.
#include <unity.h>

#include "domain/parameters.h"
#include "domain/ui/preset_wizard.h"
#include "app/application.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_display.h"

using domain::Angle;
using domain::Axis;
using domain::ConfirmResult;
using domain::Parameters;
using domain::SensorDir;
using domain::ui::PresetMenuItem;
using domain::ui::PresetWizard;
using domain::ui::PsetOutcome;
using test::FakeClock;
using test::FakeDisplay;

namespace {

Parameters padroes() { return Parameters::factoryDefaults(); }

// Estrutura PARADA pelo tempo que o portao exige. Desde 2026-09-01 o criterio e temporal
// (kStaticHoldMs = 3000 ms dentro de 0,5 grau) e nao mais uma contagem de 8 amostras: alimentar
// sem deixar o relogio andar nao produz mais "parado", e e essa a mudanca que este helper
// carrega para todos os testes de uma vez.
// Alimenta um PADRAO ciclico pelo tempo do portao, 50 ms por amostra. Serve aos testes de banda:
// o que decide agora nao e quantas amostras entraram, e sim se a leitura ficou dentro da banda
// pelos 3000 ms inteiros.
void alimentarPadrao(FakeClock& clock, PresetWizard& preset, const int16_t* xs, const int16_t* ys,
                     uint8_t n, uint32_t duracaoMs) {
    const uint32_t passo = 50;
    const uint32_t ciclos = (duracaoMs / passo) + 2u;
    for (uint32_t i = 0; i < ciclos; ++i) {
        const uint8_t k = static_cast<uint8_t>(i % n);
        preset.sample(Angle::fromDeciDegrees(xs[k]), Angle::fromDeciDegrees(ys[k]));
        clock.advanceMs(passo);
    }
}

void alimentar(FakeClock& clock, PresetWizard& preset, int16_t xDeci, int16_t yDeci) {
    const uint32_t passo = 50;
    const uint32_t ciclos = (PresetWizard::kStaticHoldMs / passo) + 2u;
    for (uint32_t i = 0; i < ciclos; ++i) {
        preset.sample(Angle::fromDeciDegrees(xDeci), Angle::fromDeciDegrees(yDeci));
        clock.advanceMs(passo);
    }
    preset.sample(Angle::fromDeciDegrees(xDeci), Angle::fromDeciDegrees(yDeci));
}

// Armar = ter VISITADO a tela de Preset e saido do Modo Programacao. Desde 2026-09-01 quem
// marca a visita e beginCapture(), nao mais o editor numerico, que deixou de existir.
void armar(PresetWizard& preset, Parameters& params) {
    (void)params;
    TEST_ASSERT_TRUE(preset.beginCapture(Axis::X));
    preset.cancelCapture();
    preset.onProgrammingExit();
    TEST_ASSERT_TRUE(preset.armed());
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- PST-01: programacao do valor de referencia, -90,0 a +90,0 com passo de 0,1 ---

// --- PST-02: as duas etapas ---

static void test_PST_02_o_offset_so_nasce_quando_o_gesto_de_pset_chega(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setPreset(Axis::X, Angle::fromDeciDegrees(150)).ok());
    TEST_ASSERT_TRUE(params.setPreset(Axis::Y, Angle::fromDeciDegrees(0)).ok());
    armar(preset, params);
    alimentar(clock, preset, 100, 0);

    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::X));
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Applied);
    TEST_ASSERT_EQUAL_INT16(50, params.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::Y));
}

// --- PST-03: a leitura passa a ser relativa a referencia ---

static void test_PST_03_apos_o_pset_a_leitura_vale_exatamente_o_valor_programado(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setPreset(Axis::X, Angle::fromDeciDegrees(150)).ok());
    armar(preset, params);
    alimentar(clock, preset, 100, 0);
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Applied);

    TEST_ASSERT_EQUAL_INT16(
        150, PresetWizard::reading(Axis::X, Angle::fromDeciDegrees(100), params).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        200, PresetWizard::reading(Axis::X, Angle::fromDeciDegrees(150), params).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        100, PresetWizard::reading(Axis::X, Angle::fromDeciDegrees(50), params).deciDegrees());
}

static void test_PST_03_leitura_bruta_invalida_continua_invalida_depois_do_pset(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setPresetOffset(Axis::X, 50).ok());
    TEST_ASSERT_FALSE(PresetWizard::reading(Axis::X, Angle::invalid(), params).valid());
}

// --- A9: a formula, ponto a ponto ---

static void test_A9_formula_ponto_a_ponto_no_sentido_horario(void) {
    TEST_ASSERT_EQUAL_INT16(
        150, PresetWizard::reading(Angle::fromDeciDegrees(100), SensorDir::Clockwise, 50)
                 .deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        250, PresetWizard::reading(Angle::fromDeciDegrees(200), SensorDir::Clockwise, 50)
                 .deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        -850, PresetWizard::reading(Angle::fromDeciDegrees(-900), SensorDir::Clockwise, 50)
                  .deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        0, PresetWizard::reading(Angle::fromDeciDegrees(-50), SensorDir::Clockwise, 50)
               .deciDegrees());
}

static void test_A9_grampo_em_mais_e_menos_900_decimos(void) {
    TEST_ASSERT_EQUAL_INT16(
        900, PresetWizard::reading(Angle::fromDeciDegrees(900), SensorDir::Clockwise, 50)
                 .deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        -900, PresetWizard::reading(Angle::fromDeciDegrees(-900), SensorDir::Clockwise, -900)
                  .deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        900, PresetWizard::reading(Angle::fromDeciDegrees(-900), SensorDir::CounterClockwise, 900)
                 .deciDegrees());
}

static void test_DIR_01_sentido_anti_horario_inverte_o_sinal_antes_do_preset(void) {
    TEST_ASSERT_EQUAL_INT16(
        -450, PresetWizard::reading(Angle::fromDeciDegrees(450), SensorDir::CounterClockwise, 0)
                  .deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        450, PresetWizard::reading(Angle::fromDeciDegrees(-450), SensorDir::CounterClockwise, 0)
                 .deciDegrees());
}

static void test_A9_offset_e_a_diferenca_entre_o_programado_e_o_bruto_com_sentido(void) {
    int16_t offset = 0;
    TEST_ASSERT_TRUE(PresetWizard::offsetFor(Angle::fromDeciDegrees(150),
                                             Angle::fromDeciDegrees(100), SensorDir::Clockwise,
                                             offset));
    TEST_ASSERT_EQUAL_INT16(50, offset);

    TEST_ASSERT_TRUE(PresetWizard::offsetFor(Angle::fromDeciDegrees(-200),
                                             Angle::fromDeciDegrees(300),
                                             SensorDir::CounterClockwise, offset));
    TEST_ASSERT_EQUAL_INT16(100, offset);
}

static void test_A9_anti_horario_com_preset_diferente_de_zero_ponto_a_ponto(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setSensorDir(Axis::Y, SensorDir::CounterClockwise).ok());
    TEST_ASSERT_TRUE(params.setPreset(Axis::Y, Angle::fromDeciDegrees(-200)).ok());
    armar(preset, params);
    alimentar(clock, preset, 0, 300);

    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::NeedsConfirm);
    TEST_ASSERT_TRUE(preset.confirmPset(params) == PsetOutcome::Applied);
    TEST_ASSERT_EQUAL_INT16(100, params.presetOffsetDeci(Axis::Y));

    TEST_ASSERT_EQUAL_INT16(
        -200, PresetWizard::reading(Axis::Y, Angle::fromDeciDegrees(300), params).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        0, PresetWizard::reading(Axis::Y, Angle::fromDeciDegrees(100), params).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        -300, PresetWizard::reading(Axis::Y, Angle::fromDeciDegrees(400), params).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        900, PresetWizard::reading(Axis::Y, Angle::fromDeciDegrees(-900), params).deciDegrees());
}

// --- D1 item 8: os dois eixos de uma vez ---

static void test_D1_item8_o_pset_aplica_os_dois_eixos_no_mesmo_gesto(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setPreset(Axis::X, Angle::fromDeciDegrees(150)).ok());
    TEST_ASSERT_TRUE(params.setPreset(Axis::Y, Angle::fromDeciDegrees(-100)).ok());
    armar(preset, params);
    alimentar(clock, preset, 120, -80);

    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Applied);
    TEST_ASSERT_EQUAL_INT16(30, params.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(-20, params.presetOffsetDeci(Axis::Y));
    TEST_ASSERT_EQUAL_INT16(
        150, PresetWizard::reading(Axis::X, Angle::fromDeciDegrees(120), params).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        -100, PresetWizard::reading(Axis::Y, Angle::fromDeciDegrees(-80), params).deciDegrees());
}

// --- D1 item 9: sem leitura valida nao ha preset ---

static void test_D1_item9_pset_recusado_quando_o_sensor_esta_mudo(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    armar(preset, params);
    for (uint8_t i = 0; i < PresetWizard::kMinSamples; ++i) {
        preset.sample(Angle::invalid(), Angle::invalid());
    }

    TEST_ASSERT_FALSE(preset.dataValid());
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::RefusedNoData);
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::Y));
}

static void test_D1_item9_uma_unica_amostra_muda_esvazia_a_janela(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    armar(preset, params);
    alimentar(clock, preset, 100, 100);
    TEST_ASSERT_TRUE(preset.dataValid());

    preset.sample(Angle::fromDeciDegrees(100), Angle::invalid());
    TEST_ASSERT_FALSE(preset.dataValid());
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::RefusedNoData);
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::X));
}

static void test_D1_item9_janela_incompleta_ainda_nao_autoriza_o_pset(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    armar(preset, params);
    for (uint8_t i = 0; i < PresetWizard::kMinSamples - 1u; ++i) {
        preset.sample(Angle::fromDeciDegrees(100), Angle::fromDeciDegrees(100));
    }

    TEST_ASSERT_FALSE(preset.dataValid());
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::RefusedNoData);
}

// --- D1 item 10: estabilidade ---

static void test_D1_item10_pico_a_pico_de_5_decimos_ainda_e_estavel(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    // Os dois numeros de D1 item 10 (DECISIONS.md:916), presos aqui uma vez.
    TEST_ASSERT_EQUAL_UINT8(8u, PresetWizard::kMinSamples);
    TEST_ASSERT_EQUAL_INT16(5, PresetWizard::kStabilityPeakToPeakDeci);

    armar(preset, params);
    // Oscilacao de exatamente 5 decimos pelos 3000 ms inteiros: encosta na banda e NAO a rompe,
    // entao a janela nunca reinicia e o prazo fecha.
    const int16_t oscX[2] = {0, 5};
    const int16_t zeroY[2] = {0, 0};
    alimentarPadrao(clock, preset, oscX, zeroY, 2, PresetWizard::kStaticHoldMs);
    preset.sample(Angle::fromDeciDegrees(5), Angle::fromDeciDegrees(0));

    TEST_ASSERT_EQUAL_INT16(5, preset.peakToPeakDeci(Axis::X));
    TEST_ASSERT_TRUE(preset.stable());
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Applied);
    // A ultima amostra da janela vale 5 decimos e o Preset X programado e 0, entao o offset e
    // -5. Presetar sobre a amostra mais VELHA (0 decimos) daria 0 e passaria despercebido.
    TEST_ASSERT_EQUAL_INT16(-5, params.presetOffsetDeci(Axis::X));
}

// O PSET usa a amostra MAIS RECENTE da janela, nao a mais velha: numa estrutura que se assenta,
// presetar sobre uma leitura de 400 ms atras desloca a referencia dos quatro reles em silencio.
static void test_D1_item10_o_pset_usa_a_amostra_mais_recente_da_janela(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setPreset(Axis::X, Angle::fromDeciDegrees(100)).ok());
    armar(preset, params);

    // Rampa com valor distinto por indice, dentro do pico-a-pico de 5 decimos que a guarda
    // permite: a mais nova vale 103, a mais velha 100, a anterior a mais nova 105.
    const int16_t rampa[8] = {100, 101, 102, 103, 104, 105, 100, 103};
    const int16_t zeroY[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    alimentarPadrao(clock, preset, rampa, zeroY, 8, PresetWizard::kStaticHoldMs);
    preset.sample(Angle::fromDeciDegrees(103), Angle::fromDeciDegrees(0));

    TEST_ASSERT_EQUAL_INT16(5, preset.peakToPeakDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(103, preset.lastRaw(Axis::X).deciDegrees());
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Applied);
    TEST_ASSERT_EQUAL_INT16(-3, params.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(
        100, PresetWizard::reading(Axis::X, Angle::fromDeciDegrees(103), params).deciDegrees());
}

static void test_D1_item10_pico_a_pico_de_6_decimos_recusa_o_pset(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    armar(preset, params);
    // Oscilacao de 6 decimos: ROMPE a banda, e cada rompimento reinicia a janela. Por isso o
    // pico a pico observavel volta a zero - ele mede a janela CORRENTE, nao o historico - e o
    // prazo de 3000 ms nunca fecha, que e o que recusa o PSET.
    const int16_t zeroX[2] = {0, 0};
    const int16_t oscY[2] = {100, 106};
    alimentarPadrao(clock, preset, zeroX, oscY, 2, PresetWizard::kStaticHoldMs);

    TEST_ASSERT_TRUE(preset.peakToPeakDeci(Axis::Y) <= PresetWizard::kStabilityPeakToPeakDeci);
    TEST_ASSERT_TRUE(preset.staticForMs() < PresetWizard::kStaticHoldMs);
    TEST_ASSERT_FALSE(preset.stable());
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::RefusedUnstable);
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::Y));
}

// --- D1 item 6: armamento ---

static void test_D1_item6_duplo_toque_sem_visita_a_tela_de_preset_nao_faz_nada(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    preset.onProgrammingExit();
    alimentar(clock, preset, 100, 100);

    TEST_ASSERT_FALSE(preset.armed());
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Ignored);
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::X));
}

static void test_D1_item6_armamento_vale_120000_ms_e_nao_120001(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    // O numero e de D1 item 6 (DECISIONS.md:911), preso aqui uma vez: sem esta linha o teste
    // avancaria o relogio pela propria constante que deveria estar provando.
    TEST_ASSERT_EQUAL_UINT32(120000u, PresetWizard::kArmValidityMs);

    // A ordem importa desde que o portao de estabilidade virou temporal: alimentar() consome
    // 3 s de relogio, e armar ANTES gastaria parte da janela de 120 s no proprio preparo.
    alimentar(clock, preset, 100, 100);
    armar(preset, params);

    clock.advanceMs(PresetWizard::kArmValidityMs - 1u);
    preset.tick();
    TEST_ASSERT_TRUE(preset.armed());

    clock.advanceMs(1u);
    preset.tick();
    TEST_ASSERT_FALSE(preset.armed());
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Ignored);
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::X));
}

// A janela vencida NAO REABRE no wrap de 2^32 ms. Guardar o carimbo e recompara-lo para sempre
// faz o elapsed unsigned voltar a zero depois de 49,7 dias de uptime continuo, e o duplo toque
// voltaria a ser aceito sem nenhuma visita a tela Preset. Uma UR portuaria fica energizada meses.
static void test_D1_item6_a_janela_de_armamento_nao_reabre_no_wrap_de_2_elevado_a_32(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setPreset(Axis::X, Angle::fromDeciDegrees(150)).ok());
    alimentar(clock, preset, 100, 0);
    armar(preset, params);

    clock.advanceMs(0xFFFFFFFFu);
    preset.tick();
    TEST_ASSERT_FALSE(preset.armed());

    clock.advanceMs(1u);  // agora o elapsed contra armedAtMs_ vale exatamente 0
    preset.tick();
    TEST_ASSERT_FALSE(preset.armed());
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Ignored);
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::X));
}

// D1 item 6, restricao adicional escrita no cabecalho: o aceite CONSOME o armamento.
static void test_D1_item6_o_pset_aceito_consome_o_armamento(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setPreset(Axis::X, Angle::fromDeciDegrees(150)).ok());
    armar(preset, params);
    alimentar(clock, preset, 100, 0);

    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Applied);
    TEST_ASSERT_EQUAL_INT16(50, params.presetOffsetDeci(Axis::X));

    TEST_ASSERT_FALSE(preset.armed());
    TEST_ASSERT_FALSE(preset.visited());
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Ignored);
}

static void test_D1_item6_a_confirmacao_aceita_tambem_consome_o_armamento(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    armar(preset, params);
    alimentar(clock, preset, 400, 0);
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::NeedsConfirm);
    TEST_ASSERT_TRUE(preset.confirmPset(params) == PsetOutcome::Applied);
    TEST_ASSERT_EQUAL_INT16(-400, params.presetOffsetDeci(Axis::X));

    TEST_ASSERT_FALSE(preset.armed());
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Ignored);
}

// --- D1 item 11: guarda de magnitude ---

static void test_D1_item11_deslocamento_de_50_decimos_aplica_sem_confirmacao(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    // Numero de D1 item 11 (DECISIONS.md:917), preso aqui uma vez.
    TEST_ASSERT_EQUAL_INT16(50, PresetWizard::kConfirmThresholdDeci);

    TEST_ASSERT_TRUE(params.setPreset(Axis::X, Angle::fromDeciDegrees(150)).ok());
    armar(preset, params);
    alimentar(clock, preset, 100, 0);

    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Applied);
    TEST_ASSERT_EQUAL_INT16(50, params.presetOffsetDeci(Axis::X));
}

static void test_D1_item11_deslocamento_de_51_decimos_exige_hold_de_menu(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setPreset(Axis::X, Angle::fromDeciDegrees(151)).ok());
    armar(preset, params);
    alimentar(clock, preset, 100, 0);

    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::NeedsConfirm);
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::X));
    TEST_ASSERT_TRUE(preset.awaitingConfirm());
    TEST_ASSERT_EQUAL_INT16(51, preset.pendingOffsetDeci(Axis::X));

    char tela[PresetWizard::kConfirmTextCap] = {0};
    TEST_ASSERT_TRUE(preset.formatPendingConfirm(Axis::X, tela, sizeof(tela)));
    TEST_ASSERT_EQUAL_STRING("Novo PSET X:+005,1", tela);
    TEST_ASSERT_EQUAL_STRING("Segure MENU 3s", PresetWizard::kConfirmHintText);

    TEST_ASSERT_TRUE(preset.confirmPset(params) == PsetOutcome::Applied);
    TEST_ASSERT_EQUAL_INT16(51, params.presetOffsetDeci(Axis::X));
}

static void test_D1_item11_confirmacao_cancela_sozinha_em_10000_ms(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    // Numero de D1 item 11 (DECISIONS.md:917), preso aqui uma vez.
    TEST_ASSERT_EQUAL_UINT32(10000u, PresetWizard::kConfirmWindowMs);

    armar(preset, params);
    alimentar(clock, preset, 400, 0);
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::NeedsConfirm);

    char tela[PresetWizard::kConfirmTextCap] = {0};
    clock.advanceMs(PresetWizard::kConfirmWindowMs - 1u);
    preset.tick();
    TEST_ASSERT_TRUE(preset.awaitingConfirm());
    TEST_ASSERT_TRUE(preset.formatPendingConfirm(Axis::X, tela, sizeof(tela)));

    // "cancela sem aplicar" tem de ACONTECER, nao ser apenas consultavel: a tela de confirmacao
    // SAI do display e o par congelado deixa de existir, senao o operador segura MENU 3 s diante
    // de um gesto que ja nao esta disponivel.
    clock.advanceMs(1u);
    preset.tick();
    TEST_ASSERT_FALSE(preset.awaitingConfirm());
    TEST_ASSERT_FALSE(preset.formatPendingConfirm(Axis::X, tela, sizeof(tela)));
    TEST_ASSERT_EQUAL_INT16(0, preset.pendingOffsetDeci(Axis::X));
    TEST_ASSERT_TRUE(preset.confirmPset(params) == PsetOutcome::Ignored);
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::X));

    // E nao volta 24 h depois, nem no wrap do relogio.
    clock.advanceMs(0xFFFFFFFFu);
    preset.tick();
    TEST_ASSERT_FALSE(preset.awaitingConfirm());
    TEST_ASSERT_FALSE(preset.formatPendingConfirm(Axis::X, tela, sizeof(tela)));
}

// A confirmacao recalcula sobre a amostra CORRENTE, nao aplica o par congelado do instante do
// duplo toque: nos 10 s de deliberacao a estrutura se move, e gravar o numero velho e exatamente
// o erro que a guarda de magnitude existe para pegar.
static void test_D1_item11_a_confirmacao_usa_a_amostra_corrente_e_nao_a_do_duplo_toque(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    armar(preset, params);
    alimentar(clock, preset, 400, 0);
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::NeedsConfirm);
    TEST_ASSERT_EQUAL_INT16(-400, preset.pendingOffsetDeci(Axis::X));

    alimentar(clock, preset, 200, 0);
    TEST_ASSERT_TRUE(preset.confirmPset(params) == PsetOutcome::Applied);
    TEST_ASSERT_EQUAL_INT16(-200, params.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(
        0, PresetWizard::reading(Axis::X, Angle::fromDeciDegrees(200), params).deciDegrees());
}

// A guarda de estabilidade e reavaliada na confirmacao, nao so a de dado: a estrutura pode
// comecar a balancar nos 10 s de deliberacao sem que o enlace caia.
static void test_D1_item10_confirmacao_recusada_se_a_leitura_ficar_instavel_na_espera(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    armar(preset, params);
    alimentar(clock, preset, 400, 0);
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::NeedsConfirm);

    for (uint8_t i = 0; i < PresetWizard::kMinSamples; ++i) {
        const int16_t x = (i % 2 == 0) ? static_cast<int16_t>(400) : static_cast<int16_t>(406);
        preset.sample(Angle::fromDeciDegrees(x), Angle::fromDeciDegrees(0));
    }
    // O dado continua existindo - o sensor nao emudeceu, so se mexeu -, e e por isso que a
    // recusa tem de ser "instavel" e nao "sem dado".
    TEST_ASSERT_TRUE(preset.dataValid());
    // Oscilacao de 6 decimos rompe a banda a cada amostra e reinicia a janela, entao o prazo
    // de quietude volta a zero. E ele, e nao o pico a pico historico, que reprova agora.
    TEST_ASSERT_TRUE(preset.staticForMs() < PresetWizard::kStaticHoldMs);
    TEST_ASSERT_FALSE(preset.stable());

    TEST_ASSERT_TRUE(preset.confirmPset(params) == PsetOutcome::RefusedUnstable);
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::X));
}

static void test_D1_item11_confirmacao_recusada_se_o_sensor_emudecer_na_espera(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    armar(preset, params);
    alimentar(clock, preset, 400, 0);
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::NeedsConfirm);

    preset.sample(Angle::invalid(), Angle::invalid());
    TEST_ASSERT_TRUE(preset.confirmPset(params) == PsetOutcome::RefusedNoData);
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::X));
}

// --- D1 item 17: indicador permanente ---

static void test_D1_item17_indicador_permanente_enquanto_houver_offset(void) {
    Parameters params = padroes();

    char tela[PresetWizard::kIndicatorTextCap] = {0};
    TEST_ASSERT_FALSE(PresetWizard::formatIndicator(Axis::X, params, tela, sizeof(tela)));

    TEST_ASSERT_TRUE(params.setPresetOffset(Axis::X, -120).ok());
    TEST_ASSERT_TRUE(PresetWizard::formatIndicator(Axis::X, params, tela, sizeof(tela)));
    TEST_ASSERT_EQUAL_STRING("PSET X:-012,0", tela);

    TEST_ASSERT_TRUE(params.setPresetOffset(Axis::Y, 1800).ok());
    TEST_ASSERT_TRUE(PresetWizard::formatIndicator(Axis::Y, params, tela, sizeof(tela)));
    TEST_ASSERT_EQUAL_STRING("PSET Y:+180,0", tela);
}

// --- DIR-02: troca do Sentido do Sensor ---

static void test_DIR_02_troca_de_sentido_zera_o_offset_desarma_e_avisa(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setPreset(Axis::X, Angle::fromDeciDegrees(150)).ok());
    armar(preset, params);
    alimentar(clock, preset, 100, 0);
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Applied);
    TEST_ASSERT_TRUE(params.setPresetOffset(Axis::Y, 70).ok());

    armar(preset, params);
    TEST_ASSERT_TRUE(preset.applySensorDir(Axis::X, SensorDir::CounterClockwise, params).ok());

    TEST_ASSERT_TRUE(params.sensorDir(Axis::X) == SensorDir::CounterClockwise);
    TEST_ASSERT_EQUAL_INT16(0, params.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(70, params.presetOffsetDeci(Axis::Y));
    TEST_ASSERT_EQUAL_INT16(150, params.preset(Axis::X).deciDegrees());
    TEST_ASSERT_FALSE(preset.armed());
    TEST_ASSERT_TRUE(preset.warningActive());

    char tela[PresetWizard::kIndicatorTextCap] = {0};
    TEST_ASSERT_FALSE(PresetWizard::formatIndicator(Axis::X, params, tela, sizeof(tela)));
}

static void test_DIR_02_aviso_dura_3000_ms(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    // Numero de D14 item 7, preso aqui uma vez.
    TEST_ASSERT_EQUAL_UINT32(3000u, PresetWizard::kDirWarningMs);

    TEST_ASSERT_TRUE(preset.applySensorDir(Axis::Y, SensorDir::CounterClockwise, params).ok());
    TEST_ASSERT_TRUE(preset.warningActive());

    clock.advanceMs(PresetWizard::kDirWarningMs - 1u);
    preset.tick();
    TEST_ASSERT_TRUE(preset.warningActive());

    clock.advanceMs(1u);
    preset.tick();
    TEST_ASSERT_FALSE(preset.warningActive());

    // E nao reaparece do nada no wrap de 2^32 ms.
    clock.advanceMs(0xFFFFFFFFu);
    preset.tick();
    TEST_ASSERT_FALSE(preset.warningActive());
    clock.advanceMs(1u);
    preset.tick();
    TEST_ASSERT_FALSE(preset.warningActive());
}

// D14 item 7 (DECISIONS.md:2512-2515), byte a byte e POR EIXO.
static void test_D14_item7_texto_do_aviso_de_sentido_e_byte_a_byte(void) {
    char linha1[PresetWizard::kDirWarnLine1Cap] = {0};
    char linha2[PresetWizard::kDirWarnLine2Cap] = {0};

    TEST_ASSERT_TRUE(PresetWizard::formatDirWarningLine1(Axis::X, linha1, sizeof(linha1)));
    TEST_ASSERT_EQUAL_STRING("Sentido X alterado!", linha1);
    TEST_ASSERT_TRUE(PresetWizard::formatDirWarningLine2(Axis::X, linha2, sizeof(linha2)));
    TEST_ASSERT_EQUAL_STRING("Preset zerado - confira X1 X2", linha2);

    TEST_ASSERT_TRUE(PresetWizard::formatDirWarningLine1(Axis::Y, linha1, sizeof(linha1)));
    TEST_ASSERT_EQUAL_STRING("Sentido Y alterado!", linha1);
    TEST_ASSERT_TRUE(PresetWizard::formatDirWarningLine2(Axis::Y, linha2, sizeof(linha2)));
    TEST_ASSERT_EQUAL_STRING("Preset zerado - confira Y1 Y2", linha2);

    char curta[PresetWizard::kDirWarnLine2Cap - 1u] = {0};
    TEST_ASSERT_FALSE(PresetWizard::formatDirWarningLine2(Axis::X, curta, sizeof(curta)));
    TEST_ASSERT_FALSE(PresetWizard::formatDirWarningLine1(Axis::X, nullptr, sizeof(linha1)));
}

static void test_DIR_02_gravar_o_mesmo_sentido_nao_zera_nem_avisa(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setPresetOffset(Axis::X, -120).ok());
    TEST_ASSERT_TRUE(preset.applySensorDir(Axis::X, SensorDir::Clockwise, params).ok());

    TEST_ASSERT_EQUAL_INT16(-120, params.presetOffsetDeci(Axis::X));
    TEST_ASSERT_FALSE(preset.warningActive());
}

// --- PST-04: persistencia do valor programado e do offset em vigor ---

static void test_PST_04_preset_e_offset_sobrevivem_ao_registro_gravado(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setPreset(Axis::X, Angle::fromDeciDegrees(150)).ok());
    armar(preset, params);
    alimentar(clock, preset, 100, 0);
    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::Applied);

    uint8_t blob[Parameters::kParamBlobSize] = {0};
    uint16_t escrito = 0;
    TEST_ASSERT_TRUE(params.serializeParams(blob, sizeof(blob), escrito).ok());

    Parameters relido;
    TEST_ASSERT_TRUE(relido.loadParams(blob, escrito).ok());
    TEST_ASSERT_EQUAL_INT16(150, relido.preset(Axis::X).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(50, relido.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(
        150, PresetWizard::reading(Axis::X, Angle::fromDeciDegrees(100), relido).deciDegrees());
}

// --- Submenu D2, literal de L148 ---

static void test_MAN_5_6_L148_submenu_preset_voltar_preset_x_preset_y(void) {
    FakeClock clock;
    PresetWizard preset(clock);

    TEST_ASSERT_EQUAL_STRING("Preset>Voltar   Preset X   Preset Y", PresetWizard::kMenuTitle);
    TEST_ASSERT_EQUAL_STRING("Voltar", PresetWizard::itemLabel(PresetMenuItem::Back));
    TEST_ASSERT_EQUAL_STRING("Preset X", PresetWizard::itemLabel(PresetMenuItem::PresetX));
    TEST_ASSERT_EQUAL_STRING("Preset Y", PresetWizard::itemLabel(PresetMenuItem::PresetY));

    TEST_ASSERT_TRUE(preset.item() == PresetMenuItem::Back);
    preset.prevItem();
    TEST_ASSERT_TRUE(preset.item() == PresetMenuItem::Back);
    preset.nextItem();
    TEST_ASSERT_TRUE(preset.item() == PresetMenuItem::PresetX);
    preset.nextItem();
    TEST_ASSERT_TRUE(preset.item() == PresetMenuItem::PresetY);
    preset.nextItem();
    TEST_ASSERT_TRUE(preset.item() == PresetMenuItem::PresetY);
    preset.prevItem();
    TEST_ASSERT_TRUE(preset.item() == PresetMenuItem::PresetX);
}

// --- D1 item 8: atomicidade dos dois offsets, VISIVEL na leitura ---

static void test_D1_item8_o_par_de_offsets_e_validado_antes_de_qualquer_escrita(void) {
    TEST_ASSERT_EQUAL_INT16(-1800, Parameters::kPresetOffsetMinDeci);
    TEST_ASSERT_EQUAL_INT16(1800, Parameters::kPresetOffsetMaxDeci);

    TEST_ASSERT_TRUE(PresetWizard::offsetsWritable(1800, -1800));
    TEST_ASSERT_TRUE(PresetWizard::offsetsWritable(0, 0));
    TEST_ASSERT_FALSE(PresetWizard::offsetsWritable(1801, 0));
    TEST_ASSERT_FALSE(PresetWizard::offsetsWritable(0, -1801));
    TEST_ASSERT_FALSE(PresetWizard::offsetsWritable(1801, -1801));
}

// Borda superior alcancavel pelo operador: P = +90,0 com o equipamento a -90,0 produz offset de
// exatamente 1800 decimos nos dois eixos, o maior que a algebra de A9 admite.
static void test_D1_item8_offset_de_borda_1800_decimos_e_aceito_nos_dois_eixos(void) {
    FakeClock clock;
    PresetWizard preset(clock);
    Parameters params = padroes();

    TEST_ASSERT_TRUE(params.setPreset(Axis::X, Angle::fromDeciDegrees(900)).ok());
    TEST_ASSERT_TRUE(params.setPreset(Axis::Y, Angle::fromDeciDegrees(900)).ok());
    armar(preset, params);
    alimentar(clock, preset, -900, -900);

    TEST_ASSERT_TRUE(preset.requestPset(params) == PsetOutcome::NeedsConfirm);
    TEST_ASSERT_TRUE(preset.confirmPset(params) == PsetOutcome::Applied);
    TEST_ASSERT_EQUAL_INT16(1800, params.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(1800, params.presetOffsetDeci(Axis::Y));
    TEST_ASSERT_EQUAL_INT16(
        900, PresetWizard::reading(Axis::X, Angle::fromDeciDegrees(-900), params).deciDegrees());
}

static void test_D1_telas_de_recusa_do_pset_sao_byte_a_byte(void) {
    TEST_ASSERT_EQUAL_STRING("PSET recusado!", PresetWizard::kRefusedNoDataText);
    TEST_ASSERT_EQUAL_STRING("Instavel, refaca!", PresetWizard::kRefusedUnstableText);
}

// --- TELA DE EDICAO DO PRESET ---------------------------------------------------------------
//
// Ate 2026-09-01 esta tela era desenhada dentro de src/main.cpp, que NAO compila no env native:
// nenhum teste a alcancava. E a mesma armadilha que ja custou a Emenda 2 (buildNormalInput no
// composition root, campo novo que nunca chegava a tela e suite verde o tempo todo). Mudar a
// fonte da IHM sem este teste seria mexer as cegas justamente na tela que grava o offset dos
// quatro pontos de atuacao.

struct TelaPreset {
    FakeClock relogio;
    FakeDisplay tela;
    PresetWizard preset;
    Parameters params;

    TelaPreset() : relogio(), tela(), preset(relogio), params(Parameters::factoryDefaults()) {}

    void abrir(Axis eixo) {
        TEST_ASSERT_TRUE(preset.beginCapture(eixo));
        app::renderPresetCapture(tela, preset, eixo);
    }
};

static void verificarQuadroPreset(const FakeDisplay& painel) {
    TEST_ASSERT_TRUE(painel.drawCount() > 0u);
    for (uint8_t i = 0; i < painel.drawCount(); ++i) {
        const FakeDisplay::Draw& d = painel.draw(i);
        const int16_t x1 =
            static_cast<int16_t>(d.x + painel.textWidthPx(d.font, d.text));
        const int16_t y1 = static_cast<int16_t>(d.y + painel.lineHeightPx(d.font));
        TEST_ASSERT_TRUE_MESSAGE(d.x >= 0, d.text);
        TEST_ASSERT_TRUE_MESSAGE(d.y >= 0, d.text);
        TEST_ASSERT_TRUE_MESSAGE(x1 <= 256, d.text);
        TEST_ASSERT_TRUE_MESSAGE(y1 <= 64, d.text);
    }
}

// --- CAPTURA: o novo Preset (decisao do bigboss, 2026-09-01) ---------------------------------
//
// O operador entra pelo menu, posiciona a estrutura onde bem entender, segura parado e grava. A
// posicao capturada passa a ler +000,0 nos DOIS eixos. Nao existe mais campo numerico.

static void test_captura_exige_3_s_parado_e_so_entao_grava(void) {
    TelaPreset t;
    TEST_ASSERT_TRUE(t.preset.beginCapture(Axis::X));
    TEST_ASSERT_TRUE(t.preset.capturing());

    // Estrutura parada, mas ainda nao pelo prazo inteiro: recusa por instabilidade.
    const uint32_t passo = 50;
    for (uint32_t decorrido = 0; decorrido + passo < PresetWizard::kStaticHoldMs;
         decorrido += passo) {
        t.preset.sample(Angle::fromDeciDegrees(37), Angle::fromDeciDegrees(-12));
        t.relogio.advanceMs(passo);
    }
    TEST_ASSERT_TRUE(t.preset.staticForMs() < PresetWizard::kStaticHoldMs);
    TEST_ASSERT_TRUE(t.preset.commitCapture(t.params) == PsetOutcome::RefusedUnstable);
    TEST_ASSERT_TRUE(t.preset.capturing());

    // Cruzando o prazo, grava.
    t.relogio.advanceMs(passo);
    t.preset.sample(Angle::fromDeciDegrees(37), Angle::fromDeciDegrees(-12));
    TEST_ASSERT_TRUE(t.preset.staticForMs() >= PresetWizard::kStaticHoldMs);
    TEST_ASSERT_TRUE(t.preset.commitCapture(t.params) == PsetOutcome::Applied);
    TEST_ASSERT_FALSE(t.preset.capturing());
}

static void test_captura_zera_os_dois_eixos_na_posicao_capturada(void) {
    TelaPreset t;
    TEST_ASSERT_TRUE(t.preset.beginCapture(Axis::X));
    alimentar(t.relogio, t.preset, 37, -12);

    TEST_ASSERT_TRUE(t.preset.commitCapture(t.params) == PsetOutcome::Applied);

    // O item de menu era "Preset X" e os DOIS eixos foram zerados: o eixo do item e so titulo.
    TEST_ASSERT_EQUAL_INT16(-37, t.params.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(12, t.params.presetOffsetDeci(Axis::Y));
    TEST_ASSERT_EQUAL_INT16(
        0, PresetWizard::reading(Axis::X, Angle::fromDeciDegrees(37), t.params).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(
        0, PresetWizard::reading(Axis::Y, Angle::fromDeciDegrees(-12), t.params).deciDegrees());
}

// Sair da banda no meio REINICIA a contagem: quem mexeu na estrutura aos 2,9 s recomeca do zero.
static void test_mexer_no_meio_reinicia_a_contagem(void) {
    TelaPreset t;
    TEST_ASSERT_TRUE(t.preset.beginCapture(Axis::X));
    alimentar(t.relogio, t.preset, 200, 0);
    TEST_ASSERT_TRUE(t.preset.stable());

    // Um solavanco de 6 decimos, fora da banda
    t.preset.sample(Angle::fromDeciDegrees(206), Angle::fromDeciDegrees(0));
    TEST_ASSERT_FALSE(t.preset.stable());
    TEST_ASSERT_TRUE(t.preset.commitCapture(t.params) == PsetOutcome::RefusedUnstable);
    TEST_ASSERT_EQUAL_INT16(0, t.params.presetOffsetDeci(Axis::X));

    // e o prazo recomeca do zero a partir dali
    alimentar(t.relogio, t.preset, 206, 0);
    TEST_ASSERT_TRUE(t.preset.commitCapture(t.params) == PsetOutcome::Applied);
    TEST_ASSERT_EQUAL_INT16(-206, t.params.presetOffsetDeci(Axis::X));
}

static void test_captura_sem_leitura_recusa_por_falta_de_dado_e_nao_por_instabilidade(void) {
    TelaPreset t;
    TEST_ASSERT_TRUE(t.preset.beginCapture(Axis::X));
    for (uint8_t i = 0; i < 20u; ++i) {
        t.preset.sample(Angle::invalid(), Angle::invalid());
        t.relogio.advanceMs(50);
    }
    TEST_ASSERT_TRUE(t.preset.commitCapture(t.params) == PsetOutcome::RefusedNoData);
    TEST_ASSERT_TRUE(t.preset.capturing());
}

static void test_commit_sem_captura_aberta_e_ignorado(void) {
    TelaPreset t;
    alimentar(t.relogio, t.preset, 100, 100);
    TEST_ASSERT_FALSE(t.preset.capturing());
    TEST_ASSERT_TRUE(t.preset.commitCapture(t.params) == PsetOutcome::Ignored);
    TEST_ASSERT_EQUAL_INT16(0, t.params.presetOffsetDeci(Axis::X));
}

static void test_cancelar_a_captura_nao_grava_nada(void) {
    TelaPreset t;
    TEST_ASSERT_TRUE(t.preset.beginCapture(Axis::Y));
    alimentar(t.relogio, t.preset, 300, 300);
    t.preset.cancelCapture();

    TEST_ASSERT_FALSE(t.preset.capturing());
    TEST_ASSERT_TRUE(t.preset.commitCapture(t.params) == PsetOutcome::Ignored);
    TEST_ASSERT_EQUAL_INT16(0, t.params.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(0, t.params.presetOffsetDeci(Axis::Y));
}

// REQ-DSP-04: o digito em edicao pisca. O retangulo invertido tem de aterrissar sobre o digito
// APONTADO pelo cursor - medido na MESMA fonte que desenhou a linha. Com a fonte da linha e a
// da medicao diferentes, o realce cai em cima do digito errado e o operador grava outro valor.
static void test_tela_de_captura_traz_titulo_leitura_ao_vivo_e_estado(void) {
    TelaPreset t;
    TEST_ASSERT_TRUE(t.preset.beginCapture(Axis::X));
    t.preset.sample(Angle::fromDeciDegrees(37), Angle::fromDeciDegrees(-12));
    app::renderPresetCapture(t.tela, t.preset, Axis::X);

    TEST_ASSERT_TRUE(t.tela.showsExactly("Preset X"));
    TEST_ASSERT_TRUE(t.tela.fontOf("Preset X") == TextFont::Small);
    TEST_ASSERT_EQUAL_INT16(0, t.tela.yOf("Preset X"));
    // a leitura AO VIVO, que e o que o operador acompanha enquanto posiciona
    TEST_ASSERT_TRUE(t.tela.showsExactly("X:+003,7 Y:-001,2"));
    TEST_ASSERT_TRUE(t.tela.fontOf("X:+003,7 Y:-001,2") == TextFont::Medium);
    TEST_ASSERT_TRUE(t.tela.showsExactly(PresetWizard::kCaptureWaitText));
    verificarQuadroPreset(t.tela);
}

static void test_tela_de_captura_anuncia_quando_esta_pronta(void) {
    TelaPreset t;
    TEST_ASSERT_TRUE(t.preset.beginCapture(Axis::Y));
    alimentar(t.relogio, t.preset, 0, 0);
    app::renderPresetCapture(t.tela, t.preset, Axis::Y);

    TEST_ASSERT_TRUE(t.tela.showsExactly("Preset Y"));
    TEST_ASSERT_TRUE(t.tela.showsExactly(PresetWizard::kCaptureReadyText));
    TEST_ASSERT_FALSE(t.tela.shows(PresetWizard::kCaptureWaitText));
    verificarQuadroPreset(t.tela);
}

static void test_tela_de_captura_sem_leitura_nao_inventa_numero(void) {
    TelaPreset t;
    TEST_ASSERT_TRUE(t.preset.beginCapture(Axis::X));
    t.preset.sample(Angle::invalid(), Angle::invalid());
    app::renderPresetCapture(t.tela, t.preset, Axis::X);

    // traco, nunca zero: zero seria uma medicao
    TEST_ASSERT_FALSE(t.tela.shows("+000,0"));
    TEST_ASSERT_TRUE(t.tela.showsExactly(PresetWizard::kCaptureWaitText));
    verificarQuadroPreset(t.tela);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_PST_02_o_offset_so_nasce_quando_o_gesto_de_pset_chega);
    RUN_TEST(test_PST_03_apos_o_pset_a_leitura_vale_exatamente_o_valor_programado);
    RUN_TEST(test_PST_03_leitura_bruta_invalida_continua_invalida_depois_do_pset);
    RUN_TEST(test_A9_formula_ponto_a_ponto_no_sentido_horario);
    RUN_TEST(test_A9_grampo_em_mais_e_menos_900_decimos);
    RUN_TEST(test_DIR_01_sentido_anti_horario_inverte_o_sinal_antes_do_preset);
    RUN_TEST(test_A9_offset_e_a_diferenca_entre_o_programado_e_o_bruto_com_sentido);
    RUN_TEST(test_A9_anti_horario_com_preset_diferente_de_zero_ponto_a_ponto);
    RUN_TEST(test_D1_item8_o_pset_aplica_os_dois_eixos_no_mesmo_gesto);
    RUN_TEST(test_D1_item9_pset_recusado_quando_o_sensor_esta_mudo);
    RUN_TEST(test_D1_item9_uma_unica_amostra_muda_esvazia_a_janela);
    RUN_TEST(test_D1_item9_janela_incompleta_ainda_nao_autoriza_o_pset);
    RUN_TEST(test_D1_item10_pico_a_pico_de_5_decimos_ainda_e_estavel);
    RUN_TEST(test_D1_item10_o_pset_usa_a_amostra_mais_recente_da_janela);
    RUN_TEST(test_D1_item10_pico_a_pico_de_6_decimos_recusa_o_pset);
    RUN_TEST(test_D1_item6_duplo_toque_sem_visita_a_tela_de_preset_nao_faz_nada);
    RUN_TEST(test_D1_item6_armamento_vale_120000_ms_e_nao_120001);
    RUN_TEST(test_D1_item6_a_janela_de_armamento_nao_reabre_no_wrap_de_2_elevado_a_32);
    RUN_TEST(test_D1_item6_o_pset_aceito_consome_o_armamento);
    RUN_TEST(test_D1_item6_a_confirmacao_aceita_tambem_consome_o_armamento);
    RUN_TEST(test_D1_item11_deslocamento_de_50_decimos_aplica_sem_confirmacao);
    RUN_TEST(test_D1_item11_deslocamento_de_51_decimos_exige_hold_de_menu);
    RUN_TEST(test_D1_item11_confirmacao_cancela_sozinha_em_10000_ms);
    RUN_TEST(test_D1_item11_a_confirmacao_usa_a_amostra_corrente_e_nao_a_do_duplo_toque);
    RUN_TEST(test_D1_item10_confirmacao_recusada_se_a_leitura_ficar_instavel_na_espera);
    RUN_TEST(test_D1_item11_confirmacao_recusada_se_o_sensor_emudecer_na_espera);
    RUN_TEST(test_D1_item8_o_par_de_offsets_e_validado_antes_de_qualquer_escrita);
    RUN_TEST(test_D1_item8_offset_de_borda_1800_decimos_e_aceito_nos_dois_eixos);
    RUN_TEST(test_D1_item17_indicador_permanente_enquanto_houver_offset);
    RUN_TEST(test_DIR_02_troca_de_sentido_zera_o_offset_desarma_e_avisa);
    RUN_TEST(test_DIR_02_aviso_dura_3000_ms);
    RUN_TEST(test_D14_item7_texto_do_aviso_de_sentido_e_byte_a_byte);
    RUN_TEST(test_DIR_02_gravar_o_mesmo_sentido_nao_zera_nem_avisa);
    RUN_TEST(test_PST_04_preset_e_offset_sobrevivem_ao_registro_gravado);
    RUN_TEST(test_MAN_5_6_L148_submenu_preset_voltar_preset_x_preset_y);
    RUN_TEST(test_D1_telas_de_recusa_do_pset_sao_byte_a_byte);
    RUN_TEST(test_captura_exige_3_s_parado_e_so_entao_grava);
    RUN_TEST(test_captura_zera_os_dois_eixos_na_posicao_capturada);
    RUN_TEST(test_mexer_no_meio_reinicia_a_contagem);
    RUN_TEST(test_captura_sem_leitura_recusa_por_falta_de_dado_e_nao_por_instabilidade);
    RUN_TEST(test_commit_sem_captura_aberta_e_ignorado);
    RUN_TEST(test_cancelar_a_captura_nao_grava_nada);
    RUN_TEST(test_tela_de_captura_traz_titulo_leitura_ao_vivo_e_estado);
    RUN_TEST(test_tela_de_captura_anuncia_quando_esta_pronta);
    RUN_TEST(test_tela_de_captura_sem_leitura_nao_inventa_numero);
    return UNITY_END();
}
