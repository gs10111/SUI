// PUSI-DI261930: dono do SPIClass do inclinometro. Barramento de 4 fios com MISO real, modo 0.
// Murata SCL3300 Rev.4 (doc 4921): quadro de 32 bits MSB first, fSCK max 8 MHz, 2 MHz recomendado.
#pragma once

#include <SPI.h>
#include <stdint.h>

#include "board_pins.h"
#include "status.h"

class SpiBus {
public:
    SpiBus(SPIClass& spi, board::Pin sclk, board::Pin miso, board::Pin mosi, const char* label);

    Status begin();
    bool ready() const { return ready_; }
    const char* label() const { return label_; }

    uint32_t transfer32(uint32_t out, const SPISettings& settings);

    board::Pin sclkPin() const { return sclk_; }
    board::Pin misoPin() const { return miso_; }
    board::Pin mosiPin() const { return mosi_; }

private:
    SPIClass& spi_;
    board::Pin sclk_;
    board::Pin miso_;
    board::Pin mosi_;
    const char* label_;
    bool ready_;
};
