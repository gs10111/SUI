// Watchdog externo STWD100YNYWY3F da sensora (ST DocID14134 Rev 11).
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

    // TOKEN DE LIVENESS (2026-09-14). O laco principal chama heartbeat() a cada volta; a ISR so
    // pulsa o WDI enquanto a ultima batida tiver menos de wdt::kLivenessDeadlineTicks. Sem este
    // par, migrar o chute para IRAM produziria um defeito PIOR que o de hoje: uma ISR que pulsa
    // incondicionalmente alimenta o cachorro para sempre e um firmware travado nunca reseta.
    virtual void heartbeat() = 0;
    virtual bool livenessArmed() const = 0;
    virtual uint32_t heartbeatCount() const = 0;
    // Prazo publicado uma unica vez, para que o laco nao tenha uma segunda copia dele.
    virtual uint32_t heartbeatTimeoutMs() const = 0;
};
