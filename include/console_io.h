// Porta de entrada/saida do console. Nenhum driver depende de Serial.
#pragma once

#include <stdint.h>

class IConsoleIO {
public:
    virtual ~IConsoleIO() = default;
    virtual void write(const char* text) = 0;
    virtual void writeLine(const char* text) = 0;
    virtual void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) = 0;
    virtual bool readByte(uint8_t& out) = 0;
    virtual uint32_t nowMs() const = 0;
    virtual void idle() = 0;
};
