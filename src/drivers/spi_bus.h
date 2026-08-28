// Dono de uma instancia SPIClass (folhas 1/2 e 2/2). Nenhum periferico chama SPI.begin() sozinho.
#pragma once

#include <SPI.h>
#include <stddef.h>
#include <stdint.h>

#include "board_pins.h"
#include "status.h"

class SpiBus {
public:
    SpiBus(SPIClass& spi, board::Pin sclk, board::Pin miso, board::Pin mosi, const char* label);

    Status begin();
    bool ready() const { return ready_; }
    const char* label() const { return label_; }

    void beginTransaction(const SPISettings& settings);
    void transferOut(const uint8_t* data, size_t len);
    void endTransaction();

private:
    SPIClass& spi_;
    board::Pin sclk_;
    board::Pin miso_;
    board::Pin mosi_;
    const char* label_;
    bool ready_;
};
