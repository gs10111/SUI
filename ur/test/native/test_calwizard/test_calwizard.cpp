// Testes da maquina de estados da IHM do assistente de Auto Calibracao (CalibrationWizard).
//
// O que esta preso aqui, com a fonte de cada numero e de cada texto:
//
// REQ-CAL-05 / MAN-5.7-L172, L177, L180, L183 e L184: a sequencia LITERAL das telas, nesta
//   ordem - o aviso de A14, "Ajuste 0Vcc:0000", "Angulo fim de escala X(graus):+045,0",
//   "Ajuste 10Vcc:0000", "Verifique -10Vcc" e "Alteracao bem sucedida!" - e o retorno
//   automatico ao Modo Normal sem nenhuma tecla. Os textos sao escritos aqui como literais
//   independentes do modulo, de proposito: e assim que a assercao prova o byte, e nao a
//   constante compartilhada.
// REQ-CAL-03 / MAN-5.7-L187 ("o ajuste do zero deve ser sempre realizado antes do ajuste do
//   ganho"): nenhum gesto da tela do zero alcanca a tela do ganho. A ordem e ESTADO.
// MAN-5.7-L174 e L182: cada etapa e confirmada por hold de aproximadamente 3 s da tecla MENU.
//   Este teste nao sintetiza o gesto: usa o FakeKeypad e o KeyGesture reais, para que a borda
//   dos 3000 ms seja a mesma que o operador produz no painel.
// A14 (DECISIONS.md L412, opcao A aprovada): tela de aviso "SAIDA SIMULADA / Bloqueie o CLP"
//   confirmada por hold de 3 s NA ENTRADA, telas de trim abrindo no VALOR CORRENTE e passo
//   obrigatorio de verificacao do ramo negativo em -10 V, que L165 afirma e o procedimento de
//   L169..L183 nunca mede.
// A14 / decisao 6 item 11: o commit e do PAR zero+ganho. Ate o hold de L182 o par gravado
//   continua sendo o anterior, integro.
// A14, excecao a L136: o timeout NAO grava o buffer do assistente - nem a inatividade de
//   120 s, nem o teto absoluto de override de 300 s da decisao 6 item 8, nem o aborto por
//   tecla. Gravar zero novo com ganho velho produz saida plausivel e errada, sem indicio.
// A6 / decisao 6 itens 1 e 7: o aviso de saida simulada e PERMANENTE em toda tela com
//   override, e NAO aparece na tela de bloqueio, onde a saida nunca deixou de ser real.
// Decisao 6 item 6: marcador de entrada de 1000 ms antes de qualquer tela de MEDICAO.
// Decisao 6 item 7: o mapa de codigo da saida analogica passo a passo, com os marcadores de
//   -11,00 V (codigo 3932, decisao A2) na entrada, no angulo de fundo de escala e na saida.
// Decisao 6 itens 12 e 13: commit implausivel recusado sem gravar, com "CALIBRACAO REJEITADA"
//   por 2000 ms e aborto; assistente recusado com limite sinalizado, com "CALIBRACAO
//   BLOQUEADA" por 2000 ms.
// docs/ihm-estados.md, invariante 6: gesto sem funcao declarada e IGNORADO - e, em
//   particular, nao rearma o timeout de inatividade.
// docs/ihm-estados.md 3.6, F5 e F6: as telas de permanencia ignoram TODA tecla.
//
// TODO PRAZO E AFIRMADO CONTRA O LITERAL DO DOCUMENTO, e so depois exercido, e sempre pela
// fronteira: um milissegundo antes NAO dispara, no prazo exato dispara. Teste que avanca o
// relogio pela propria constante que deveria estar provando fica verde com qualquer valor.
//
// O tempo vem do FakeClock canonico de test/fakes/fake_clock.h, que comeca em 0xFFFF0000:
// todo prazo deste arquivo atravessa o wrap de 2^32 ms, entao prazo escrito como "a > b" em
// vez da subtracao unsigned de ports/i_clock.h reprova aqui.
#include <unity.h>

#include "domain/ui/calibration_wizard.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_display.h"
#include "fakes/fake_keypad.h"

using domain::AnalogCalibration;
using domain::AnalogScaler;
using domain::Angle;
using domain::Axis;
using domain::CalibrationWizard;
using domain::Gesture;
using domain::GestureKind;
using domain::KeyGesture;
using test::FakeClock;
using test::FakeDisplay;
using test::FakeKeypad;

