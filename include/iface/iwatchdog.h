// Watchdog externo STWD100YNYWY3F (ST DocID14134 Rev 11), folha 1/2.
#pragma once

#include <stdint.h>

#include "status.h"

class IWatchdog {
public:
    virtual ~IWatchdog() = default;
    virtual Status begin() = 0;
    virtual void kickNow() = 0;
    virtual Status setKicking(bool enable) = 0;
    virtual bool kicking() const = 0;
    virtual uint32_t kickPeriodMs() const = 0;
    virtual uint32_t kickCount() const = 0;
    virtual uint32_t minTimeoutMs() const = 0;
    virtual uint32_t typTimeoutMs() const = 0;
};
