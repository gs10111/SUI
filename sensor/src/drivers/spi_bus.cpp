// PUSI-DI261930: HSPI/VSPI com os quatro pinos reais do SCL3300 (board::kSclSclk/Miso/Mosi/Cs).
// MISO nunca pode ir como -1 para SPIClass::begin: o core ESP32 substitui pelo pino default do barramento.
#include "drivers/spi_bus.h"

namespace {

constexpr uint8_t kFrameBytes = 4;

}  // namespace

SpiBus::SpiBus(SPIClass& spi, board::Pin sclk, board::Pin miso, board::Pin mosi, const char* label)
    : spi_(spi), sclk_(sclk), miso_(miso), mosi_(mosi), label_(label), ready_(false) {}

Status SpiBus::begin() {
    if (ready_) {
        return kOk;
    }
    if (sclk_ == board::kNoPin || miso_ == board::kNoPin || mosi_ == board::kNoPin) {
        return Status(Err::Param);
    }
    spi_.begin(sclk_, miso_, mosi_, board::kNoPin);
    ready_ = true;
    return kOk;
}

uint32_t SpiBus::transfer32(uint32_t out, const SPISettings& settings) {
    if (!ready_) {
        return 0;
    }
    uint32_t rxWord = 0;
    spi_.beginTransaction(settings);
    for (uint8_t i = 0; i < kFrameBytes; ++i) {
        const uint8_t shift = static_cast<uint8_t>(24 - 8 * i);
        const uint8_t txByte = static_cast<uint8_t>((out >> shift) & 0xFFu);
        const uint8_t rxByte = spi_.transfer(txByte);
        rxWord = (rxWord << 8) | static_cast<uint32_t>(rxByte);
    }
    spi_.endTransaction();
    return rxWord;
}
