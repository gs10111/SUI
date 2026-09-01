// Implementacao do Preset (5.6) e da sua interacao com o Sentido do Sensor (5.8).
// Decisao A9 aprovada em 2026-08-31: leitura = clamp(dir * bruto + offset, -900, +900), com
// offset := P - dir * bruto no aceite do gesto. Guardas de D1 itens 6, 9, 10 e 11 na ordem
// armamento -> dado valido -> estabilidade -> magnitude. Toda a aritmetica e inteira, em decimos
// de grau, e o unico ponto em que ela sai de int16 e a soma intermediaria de reading(), feita em
// int32 antes do grampo, como o cabecalho de domain/angle.h exige.
#include "domain/ui/preset_wizard.h"

namespace domain {
namespace ui {

namespace {

const char kLabelBack[] = "Voltar";
const char kLabelPresetX[] = "Preset X";
const char kLabelPresetY[] = "Preset Y";


int16_t absDeci(int16_t value) { return value < 0 ? static_cast<int16_t>(-value) : value; }

}  // namespace

PresetWizard::PresetWizard(const IClock& clock)
    : clock_(clock),
      item_(PresetMenuItem::Back),
      editing_(false),
      editAxis_(Axis::X),
      visited_(false),
      armWindowOpen_(false),
      armedAtMs_(0),
      staticSinceMs_(0),
      minDeci_{0, 0},
      maxDeci_{0, 0},
      lastDeci_{0, 0},
      validCount_(0),
      windowCount_(0),
      haveSample_(false),
      capturing_(false),
      pending_(false),
      pendingSinceMs_(0),
      pendingOffsetDeci_{0, 0},
      warning_(false),
      warningSinceMs_(0) {}

int16_t PresetWizard::directed(Angle raw, SensorDir dir) {
    const int16_t deci = raw.deciDegrees();
    return (dir == SensorDir::CounterClockwise) ? static_cast<int16_t>(-deci) : deci;
}

Angle PresetWizard::reading(Angle raw, SensorDir dir, int16_t offsetDeci) {
    if (!raw.valid()) {
        return Angle::invalid();
    }
    const int32_t soma = static_cast<int32_t>(directed(raw, dir)) + static_cast<int32_t>(offsetDeci);
    return Angle::clamped(soma);
}

bool PresetWizard::offsetFor(Angle target, Angle raw, SensorDir dir, int16_t& outOffsetDeci) {
    if (!target.valid() || !raw.valid()) {
        return false;
    }
    const int32_t offset =
        static_cast<int32_t>(target.deciDegrees()) - static_cast<int32_t>(directed(raw, dir));
    outOffsetDeci = static_cast<int16_t>(offset);
    return true;
}

Angle PresetWizard::reading(Axis axis, Angle raw, const Parameters& params) {
    if (!Parameters::axisValid(axis)) {
        return Angle::invalid();
    }
    return reading(raw, params.sensorDir(axis), params.presetOffsetDeci(axis));
}

void PresetWizard::nextItem() {
    if (item_ != PresetMenuItem::PresetY) {
        item_ = static_cast<PresetMenuItem>(static_cast<uint8_t>(item_) + 1u);
    }
}

void PresetWizard::prevItem() {
    if (item_ != PresetMenuItem::Back) {
        item_ = static_cast<PresetMenuItem>(static_cast<uint8_t>(item_) - 1u);
    }
}

const char* PresetWizard::itemLabel(PresetMenuItem which) {
    switch (which) {
        case PresetMenuItem::PresetX:
            return kLabelPresetX;
        case PresetMenuItem::PresetY:
            return kLabelPresetY;
        case PresetMenuItem::Back:
        default:
            return kLabelBack;
    }
}

// --- CAPTURA -------------------------------------------------------------------------------

bool PresetWizard::beginCapture(Axis axis) {
    if (!Parameters::axisValid(axis)) {
        return false;
    }
    editAxis_ = axis;
    capturing_ = true;
    editing_ = false;
    // Mesmo armamento do editor antigo: ter passado pelo Preset e o que autoriza o duplo toque
    // em CIMA na tela principal depois de sair do Modo Programacao.
    visited_ = true;
    clearPending();
    return true;
}

void PresetWizard::cancelCapture() {
    capturing_ = false;
}

PsetOutcome PresetWizard::commitCapture(Parameters& params) {
    if (!capturing_) {
        return PsetOutcome::Ignored;
    }
    if (!dataValid()) {
        return PsetOutcome::RefusedNoData;
    }
    if (!stable()) {
        return PsetOutcome::RefusedUnstable;
    }
    int16_t novos[Parameters::kAxisCount] = {0, 0};
    if (!computeOffsets(params, novos)) {
        return PsetOutcome::RefusedNoData;
    }
    if (!offsetsWritable(novos[idx(Axis::X)], novos[idx(Axis::Y)])) {
        return PsetOutcome::RefusedNoData;
    }
    if (!applyOffsets(params, novos)) {
        return PsetOutcome::RefusedNoData;
    }
    capturing_ = false;
    return PsetOutcome::Applied;
}


void PresetWizard::clearPending() {
    pending_ = false;
    pendingOffsetDeci_[idx(Axis::X)] = 0;
    pendingOffsetDeci_[idx(Axis::Y)] = 0;
}

void PresetWizard::tick() {
    const uint32_t agora = clock_.nowMs();
    if (armWindowOpen_ && deadlineReached(armedAtMs_, agora, kArmValidityMs)) {
        armWindowOpen_ = false;
        visited_ = false;
    }
    if (pending_ && deadlineReached(pendingSinceMs_, agora, kConfirmWindowMs)) {
        clearPending();
    }
    if (warning_ && deadlineReached(warningSinceMs_, agora, kDirWarningMs)) {
        warning_ = false;
    }
}

void PresetWizard::onProgrammingExit() {
    editing_ = false;
    if (!visited_) {
        return;
    }
    armWindowOpen_ = true;
    armedAtMs_ = clock_.nowMs();
}

bool PresetWizard::armed() const { return visited_ && armWindowOpen_; }

void PresetWizard::disarm() {
    visited_ = false;
    armWindowOpen_ = false;
    clearPending();
}

void PresetWizard::clearSamples() {
    validCount_ = 0;
    windowCount_ = 0;
    haveSample_ = false;
    staticSinceMs_ = 0;
}

void PresetWizard::sample(Angle rawX, Angle rawY) {
    if (!rawX.valid() || !rawY.valid()) {
        clearSamples();
        return;
    }
    const int16_t agoraDeci[Parameters::kAxisCount] = {rawX.deciDegrees(), rawY.deciDegrees()};
    const uint32_t agora = clock_.nowMs();

    if (validCount_ < kMinSamples) {
        ++validCount_;
    }
    if (!haveSample_) {
        abrirJanela(agoraDeci, agora);
        return;
    }

    for (uint8_t i = 0; i < Parameters::kAxisCount; ++i) {
        lastDeci_[i] = agoraDeci[i];
        if (agoraDeci[i] < minDeci_[i]) {
            minDeci_[i] = agoraDeci[i];
        }
        if (agoraDeci[i] > maxDeci_[i]) {
            maxDeci_[i] = agoraDeci[i];
        }
    }
    for (uint8_t i = 0; i < Parameters::kAxisCount; ++i) {
        if (static_cast<int32_t>(maxDeci_[i]) - static_cast<int32_t>(minDeci_[i]) >
            static_cast<int32_t>(kStabilityPeakToPeakDeci)) {
            // Saiu da banda: a janela recomeca AQUI, com a amostra que a rompeu. Nao ha
            // credito parcial - "parado por 3 s" volta a contar do zero.
            abrirJanela(agoraDeci, agora);
            return;
        }
    }
    if (windowCount_ < kMinSamples) {
        ++windowCount_;
    }
}

void PresetWizard::abrirJanela(const int16_t (&deci)[Parameters::kAxisCount], uint32_t nowMs) {
    for (uint8_t i = 0; i < Parameters::kAxisCount; ++i) {
        minDeci_[i] = deci[i];
        maxDeci_[i] = deci[i];
        lastDeci_[i] = deci[i];
    }
    staticSinceMs_ = nowMs;
    windowCount_ = 1;
    haveSample_ = true;
}

bool PresetWizard::dataValid() const { return haveSample_ && validCount_ >= kMinSamples; }

uint32_t PresetWizard::staticForMs() const {
    if (!haveSample_) {
        return 0;
    }
    return elapsedMs(staticSinceMs_, clock_.nowMs());
}

int16_t PresetWizard::peakToPeakDeci(Axis axis) const {
    if (!Parameters::axisValid(axis) || !haveSample_) {
        return 0;
    }
    return static_cast<int16_t>(maxDeci_[idx(axis)] - minDeci_[idx(axis)]);
}

bool PresetWizard::stable() const {
    if (!dataValid()) {
        return false;
    }
    // A banda ja e garantida pela janela: sair dela reinicia a contagem. Faltam o TEMPO e um
    // piso de amostras DENTRO da janela corrente, para que duas leituras separadas por 3 s nao
    // passem por "parado".
    return windowCount_ >= kMinSamples && staticForMs() >= kStaticHoldMs;
}

Angle PresetWizard::lastRaw(Axis axis) const {
    if (!Parameters::axisValid(axis) || !haveSample_) {
        return Angle::invalid();
    }
    return Angle::fromDeciDegrees(lastDeci_[idx(axis)]);
}

bool PresetWizard::computeOffsets(const Parameters& params,
                                  int16_t (&outOffsets)[Parameters::kAxisCount]) const {
    const Axis eixo[Parameters::kAxisCount] = {Axis::X, Axis::Y};
    for (uint8_t i = 0; i < Parameters::kAxisCount; ++i) {
        if (!offsetFor(params.preset(eixo[i]), lastRaw(eixo[i]), params.sensorDir(eixo[i]),
                       outOffsets[i])) {
            return false;
        }
    }
    return true;
}

bool PresetWizard::offsetsWritable(int16_t offsetXDeci, int16_t offsetYDeci) {
    const int16_t par[Parameters::kAxisCount] = {offsetXDeci, offsetYDeci};
    for (uint8_t i = 0; i < Parameters::kAxisCount; ++i) {
        if (par[i] < Parameters::kPresetOffsetMinDeci || par[i] > Parameters::kPresetOffsetMaxDeci) {
            return false;
        }
    }
    return true;
}

bool PresetWizard::applyOffsets(Parameters& params,
                                const int16_t (&offsets)[Parameters::kAxisCount]) const {
    if (!offsetsWritable(offsets[idx(Axis::X)], offsets[idx(Axis::Y)])) {
        return false;
    }
    const Status x = params.setPresetOffset(Axis::X, offsets[idx(Axis::X)]);
    const Status y = params.setPresetOffset(Axis::Y, offsets[idx(Axis::Y)]);
    return x.ok() && y.ok();
}

PsetOutcome PresetWizard::requestPset(Parameters& params) {
    tick();
    if (!armed()) {
        clearPending();
        return PsetOutcome::Ignored;
    }
    if (!dataValid()) {
        clearPending();
        return PsetOutcome::RefusedNoData;
    }
    if (!stable()) {
        clearPending();
        return PsetOutcome::RefusedUnstable;
    }
    int16_t novos[Parameters::kAxisCount] = {0, 0};
    if (!computeOffsets(params, novos)) {
        clearPending();
        return PsetOutcome::RefusedNoData;
    }
    const int16_t deslocX =
        absDeci(static_cast<int16_t>(novos[idx(Axis::X)] - params.presetOffsetDeci(Axis::X)));
    const int16_t deslocY =
        absDeci(static_cast<int16_t>(novos[idx(Axis::Y)] - params.presetOffsetDeci(Axis::Y)));
    if (deslocX > kConfirmThresholdDeci || deslocY > kConfirmThresholdDeci) {
        pending_ = true;
        pendingSinceMs_ = clock_.nowMs();
        pendingOffsetDeci_[idx(Axis::X)] = novos[idx(Axis::X)];
        pendingOffsetDeci_[idx(Axis::Y)] = novos[idx(Axis::Y)];
        return PsetOutcome::NeedsConfirm;
    }
    clearPending();
    if (!applyOffsets(params, novos)) {
        return PsetOutcome::RefusedNoData;
    }
    disarm();
    return PsetOutcome::Applied;
}

bool PresetWizard::awaitingConfirm() const { return pending_; }

int16_t PresetWizard::pendingOffsetDeci(Axis axis) const {
    if (!Parameters::axisValid(axis)) {
        return 0;
    }
    return pendingOffsetDeci_[idx(axis)];
}

PsetOutcome PresetWizard::confirmPset(Parameters& params) {
    tick();
    if (!awaitingConfirm()) {
        clearPending();
        return PsetOutcome::Ignored;
    }
    clearPending();
    if (!armed()) {
        return PsetOutcome::Ignored;
    }
    if (!dataValid()) {
        return PsetOutcome::RefusedNoData;
    }
    if (!stable()) {
        return PsetOutcome::RefusedUnstable;
    }
    int16_t novos[Parameters::kAxisCount] = {0, 0};
    if (!computeOffsets(params, novos)) {
        return PsetOutcome::RefusedNoData;
    }
    if (!applyOffsets(params, novos)) {
        return PsetOutcome::RefusedNoData;
    }
    disarm();
    return PsetOutcome::Applied;
}

void PresetWizard::cancelPset() { clearPending(); }

Status PresetWizard::applySensorDir(Axis axis, SensorDir dir, Parameters& params) {
    if (!Parameters::axisValid(axis)) {
        return Err::Param;
    }
    if (dir != SensorDir::Clockwise && dir != SensorDir::CounterClockwise) {
        return Err::Range;
    }
    if (params.sensorDir(axis) == dir) {
        return kOk;
    }
    const Status gravado = params.setSensorDir(axis, dir);
    if (gravado.failed()) {
        return gravado;
    }
    const Status zerado = params.setPresetOffset(axis, 0);
    if (zerado.failed()) {
        return zerado;
    }
    disarm();
    warning_ = true;
    warningSinceMs_ = clock_.nowMs();
    return kOk;
}

bool PresetWizard::warningActive() const { return warning_; }

bool PresetWizard::formatDeci(int16_t deci, char* out, uint8_t cap) {
    if (out == nullptr || cap < kValueTextCap) {
        return false;
    }
    const int16_t magnitude = absDeci(deci);
    const int16_t inteiro = static_cast<int16_t>(magnitude / 10);
    if (inteiro > 999) {
        return false;
    }
    out[0] = (deci < 0) ? '-' : '+';
    out[1] = static_cast<char>('0' + (inteiro / 100));
    out[2] = static_cast<char>('0' + ((inteiro / 10) % 10));
    out[3] = static_cast<char>('0' + (inteiro % 10));
    out[4] = ',';
    out[5] = static_cast<char>('0' + (magnitude % 10));
    out[6] = '\0';
    return true;
}

namespace {

bool escrever(char* out, uint8_t cap, uint8_t& pos, const char* texto) {
    for (uint8_t i = 0; texto[i] != '\0'; ++i) {
        if (pos + 1u >= cap) {
            return false;
        }
        out[pos] = texto[i];
        ++pos;
    }
    return true;
}

}  // namespace

bool PresetWizard::formatIndicator(Axis axis, const Parameters& params, char* out, uint8_t cap) {
    if (out == nullptr || cap < kIndicatorTextCap || !Parameters::axisValid(axis)) {
        return false;
    }
    const int16_t offset = params.presetOffsetDeci(axis);
    if (offset == 0) {
        return false;
    }
    uint8_t pos = 0;
    if (!escrever(out, cap, pos, "PSET ")) {
        return false;
    }
    out[pos] = (axis == Axis::Y) ? 'Y' : 'X';
    ++pos;
    if (!escrever(out, cap, pos, ":")) {
        return false;
    }
    if (!formatDeci(offset, out + pos, static_cast<uint8_t>(cap - pos))) {
        return false;
    }
    return true;
}

bool PresetWizard::formatDirWarningLine1(Axis axis, char* out, uint8_t cap) {
    if (out == nullptr || cap < kDirWarnLine1Cap || !Parameters::axisValid(axis)) {
        return false;
    }
    uint8_t pos = 0;
    if (!escrever(out, cap, pos, "Sentido ")) {
        return false;
    }
    out[pos] = (axis == Axis::Y) ? 'Y' : 'X';
    ++pos;
    if (!escrever(out, cap, pos, " alterado!")) {
        return false;
    }
    out[pos] = '\0';
    return true;
}

bool PresetWizard::formatDirWarningLine2(Axis axis, char* out, uint8_t cap) {
    if (out == nullptr || cap < kDirWarnLine2Cap || !Parameters::axisValid(axis)) {
        return false;
    }
    const char letra = (axis == Axis::Y) ? 'Y' : 'X';
    uint8_t pos = 0;
    if (!escrever(out, cap, pos, "Preset zerado - confira ")) {
        return false;
    }
    out[pos] = letra;
    ++pos;
    if (!escrever(out, cap, pos, "1 ")) {
        return false;
    }
    out[pos] = letra;
    ++pos;
    if (!escrever(out, cap, pos, "2")) {
        return false;
    }
    out[pos] = '\0';
    return true;
}

bool PresetWizard::formatPendingConfirm(Axis axis, char* out, uint8_t cap) const {
    if (out == nullptr || cap < kConfirmTextCap || !Parameters::axisValid(axis) ||
        !awaitingConfirm()) {
        return false;
    }
    uint8_t pos = 0;
    if (!escrever(out, cap, pos, "Novo PSET ")) {
        return false;
    }
    out[pos] = (axis == Axis::Y) ? 'Y' : 'X';
    ++pos;
    if (!escrever(out, cap, pos, ":")) {
        return false;
    }
    if (!formatDeci(pendingOffsetDeci_[idx(axis)], out + pos, static_cast<uint8_t>(cap - pos))) {
        return false;
    }
    return true;
}

}  // namespace ui
}  // namespace domain
