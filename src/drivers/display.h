// Display do CN4 (folha 1/2): VSPI so de escrita, sem MISO, controlador em aberto.
// Datasheet aplicavel: ESP32-WROOM-32D (GPIO/SPI); folha do controlador do display indefinida.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board_pins.h"
#include "drivers/spi_bus.h"
#include "iface/idisplay.h"
#include "status.h"

constexpr uint8_t kDisplayPatternCount = 6;
constexpr uint8_t kDisplayContrastMin = 0x00;
constexpr uint8_t kDisplayContrastMax = 0xFF;

const char* displayPatternDescription(uint8_t index);

class RawSpiDisplay : public IDisplay {
public:
    explicit RawSpiDisplay(SpiBus& bus);

    Status begin() override;
    Status hardReset() override;
    Status showPattern(uint8_t index) override;
    uint8_t patternCount() const override;
    const char* patternDescription(uint8_t index) const override;
    Status writeText(const char* text) override;
    Status setContrast(uint8_t value) override;
    Status off() override;
    const char* driverName() const override;

private:
    void openFrame(uint8_t dcLevel);
    void closeFrame();
    Status sendCommand(const uint8_t* data, size_t len);
    Status sendData(const uint8_t* data, size_t len);
    Status sendRepeated(const uint8_t* pattern, size_t patternLen, size_t total);
    Status showFill(uint8_t value);
    Status showCheckerboard();
    Status showIdentity();

    SpiBus& bus_;
    bool ready_;
};

class NullDisplay : public IDisplay {
public:
    NullDisplay() = default;

    Status begin() override { return kOk; }
    Status hardReset() override { return kOk; }

    Status showPattern(uint8_t index) override {
        (void)index;
        return kOk;
    }

    uint8_t patternCount() const override { return kDisplayPatternCount; }

    const char* patternDescription(uint8_t index) const override {
        return displayPatternDescription(index);
    }

    Status writeText(const char* text) override {
        (void)text;
        return kOk;
    }

    Status setContrast(uint8_t value) override {
        (void)value;
        return kOk;
    }

    Status off() override { return kOk; }
    const char* driverName() const override { return "null"; }
};
