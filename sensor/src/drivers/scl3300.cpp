// PUSI-DI261930: driver do Murata SCL3300 (Rev.4, doc 4921, Tabelas 11 e 18). CS alto por >= 10 us
// entre quadros, medido com micros(); a resposta do quadro N pertence ao comando do quadro N-1.
#include "drivers/scl3300.h"

#include <Arduino.h>

namespace {

constexpr uint32_t kFrameStuckLow = 0x00000000u;
constexpr uint32_t kFrameStuckHigh = 0xFFFFFFFFu;

constexpr uint16_t kStatusHardMask =
    static_cast<uint16_t>(scl::kStatusFault & static_cast<uint16_t>(~static_cast<uint32_t>(scl::kStatusSat)));

constexpr uint8_t kIdxAngX = 1;
constexpr uint8_t kIdxAngY = 2;
constexpr uint8_t kIdxAngZ = 3;
constexpr uint8_t kIdxTemp = 4;
constexpr uint8_t kIdxStatus = 5;

}  // namespace

Scl3300::Scl3300(SpiBus& bus, board::Pin cs, uint32_t clockHz, uint8_t mode)
    : bus_(bus),
      cs_(cs),
      mode_(clampMode(mode)),
      clockHz_(clampHz(clockHz)),
      settings_(clockHz_, MSBFIRST, SPI_MODE0),
      lastFrameEndUs_(0),
      frames_(0),
      reads_(0),
      crcErrors_(0),
      frameErrors_(0),
      whoAmi_(0),
      lastStatus_(0),
      lastErrFlag1_(0),
      lastErrFlag2_(0),
      lastSto_(0),
      lastRs_(scl::Rs::Startup),
      ready_(false),
      selfTestFailed_(false),
      flagsRead_(false),
      trace_{},
      traceFill_(0),
      traceHead_(0) {}

uint32_t Scl3300::clampHz(uint32_t hz) {
    if (hz < kSpiMinHz) {
        return kSpiMinHz;
    }
    if (hz > kSpiMaxHz) {
        return kSpiMaxHz;
    }
    return hz;
}

uint8_t Scl3300::clampMode(uint8_t requested) {
    if (requested < scl::kModeMin || requested > scl::kModeMax) {
        return scl::kModeMin;
    }
    return requested;
}

void Scl3300::waitCsHigh() {
    const uint32_t elapsed = static_cast<uint32_t>(micros()) - lastFrameEndUs_;
    if (elapsed < kCsHighUs) {
        delayMicroseconds(kCsHighUs - elapsed);
    }
}

Status Scl3300::sendFrame(uint32_t command, uint32_t& previousResponse) {
    previousResponse = 0;
    if (cs_ == board::kNoPin) {
        return Status(Err::Param);
    }
    if (!bus_.ready()) {
        return Status(Err::NotInit);
    }
    waitCsHigh();
    digitalWrite(static_cast<uint8_t>(cs_), LOW);
    const uint32_t rx = bus_.transfer32(command, settings_);
    digitalWrite(static_cast<uint8_t>(cs_), HIGH);
    lastFrameEndUs_ = static_cast<uint32_t>(micros());
    ++frames_;
    previousResponse = rx;
    recordTrace(command, rx);
    return kOk;
}

Status Scl3300::exchange(uint32_t command, uint32_t& previousResponse) {
    const Status st = sendFrame(command, previousResponse);
    if (st.failed()) {
        return st;
    }
    if (previousResponse == kFrameStuckLow || previousResponse == kFrameStuckHigh) {
        ++frameErrors_;
        return Status(Err::Io);
    }
    if (!scl::frameCrcOk(previousResponse)) {
        ++crcErrors_;
        return Status(Err::Crc);
    }
    lastRs_ = scl::rsOf(previousResponse);
    if (lastRs_ == scl::Rs::Reserved) {
        ++frameErrors_;
        return Status(Err::HwFault);
    }
    return kOk;
}

Status Scl3300::readRegister(uint32_t readCommand, uint16_t& value) {
    uint32_t resp = 0;
    Status st = exchange(readCommand, resp);
    if (st.failed()) {
        return st;
    }
    st = exchange(readCommand, resp);
    if (st.failed()) {
        return st;
    }
    value = scl::frameData(resp);
    return kOk;
}

