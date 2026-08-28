// UART0 a 115200 (PUSI-DI261930). Buffer fixo: nenhuma alocacao depois do setup().
#include "platform/serial_sensor_io.h"

#include <Arduino.h>

#include "board_pins.h"
#include <stdarg.h>
#include <stdio.h>

namespace {
constexpr uint16_t kPrintBuffer = 256;
}

SerialSensorIO::SerialSensorIO() : ready_(false) {}

void SerialSensorIO::begin() {
    if (ready_) {
        return;
    }
    Serial.begin(board::kConsoleBaud);
    ready_ = true;
}

void SerialSensorIO::write(const char* text) {
    if (!ready_ || text == nullptr) {
        return;
    }
    Serial.print(text);
}

void SerialSensorIO::writeLine(const char* text) {
    write(text);
    if (ready_) {
        Serial.print("\r\n");
    }
}

void SerialSensorIO::printf(const char* fmt, ...) {
    if (!ready_ || fmt == nullptr) {
        return;
    }
    char buf[kPrintBuffer];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.print(buf);
}

bool SerialSensorIO::readByte(uint8_t& out) {
    if (!ready_ || Serial.available() <= 0) {
        return false;
    }
    const int value = Serial.read();
    if (value < 0) {
        return false;
    }
    out = static_cast<uint8_t>(value);
    return true;
}

uint32_t SerialSensorIO::nowMs() const {
    return millis();
}
