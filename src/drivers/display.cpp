// Display do CN4 (folha 1/2): reset por IO27 e bytes crus por VSPI (IO23/IO18) com D/C em IO4 e CS em IO5.
// Datasheet aplicavel: ESP32-WROOM-32D; controlador do display em aberto, veredito e visual.
#include "drivers/display.h"

#include <Arduino.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

#ifndef DISPLAY_DRIVER
#define DISPLAY_DRIVER RAW
#endif

#ifndef FW_VERSION
#define FW_VERSION "0.0.0"
#endif

#ifndef BOARD_REV
#define BOARD_REV "?"
#endif

#define DISPLAY_DRIVER_TEXT_INNER(x) #x
#define DISPLAY_DRIVER_TEXT(x) DISPLAY_DRIVER_TEXT_INNER(x)

namespace {

constexpr size_t kChunkBytes = 64;
constexpr size_t kFrameBytes = 1024;
constexpr size_t kMaxTextBytes = 128;
constexpr uint32_t kResetLowMs = 10;
constexpr uint32_t kResetSettleMs = 120;
constexpr uint8_t kCmdSetContrast = 0x81;
constexpr uint8_t kByteAllOn = 0xFF;
constexpr uint8_t kByteAllOff = 0x00;
constexpr uint8_t kByteCheckerA = 0xAA;
constexpr uint8_t kByteCheckerB = 0x55;

constexpr uint8_t kPinCs = static_cast<uint8_t>(board::kDispCs);
constexpr uint8_t kPinDc = static_cast<uint8_t>(board::kDispDc);
constexpr uint8_t kPinReset = static_cast<uint8_t>(board::kDispReset);

const SPISettings kDisplaySettings(board::kDisplaySpiHz, MSBFIRST, SPI_MODE0);

const char* const kPatternText[kDisplayPatternCount] = {
    "tudo aceso",
    "tudo apagado",
    "tabuleiro de xadrez",
    "texto com versao e serie",
    "contraste minimo",
    "contraste maximo",
};

}  // namespace

const char* displayPatternDescription(uint8_t index) {
    if (index >= kDisplayPatternCount) {
        return "padrao inexistente";
    }
    return kPatternText[index];
}

RawSpiDisplay::RawSpiDisplay(SpiBus& bus) : bus_(bus), ready_(false) {}

Status RawSpiDisplay::begin() {
    pinMode(kPinCs, OUTPUT);
    digitalWrite(kPinCs, HIGH);
    pinMode(kPinDc, OUTPUT);
    digitalWrite(kPinDc, HIGH);
    pinMode(kPinReset, OUTPUT);
    digitalWrite(kPinReset, HIGH);

    const Status st = bus_.begin();
    if (st.failed()) {
        return st;
    }
    if (!bus_.ready()) {
        return Err::Io;
    }
    ready_ = true;
    return hardReset();
}

Status RawSpiDisplay::hardReset() {
    if (!ready_) {
        return Err::NotInit;
    }
    digitalWrite(kPinCs, HIGH);
    digitalWrite(kPinReset, LOW);
    delay(kResetLowMs);
    digitalWrite(kPinReset, HIGH);
    delay(kResetSettleMs);
    return kOk;
}

void RawSpiDisplay::openFrame(uint8_t dcLevel) {
    bus_.beginTransaction(kDisplaySettings);
    digitalWrite(kPinDc, dcLevel);
    digitalWrite(kPinCs, LOW);
}

void RawSpiDisplay::closeFrame() {
    digitalWrite(kPinCs, HIGH);
    bus_.endTransaction();
}

Status RawSpiDisplay::sendCommand(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) {
        return Err::Param;
    }
    openFrame(LOW);
    bus_.transferOut(data, len);
    closeFrame();
    return kOk;
}

Status RawSpiDisplay::sendData(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) {
        return Err::Param;
    }
    openFrame(HIGH);
    bus_.transferOut(data, len);
    closeFrame();
    return kOk;
}

Status RawSpiDisplay::sendRepeated(const uint8_t* pattern, size_t patternLen, size_t total) {
    if (pattern == nullptr || patternLen == 0 || total == 0) {
        return Err::Param;
    }
    openFrame(HIGH);
    size_t sent = 0;
    while (sent < total) {
        size_t chunk = total - sent;
        if (chunk > patternLen) {
            chunk = patternLen;
        }
        bus_.transferOut(pattern, chunk);
        sent += chunk;
    }
    closeFrame();
    return kOk;
}

Status RawSpiDisplay::showFill(uint8_t value) {
    uint8_t block[kChunkBytes];
    memset(block, value, sizeof(block));
    return sendRepeated(block, sizeof(block), kFrameBytes);
}

Status RawSpiDisplay::showCheckerboard() {
    uint8_t block[kChunkBytes];
    for (size_t i = 0; i < sizeof(block); ++i) {
        block[i] = ((i & 1u) == 0u) ? kByteCheckerA : kByteCheckerB;
    }
    return sendRepeated(block, sizeof(block), kFrameBytes);
}

Status RawSpiDisplay::showIdentity() {
    char text[kMaxTextBytes];
    (void)snprintf(text, sizeof(text), "DE-PURI-DI261924 REV %s FW %s", BOARD_REV, FW_VERSION);
    return writeText(text);
}

Status RawSpiDisplay::showPattern(uint8_t index) {
    if (!ready_) {
        return Err::NotInit;
    }
    if (index >= kDisplayPatternCount) {
        return Err::Range;
    }
    switch (index) {
        case 0:
            return showFill(kByteAllOn);
        case 1:
            return showFill(kByteAllOff);
        case 2:
            return showCheckerboard();
        case 3:
            return showIdentity();
        case 4: {
            const Status st = setContrast(kDisplayContrastMin);
            if (st.failed()) {
                return st;
            }
            return showFill(kByteAllOn);
        }
        case 5: {
            const Status st = setContrast(kDisplayContrastMax);
            if (st.failed()) {
                return st;
            }
            return showFill(kByteAllOn);
        }
        default:
            break;
    }
    return Err::Range;
}

uint8_t RawSpiDisplay::patternCount() const {
    return kDisplayPatternCount;
}

const char* RawSpiDisplay::patternDescription(uint8_t index) const {
    return displayPatternDescription(index);
}

Status RawSpiDisplay::writeText(const char* text) {
    if (!ready_) {
        return Err::NotInit;
    }
    if (text == nullptr) {
        return Err::Param;
    }
    size_t len = strlen(text);
    if (len == 0) {
        return Err::Param;
    }
    if (len > kMaxTextBytes) {
        len = kMaxTextBytes;
    }
    return sendData(reinterpret_cast<const uint8_t*>(text), len);
}

Status RawSpiDisplay::setContrast(uint8_t value) {
    if (!ready_) {
        return Err::NotInit;
    }
    const uint8_t frame[2] = {kCmdSetContrast, value};
    return sendCommand(frame, sizeof(frame));
}

Status RawSpiDisplay::off() {
    if (!ready_) {
        return Err::NotInit;
    }
    const Status st = showFill(kByteAllOff);
    digitalWrite(kPinCs, HIGH);
    return st;
}

const char* RawSpiDisplay::driverName() const {
    return DISPLAY_DRIVER_TEXT(DISPLAY_DRIVER);
}