namespace {

using Step = CalibrationWizard::Step;

constexpr uint16_t kFabricaZero = 32768;
constexpr uint16_t kFabricaFundo = 58982;
constexpr uint16_t kCodigoFalha = 3932;

constexpr const char* kTelaAvisoX = "Auto Calibracao X";
constexpr const char* kTelaAvisoY = "Auto Calibracao Y";
constexpr const char* kTelaZeroNeutro = "Ajuste 0Vcc:5000";
constexpr const char* kTelaZeroEmZeros = "Ajuste 0Vcc:0000";
constexpr const char* kTelaAnguloX = "Angulo fim de escala X(graus):+045,0";
constexpr const char* kTelaAnguloY = "Angulo fim de escala Y(graus):+045,0";
constexpr const char* kTelaGanhoNeutro = "Ajuste 10Vcc:5000";
constexpr const char* kTelaGanhoEmZeros = "Ajuste 10Vcc:0000";
constexpr const char* kTelaVerificaNegativo = "Verifique -10Vcc";
constexpr const char* kTelaSucesso = "Alteracao bem sucedida!";
constexpr const char* kTelaRejeitada = "CALIBRACAO REJEITADA";
constexpr const char* kTelaBloqueada = "CALIBRACAO BLOQUEADA";
constexpr const char* kAvisoSimulada = "SAIDA SIMULADA";
constexpr const char* kAvisoClp = "Bloqueie o CLP";

void toque(CalibrationWizard& assistente, FakeClock& clock, Key tecla) {
    clock.advanceMs(50u);
    assistente.onGesture(Gesture{GestureKind::ShortTap, tecla, clock.nowMs()});
}

void holdDeMenu(CalibrationWizard& assistente, FakeClock& clock) {
    clock.advanceMs(3000u);
    assistente.onGesture(Gesture{GestureKind::Hold, Key::Menu, clock.nowMs()});
}

// Os quatro gestos que NAO tem funcao declarada em nenhuma tela deste assistente.
void gestosSemFuncao(CalibrationWizard& assistente, uint32_t agora) {
    assistente.onGesture(Gesture{GestureKind::Hold, Key::Up, agora});
    assistente.onGesture(Gesture{GestureKind::Hold, Key::Down, agora});
    assistente.onGesture(Gesture{GestureKind::DoubleTap, Key::Up, agora});
    assistente.onGesture(Gesture{GestureKind::DoubleTap, Key::Menu, agora});
}

void digitosDaTela(const CalibrationWizard& assistente, char saida[5]) {
    char texto[48];
    saida[0] = '\0';
    if (!assistente.screenText(texto, static_cast<uint8_t>(sizeof(texto)))) {
        return;
    }
    uint8_t fim = 0;
    while (texto[fim] != '\0') {
        ++fim;
    }
    uint8_t restam = 4;
    while (fim > 0 && restam > 0) {
        --fim;
        if (texto[fim] >= '0' && texto[fim] <= '9') {
            --restam;
            saida[restam] = texto[fim];
        }
    }
    saida[4] = '\0';
}

// Escreve os quatro digitos do campo corrente pelo caminho do operador: MENU seleciona o
// digito (L173) e UP altera o valor (L173), com rolagem circular de 10. Termina com o cursor
// de volta no digito mais a direita, que e onde o campo abriu.
void escreveCampo(CalibrationWizard& assistente, FakeClock& clock, const char* alvo) {
    char atual[5];
    digitosDaTela(assistente, atual);
    for (int8_t posicao = 3; posicao >= 0; --posicao) {
        const uint8_t indice = static_cast<uint8_t>(posicao);
        const uint8_t passos =
            static_cast<uint8_t>((10 + (alvo[indice] - atual[indice])) % 10);
        for (uint8_t i = 0; i < passos; ++i) {
            toque(assistente, clock, Key::Up);
        }
        toque(assistente, clock, Key::Menu);
    }
}

void drenar(KeyGesture& gestos, CalibrationWizard& assistente) {
    Gesture gesto{};
    while (gestos.takeGesture(gesto)) {
        assistente.onGesture(gesto);
    }
}

void confirmaPorHoldReal(FakeClock& clock, FakeKeypad& teclado, KeyGesture& gestos,
                         CalibrationWizard& assistente) {
    teclado.press(Key::Menu);
    clock.advanceMs(3000u);
    gestos.update();
    drenar(gestos, assistente);
    teclado.release(Key::Menu);
    gestos.update();
    drenar(gestos, assistente);
}

// Abre o assistente e passa a tela de aviso de A14 pelo hold de 3 s, que e a unica saida dela.
void abreAteOZero(CalibrationWizard& assistente, FakeClock& clock, Axis eixo = Axis::X) {
    TEST_ASSERT_TRUE(assistente.begin(eixo, false).ok());
    TEST_ASSERT_TRUE(assistente.step() == Step::Warning);
    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::Zero);
}

// Leva o assistente ate a tela do ganho pelo caminho literal do manual: aviso, zero, hold,
// angulo de fundo de escala, hold.
void abreAteOGanho(CalibrationWizard& assistente, FakeClock& clock) {
    abreAteOZero(assistente, clock);
    holdDeMenu(assistente, clock);
    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::Gain);
}

bool telaMostra(const CalibrationWizard& assistente, FakeDisplay& display, const char* texto) {
    if (assistente.render(display).failed()) {
        return false;
    }
    return display.showsExactly(texto);
}

// Todo desenho do quadro corrente cabe DENTRO do painel, medido pela propria porta.
void conferirGeometria(FakeDisplay& display) {
    TEST_ASSERT_TRUE(display.drawCount() > 0);
    for (uint8_t i = 0; i < display.drawCount(); ++i) {
        const FakeDisplay::Draw& d = display.draw(i);
        const int32_t x = static_cast<int32_t>(d.x);
        const int32_t y = static_cast<int32_t>(d.y);
        const int32_t largura = static_cast<int32_t>(display.textWidthPx(d.font, d.text));
        const int32_t altura = static_cast<int32_t>(display.lineHeightPx(d.font));
        TEST_ASSERT_TRUE(x >= 0);
        TEST_ASSERT_TRUE(y >= 0);
        TEST_ASSERT_TRUE(x + largura <= static_cast<int32_t>(display.widthPx()));
        TEST_ASSERT_TRUE(y + altura <= static_cast<int32_t>(display.heightPx()));
    }
}

// O unico desenho em video inverso do quadro, ou nullptr se nao houver exatamente um.
const FakeDisplay::Draw* unicoInverso(const FakeDisplay& display) {
    const FakeDisplay::Draw* achado = nullptr;
    for (uint8_t i = 0; i < display.drawCount(); ++i) {
        if (display.draw(i).ink == TextInk::Inverse) {
            if (achado != nullptr) {
                return nullptr;
            }
            achado = &display.draw(i);
        }
    }
    return achado;
}