Status Scl3300::begin() {
    ready_ = false;
    selfTestFailed_ = false;
    flagsRead_ = false;
    traceFill_ = 0;
    traceHead_ = 0;
    lastRs_ = scl::Rs::Startup;
    if (cs_ == board::kNoPin) {
        return Status(Err::Param);
    }
    if (!bus_.ready()) {
        return Status(Err::NotInit);
    }

    pinMode(static_cast<uint8_t>(cs_), OUTPUT);
    digitalWrite(static_cast<uint8_t>(cs_), HIGH);
    lastFrameEndUs_ = static_cast<uint32_t>(micros());

    uint32_t resp = 0;
    Status st = sendFrame(scl::kCmdSwReset, resp);
    if (st.failed()) {
        return st;
    }
    delay(scl::kResetSettleMs);

    st = sendFrame(scl::modeCommand(mode_), resp);
    if (st.failed()) {
        return st;
    }

    st = sendFrame(scl::kCmdEnableAngle, resp);
    if (st.failed()) {
        return st;
    }
    delay(scl::modeSettleMs(mode_));

    uint32_t statusFrame = 0;
    for (uint8_t i = 0; i < kStatusReadsOnBegin; ++i) {
        st = sendFrame(scl::kCmdReadStatus, statusFrame);
        if (st.failed()) {
            return st;
        }
    }
    if (statusFrame == kFrameStuckLow || statusFrame == kFrameStuckHigh) {
        ++frameErrors_;
        return Status(Err::Io);
    }
    if (!scl::frameCrcOk(statusFrame)) {
        ++crcErrors_;
        return Status(Err::Crc);
    }
    lastRs_ = scl::rsOf(statusFrame);
    lastStatus_ = scl::frameData(statusFrame);
    if (lastRs_ == scl::Rs::Reserved) {
        ++frameErrors_;
        captureErrorFlags();
        return Status(Err::HwFault);
    }
    if (lastRs_ == scl::Rs::Startup) {
        return Status(Err::Busy);
    }
    if (lastRs_ != scl::Rs::Ok) {
        captureErrorFlags();
        return Status(Err::HwFault);
    }

    st = readRegister(scl::kCmdReadWhoAmI, whoAmi_);
    if (st.failed()) {
        return st;
    }
    if (whoAmi_ != scl::kWhoAmIValue) {
        return Status(Err::HwFault);
    }

    ready_ = true;
    return kOk;
}

Status Scl3300::reinit() {
    return begin();
}

Status Scl3300::read(Tilt& out) {
    out.xDeci = 0;
    out.yDeci = 0;
    out.zDeci = 0;
    out.tempDeciC = 0;
    out.status = 0;
    out.valid = false;

    if (!ready_) {
        out.status = kStsSclNotResponding;
        return Status(Err::NotInit);
    }

    const uint32_t commands[kBurstFrames] = {
        scl::kCmdReadAngX, scl::kCmdReadAngY, scl::kCmdReadAngZ,
        scl::kCmdReadTemp, scl::kCmdReadStatus, scl::kCmdReadStatus,
    };
    uint16_t payload[kBurstFrames] = {0, 0, 0, 0, 0, 0};
    bool frameOk[kBurstFrames] = {false, false, false, false, false, false};

    bool crcBad = false;
    bool linkBad = false;
    bool startupSeen = false;
    bool rsErrorSeen = false;
    Status firstFail = kOk;

    for (uint8_t i = 0; i < kBurstFrames; ++i) {
        uint32_t resp = 0;
        const Status st = exchange(commands[i], resp);
        if (st.failed()) {
            if (firstFail.ok()) {
                firstFail = st;
            }
            if (st.err == Err::Crc) {
                crcBad = true;
            } else if (st.err == Err::Io || st.err == Err::HwFault) {
                linkBad = true;
            } else {
                ++reads_;
                out.status = kStsSclNotResponding;
                return st;
            }
            continue;
        }
        frameOk[i] = true;
        payload[i] = scl::frameData(resp);
        if (lastRs_ == scl::Rs::Startup) {
            startupSeen = true;
        } else if (lastRs_ == scl::Rs::Error) {
            rsErrorSeen = true;
        }
    }
    ++reads_;

    if (frameOk[kIdxStatus]) {
        lastStatus_ = payload[kIdxStatus];
    }
    const bool statusKnown = frameOk[kIdxStatus];
    const bool saturated = statusKnown && ((lastStatus_ & scl::kStatusSat) != 0);
    const bool hardFault = statusKnown && ((lastStatus_ & kStatusHardMask) != 0);

    if (frameOk[kIdxAngX]) {
        out.xDeci = scl::angleDeciDegrees(payload[kIdxAngX]);
    }
    if (frameOk[kIdxAngY]) {
        out.yDeci = scl::angleDeciDegrees(payload[kIdxAngY]);
    }
    if (frameOk[kIdxAngZ]) {
        out.zDeci = scl::angleDeciDegrees(payload[kIdxAngZ]);
    }
    if (frameOk[kIdxTemp]) {
        out.tempDeciC = scl::temperatureDeciC(payload[kIdxTemp]);
    }

    bool allFramesOk = true;
    for (uint8_t i = 0; i < kBurstFrames; ++i) {
        if (!frameOk[i]) {
            allFramesOk = false;
        }
    }

    uint16_t flags = 0;
    if (crcBad) {
        flags = static_cast<uint16_t>(flags | kStsSclCrcError);
    }
    if (linkBad || !statusKnown) {
        flags = static_cast<uint16_t>(flags | kStsSclNotResponding);
    }
    if (startupSeen || (rsErrorSeen && !hardFault && !saturated)) {
        flags = static_cast<uint16_t>(flags | kStsSclStartup);
    }
    if (saturated) {
        flags = static_cast<uint16_t>(flags | kStsSaturated);
    }
    if (hardFault || selfTestFailed_) {
        flags = static_cast<uint16_t>(flags | kStsSclSelfTestFail);
    }

    const bool valid = allFramesOk && !crcBad && !linkBad && !startupSeen && !rsErrorSeen &&
                       !saturated && !hardFault && (lastRs_ == scl::Rs::Ok);
    if (valid) {
        flags = static_cast<uint16_t>(flags | kStsDataValid);
    }
    out.status = flags;
    out.valid = valid;

    if (firstFail.failed()) {
        return firstFail;
    }
    if (!allFramesOk) {
        return Status(Err::Io);
    }
    if (hardFault) {
        return Status(Err::HwFault);
    }
    if (saturated) {
        return Status(Err::Range);
    }
    if (startupSeen || rsErrorSeen) {
        return Status(Err::Busy);
    }
    if (!valid) {
        return Status(Err::Io);
    }
    return kOk;
}

