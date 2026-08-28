// Contexto da sensora PUSI-DI261930 montado em main.cpp: agregado de interfaces estreitas.
// A saida do console e uma porta propria para que nenhum modulo dependa de Serial.
#pragma once

#include <stdint.h>

#include "iface/iinclinometer.h"
#include "iface/islave_protocol.h"
#include "iface/iserial_transport.h"
#include "iface/iwatchdog.h"

class ISensorIO {
public:
    virtual ~ISensorIO() = default;
    virtual void write(const char* text) = 0;
    virtual void writeLine(const char* text) = 0;
    virtual void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) = 0;
    virtual bool readByte(uint8_t& out) = 0;
    virtual uint32_t nowMs() const = 0;
};

struct SensorCtx {
    ISensorIO& io;
    IInclinometer& tilt;
    ISerialTransport& link;
    IWatchdog& wdt;
    ISlaveProtocol** protocol;
    ISlaveProtocol* jigProtocol;
    ISlaveProtocol* modbusProtocol;
    const uint16_t* registers;
    uint16_t registerCount;
    const char* fwVersion;
    const char* boardRev;
};
