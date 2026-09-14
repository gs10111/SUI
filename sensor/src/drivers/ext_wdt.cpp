// ExtWatchdog: pulso no WDI (board::kWdi = IO14) do STWD100YNYWY3F da PUSI-DI261930.
// O cabecalho explica o mecanismo e o porque do token de liveness; aqui fica a ISR.
//
// A ISR e registrada por timer_isr_callback_add() e NAO por timerAttachInterrupt(): a funcao do
// core e void e DESCARTA o esp_err_t (esp32-hal-timer.c), entao com ela um begin() que falhou na
// instalacao devolveria kOk e a placa entraria em boot loop sem codigo de erro. Aqui o erro e
// checado e, depois disso, o tique e PROVADO por espera limitada antes de declarar ready_:
// instalar sem erro nao e o mesmo que tiquetar - divisor errado ou APB fora dos 80 MHz dariam um
// watchdog silenciosamente parado.
//
// Nada dentro da ISR toca em flash: sem digitalWrite, sem delayMicroseconds, sem .rodata.
#include "drivers/ext_wdt.h"

#include <Arduino.h>

#include <driver/timer.h>
#include <esp_intr_alloc.h>
#include <freertos/FreeRTOS.h>
#include <soc/gpio_struct.h>

#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR != 2
#error "timerBegin/timerAlarmWrite sao a API do core 2.x; revalidar a IRAM da ISR antes de trocar"
#endif

static_assert(board::kWdiPulseUs >= kWdiMinPulseUs, "pulso em WDI abaixo do minimo do STWD100");
static_assert(board::kWdiPulseUs * 1000u > kWdiGlitchRejectNs, "pulso em WDI seria tratado como glitch");
static_assert(board::kWdtKickPeriodMs * 3u <= board::kWdtMinTimeoutMs, "margem de kick insuficiente para tWD minimo");
static_assert(board::kWdtKickPeriodMs == wdt::kKickPeriodTicks,
              "a cadencia do board_pins e a do portao puro divergiram");
static_assert(board::kWdtMinTimeoutMs == wdt::kWdtMinTimeoutMs,
              "o tWD minimo do board_pins e o do portao puro divergiram");
static_assert(board::kWdi < 32, "o WDI tem de estar no banco baixo do GPIO para out_w1ts/out_w1tc");

namespace {

constexpr uint32_t kWdiMask = 1u << static_cast<uint32_t>(board::kWdi);

portMUX_TYPE g_wdiMux = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t* g_timer = nullptr;
bool g_armed = false;

struct IsrState {
    volatile uint32_t tickMs;
    volatile uint32_t lastBeatTick;
    volatile uint32_t countdown;
    volatile uint32_t kicks;
    volatile bool pulseHigh;
    volatile bool livenessArmed;
    volatile bool enabled;
};

IsrState g_isr = {};

void IRAM_ATTR wdiIsr() {
    portENTER_CRITICAL_ISR(&g_wdiMux);

    const uint32_t tick = g_isr.tickMs + 1u;
    g_isr.tickMs = tick;

    // O pulso levantado no tique anterior cai aqui: largura de 1 ms, muito acima da rejeicao de
    // glitch de 100 ns e muito abaixo do tPW de 210 ms.
    if (g_isr.pulseHigh) {
        GPIO.out_w1tc = kWdiMask;
        g_isr.pulseHigh = false;
    }

    const bool gateOpen = wdt::gateOpen(tick, g_isr.lastBeatTick, g_isr.livenessArmed);

    if (g_isr.enabled && gateOpen) {
        if (g_isr.countdown != 0u) {
            g_isr.countdown = g_isr.countdown - 1u;
        } else {
            GPIO.out_w1ts = kWdiMask;
            g_isr.pulseHigh = true;
            g_isr.countdown = wdt::kKickPeriodTicks - 1u;
            g_isr.kicks = g_isr.kicks + 1u;
        }
    }

    portEXIT_CRITICAL_ISR(&g_wdiMux);
}

bool IRAM_ATTR wdiIsrCb(void*) {
    wdiIsr();
    return false;
}

// Pulso sincrono, usado so antes de a ISR existir (o primeiro chute dentro de begin()).
void pulseWdiBlocking() {
    GPIO.out_w1ts = kWdiMask;
    delayMicroseconds(board::kWdiPulseUs);
    GPIO.out_w1tc = kWdiMask;
}

}  // namespace