// Registro de 6 bytes de AnalogCalibration::restore(): angulo, zero e fundo, little endian.
void registroDe(uint8_t* out, int16_t anguloDeci, uint16_t zero, uint16_t fundo) {
    const uint16_t a = static_cast<uint16_t>(anguloDeci);
    out[0] = static_cast<uint8_t>(a & 0xFFu);
    out[1] = static_cast<uint8_t>((a >> 8) & 0xFFu);
    out[2] = static_cast<uint8_t>(zero & 0xFFu);
    out[3] = static_cast<uint8_t>((zero >> 8) & 0xFFu);
    out[4] = static_cast<uint8_t>(fundo & 0xFFu);
    out[5] = static_cast<uint8_t>((fundo >> 8) & 0xFFu);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- REQ-CAL-05: a sequencia literal das telas, na ordem (L172, L177, L180, L183, L184) ---

static void test_REQ_CAL_05_as_telas_literais_na_ordem_e_o_retorno_ao_modo_normal(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).ok());
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaAvisoX));

    holdDeMenu(assistente, clock);
    escreveCampo(assistente, clock, "0000");
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaZeroEmZeros));

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaAnguloX));

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaGanhoNeutro));
    escreveCampo(assistente, clock, "0000");
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaGanhoEmZeros));

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::VerifyNegative);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaVerificaNegativo));

    toque(assistente, clock, Key::Menu);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaSucesso));
    TEST_ASSERT_TRUE(assistente.step() == Step::Done);

    TEST_ASSERT_EQUAL_UINT32(1500u, CalibrationWizard::kDoneMs);
    clock.advanceMs(1499u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Done);
    clock.advanceMs(1u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Idle);
    TEST_ASSERT_FALSE(assistente.active());
    TEST_ASSERT_FALSE(assistente.overriding());
    TEST_ASSERT_TRUE(assistente.render(display).err == Err::NotInit);

    TEST_ASSERT_EQUAL_UINT16(27768u, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(48982u, cal.scaler().fullScaleCode());
}

static void test_A14_as_telas_de_trim_abrem_no_valor_corrente_e_nao_em_0000(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    abreAteOZero(assistente, clock);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaZeroNeutro));

    holdDeMenu(assistente, clock);
    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaGanhoNeutro));
}

// A abertura no valor corrente so e distinguivel de uma constante quando o par gravado NAO e
// o neutro: com o par de fabrica, 5000, 5000 e 45,0 graus sao exatamente o que uma constante
// hardcoded produziria.
static void test_A14_com_par_gravado_nao_neutro_as_tres_telas_abrem_no_valor_gravado(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    uint8_t registro[AnalogCalibration::kRecordBytes];
    registroDe(registro, 300, 33500u, 60000u);
    TEST_ASSERT_TRUE(cal.restore(registro, AnalogCalibration::kRecordBytes));

    abreAteOZero(assistente, clock);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, "Ajuste 0Vcc:5732"));
    TEST_ASSERT_EQUAL_UINT16(33500u, assistente.outputCode());

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, "Angulo fim de escala X(graus):+030,0"));

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, "Ajuste 10Vcc:5286"));
    TEST_ASSERT_EQUAL_UINT16(60000u, assistente.outputCode());
}

static void test_REQ_CAL_01_o_eixo_Y_tem_a_propria_tela_de_fundo_de_escala(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_TRUE(assistente.begin(Axis::Y, false).ok());
    TEST_ASSERT_TRUE(assistente.axis() == Axis::Y);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaAvisoY));

    holdDeMenu(assistente, clock);
    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaAnguloY));
}

// --- A14: a tela de aviso e confirmada por hold de 3 s e e a unica porta para a medicao ---

static void test_A14_o_aviso_de_saida_simulada_e_confirmado_por_hold_de_menu(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).ok());
    TEST_ASSERT_TRUE(assistente.step() == Step::Warning);
    TEST_ASSERT_TRUE(assistente.overriding());
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());
    TEST_ASSERT_TRUE(assistente.render(display).ok());
    TEST_ASSERT_TRUE(display.showsExactly(kTelaAvisoX));
    TEST_ASSERT_TRUE(display.showsExactly(kAvisoSimulada));
    TEST_ASSERT_TRUE(display.showsExactly(kAvisoClp));

    clock.advanceMs(5000u);
    toque(assistente, clock, Key::Menu);
    toque(assistente, clock, Key::Up);
    toque(assistente, clock, Key::Down);
    gestosSemFuncao(assistente, clock.nowMs());
    TEST_ASSERT_TRUE(assistente.step() == Step::Warning);
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::Zero);
}

// Decisao 6 item 6: o marcador de entrada de 1000 ms vale ANTES de qualquer tela de medicao,
// entao nem o hold pode abrir o campo de digitos antes dele.
static void test_D6_item6_o_hold_nao_abre_a_tela_de_medicao_antes_de_1000_ms(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_EQUAL_UINT32(1000u, CalibrationWizard::kEntryMarkerMs);
    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).ok());

    clock.advanceMs(999u);
    assistente.onGesture(Gesture{GestureKind::Hold, Key::Menu, clock.nowMs()});
    TEST_ASSERT_TRUE(assistente.step() == Step::Warning);
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());

    clock.advanceMs(1u);
    assistente.onGesture(Gesture{GestureKind::Hold, Key::Menu, clock.nowMs()});
    TEST_ASSERT_TRUE(assistente.step() == Step::Zero);
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, assistente.outputCode());
}

// --- REQ-CAL-03 e MAN-5.7-L187: pular o zero e ir direto ao ganho e recusado ---

