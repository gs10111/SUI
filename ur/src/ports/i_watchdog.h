// src/ports/i_watchdog.h
// Watchdog externo STWD100YNYWY3F (ST DocID14134 Rev 11), WDI em IO19, RST# -> EN por J15.
// Alvo: ExtWatchdog (src/drivers/ext_wdt.cpp) - pulso de 5 us gerado por esp_timer
//       periodico, NUNCA pelo laco; o esp_timer so pulsa se houver heartbeat recente.
// Fake: FakeWatchdog (test/native) - conta chutes e heartbeats, expoe "teria resetado?".
// REQ:  decisao 7 item 12 (laco travado tem de virar reset), decisao 12 item 7
//       (U8g2/SPI.begin() sequestra IO19: rearmPin obrigatorio depois do display),
//       MAN-7-L297..300 (falha e falta de energia).
#pragma once

#include <stdint.h>

#include "status.h"

class IWatchdog {
public:
    virtual ~IWatchdog() = default;
    IWatchdog(const IWatchdog&) = delete;
    IWatchdog& operator=(const IWatchdog&) = delete;

    // Assume o pino WDI e arma a geracao periodica de pulso fora do laco.
    virtual Status begin() = 0;

    // Prova de vida do laco principal. TEM de ser chamada uma vez por ciclo de
    // controle. O gerador de pulso so continua chutando enquanto houver heartbeat
    // dentro de heartbeatTimeoutMs(); sem isso um laco travado nunca reseta a placa.
    virtual void heartbeat() = 0;

    // Pulso imediato, fora da cadencia. So para trechos longos e conhecidos do
    // setup (ex.: settle de 100 ms do SCL3300). Nunca substitui heartbeat().
    virtual void kickNow() = 0;

    // Retoma a posse eletrica do pino WDI. Obrigatorio depois de qualquer
    // inicializacao que reconfigure o barramento SPI (o display prende IO19).
    virtual Status rearmPin() = 0;

    virtual bool kicking() const = 0;
    virtual uint32_t kickPeriodMs() const = 0;       // 250 ms
    virtual uint32_t heartbeatTimeoutMs() const = 0; // 750 ms (3 chutes de margem)
    virtual uint32_t minTimeoutMs() const = 0;       // 1120 ms (STWD100)
    virtual uint32_t typTimeoutMs() const = 0;       // 1600 ms
    virtual uint32_t kickCount() const = 0;
    virtual uint32_t heartbeatCount() const = 0;

protected:
    IWatchdog() = default;
};
