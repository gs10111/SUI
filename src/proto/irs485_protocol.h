// Camada de aplicacao do link com a PUSI-DI261930. Protocolo definitivo em aberto: trocavel.
#pragma once

#include <stdint.h>

#include "iface/iserial_transport.h"
#include "status.h"

struct Angle {
    float x;
    float y;
    bool valid;
};

class IRs485Protocol {
public:
    virtual ~IRs485Protocol() = default;
    virtual const char* name() const = 0;
    virtual Status begin(ISerialTransport& transport) = 0;
    virtual Status request() = 0;
    virtual bool poll(Angle& out) = 0;
    virtual void serviceEcho() = 0;
    virtual bool lastAngle(Angle& out) const = 0;
    virtual uint32_t framesOk() const = 0;
    virtual uint32_t framesBad() const = 0;
    virtual void resetCounters() = 0;
};