static void test_REQ_CAL_03_nenhum_gesto_da_tela_do_zero_alcanca_o_ajuste_do_ganho(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    abreAteOZero(assistente, clock);

    gestosSemFuncao(assistente, clock.nowMs());
    toque(assistente, clock, Key::Up);
    toque(assistente, clock, Key::Down);
    toque(assistente, clock, Key::Menu);

    TEST_ASSERT_TRUE(assistente.step() == Step::Zero);
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Zero);
    TEST_ASSERT_TRUE(assistente.render(display).ok());
    TEST_ASSERT_FALSE(display.shows("Ajuste 10Vcc"));
    TEST_ASSERT_EQUAL_UINT16(5000u, cal.gainField());

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::FullScaleAngle);
    TEST_ASSERT_TRUE(assistente.render(display).ok());
    TEST_ASSERT_FALSE(display.shows("Ajuste 10Vcc"));

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::Gain);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaGanhoNeutro));
}

static void test_REQ_CAL_03_fundo_de_escala_em_000_0_e_recusado_e_a_tela_permanece(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    abreAteOZero(assistente, clock);
    holdDeMenu(assistente, clock);
    escreveCampo(assistente, clock, "0000");
    TEST_ASSERT_TRUE(telaMostra(assistente, display, "Angulo fim de escala X(graus):+000,0"));

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::FullScaleAngle);
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::FullScaleAngle);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, "Angulo fim de escala X(graus):+000,0"));
}

// --- MAN-5.7-L174 e L182: cada etapa e confirmada por hold de ~3 s da tecla MENU ---

static void test_MAN_5_7_L174_L182_cada_etapa_e_confirmada_por_hold_de_3_s(void) {
    FakeClock clock;
    FakeKeypad teclado(clock);
    KeyGesture gestos(teclado, clock);
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).ok());

    teclado.press(Key::Menu);
    clock.advanceMs(2999u);
    gestos.update();
    drenar(gestos, assistente);
    TEST_ASSERT_TRUE(assistente.step() == Step::Warning);

    clock.advanceMs(1u);
    gestos.update();
    drenar(gestos, assistente);
    TEST_ASSERT_TRUE(assistente.step() == Step::Zero);

    teclado.release(Key::Menu);
    gestos.update();
    drenar(gestos, assistente);
    TEST_ASSERT_TRUE(assistente.step() == Step::Zero);

    confirmaPorHoldReal(clock, teclado, gestos, assistente);
    TEST_ASSERT_TRUE(assistente.step() == Step::FullScaleAngle);

    confirmaPorHoldReal(clock, teclado, gestos, assistente);
    TEST_ASSERT_TRUE(assistente.step() == Step::Gain);

    confirmaPorHoldReal(clock, teclado, gestos, assistente);
    TEST_ASSERT_TRUE(assistente.step() == Step::VerifyNegative);
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(kFabricaFundo, cal.scaler().fullScaleCode());
}

// --- A14 e decisao 6 item 11: o commit e do PAR, nunca de meia calibracao ---

static void test_A14_o_commit_e_do_par_zero_mais_ganho_e_nunca_de_meia_calibracao(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    abreAteOZero(assistente, clock);
    escreveCampo(assistente, clock, "5100");
    TEST_ASSERT_EQUAL_UINT16(5100u, cal.zeroField());
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, cal.scaler().zeroCode());

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::FullScaleAngle);
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(kFabricaFundo, cal.scaler().fullScaleCode());

    holdDeMenu(assistente, clock);
    escreveCampo(assistente, clock, "5100");
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(kFabricaFundo, cal.scaler().fullScaleCode());

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::VerifyNegative);
    TEST_ASSERT_EQUAL_UINT16(32868u, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(59182u, cal.scaler().fullScaleCode());
}

// --- A14, excecao a L136: nenhum timeout grava o buffer do assistente ---

static void test_A14_timeout_de_inatividade_de_120_s_nao_grava_o_buffer(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_EQUAL_UINT32(120000u, CalibrationWizard::kInactivityMs);
    TEST_ASSERT_EQUAL_UINT32(1000u, CalibrationWizard::kExitMarkerMs);

    abreAteOZero(assistente, clock);
    escreveCampo(assistente, clock, "5100");
    holdDeMenu(assistente, clock);
    holdDeMenu(assistente, clock);
    escreveCampo(assistente, clock, "5200");
    TEST_ASSERT_TRUE(assistente.step() == Step::Gain);

    clock.advanceMs(119999u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Gain);

    clock.advanceMs(1u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::ExitMarker);
    TEST_ASSERT_TRUE(assistente.overriding());
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());

    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(kFabricaFundo, cal.scaler().fullScaleCode());
    TEST_ASSERT_EQUAL_INT16(450, cal.scaler().fullScaleAngleDeci());
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Idle);
    TEST_ASSERT_EQUAL_UINT16(5000u, cal.zeroField());
    TEST_ASSERT_EQUAL_UINT16(5000u, cal.gainField());

    clock.advanceMs(999u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::ExitMarker);
    clock.advanceMs(1u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Idle);
    TEST_ASSERT_FALSE(assistente.overriding());
}

// Invariante 6 de docs/ihm-estados.md: gesto sem funcao e ignorado - e ignorar inclui NAO
// rearmar os 120 s. Com IO34/IO35 sem pull-up, cabo de IHM solto gera tecla fantasma; fantasma
// que rearmasse o prazo prenderia o override mentindo para o CLP ate o teto de 300 s.
static void test_A14_gesto_sem_funcao_nao_rearma_o_timeout_de_inatividade(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    abreAteOGanho(assistente, clock);
    const uint32_t ultimoGestoUtil = clock.nowMs();

    clock.advanceMs(100000u);
    gestosSemFuncao(assistente, clock.nowMs());
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Gain);

    clock.advanceMs(19999u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Gain);

    clock.advanceMs(1u);
    TEST_ASSERT_EQUAL_UINT32(ultimoGestoUtil + 120000u, clock.nowMs());
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::ExitMarker);
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, cal.scaler().zeroCode());
}