Status Scl3300::selfTest() {
    if (!ready_) {
        return Status(Err::NotInit);
    }

    uint16_t cleared = 0;
    Status st = readRegister(scl::kCmdReadStatus, cleared);
    if (st.failed()) {
        return st;
    }
    uint16_t summary = 0;
    st = readRegister(scl::kCmdReadStatus, summary);
    if (st.failed()) {
        return st;
    }
    uint16_t sto = 0;
    st = readRegister(scl::kCmdReadSto, sto);
    if (st.failed()) {
        return st;
    }
    uint16_t flag1 = 0;
    st = readRegister(scl::kCmdReadErrFlag1, flag1);
    if (st.failed()) {
        return st;
    }
    uint16_t flag2 = 0;
    st = readRegister(scl::kCmdReadErrFlag2, flag2);
    if (st.failed()) {
        return st;
    }

    lastStatus_ = summary;
    lastSto_ = sto;
    lastErrFlag1_ = flag1;
    lastErrFlag2_ = flag2;
    selfTestFailed_ = ((summary & scl::kStatusFault) != 0) || (flag1 != 0) || (flag2 != 0);
    if (selfTestFailed_) {
        return Status(Err::HwFault);
    }
    return kOk;
}

uint16_t Scl3300::whoAmI() const {
    return whoAmi_;
}

const char* Scl3300::name() const {
    return "SCL3300";
}

uint32_t Scl3300::reads() const {
    return reads_;
}

uint32_t Scl3300::crcErrors() const {
    return crcErrors_;
}

uint32_t Scl3300::frameErrors() const {
    return frameErrors_;
}

void Scl3300::recordTrace(uint32_t command, uint32_t response) {
    trace_[traceHead_].command = command;
    trace_[traceHead_].response = response;
    traceHead_ = static_cast<uint8_t>((traceHead_ + 1u) % kTraceDepth);
    if (traceFill_ < kTraceDepth) {
        ++traceFill_;
    }
}

uint8_t Scl3300::traceCount() const {
    return traceFill_;
}

bool Scl3300::traceAt(uint8_t index, FrameTrace& out) const {
    if (index >= traceFill_) {
        return false;
    }
    const uint8_t start = static_cast<uint8_t>((traceHead_ + kTraceDepth - traceFill_) % kTraceDepth);
    out = trace_[(start + index) % kTraceDepth];
    return true;
}

void Scl3300::captureErrorFlags() {
    uint16_t value = 0;
    if (readRegister(scl::kCmdReadErrFlag1, value).ok()) {
        lastErrFlag1_ = value;
        flagsRead_ = true;
    }
    if (readRegister(scl::kCmdReadErrFlag2, value).ok()) {
        lastErrFlag2_ = value;
        flagsRead_ = true;
    }
}

void Scl3300::diagnostics(InclinometerDiag& out) const {
    out.status = lastStatus_;
    out.errFlag1 = lastErrFlag1_;
    out.errFlag2 = lastErrFlag2_;
    out.sto = lastSto_;
    out.returnStatus = static_cast<uint8_t>(lastRs_);
    out.ready = ready_;
    out.flagsRead = flagsRead_;
}
