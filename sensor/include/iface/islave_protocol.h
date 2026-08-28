// Protocolo escravo do enlace com a supervisora. Trocavel: quadro do jig ou Modbus RTU.
#pragma once

#include <stdint.h>

#include "status.h"
#include "tilt.h"

class ISlaveProtocol {
public:
    virtual ~ISlaveProtocol() = default;
    virtual const char* name() const = 0;
    virtual void reset() = 0;
    virtual uint16_t handle(const uint8_t* request, uint16_t len, const uint16_t* registers,
                            uint16_t registerCount, uint8_t* response, uint16_t cap) = 0;
    virtual uint32_t requests() const = 0;
    virtual uint32_t responses() const = 0;
    virtual uint32_t badFrames() const = 0;
};