// A tela de aviso tambem esta sob os 120 s de L136: sem isso, um tecnico que abre o
// assistente e sai de perto deixa a saida do eixo em -11,00 V ate o teto de 300 s.
static void test_A14_o_aviso_tambem_expira_pela_inatividade_de_120_s(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).ok());
    TEST_ASSERT_TRUE(assistente.step() == Step::Warning);

    clock.advanceMs(119999u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Warning);

    clock.advanceMs(1u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::ExitMarker);
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Idle);
}

static void test_D6_item8_teto_de_300_s_encerra_sem_gravar_mesmo_com_teclas(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_EQUAL_UINT32(300000u, CalibrationWizard::kOverrideCeilingMs);
    const uint32_t inicio = clock.nowMs();
    abreAteOGanho(assistente, clock);

    for (uint8_t volta = 0; volta < 4u; ++volta) {
        clock.advanceMs(59000u);
        assistente.onGesture(Gesture{GestureKind::ShortTap, Key::Up, clock.nowMs()});
        assistente.tick();
        TEST_ASSERT_TRUE(assistente.step() == Step::Gain);
    }

    clock.advanceMs(54999u);
    TEST_ASSERT_EQUAL_UINT32(inicio + 299999u, clock.nowMs());
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Gain);

    clock.advanceMs(1u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::ExitMarker);
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(kFabricaFundo, cal.scaler().fullScaleCode());
}

static void test_A14_aborto_por_tecla_devolve_o_par_anterior_integro(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    abreAteOZero(assistente, clock);
    escreveCampo(assistente, clock, "4800");
    holdDeMenu(assistente, clock);
    holdDeMenu(assistente, clock);
    escreveCampo(assistente, clock, "5300");

    assistente.abort();
    TEST_ASSERT_TRUE(assistente.step() == Step::ExitMarker);
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(kFabricaFundo, cal.scaler().fullScaleCode());
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Idle);

    clock.advanceMs(999u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::ExitMarker);
    clock.advanceMs(1u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Idle);
    TEST_ASSERT_FALSE(assistente.overriding());
}

// --- A6 e decisao 6 itens 1 e 7: aviso permanente de saida simulada ---

static void test_A6_o_aviso_de_saida_simulada_esta_em_todas_as_telas_do_procedimento(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).ok());
    for (uint8_t passo = 0; passo < 5u; ++passo) {
        TEST_ASSERT_TRUE(assistente.overriding());
        TEST_ASSERT_TRUE(assistente.render(display).ok());
        TEST_ASSERT_TRUE(display.showsExactly(kAvisoSimulada));
        TEST_ASSERT_TRUE(display.showsExactly(kAvisoClp));
        holdDeMenu(assistente, clock);
    }
    TEST_ASSERT_TRUE(assistente.step() == Step::VerifyNegative);

    toque(assistente, clock, Key::Menu);
    TEST_ASSERT_TRUE(assistente.render(display).ok());
    TEST_ASSERT_TRUE(display.showsExactly(kTelaSucesso));
    TEST_ASSERT_TRUE(display.showsExactly(kAvisoSimulada));
    TEST_ASSERT_TRUE(display.showsExactly(kAvisoClp));
}

// A geometria e da PORTA, nao de constante chutada: com kWarnY2 = 58 e lineHeightPx = 12 a
// linha "Bloqueie o CLP" terminava em 69, seis pixels alem do painel de 64 - o aviso que A14
// exige, desenhado fora da tela, com showsExactly() respondendo a mesma coisa.
static void test_REQ_DSP_a_geometria_de_todas_as_telas_cabe_no_painel(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).ok());
    for (uint8_t passo = 0; passo < 5u; ++passo) {
        TEST_ASSERT_TRUE(assistente.render(display).ok());
        conferirGeometria(display);
        holdDeMenu(assistente, clock);
    }
    TEST_ASSERT_TRUE(assistente.render(display).ok());
    conferirGeometria(display);

    toque(assistente, clock, Key::Menu);
    TEST_ASSERT_TRUE(assistente.step() == Step::Done);
    TEST_ASSERT_TRUE(assistente.render(display).ok());
    conferirGeometria(display);

    FakeClock clock2;
    FakeDisplay display2;
    AnalogCalibration cal2;
    CalibrationWizard bloqueado(cal2, clock2);
    TEST_ASSERT_TRUE(bloqueado.begin(Axis::Y, true).err == Err::Aborted);
    TEST_ASSERT_TRUE(bloqueado.render(display2).ok());
    conferirGeometria(display2);
}

