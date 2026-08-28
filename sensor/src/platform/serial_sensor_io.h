// Console de diagnostico da PUSI-DI261930 em UART0. Unico lugar do projeto que conhece Serial.
#pragma once

#include <stdint.h>

#include "core/sensor_ctx.h"

class SerialSensorIO : public ISensorIO {
public:
    SerialSensorIO();

    void begin();
    bool ready() const { return ready_; }

    void write(const char* text) override;
    void writeLine(const char* text) override;
    void printf(const char* fmt, ...) override __attribute__((format(printf, 2, 3)));
    bool readByte(uint8_t& out) override;
    uint32_t nowMs() const override;

private:
    bool ready_;
};
