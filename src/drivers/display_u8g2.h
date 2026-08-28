// Display do CN4 (folha 1/2) via U8g2: SSD1322 NHD 256x64, SPI de 4 fios, mesmo controlador do CDM4L-DI221651.
// U8g2 chama SPI.begin() sem argumentos e o core prende o MISO default do VSPI (IO19 = WDI): ver rearmPin().
#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include <U8g2lib.h>
#pragma GCC diagnostic pop

#include <stdint.h>

#include "board_pins.h"
#include "iface/idisplay.h"
#include "status.h"

class U8g2Display : public IDisplay {
public:
    static constexpr uint8_t kPatternCount = 6;
    static constexpr uint8_t kContrastMin = 0;
    static constexpr uint8_t kContrastMax = 255;

    U8g2Display();

    Status begin() override;
    Status hardReset() override;
    Status showPattern(uint8_t index) override;
    uint8_t patternCount() const override { return kPatternCount; }
    const char* patternDescription(uint8_t index) const override;
    Status writeText(const char* text) override;
    Status setContrast(uint8_t value) override;
    Status off() override;
    const char* driverName() const override { return "u8g2-ssd1322-256x64"; }

    bool ready() const { return ready_; }
    uint16_t width() const;
    uint16_t height() const;

private:
    void fill(bool on);
    void checkerboard();

    U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2_;
    bool ready_;
    uint8_t contrast_;
};