// REQ-DSP-04: o video inverso marca o DIGITO EM EDICAO. Perguntar so "existe algum inverso"
// deixa passar o inverso piscando dentro da palavra "Ajuste".
static void test_REQ_DSP_04_o_video_inverso_cai_no_digito_em_edicao(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    uint8_t registro[AnalogCalibration::kRecordBytes];
    registroDe(registro, 300, 33500u, 60000u);
    TEST_ASSERT_TRUE(cal.restore(registro, AnalogCalibration::kRecordBytes));

    abreAteOZero(assistente, clock);
    TEST_ASSERT_TRUE(assistente.render(display).ok());
    const FakeDisplay::Draw* marcado = unicoInverso(display);
    TEST_ASSERT_NOT_NULL(marcado);
    TEST_ASSERT_EQUAL_STRING("2", marcado->text);
    TEST_ASSERT_EQUAL_INT16(
        static_cast<int16_t>(display.textWidthPx(TextFont::Small, "Ajuste 0Vcc:573")),
        marcado->x);

    toque(assistente, clock, Key::Menu);
    TEST_ASSERT_TRUE(assistente.render(display).ok());
    marcado = unicoInverso(display);
    TEST_ASSERT_NOT_NULL(marcado);
    TEST_ASSERT_EQUAL_STRING("3", marcado->text);
    TEST_ASSERT_EQUAL_INT16(
        static_cast<int16_t>(display.textWidthPx(TextFont::Small, "Ajuste 0Vcc:57")),
        marcado->x);

    holdDeMenu(assistente, clock);
    holdDeMenu(assistente, clock);
    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::VerifyNegative);
    TEST_ASSERT_TRUE(assistente.render(display).ok());
    TEST_ASSERT_FALSE(display.hasInverse());
}

// --- decisao 6 item 7 e A2: o mapa da saida analogica passo a passo ---

static void test_D6_item7_a_saida_so_sai_do_marcador_nas_telas_de_medicao(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);
    char tela[48];

    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).ok());
    TEST_ASSERT_TRUE(assistente.overriding());
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());

    holdDeMenu(assistente, clock);
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, assistente.outputCode());

    // L173: as DUAS setas alteram o valor "acompanhando a leitura do voltimetro", entao as
    // duas tem de mover o codigo emitido, nao so o texto da tela.
    toque(assistente, clock, Key::Down);
    TEST_ASSERT_TRUE(assistente.screenText(tela, static_cast<uint8_t>(sizeof(tela))));
    TEST_ASSERT_EQUAL_STRING("Ajuste 0Vcc:5009", tela);
    TEST_ASSERT_EQUAL_UINT16(32777u, assistente.outputCode());

    toque(assistente, clock, Key::Up);
    TEST_ASSERT_TRUE(assistente.screenText(tela, static_cast<uint8_t>(sizeof(tela))));
    TEST_ASSERT_EQUAL_STRING(kTelaZeroNeutro, tela);
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, assistente.outputCode());

    escreveCampo(assistente, clock, "5100");
    TEST_ASSERT_EQUAL_UINT16(32868u, assistente.outputCode());

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::FullScaleAngle);
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::Gain);
    TEST_ASSERT_EQUAL_UINT16(59082u, assistente.outputCode());

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::VerifyNegative);
    TEST_ASSERT_EQUAL_UINT16(6654u, assistente.outputCode());

    toque(assistente, clock, Key::Menu);
    TEST_ASSERT_TRUE(assistente.step() == Step::Done);
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());

    clock.advanceMs(1500u);
    assistente.tick();
    TEST_ASSERT_FALSE(assistente.overriding());
}

static void test_D6_item7_o_codigo_do_ganho_e_grampeado_em_61342(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    abreAteOGanho(assistente, clock);
    escreveCampo(assistente, clock, "9999");
    TEST_ASSERT_EQUAL_UINT16(61342u, assistente.outputCode());
}

// --- A14: verificacao obrigatoria do ramo negativo, F4 CAL_VERIF_NEG ---

static void test_A14_a_verificacao_negativa_emite_o_espelho_e_sai_em_20_s(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_EQUAL_UINT32(20000u, CalibrationWizard::kVerifyNegativeMs);

    abreAteOZero(assistente, clock);
    escreveCampo(assistente, clock, "6000");
    holdDeMenu(assistente, clock);
    holdDeMenu(assistente, clock);
    holdDeMenu(assistente, clock);

    TEST_ASSERT_TRUE(assistente.step() == Step::VerifyNegative);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaVerificaNegativo));
    TEST_ASSERT_EQUAL_UINT16(33768u, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(59982u, cal.scaler().fullScaleCode());
    TEST_ASSERT_EQUAL_UINT16(7554u, assistente.outputCode());

    clock.advanceMs(19999u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::VerifyNegative);

    clock.advanceMs(1u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Done);
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());
}

// O grampo INFERIOR de 6554 so tem caminho pela tela de verificacao: o gate de make() aceita
// espelho ate 5243, e -10,00 Vcc e o fim da faixa util de L185.
static void test_A14_o_espelho_abaixo_de_6554_e_grampeado_em_6554(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    abreAteOZero(assistente, clock);
    escreveCampo(assistente, clock, "0000");
    holdDeMenu(assistente, clock);
    holdDeMenu(assistente, clock);
    escreveCampo(assistente, clock, "1000");
    holdDeMenu(assistente, clock);

    TEST_ASSERT_TRUE(assistente.step() == Step::VerifyNegative);
    TEST_ASSERT_EQUAL_UINT16(27768u, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(49982u, cal.scaler().fullScaleCode());
    TEST_ASSERT_EQUAL_UINT16(5554u, cal.scaler().mirrorCode());
    TEST_ASSERT_EQUAL_UINT16(6554u, assistente.outputCode());
}

// --- decisao 6 itens 12 e 13: recusa de plausibilidade e gate de entrada ---

static void test_D6_item12_tela_CALIBRACAO_REJEITADA_por_2000_ms_e_aborto(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_EQUAL_UINT32(2000u, CalibrationWizard::kRejectedMs);

    abreAteOZero(assistente, clock);
    escreveCampo(assistente, clock, "0000");
    holdDeMenu(assistente, clock);
    holdDeMenu(assistente, clock);
    escreveCampo(assistente, clock, "9999");

    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::Rejected);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaRejeitada));
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(kFabricaFundo, cal.scaler().fullScaleCode());

    clock.advanceMs(1999u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Rejected);

    clock.advanceMs(1u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::ExitMarker);
    TEST_ASSERT_EQUAL_UINT16(kCodigoFalha, assistente.outputCode());
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Idle);

    clock.advanceMs(1000u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Idle);
    TEST_ASSERT_FALSE(assistente.overriding());
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(kFabricaFundo, cal.scaler().fullScaleCode());
}

