// Saida analogica de um eixo: DAC8562 (TI SLAS719E) + XTR300 (folha 2/2).
#pragma once

#include <stdint.h>

#include "status.h"

enum class AoMode : uint8_t {
    Voltage = 0,
    Current = 1,
};

class IAnalogOutput {
public:
    virtual ~IAnalogOutput() = default;
    virtual Status begin() = 0;
    virtual Status setRaw(uint8_t axis, uint16_t code) = 0;
    virtual Status getRaw(uint8_t axis, uint16_t& code) const = 0;
    virtual Status setEngineering(uint8_t axis, float value) = 0;
    virtual Status setMode(AoMode mode) = 0;
    virtual AoMode mode() const = 0;
    virtual Status zeroAll() = 0;
    virtual Status setSpiHz(uint32_t hz) = 0;
    virtual uint32_t spiHz() const = 0;
    virtual uint16_t fullScaleCode() const = 0;
};
