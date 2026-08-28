// Watchdog externo STWD100YNYWY3F com WDI em IO19 (folha 1/2), ST DocID14134 Rev 11.
// tWD 1,12 s min / 1,6 s tip / 2,24 s max, tPW 210 ms; kick por esp_timer periodico, nunca pelo loop().
#pragma once

#include <stdint.h>

#include <esp_timer.h>

#include "board_pins.h"
#include "iface/iwatchdog.h"
#include "status.h"

constexpr uint32_t kWdtMaxTimeoutMs = 2240;
constexpr uint32_t kWdtResetPulseMs = 210;
constexpr uint32_t kWdiMinPulseUs = 1;
constexpr uint32_t kWdiGlitchRejectNs = 100;

class ExtWatchdog : public IWatchdog {
public:
    ExtWatchdog();

    Status begin() override;
    void kickNow() override;
    Status setKicking(bool enable) override;
    bool kicking() const override;
    uint32_t kickPeriodMs() const override;
    uint32_t kickCount() const override;
    uint32_t minTimeoutMs() const override;
    uint32_t typTimeoutMs() const override;

    uint32_t maxTimeoutMs() const { return kWdtMaxTimeoutMs; }
    uint32_t resetPulseMs() const { return kWdtResetPulseMs; }
    uint32_t pulseUs() const { return board::kWdiPulseUs; }
    board::Pin pin() const { return board::kWdi; }
    bool ready() const { return ready_; }

private:
    static void onTimer(void* arg);
    Status startTimer();

    esp_timer_handle_t timer_;
    volatile uint32_t kickCount_;
    bool ready_;
    bool kicking_;
};
