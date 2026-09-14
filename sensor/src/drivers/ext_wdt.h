// Watchdog externo STWD100YNYWY3F da sensora PUSI-DI261930, WDI em board::kWdi (IO14).
// ST DocID14134 Rev 11: tWD 1,12 s min / 1,6 s tip / 2,24 s max, tPW 210 ms, largura minima de
// WDI 1 us, rejeicao de glitch 100 ns. EN com pull-down interno: o chip esta SEMPRE habilitado,
// nao existe desliga-lo por software.
//
// MECANISMO, corrigido em 2026-09-14. O chute NAO sai mais de esp_timer. O callback de esp_timer
// roda na ESP_TIMER_TASK, que executa DE FLASH e PARA durante qualquer apagamento de setor (cache
// desabilitada) - exatamente a janela em que o watchdog mais importa. A UR ja tinha migrado por
// esse motivo e o DECISIONS.md registra a migracao como "requisito de base, nao otimizacao"; a
// sensora ficou para tras ate aqui. Agora o chute sai de ISR de timer de HARDWARE (grupo 0,
// timer 1) a 1 kHz, alocada com ESP_INTR_FLAG_IRAM e escrita inteira em IRAM_ATTR: a cada
// kKickPeriodTicks a ISR levanta o WDI por GPIO.out_w1ts e no tique seguinte o baixa por
// GPIO.out_w1tc. Pulso de 1 ms - 10.000x acima da rejeicao de glitch e 210x abaixo do tPW. Sem
// digitalWrite, sem delayMicroseconds e sem tocar em nada que more em flash dentro da ISR.
//
// TOKEN DE LIVENESS, e por que ele e inseparavel da migracao. Uma ISR em IRAM que pulsasse
// incondicionalmente alimentaria o STWD100 para sempre: o firmware poderia travar e o cachorro
// nunca morderia. A ISR so pulsa enquanto o portao de drivers/wdt_gate.h estiver aberto - com
// token, vale o prazo de 800 ms renovado por heartbeat(); sem token, vale a carencia de boot,
// que TEM fim. A regra e pura e tem teste de host em test/native/test_wdt.
//
// CARENCIA DE BOOT, e nao uma janela sem fim: antes do primeiro heartbeat() nao existe token, e
// o setup() bloqueia antes de o laco principal existir. Semear o token em begin() transformaria
// um boot lento em boot loop; uma carencia sem fim cobriria para sempre o modo de falha em que o
// laco nunca comeca. Por isso a ISR chuta incondicionalmente, mas so por kBootGraceTicks.
#pragma once

#include <stdint.h>

#include "board_pins.h"
#include "drivers/wdt_gate.h"
#include "iface/iwatchdog.h"
#include "status.h"

constexpr uint32_t kWdtMaxTimeoutMs = 2240;
constexpr uint32_t kWdtResetPulseMs = 210;
constexpr uint32_t kWdiMinPulseUs = 1;
constexpr uint32_t kWdiGlitchRejectNs = 100;

class ExtWatchdog : public IWatchdog {
public:
    // Timer de hardware: grupo 0, timer 1. Divisor 80 sobre o APB de 80 MHz da 1 us por tique de
    // timer; 1000 deles fecham o milissegundo da ISR.
    //
    // O INDICE 2 E O PAR (GRUPO 0, TIMER 1) TEM DE CASAR, e o mapeamento nao e obvio: o core
    // guarda timer_dev[4] = {{0,0}, {1,0}, {0,1}, {1,1}} em esp32-hal-timer.c:42, ou seja
    // {grupo, timer} - o indice 2 e grupo 0 / timer 1, e NAO grupo 1 / timer 0 como a divisao
    // por dois sugeriria. Conferido na fonte do core instalado, nao deduzido. Errar aqui
    // registra o callback num timer que nao esta correndo: a ISR nunca dispara, e o unico
    // motivo de isso nao virar placa morta em campo e a prova de vida dentro de begin().
    static constexpr uint8_t kTimerIndex = 2;
    static constexpr uint16_t kTimerDivider = 80;
    static constexpr uint32_t kTimerTicksPerIsr = 1000;
    // Teto do bloqueio na prova de vida da ISR. Em 1 kHz o primeiro tique vem em ~1 ms.
    static constexpr uint32_t kIsrProbeTimeoutUs = 5000;

    ExtWatchdog();

    Status begin() override;
    void kickNow() override;
    Status setKicking(bool enable) override;
    bool kicking() const override;
    uint32_t kickPeriodMs() const override;
    uint32_t kickCount() const override;
    uint32_t minTimeoutMs() const override;
    uint32_t typTimeoutMs() const override;

    void heartbeat() override;
    bool livenessArmed() const override;
    uint32_t heartbeatCount() const override;
    uint32_t heartbeatTimeoutMs() const override { return wdt::kLivenessDeadlineTicks; }

    uint32_t maxTimeoutMs() const { return kWdtMaxTimeoutMs; }
    uint32_t resetPulseMs() const { return kWdtResetPulseMs; }
    uint32_t pulseUs() const { return board::kWdiPulseUs; }
    board::Pin pin() const { return board::kWdi; }
    bool ready() const { return ready_; }
    uint32_t bootGraceMs() const { return wdt::kBootGraceTicks; }
    // Tiques desde begin(), para o console mostrar que a ISR esta viva.
    uint32_t isrTicks() const;

private:
    bool ready_;
    bool kicking_;
    uint32_t manualKicks_;
    uint32_t heartbeats_;
};
