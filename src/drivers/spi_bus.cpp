// SpiBus: VSPI para o display (CN4) e HSPI remapeado para o DAC8562 (folha 2/2). Sem MISO em ambos.
#include "drivers/spi_bus.h"

SpiBus::SpiBus(SPIClass& spi, board::Pin sclk, board::Pin miso, board::Pin mosi, const char* label)
    : spi_(spi), sclk_(sclk), miso_(miso), mosi_(mosi), label_(label), ready_(false) {}

Status SpiBus::begin() {
    if (ready_) {
        return kOk;
    }
    spi_.begin(sclk_, miso_, mosi_, board::kNoPin);
    ready_ = true;
    return kOk;
}

void SpiBus::beginTransaction(const SPISettings& settings) {
    if (!ready_) {
        return;
    }
    spi_.beginTransaction(settings);
}

void SpiBus::transferOut(const uint8_t* data, size_t len) {
    if (!ready_ || data == nullptr) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        spi_.transfer(data[i]);
    }
}

void SpiBus::endTransaction() {
    if (!ready_) {
        return;
    }
    spi_.endTransaction();
}