static void test_D6_item13_tela_CALIBRACAO_BLOQUEADA_por_2000_ms(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_EQUAL_UINT32(2000u, CalibrationWizard::kBlockedMs);

    TEST_ASSERT_TRUE(assistente.begin(Axis::X, true).err == Err::Aborted);
    TEST_ASSERT_TRUE(assistente.step() == Step::Blocked);
    TEST_ASSERT_TRUE(assistente.active());
    TEST_ASSERT_FALSE(assistente.overriding());
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Idle);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaBloqueada));

    // Nao ha override: escrever "SAIDA SIMULADA" sobre uma saida que continua real seria
    // mentira na direcao perigosa.
    TEST_ASSERT_FALSE(display.shows(kAvisoSimulada));
    TEST_ASSERT_FALSE(display.shows(kAvisoClp));
    TEST_ASSERT_EQUAL_UINT8(1u, display.drawCount());

    clock.advanceMs(1999u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Blocked);

    clock.advanceMs(1u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Idle);
    TEST_ASSERT_TRUE(assistente.render(display).err == Err::NotInit);

    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).ok());
}

// As duas recusas de begin() sao causas incompativeis e por isso codigos distintos: Busy diz
// "ignore, ja esta aberto"; Aborted diz "recusei, e a explicacao ja esta na tela".
static void test_D6_item13_reentrada_e_gate_de_limite_tem_codigos_distintos(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_TRUE(assistente.begin(Axis::X, true).err == Err::Aborted);
    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).err == Err::Busy);
    TEST_ASSERT_TRUE(assistente.step() == Step::Blocked);

    clock.advanceMs(2000u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).ok());
    TEST_ASSERT_TRUE(assistente.begin(Axis::Y, false).err == Err::Busy);
    TEST_ASSERT_TRUE(assistente.axis() == Axis::X);
}

// docs/ihm-estados.md 3.6: em F5 e F6 as quatro colunas de tecla estao marcadas "ignorado".
// Sem este teste, uma guarda frouxa em onGesture faz um hold de MENU sobre a tela de sucesso
// chamar commit() de novo, receber Err::NotCalibrated e pintar a recusa logo depois de uma
// calibracao BEM SUCEDIDA.
static void test_ihm_F5_e_F6_ignoram_toda_tecla_e_nao_reiniciam_o_prazo(void) {
    FakeClock clock;
    FakeDisplay display;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    abreAteOGanho(assistente, clock);
    holdDeMenu(assistente, clock);
    toque(assistente, clock, Key::Menu);
    TEST_ASSERT_TRUE(assistente.step() == Step::Done);

    clock.advanceMs(500u);
    assistente.onGesture(Gesture{GestureKind::Hold, Key::Menu, clock.nowMs()});
    assistente.onGesture(Gesture{GestureKind::ShortTap, Key::Menu, clock.nowMs()});
    assistente.onGesture(Gesture{GestureKind::ShortTap, Key::Up, clock.nowMs()});
    gestosSemFuncao(assistente, clock.nowMs());
    TEST_ASSERT_TRUE(assistente.step() == Step::Done);
    TEST_ASSERT_TRUE(telaMostra(assistente, display, kTelaSucesso));
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(kFabricaFundo, cal.scaler().fullScaleCode());

    clock.advanceMs(999u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Done);
    clock.advanceMs(1u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Idle);

    FakeClock clock2;
    AnalogCalibration cal2;
    CalibrationWizard recusado(cal2, clock2);
    abreAteOZero(recusado, clock2);
    escreveCampo(recusado, clock2, "0000");
    holdDeMenu(recusado, clock2);
    holdDeMenu(recusado, clock2);
    escreveCampo(recusado, clock2, "9999");
    holdDeMenu(recusado, clock2);
    TEST_ASSERT_TRUE(recusado.step() == Step::Rejected);

    clock2.advanceMs(1000u);
    recusado.onGesture(Gesture{GestureKind::Hold, Key::Menu, clock2.nowMs()});
    recusado.onGesture(Gesture{GestureKind::ShortTap, Key::Menu, clock2.nowMs()});
    gestosSemFuncao(recusado, clock2.nowMs());
    TEST_ASSERT_TRUE(recusado.step() == Step::Rejected);

    clock2.advanceMs(999u);
    recusado.tick();
    TEST_ASSERT_TRUE(recusado.step() == Step::Rejected);
    clock2.advanceMs(1u);
    recusado.tick();
    TEST_ASSERT_TRUE(recusado.step() == Step::ExitMarker);
}

