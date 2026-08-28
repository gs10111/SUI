// Console de fabrica na UART0 do ESP32-WROOM-32D (folha 1/2, U1): unica porta de texto do firmware.
// Datasheet aplicavel: ESP32-WROOM-32D (UART0 em IO1/IO3, 115200 8N1).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board_pins.h"
#include "console_io.h"
#include "status.h"

constexpr size_t kConsoleFormatCap = 256;

class SerialConsoleIO : public IConsoleIO {
public:
    SerialConsoleIO();

    Status begin();
    bool ready() const { return ready_; }

    void write(const char* text) override;
    void writeLine(const char* text) override;
    void printf(const char* fmt, ...) override __attribute__((format(printf, 2, 3)));
    bool readByte(uint8_t& out) override;
    uint32_t nowMs() const override;
    void idle() override;

private:
    void ensure();
    void writeRaw(const char* text, size_t len);

    bool ready_;
};
