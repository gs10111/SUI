// UART0 do ESP32-WROOM-32D (folha 1/2, U1): escrita bloqueante curta, leitura nao bloqueante.
// Datasheet aplicavel: ESP32-WROOM-32D (UART0, 115200 8N1, sem controle de fluxo).
#include "platform/serial_console_io.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

const char kEol[] = "\r\n";

}  // namespace

SerialConsoleIO::SerialConsoleIO() : ready_(false) {}

Status SerialConsoleIO::begin() {
    if (ready_) {
        return kOk;
    }
    Serial.begin(board::kConsoleBaud);
    ready_ = true;
    return kOk;
}

void SerialConsoleIO::ensure() {
    if (!ready_) {
        (void)begin();
    }
}

void SerialConsoleIO::writeRaw(const char* text, size_t len) {
    if (text == nullptr || len == 0) {
        return;
    }
    (void)Serial.write(reinterpret_cast<const uint8_t*>(text), len);
}

void SerialConsoleIO::write(const char* text) {
    ensure();
    if (text == nullptr) {
        return;
    }
    writeRaw(text, strlen(text));
}

void SerialConsoleIO::writeLine(const char* text) {
    ensure();
    if (text != nullptr) {
        writeRaw(text, strlen(text));
    }
    writeRaw(kEol, sizeof(kEol) - 1);
}

void SerialConsoleIO::printf(const char* fmt, ...) {
    ensure();
    if (fmt == nullptr) {
        return;
    }
    char buffer[kConsoleFormatCap];
    va_list args;
    va_start(args, fmt);
    const int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (written <= 0) {
        return;
    }
    size_t len = static_cast<size_t>(written);
    if (len > sizeof(buffer) - 1) {
        len = sizeof(buffer) - 1;
    }
    writeRaw(buffer, len);
}

bool SerialConsoleIO::readByte(uint8_t& out) {
    ensure();
    if (Serial.available() <= 0) {
        return false;
    }
    const int value = Serial.read();
    if (value < 0) {
        return false;
    }
    out = static_cast<uint8_t>(value);
    return true;
}

uint32_t SerialConsoleIO::nowMs() const {
    return static_cast<uint32_t>(millis());
}

void SerialConsoleIO::idle() {
    yield();
}
