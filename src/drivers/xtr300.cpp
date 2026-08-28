// Eixo X = DAC-A, eixo Y = DAC-B (folha 2/2). Troca de modo sempre com os dois canais em 0x0000,
// conforme a sequencia recomendada para o XTR300 (TI SBOS404): sem comutar com saida em fundo de escala.
#include "drivers/xtr300.h"

#include <Arduino.h>

namespace {

constexpr uint32_t kPreSwitchSettleMs = 5;
constexpr uint32_t kModeSettleMs = 10;
constexpr uint16_t kZeroCode = 0x0000;

}  // namespace

static_assert(board::kAxisCount == Dac8562::kChannelCount, "um eixo por canal do DAC8562");

Xtr300AnalogOutput::Xtr300AnalogOutput(Dac8562& dac, board::Pin opModePin, CalibrationStore& cal)
    : dac_(dac), opModePin_(opModePin), cal_(cal), mode_(AoMode::Voltage), lastCode_(), ready_(false) {}

void Xtr300AnalogOutput::driveOpMode(AoMode desired) {
    digitalWrite(static_cast<uint8_t>(opModePin_), desired == AoMode::Current ? HIGH : LOW);
}

Status Xtr300AnalogOutput::begin() {
    ready_ = false;
    if (opModePin_ == board::kNoPin) {
        return Status(Err::Param);
    }
    const Status st = dac_.begin();
    if (st.failed()) {
        return st;
    }
    ready_ = true;
    const Status zeroSt = zeroAll();
    if (zeroSt.failed()) {
        ready_ = false;
        return zeroSt;
    }
    pinMode(static_cast<uint8_t>(opModePin_), OUTPUT);
    mode_ = AoMode::Voltage;
    driveOpMode(mode_);
    delay(kModeSettleMs);
    return kOk;
}

Status Xtr300AnalogOutput::setRaw(uint8_t axis, uint16_t code) {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    if (axis >= board::kAxisCount) {
        return Status(Err::Param);
    }
    const Status st = dac_.writeChannel(axis, code);
    if (st.ok()) {
        lastCode_[axis] = code;
    }
    return st;
}

Status Xtr300AnalogOutput::getRaw(uint8_t axis, uint16_t& code) const {
    code = 0;
    if (axis >= board::kAxisCount) {
        return Status(Err::Param);
    }
    if (!ready_) {
        return Status(Err::NotInit);
    }
    code = lastCode_[axis];
    return kOk;
}

Status Xtr300AnalogOutput::setEngineering(uint8_t axis, float value) {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    if (axis >= board::kAxisCount) {
        return Status(Err::Param);
    }
    uint16_t code = 0;
    const Status st = cal_.codeFor(axis, mode_, value, fullScaleCode(), code);
    if (st.failed()) {
        return st;
    }
    return setRaw(axis, code);
}

Status Xtr300AnalogOutput::setMode(AoMode desired) {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    if (desired != AoMode::Voltage && desired != AoMode::Current) {
        return Status(Err::Param);
    }
    if (desired == mode_) {
        return kOk;
    }
    const Status st = zeroAll();
    if (st.failed()) {
        return st;
    }
    delay(kPreSwitchSettleMs);
    driveOpMode(desired);
    mode_ = desired;
    delay(kModeSettleMs);
    return kOk;
}

Status Xtr300AnalogOutput::zeroAll() {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    Status first = kOk;
    for (uint8_t axis = 0; axis < board::kAxisCount; ++axis) {
        const Status st = dac_.writeChannel(axis, kZeroCode);
        if (st.ok()) {
            lastCode_[axis] = kZeroCode;
        } else if (first.ok()) {
            first = st;
        }
    }
    return first;
}

Status Xtr300AnalogOutput::setSpiHz(uint32_t hz) {
    return dac_.setClockHz(hz);
}

uint32_t Xtr300AnalogOutput::spiHz() const {
    return dac_.clockHz();
}
