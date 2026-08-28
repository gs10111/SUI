// Display SPI do CN4. Sem MISO: nenhuma leitura de volta, veredito e visual.
#pragma once

#include <stdint.h>

#include "status.h"

class IDisplay {
public:
    virtual ~IDisplay() = default;
    virtual Status begin() = 0;
    virtual Status hardReset() = 0;
    virtual Status showPattern(uint8_t index) = 0;
    virtual uint8_t patternCount() const = 0;
    virtual const char* patternDescription(uint8_t index) const = 0;
    virtual Status writeText(const char* text) = 0;
    virtual Status setContrast(uint8_t value) = 0;
    virtual Status off() = 0;
    virtual const char* driverName() const = 0;
};
