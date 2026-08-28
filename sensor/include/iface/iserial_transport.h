// Transporte serial half-duplex da sensora PUSI-DI261930 (SN65HVD75D).
#pragma once

#include <stdint.h>

#include "status.h"

struct SerialStats {
    uint32_t framesOk;
    uint32_t timeouts;
    uint32_t crcErrors;
    uint32_t framingErrors;
    uint32_t bytesRx;
    uint32_t bytesTx;
};

class ISerialTransport {
public:
    virtual ~ISerialTransport() = default;
    virtual Status begin(uint32_t baud, uint8_t dataBits, char parity, uint8_t stopBits) = 0;
    virtual Status end() = 0;
    virtual Status write(const uint8_t* data, uint16_t len) = 0;
    virtual uint16_t read(uint8_t* buf, uint16_t cap, uint32_t timeoutMs) = 0;
    virtual uint16_t available() const = 0;
    virtual Status flushRx() = 0;
    virtual Status driveStatic(bool enable, bool level) = 0;
    virtual uint32_t lastTurnaroundUs() const = 0;
    virtual const SerialStats& stats() const = 0;
    virtual void resetStats() = 0;
    virtual void noteCrcError() = 0;
    virtual void noteFrameOk() = 0;
    virtual void noteTimeout() = 0;
    virtual uint32_t baud() const = 0;
    virtual uint32_t charTimeUs() const = 0;
};
