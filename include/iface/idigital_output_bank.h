// Banco de saidas digitais (reles de limite RL2..RL5, folha 2/2).
#pragma once

#include <stdint.h>

#include "status.h"

class IDigitalOutputBank {
public:
    virtual ~IDigitalOutputBank() = default;
    virtual Status begin() = 0;
    virtual uint8_t count() const = 0;
    virtual Status set(uint8_t index, bool on) = 0;
    virtual Status get(uint8_t index, bool& on) const = 0;
    virtual Status allOff() = 0;
    virtual Status allOn() = 0;
};
