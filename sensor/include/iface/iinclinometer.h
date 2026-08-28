// Inclinometro da placa sensora. Implementacao real: Murata SCL3300 por SPI.
#pragma once

#include <stdint.h>

#include "status.h"
#include "tilt.h"

struct InclinometerDiag {
    uint16_t status;
    uint16_t errFlag1;
    uint16_t errFlag2;
    uint16_t sto;
    uint8_t returnStatus;
    bool ready;
    bool flagsRead;
};

class IInclinometer {
public:
    virtual ~IInclinometer() = default;
    virtual Status begin() = 0;
    virtual Status read(Tilt& out) = 0;
    virtual Status selfTest() = 0;
    virtual uint16_t whoAmI() const = 0;
    virtual const char* name() const = 0;
    virtual uint32_t reads() const = 0;
    virtual uint32_t crcErrors() const = 0;
    virtual uint32_t frameErrors() const = 0;
    virtual void diagnostics(InclinometerDiag& out) const = 0;
};
