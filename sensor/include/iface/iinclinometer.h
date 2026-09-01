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
    uint8_t mode;
    // Datasheet 6.2: leituras CONSECUTIVAS de STO fora da faixa, e se a rajada ja configurou
    // falha. Uma amostra isolada fora nao e defeito; a rajada e.
    uint8_t stoRun;
    bool stoFaulted;
    bool benchBypass;
    bool ready;
    bool flagsRead;
};

struct FrameTrace {
    uint32_t command;
    uint32_t response;
};

class IInclinometer {
public:
    virtual ~IInclinometer() = default;
    virtual Status begin() = 0;
    virtual Status read(Tilt& out) = 0;
    virtual Status selfTest() = 0;
    virtual uint16_t whoAmI() const = 0;
    virtual Status probeWhoAmI(uint16_t& out) = 0;
    virtual Status exchangeRaw(uint32_t command, uint32_t& response) = 0;
    virtual const char* name() const = 0;
    virtual uint32_t reads() const = 0;
    virtual uint32_t crcErrors() const = 0;
    virtual uint32_t frameErrors() const = 0;
    virtual void diagnostics(InclinometerDiag& out) const = 0;

    // BYPASS DE BANCADA. Padrao: nao faz nada. So o driver do SCL3300 implementa, porque so ele
    // tem bits de erro externos que fazem sentido tolerar; para qualquer outra implementacao a
    // porta continua estrita, que e o comportamento seguro.
    virtual void setBenchBypass(bool on) { (void)on; }
    virtual uint8_t traceCount() const = 0;
    virtual bool traceAt(uint8_t index, FrameTrace& out) const = 0;
};
