// Implementacao do assistente de Auto Calibracao. Contrato, citacoes de manual (5.7, L164 a
// L187), decisao A14, decisao 6 (itens 1, 2, 6, 7, 8, 11, 12, 13 e 14) e o mapa de codigo da
// saida analogica passo a passo estao em domain/ui/calibration_wizard.h.
//
// Os textos das telas sao literais deste arquivo, escritos byte a byte como o manual imprime,
// como docs/ihm-estados.md secao 3.6 registra e como DECISIONS.md aprova: "Ajuste 0Vcc:",
// "Angulo fim de escala X(graus):", "Ajuste 10Vcc:", "Verifique -10Vcc",
// "Alteracao bem sucedida!", "CALIBRACAO REJEITADA" e "CALIBRACAO BLOQUEADA" - sem cedilha,
// sem til e sem circunflexo, porque e assim que o painel escreve.
#include "domain/ui/calibration_wizard.h"

namespace domain {

namespace {

constexpr const char kPrefixZero[] = "Ajuste 0Vcc:";
constexpr const char kPrefixGain[] = "Ajuste 10Vcc:";
constexpr const char kPrefixAngleX[] = "Angulo fim de escala X(graus):";
constexpr const char kPrefixAngleY[] = "Angulo fim de escala Y(graus):";
constexpr const char kTextWarningX[] = "Auto Calibracao X";
constexpr const char kTextWarningY[] = "Auto Calibracao Y";
constexpr const char kTextVerifyNegative[] = "Verifique -10Vcc";
constexpr const char kTextDone[] = "Alteracao bem sucedida!";
constexpr const char kTextRejected[] = "CALIBRACAO REJEITADA";
constexpr const char kTextBlocked[] = "CALIBRACAO BLOQUEADA";
constexpr const char kWarnSimulated[] = "SAIDA SIMULADA";
constexpr const char kWarnBlockPlc[] = "Bloqueie o CLP";
constexpr const char kAngleOutOfRange[] = "Fundo de escala invalido!";

constexpr int16_t kTextX = 0;
constexpr int16_t kTextY = 0;

uint8_t comprimentoDe(const char* texto) {
    uint8_t total = 0;
    while (texto != nullptr && texto[total] != '\0' && total < 255u) {
        ++total;
    }
    return total;
}

bool copiar(char* destino, uint8_t cap, uint8_t& escrito, const char* origem) {
    if (destino == nullptr || origem == nullptr) {
        return false;
    }
    uint8_t lido = 0;
    while (origem[lido] != '\0') {
        if (escrito + 1u >= cap) {
            return false;
        }
        destino[escrito] = origem[lido];
        ++escrito;
        ++lido;
    }
    destino[escrito] = '\0';
    return true;
}

}  // namespace

CalibrationWizard::CalibrationWizard(AnalogCalibration& calibration, const IClock& clock)
    : cal_(calibration),
      clock_(clock),
      editor_(),
      step_(Step::Idle),
      axis_(Axis::X),
      beganMs_(0),
      stepSinceMs_(0),
      lastKeyMs_(0),
      flushRequested_(false) {}

uint16_t CalibrationWizard::clampCode(int32_t code) {
    if (code < static_cast<int32_t>(kCodeClampMin)) {
        return kCodeClampMin;
    }
    if (code > static_cast<int32_t>(kCodeClampMax)) {
        return kCodeClampMax;
    }
    return static_cast<uint16_t>(code);
}

bool CalibrationWizard::editing() const {
    return step_ == Step::Zero || step_ == Step::FullScaleAngle || step_ == Step::Gain;
}

bool CalibrationWizard::watched() const { return step_ == Step::Warning || editing(); }

const char* CalibrationWizard::prefix() const {
    switch (step_) {
        case Step::Zero:
            return kPrefixZero;
        case Step::FullScaleAngle:
            return (axis_ == Axis::Y) ? kPrefixAngleY : kPrefixAngleX;
        case Step::Gain:
            return kPrefixGain;
        default:
            return nullptr;
    }
}

const char* CalibrationWizard::fixedText() const {
    switch (step_) {
        case Step::Blocked:
            return kTextBlocked;
        case Step::Warning:
            return (axis_ == Axis::Y) ? kTextWarningY : kTextWarningX;
        case Step::VerifyNegative:
            return kTextVerifyNegative;
        case Step::Done:
            return kTextDone;
        case Step::Rejected:
            return kTextRejected;
        default:
            return nullptr;
    }
}

uint8_t CalibrationWizard::prefixLength() const { return comprimentoDe(prefix()); }

void CalibrationWizard::openTrimEditor(uint16_t field) {
    DigitFieldSpec spec{};
    spec.digits = 4;
    spec.decimals = 0;
    spec.signedField = false;
    spec.min = 0;
    spec.max = static_cast<int16_t>(AnalogCalibration::kTrimFieldMax);
    spec.outOfRangeMessage = nullptr;
    (void)editor_.open(spec, static_cast<int16_t>(field));
}

void CalibrationWizard::openAngleEditor(int16_t deci) {
    DigitFieldSpec spec{};
    spec.digits = 4;
    spec.decimals = 1;
    spec.signedField = true;
    spec.min = AnalogScaler::kFullScaleMinDeci;
    spec.max = AnalogScaler::kFullScaleMaxDeci;
    spec.outOfRangeMessage = kAngleOutOfRange;
    (void)editor_.open(spec, deci);
}

void CalibrationWizard::pushEditorValue() {
    const int16_t valor = editor_.value();
    if (step_ == Step::Zero) {
        (void)cal_.setZeroField(static_cast<uint16_t>(valor));
        return;
    }
    if (step_ == Step::Gain) {
        (void)cal_.setGainField(static_cast<uint16_t>(valor));
    }
}

void CalibrationWizard::enterStep(Step next, uint32_t atMs) {
    step_ = next;
    stepSinceMs_ = atMs;
    flushRequested_ = true;
}

void CalibrationWizard::abortAt(uint32_t atMs) {
    cal_.abort();
    enterStep(Step::ExitMarker, atMs);
}

bool CalibrationWizard::consumeFlushRequest() {
    const bool pedido = flushRequested_;
    flushRequested_ = false;
    return pedido;
}

Status CalibrationWizard::begin(Axis axis, bool anyLimitSignalled) {
    if (step_ != Step::Idle) {
        return Status(Err::Busy);
    }
    if (!Parameters::axisValid(axis)) {
        return Status(Err::Param);
    }
    const uint32_t agora = clock_.nowMs();
    if (anyLimitSignalled) {
        axis_ = axis;
        enterStep(Step::Blocked, agora);
        return Status(Err::Aborted);
    }
    const Status abertura = cal_.begin();
    if (abertura.failed()) {
        return abertura;
    }
    axis_ = axis;
    beganMs_ = agora;
    lastKeyMs_ = agora;
    enterStep(Step::Warning, agora);
    return kOk;
}

void CalibrationWizard::onGesture(const Gesture& gesture) {
    if (step_ == Step::Warning) {
        if (gesture.kind != GestureKind::Hold || gesture.key != Key::Menu) {
            return;
        }
        if (!deadlineReached(beganMs_, gesture.atMs, kEntryMarkerMs)) {
            return;
        }
        lastKeyMs_ = gesture.atMs;
        openTrimEditor(cal_.zeroField());
        enterStep(Step::Zero, gesture.atMs);
        return;
    }

    if (step_ == Step::VerifyNegative) {
        if (gesture.kind != GestureKind::ShortTap || gesture.key != Key::Menu) {
            return;
        }
        lastKeyMs_ = gesture.atMs;
        enterStep(Step::Done, gesture.atMs);
        return;
    }

    if (!editing()) {
        return;
    }

    if (gesture.kind == GestureKind::ShortTap) {
        if (gesture.key == Key::Menu) {
            lastKeyMs_ = gesture.atMs;
            editor_.menu();
        } else if (gesture.key == Key::Up) {
            lastKeyMs_ = gesture.atMs;
            editor_.up();
            pushEditorValue();
        } else if (gesture.key == Key::Down) {
            lastKeyMs_ = gesture.atMs;
            editor_.down();
            pushEditorValue();
        }
        return;
    }

    if (gesture.kind != GestureKind::Hold || gesture.key != Key::Menu) {
        return;
    }
    lastKeyMs_ = gesture.atMs;

    if (step_ == Step::Zero) {
        if (cal_.confirmZero().failed()) {
            return;
        }
        openAngleEditor(cal_.fullScaleAngle());
        enterStep(Step::FullScaleAngle, gesture.atMs);
        return;
    }

    if (step_ == Step::FullScaleAngle) {
        if (editor_.confirm() != ConfirmResult::Ok) {
            return;
        }
        if (cal_.setFullScaleAngle(editor_.value()).failed()) {
            return;
        }
        if (cal_.confirmFullScaleAngle().failed()) {
            return;
        }
        openTrimEditor(cal_.gainField());
        enterStep(Step::Gain, gesture.atMs);
        return;
    }

    if (cal_.commit().ok()) {
        enterStep(Step::VerifyNegative, gesture.atMs);
        return;
    }
    enterStep(Step::Rejected, gesture.atMs);
}

void CalibrationWizard::tick() {
    const uint32_t agora = clock_.nowMs();

    if (step_ == Step::Idle) {
        return;
    }

    if (step_ == Step::Blocked) {
        if (deadlineReached(stepSinceMs_, agora, kBlockedMs)) {
            enterStep(Step::Idle, agora);
        }
        return;
    }

    if (step_ != Step::ExitMarker && deadlineReached(beganMs_, agora, kOverrideCeilingMs)) {
        abortAt(agora);
        return;
    }

    if (watched() && deadlineReached(lastKeyMs_, agora, kInactivityMs)) {
        abortAt(agora);
        return;
    }

    if (step_ == Step::VerifyNegative && deadlineReached(stepSinceMs_, agora, kVerifyNegativeMs)) {
        enterStep(Step::Done, agora);
        return;
    }

    if (step_ == Step::Done && deadlineReached(stepSinceMs_, agora, kDoneMs)) {
        enterStep(Step::Idle, agora);
        return;
    }

    if (step_ == Step::Rejected && deadlineReached(stepSinceMs_, agora, kRejectedMs)) {
        abortAt(agora);
        return;
    }

    if (step_ == Step::ExitMarker && deadlineReached(stepSinceMs_, agora, kExitMarkerMs)) {
        enterStep(Step::Idle, agora);
    }
}

void CalibrationWizard::abort() {
    if (step_ == Step::Idle || step_ == Step::ExitMarker) {
        return;
    }
    if (step_ == Step::Blocked) {
        enterStep(Step::Idle, clock_.nowMs());
        return;
    }
    abortAt(clock_.nowMs());
}

uint16_t CalibrationWizard::outputCode() const {
    if (step_ == Step::Zero) {
        const int32_t codigo = static_cast<int32_t>(AnalogScaler::kZeroCode) +
                               static_cast<int32_t>(cal_.zeroField()) -
                               static_cast<int32_t>(AnalogCalibration::kTrimNeutral);
        return clampCode(codigo);
    }

    if (step_ == Step::Gain) {
        const int32_t zero = static_cast<int32_t>(AnalogScaler::kZeroCode) +
                             static_cast<int32_t>(cal_.zeroField()) -
                             static_cast<int32_t>(AnalogCalibration::kTrimNeutral);
        const int32_t codigo = zero + AnalogScaler::kNominalSpan +
                               static_cast<int32_t>(cal_.gainField()) -
                               static_cast<int32_t>(AnalogCalibration::kTrimNeutral);
        return clampCode(codigo);
    }

    if (step_ == Step::VerifyNegative) {
        return clampCode(static_cast<int32_t>(cal_.scaler().mirrorCode()));
    }

    return kFaultCode;
}

bool CalibrationWizard::screenText(char* out, uint8_t cap) const {
    if (out == nullptr || cap == 0) {
        return false;
    }
    uint8_t escrito = 0;
    const char* fixo = fixedText();
    if (fixo != nullptr) {
        return copiar(out, cap, escrito, fixo);
    }
    if (!editing()) {
        return false;
    }
    if (!copiar(out, cap, escrito, prefix())) {
        return false;
    }
    char campo[DigitEditor::kTextCap];
    if (!editor_.format(campo, DigitEditor::kTextCap)) {
        return false;
    }
    return copiar(out, cap, escrito, campo);
}

Status CalibrationWizard::render(IDisplay& display) const {
    char texto[kTextCap];
    if (!screenText(texto, kTextCap)) {
        return Status(Err::NotInit);
    }

    Status resultado = display.clear();
    if (resultado.failed()) {
        return resultado;
    }
    resultado = display.drawText(kTextX, kTextY, texto, TextFont::Small, TextInk::Normal);
    if (resultado.failed()) {
        return resultado;
    }

    if (editing()) {
        const uint8_t indice =
            static_cast<uint8_t>(prefixLength() + editor_.cursorTextIndex());
        if (indice < comprimentoDe(texto)) {
            char recorte[kTextCap];
            for (uint8_t i = 0; i < indice; ++i) {
                recorte[i] = texto[i];
            }
            recorte[indice] = '\0';
            const char glifo[2] = {texto[indice], '\0'};
            const int16_t x = static_cast<int16_t>(
                kTextX + static_cast<int16_t>(display.textWidthPx(TextFont::Small, recorte)));
            resultado = display.drawText(x, kTextY, glifo, TextFont::Small, TextInk::Inverse);
            if (resultado.failed()) {
                return resultado;
            }
        }
    }

    if (overriding()) {
        const int16_t passo = static_cast<int16_t>(display.lineHeightPx(TextFont::Small) + 1);
        const int16_t avisoY2 = static_cast<int16_t>(display.heightPx() - passo);
        const int16_t avisoY1 = static_cast<int16_t>(avisoY2 - passo);
        resultado = display.drawText(kTextX, avisoY1, kWarnSimulated, TextFont::Small,
                                     TextInk::Normal);
        if (resultado.failed()) {
            return resultado;
        }
        resultado = display.drawText(kTextX, avisoY2, kWarnBlockPlc, TextFont::Small,
                                     TextInk::Normal);
        if (resultado.failed()) {
            return resultado;
        }
    }
    return display.present();
}

}  // namespace domain
