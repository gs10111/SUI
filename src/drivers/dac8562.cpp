// Quadro de 24 bits (comando + endereco + 16 bits de dado), dado amostrado na borda de descida
// do SCLK com SYNC baixo do primeiro ao ultimo bit: folha 2/2, TI SLAS719E tabelas 8, 9 e 17.
#include "drivers/dac8562.h"

#include <Arduino.h>
#include <SPI.h>

namespace {

constexpr uint8_t kCmdWriteUpdateA = 0x18;
constexpr uint8_t kCmdWriteUpdateB = 0x19;
constexpr uint8_t kCmdPowerUp = 0x20;
constexpr uint8_t kCmdSoftReset = 0x28;
constexpr uint8_t kCmdRefGain = 0x38;
constexpr uint8_t kCmdLdacRegister = 0x30;

constexpr uint16_t kDataPowerUpBoth = 0x0003;
constexpr uint16_t kDataResetAll = 0x0001;
constexpr uint16_t kDataRefIntGain2 = 0x0001;
constexpr uint16_t kDataLdacIgnorePin = 0x0003;
// Saida nula da placa, nao zero-scale do conversor: ver board::kDacZeroCode.

constexpr uint32_t kSyncSettleMs = 1;
constexpr uint32_t kResetSettleMs = 2;

constexpr uint8_t kFrameBytes = 3;

}  // namespace

Dac8562::Dac8562(SpiBus& bus, board::Pin sync, uint32_t clockHz)
    : bus_(bus), sync_(sync), clockHz_(clampHz(clockHz)), lastCode_(), ready_(false) {}

uint32_t Dac8562::clampHz(uint32_t hz) {
    if (hz < board::kDacSpiMinHz) {
        return board::kDacSpiMinHz;
    }
    if (hz > board::kDacSpiMaxHz) {
        return board::kDacSpiMaxHz;
    }
    return hz;
}

Status Dac8562::writeFrame(uint8_t cmd, uint16_t data) {
    if (sync_ == board::kNoPin) {
        return Status(Err::Param);
    }
    if (!ready_ || !bus_.ready()) {
        return Status(Err::NotInit);
    }
    uint8_t buf[kFrameBytes];
    buf[0] = cmd;
    buf[1] = static_cast<uint8_t>((data >> 8) & 0xFF);
    buf[2] = static_cast<uint8_t>(data & 0xFF);
    bus_.beginTransaction(SPISettings(clockHz_, MSBFIRST, SPI_MODE1));
    digitalWrite(static_cast<uint8_t>(sync_), LOW);
    bus_.transferOut(buf, kFrameBytes);
    digitalWrite(static_cast<uint8_t>(sync_), HIGH);
    bus_.endTransaction();
    return kOk;
}

Status Dac8562::begin() {
    ready_ = false;
    if (sync_ == board::kNoPin) {
        return Status(Err::Param);
    }
    if (!bus_.ready()) {
        return Status(Err::NotInit);
    }
    pinMode(static_cast<uint8_t>(sync_), OUTPUT);
    digitalWrite(static_cast<uint8_t>(sync_), HIGH);
    delay(kSyncSettleMs);
    ready_ = true;

    Status st = softReset();
    if (st.failed()) {
        ready_ = false;
        return st;
    }
    delay(kResetSettleMs);
    st = powerUpBoth();
    if (st.failed()) {
        ready_ = false;
        return st;
    }
    st = enableInternalRefGain2();
    if (st.failed()) {
        ready_ = false;
        return st;
    }
    st = ignoreLdacPin();
    if (st.failed()) {
        ready_ = false;
        return st;
    }
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        st = writeChannel(ch, board::kDacZeroCode);
        if (st.failed()) {
            ready_ = false;
            return st;
        }
    }
    return kOk;
}

Status Dac8562::softReset() {
    const Status st = writeFrame(kCmdSoftReset, kDataResetAll);
    if (st.ok()) {
        for (uint8_t i = 0; i < kChannelCount; ++i) {
            lastCode_[i] = 0;
        }
    }
    return st;
}

Status Dac8562::powerUpBoth() {
    return writeFrame(kCmdPowerUp, kDataPowerUpBoth);
}

Status Dac8562::enableInternalRefGain2() {
    return writeFrame(kCmdRefGain, kDataRefIntGain2);
}

Status Dac8562::ignoreLdacPin() {
    return writeFrame(kCmdLdacRegister, kDataLdacIgnorePin);
}

Status Dac8562::writeChannel(uint8_t ch, uint16_t code) {
    if (ch >= kChannelCount) {
        return Status(Err::Param);
    }
    const uint8_t cmd = (ch == 0) ? kCmdWriteUpdateA : kCmdWriteUpdateB;
    const Status st = writeFrame(cmd, code);
    if (st.ok()) {
        lastCode_[ch] = code;
    }
    return st;
}

Status Dac8562::setClockHz(uint32_t hz) {
    if (hz < board::kDacSpiMinHz || hz > board::kDacSpiMaxHz) {
        return Status(Err::Range);
    }
    clockHz_ = hz;
    return kOk;
}

uint16_t Dac8562::lastCode(uint8_t ch) const {
    if (ch >= kChannelCount) {
        return 0;
    }
    return lastCode_[ch];
}