// Contrato de flush de src/domain/ui/key_gesture.h: toda troca de tela e de modo tem de
// mandar o chamador drenar a fila de gestos, ou um duplo toque de UP iniciado na tela do
// ganho vira PSET no Modo Normal ate 400 ms depois.
static void test_key_gesture_flush_e_pedido_em_toda_troca_de_tela_e_de_modo(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    TEST_ASSERT_FALSE(assistente.consumeFlushRequest());

    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).ok());
    TEST_ASSERT_TRUE(assistente.consumeFlushRequest());
    TEST_ASSERT_FALSE(assistente.consumeFlushRequest());

    TEST_ASSERT_TRUE(assistente.begin(Axis::X, false).err == Err::Busy);
    TEST_ASSERT_FALSE(assistente.consumeFlushRequest());

    for (uint8_t passo = 0; passo < 4u; ++passo) {
        holdDeMenu(assistente, clock);
        TEST_ASSERT_TRUE(assistente.consumeFlushRequest());
        TEST_ASSERT_FALSE(assistente.consumeFlushRequest());
    }
    TEST_ASSERT_TRUE(assistente.step() == Step::VerifyNegative);

    toque(assistente, clock, Key::Menu);
    TEST_ASSERT_TRUE(assistente.step() == Step::Done);
    TEST_ASSERT_TRUE(assistente.consumeFlushRequest());

    clock.advanceMs(1500u);
    assistente.tick();
    TEST_ASSERT_TRUE(assistente.step() == Step::Idle);
    TEST_ASSERT_TRUE(assistente.consumeFlushRequest());
    TEST_ASSERT_FALSE(assistente.consumeFlushRequest());
}

// --- REQ-CAL-06 e REQ-CAL-07: o par gravado pelo assistente governa a saida ---

static void test_REQ_CAL_06_CAL_07_o_par_gravado_governa_proporcao_e_saturacao(void) {
    FakeClock clock;
    AnalogCalibration cal;
    CalibrationWizard assistente(cal, clock);

    abreAteOGanho(assistente, clock);
    holdDeMenu(assistente, clock);
    TEST_ASSERT_TRUE(assistente.step() == Step::VerifyNegative);

    const AnalogScaler& escala = cal.scaler();
    TEST_ASSERT_EQUAL_UINT16(kFabricaZero, escala.codeFor(Angle::fromDeciDegrees(0)));
    TEST_ASSERT_EQUAL_UINT16(kFabricaFundo, escala.codeFor(Angle::fromDeciDegrees(450)));
    TEST_ASSERT_EQUAL_UINT16(6554u, escala.codeFor(Angle::fromDeciDegrees(-450)));
    TEST_ASSERT_EQUAL_UINT16(kFabricaFundo, escala.codeFor(Angle::fromDeciDegrees(900)));
    TEST_ASSERT_EQUAL_UINT16(6554u, escala.codeFor(Angle::fromDeciDegrees(-900)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_REQ_CAL_05_as_telas_literais_na_ordem_e_o_retorno_ao_modo_normal);
    RUN_TEST(test_A14_as_telas_de_trim_abrem_no_valor_corrente_e_nao_em_0000);
    RUN_TEST(test_A14_com_par_gravado_nao_neutro_as_tres_telas_abrem_no_valor_gravado);
    RUN_TEST(test_REQ_CAL_01_o_eixo_Y_tem_a_propria_tela_de_fundo_de_escala);
    RUN_TEST(test_A14_o_aviso_de_saida_simulada_e_confirmado_por_hold_de_menu);
    RUN_TEST(test_D6_item6_o_hold_nao_abre_a_tela_de_medicao_antes_de_1000_ms);
    RUN_TEST(test_REQ_CAL_03_nenhum_gesto_da_tela_do_zero_alcanca_o_ajuste_do_ganho);
    RUN_TEST(test_REQ_CAL_03_fundo_de_escala_em_000_0_e_recusado_e_a_tela_permanece);
    RUN_TEST(test_MAN_5_7_L174_L182_cada_etapa_e_confirmada_por_hold_de_3_s);
    RUN_TEST(test_A14_o_commit_e_do_par_zero_mais_ganho_e_nunca_de_meia_calibracao);
    RUN_TEST(test_A14_timeout_de_inatividade_de_120_s_nao_grava_o_buffer);
    RUN_TEST(test_A14_gesto_sem_funcao_nao_rearma_o_timeout_de_inatividade);
    RUN_TEST(test_A14_o_aviso_tambem_expira_pela_inatividade_de_120_s);
    RUN_TEST(test_D6_item8_teto_de_300_s_encerra_sem_gravar_mesmo_com_teclas);
    RUN_TEST(test_A14_aborto_por_tecla_devolve_o_par_anterior_integro);
    RUN_TEST(test_A6_o_aviso_de_saida_simulada_esta_em_todas_as_telas_do_procedimento);
    RUN_TEST(test_REQ_DSP_a_geometria_de_todas_as_telas_cabe_no_painel);
    RUN_TEST(test_REQ_DSP_04_o_video_inverso_cai_no_digito_em_edicao);
    RUN_TEST(test_D6_item7_a_saida_so_sai_do_marcador_nas_telas_de_medicao);
    RUN_TEST(test_D6_item7_o_codigo_do_ganho_e_grampeado_em_61342);
    RUN_TEST(test_A14_a_verificacao_negativa_emite_o_espelho_e_sai_em_20_s);
    RUN_TEST(test_A14_o_espelho_abaixo_de_6554_e_grampeado_em_6554);
    RUN_TEST(test_D6_item12_tela_CALIBRACAO_REJEITADA_por_2000_ms_e_aborto);
    RUN_TEST(test_D6_item13_tela_CALIBRACAO_BLOQUEADA_por_2000_ms);
    RUN_TEST(test_D6_item13_reentrada_e_gate_de_limite_tem_codigos_distintos);
    RUN_TEST(test_ihm_F5_e_F6_ignoram_toda_tecla_e_nao_reiniciam_o_prazo);
    RUN_TEST(test_key_gesture_flush_e_pedido_em_toda_troca_de_tela_e_de_modo);
    RUN_TEST(test_REQ_CAL_06_CAL_07_o_par_gravado_governa_proporcao_e_saturacao);
    return UNITY_END();
}