ExtWatchdog::ExtWatchdog() : ready_(false), kicking_(false), manualKicks_(0), heartbeats_(0) {}

Status ExtWatchdog::begin() {
    if (ready_) {
        return kOk;
    }
    if (board::kWdi == board::kNoPin) {
        return Status(Err::Param);
    }
    if (g_armed) {
        return Status(Err::Busy);
    }

    const uint8_t pinNum = static_cast<uint8_t>(board::kWdi);
    GPIO.out_w1tc = kWdiMask;
    pinMode(pinNum, OUTPUT);
    GPIO.out_w1tc = kWdiMask;

    g_isr.tickMs = 0;
    g_isr.lastBeatTick = 0;
    g_isr.countdown = wdt::kKickPeriodTicks - 1u;
    g_isr.kicks = 0;
    g_isr.pulseHigh = false;
    g_isr.livenessArmed = false;
    g_isr.enabled = true;

    // Primeiro chute, antes de existir ISR. Conta em kickCount() porque foi pulso real no WDI.
    pulseWdiBlocking();
    manualKicks_ = 1u;

    g_timer = timerBegin(kTimerIndex, kTimerDivider, true);
    if (g_timer == nullptr) {
        return Status(Err::HwFault);
    }

    // ESP_ERR_NOT_FOUND quando nao ha vaga de interrupcao compativel com ESP_INTR_FLAG_IRAM;
    // ESP_ERR_INVALID_STATE se ja houver callback registrado.
    if (timer_isr_callback_add(TIMER_GROUP_0, TIMER_1, &wdiIsrCb, nullptr, ESP_INTR_FLAG_IRAM) !=
        ESP_OK) {
        timerEnd(g_timer);
        g_timer = nullptr;
        return Status(Err::HwFault);
    }

    timerAlarmWrite(g_timer, kTimerTicksPerIsr, true);
    timerAlarmEnable(g_timer);

    // PROVA DE VIDA DA ISR: instalar sem erro nao e tiquetar.
    const uint32_t probeStartUs = micros();
    while (g_isr.tickMs == 0u && (micros() - probeStartUs) < kIsrProbeTimeoutUs) {
    }
    if (g_isr.tickMs == 0u) {
        timerDetachInterrupt(g_timer);
        timerEnd(g_timer);
        g_timer = nullptr;
        return Status(Err::HwFault);
    }

    g_armed = true;
    ready_ = true;
    kicking_ = true;
    return kOk;
}

// Chute manual, fora da cadencia da ISR. Continua existindo para o comando de console e para o
// caminho de boot; nao renova o token - quem renova e heartbeat(), e confundir os dois
// devolveria o chute incondicional pela porta dos fundos.
void ExtWatchdog::kickNow() {
    pulseWdiBlocking();
    ++manualKicks_;
}

void ExtWatchdog::heartbeat() {
    portENTER_CRITICAL(&g_wdiMux);
    g_isr.lastBeatTick = g_isr.tickMs;
    g_isr.livenessArmed = true;
    portEXIT_CRITICAL(&g_wdiMux);
    ++heartbeats_;
}

bool ExtWatchdog::livenessArmed() const {
    return g_isr.livenessArmed;
}

uint32_t ExtWatchdog::heartbeatCount() const {
    return heartbeats_;
}

uint32_t ExtWatchdog::isrTicks() const {
    return g_isr.tickMs;
}

Status ExtWatchdog::setKicking(bool enable) {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    portENTER_CRITICAL(&g_wdiMux);
    g_isr.enabled = enable;
    portEXIT_CRITICAL(&g_wdiMux);
    kicking_ = enable;
    return kOk;
}

// kicking() denuncia o portao fechado, e nao so a chave: um watchdog cujo token venceu NAO esta
// chutando, por mais que ninguem tenha chamado setKicking(false).
bool ExtWatchdog::kicking() const {
    if (!kicking_) {
        return false;
    }
    return wdt::gateOpen(g_isr.tickMs, g_isr.lastBeatTick, g_isr.livenessArmed);
}

uint32_t ExtWatchdog::kickPeriodMs() const {
    return board::kWdtKickPeriodMs;
}

uint32_t ExtWatchdog::kickCount() const {
    return manualKicks_ + g_isr.kicks;
}

uint32_t ExtWatchdog::minTimeoutMs() const {
    return board::kWdtMinTimeoutMs;
}

uint32_t ExtWatchdog::typTimeoutMs() const {
    return board::kWdtTypTimeoutMs;
}
