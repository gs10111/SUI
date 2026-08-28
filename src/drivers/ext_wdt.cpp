// ExtWatchdog: pulso em WDI (IO19) para o STWD100YNYWY3F, folha 1/2 (ST DocID14134 Rev 11).
// EN tem pull-down interno: o chip fica sempre habilitado, nao ha como desligar por software.
#include "drivers/ext_wdt.h"

#include <Arduino.h>

static_assert(board::kWdiPulseUs >= kWdiMinPulseUs, "pulso em WDI abaixo do minimo do STWD100");
static_assert(board::kWdiPulseUs * 1000u > kWdiGlitchRejectNs, "pulso em WDI seria tratado como glitch");
static_assert(board::kWdtKickPeriodMs * 3u <= board::kWdtMinTimeoutMs, "margem de kick insuficiente para tWD minimo");

ExtWatchdog::ExtWatchdog() : timer_(nullptr), kickCount_(0), ready_(false), kicking_(false) {}

void ExtWatchdog::onTimer(void* arg) {
    if (arg == nullptr) {
        return;
    }
    static_cast<ExtWatchdog*>(arg)->kickNow();
}

Status ExtWatchdog::startTimer() {
    const uint64_t periodUs = static_cast<uint64_t>(board::kWdtKickPeriodMs) * 1000u;
    if (esp_timer_start_periodic(timer_, periodUs) != ESP_OK) {
        return Status(Err::HwFault);
    }
    return kOk;
}

Status ExtWatchdog::begin() {
    if (ready_) {
        return kOk;
    }
    const uint8_t pinNum = static_cast<uint8_t>(board::kWdi);
    digitalWrite(pinNum, LOW);
    pinMode(pinNum, OUTPUT);
    digitalWrite(pinNum, LOW);
    kickNow();

    esp_timer_create_args_t args = {};
    args.callback = &ExtWatchdog::onTimer;
    args.arg = this;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "wdi_kick";
    if (esp_timer_create(&args, &timer_) != ESP_OK) {
        timer_ = nullptr;
        return Status(Err::HwFault);
    }
    const Status st = startTimer();
    if (st.failed()) {
        esp_timer_delete(timer_);
        timer_ = nullptr;
        return st;
    }
    ready_ = true;
    kicking_ = true;
    return kOk;
}

void ExtWatchdog::kickNow() {
    const uint8_t pinNum = static_cast<uint8_t>(board::kWdi);
    digitalWrite(pinNum, HIGH);
    delayMicroseconds(board::kWdiPulseUs);
    digitalWrite(pinNum, LOW);
    kickCount_ = kickCount_ + 1u;
}

Status ExtWatchdog::setKicking(bool enable) {
    if (!ready_ || timer_ == nullptr) {
        return Status(Err::NotInit);
    }
    if (enable == kicking_) {
        return kOk;
    }
    if (enable) {
        kickNow();
        const Status st = startTimer();
        if (st.failed()) {
            return st;
        }
        kicking_ = true;
        return kOk;
    }
    if (esp_timer_stop(timer_) != ESP_OK) {
        return Status(Err::HwFault);
    }
    kicking_ = false;
    return kOk;
}

bool ExtWatchdog::kicking() const {
    return kicking_;
}

uint32_t ExtWatchdog::kickPeriodMs() const {
    return board::kWdtKickPeriodMs;
}

uint32_t ExtWatchdog::kickCount() const {
    return kickCount_;
}

uint32_t ExtWatchdog::minTimeoutMs() const {
    return board::kWdtMinTimeoutMs;
}

uint32_t ExtWatchdog::typTimeoutMs() const {
    return board::kWdtTypTimeoutMs;
}
