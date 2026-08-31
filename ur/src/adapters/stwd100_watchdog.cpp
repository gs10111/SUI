// Stwd100Watchdog: pulso em WDI (IO19) para o STWD100YNYWY3F CI4, folha 1/2 do
// DE-PURI-DI261924 REV A, ST DocID14134 Rev 11. Contrato e justificativa do mecanismo em
// src/adapters/stwd100_watchdog.h; base comum Parte 2 item 6 e passo 1 da ordem de boot.
//
// O que precisa ficar em IRAM e por que: durante o apagamento de setor da NVS a cache de
// flash e desabilitada. Nesse intervalo NAO se pode tocar em codigo nem em dado que more em
// flash. Por isso, dentro da ISR: nenhuma chamada de biblioteca (nem digitalWrite, nem
// delayMicroseconds, nem millis), nenhum acesso a .rodata (nada de string, nada de tabela
// const), nenhuma chamada virtual (a vtable mora em flash) e nenhuma divisao por variavel
// (o libgcc pode nao estar em IRAM) - o compasso do chute e um contador decrescente. O
// estado que a ISR toca e este POD em .bss (DRAM), fora do objeto, e nao a instancia da
// classe. GPIO.out_w1ts / GPIO.out_w1tc sao registradores, sempre acessiveis.
//
// O spinlock tambem e legitimo aqui: portENTER_CRITICAL/portEXIT_CRITICAL entram por
// vPortEnterCritical (inline) e vPortExitCritical/xPortEnterCriticalTimeout, e o sections.ld
// do IDF 4.4 empacotado com o core 2.x lista .text.vPortExitCritical,
// .text.vPortExitCriticalCompliance, .text.xPortEnterCriticalTimeout e
// .text.xPortEnterCriticalTimeoutCompliance dentro de .iram0.text (e o .flash.text exclui
// libfreertos.a inteira) - verificado em tools/sdk/esp32/ld/sections.ld:178,324-325,773. O
// mux vive em .data (DRAM). Nada disso sai de IRAM com a cache desligada.
//
// A cadeia de interrupcao tambem tem de ser IRAM inteira: ESP_INTR_FLAG_IRAM impede o
// esp_intr_noniram_disable() de mascarar a fonte com a cache desligada; o timer_isr_default
// do driver de timer group ja e IRAM_ATTR no core 2.x (verificado em libdriver.a, secao
// .iram1). O callback e registrado por timer_isr_callback_add() e NAO por
// timerAttachInterruptFlag(): a funcao do core e void e DESCARTA o esp_err_t
// (esp32-hal-timer.c:227-230), entao com ela um begin() que falhou na instalacao devolveria
// kOk e a placa entraria em boot loop sem codigo de erro. Aqui o erro e checado, e depois
// disso o tique e PROVADO por espera limitada antes de declarar ready_.
#include "adapters/stwd100_watchdog.h"

#include <Arduino.h>

#include <driver/timer.h>
#include <esp_intr_alloc.h>
#include <freertos/FreeRTOS.h>
#include <soc/gpio_struct.h>
#include <soc/soc.h>

#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR != 2
#error "timerBegin/timerAlarmWrite sao a API do core 2.x; revalidar a IRAM da ISR antes de trocar"
#endif

namespace {

constexpr uint32_t kWdiMask = 1u << static_cast<uint32_t>(board::kWdi);
constexpr uint32_t kLedMask = 1u << static_cast<uint32_t>(board::kLedTest);
constexpr uint32_t kLedOnTicks = Stwd100Watchdog::kLedOnMs * Stwd100Watchdog::kIsrTickHz / 1000u;
constexpr uint32_t kLedOffTicks = Stwd100Watchdog::kLedOffMs * Stwd100Watchdog::kIsrTickHz / 1000u;
constexpr uint32_t kKickPeriodTicks =
    board::kWdtKickPeriodMs * Stwd100Watchdog::kIsrTickHz / 1000u;
constexpr uint32_t kLivenessDeadlineTicks =
    Stwd100Watchdog::kLivenessDeadlineMs * Stwd100Watchdog::kIsrTickHz / 1000u;
constexpr uint32_t kBootGraceTicks =
    Stwd100Watchdog::kBootGraceMs * Stwd100Watchdog::kIsrTickHz / 1000u;

// Numeros da ordem de boot (DECISIONS.md Parte 2, itens 4 e 15), aqui so para travar a
// carencia e a latencia publicada. Nao sao politica deste adaptador.
constexpr uint32_t kBootBlockingWorstMs = 971;   // soma do setup com NVS patologica
constexpr uint32_t kSplashMs = 1200;             // 600 de autoteste + 600 de logomarca
constexpr uint32_t kBootloaderDeadTimeMs = 300;  // A_MEDIR (medicao 5)
constexpr uint32_t kSetupToDacMs = 6;            // passo 5: saida analogica corrigida
constexpr uint32_t kStaleOutputDeclaredMs = 3300;  // DECISIONS.md decisao 13 item 16

// Idade maxima do token no ultimo chute que ainda passa pelo portao: os chutes so saem na
// cadencia de 250 ms, entao e o maior multiplo do periodo abaixo do prazo, nao o prazo.
constexpr uint32_t kLastKickAfterStallMs =
    ((Stwd100Watchdog::kLivenessDeadlineMs - 1u) / board::kWdtKickPeriodMs) *
    board::kWdtKickPeriodMs;

static_assert(board::kWdi >= 0 && board::kWdi < 32, "WDI fora do banco baixo de GPIO");
static_assert(board::kLedTest >= 0 && board::kLedTest < 32, "LED LIG fora do banco baixo de GPIO");
static_assert(kLedOnTicks > 0u && kLedOffTicks > 0u, "fase do LED LIG menor que um tique da ISR");
static_assert(Stwd100Watchdog::kLedOnMs + Stwd100Watchdog::kLedOffMs == 1000u,
              "batimento do LED LIG fora do periodo de 1 s da decisao 12 item 14");
static_assert(Stwd100Watchdog::kIsrTickHz == 1000u, "o compasso da ISR e a base de 1 ms");
static_assert(kKickPeriodTicks == 250u, "chute do WDI fora dos 250 ms da base comum");
static_assert(kLivenessDeadlineTicks == 800u, "token de liveness fora dos 800 ms da base comum");
static_assert(board::kWdtKickPeriodMs * 3u <= board::kWdtMinTimeoutMs,
              "margem de chute insuficiente para o tWD minimo do STWD100");
static_assert(Stwd100Watchdog::kLivenessDeadlineMs + board::kWdtKickPeriodMs <
                  board::kWdtMinTimeoutMs,
              "prazo do token mais um periodo de chute nao cabe sob o tWD minimo");
static_assert(Stwd100Watchdog::kPulseMs * 1000u >= Stwd100Watchdog::kMinPulseUs,
              "pulso da ISR abaixo da largura minima de WDI");
static_assert(Stwd100Watchdog::kPulseMs * 1000000u > Stwd100Watchdog::kGlitchRejectNs,
              "pulso da ISR seria tratado como glitch");
static_assert(Stwd100Watchdog::kPulseMs < Stwd100Watchdog::kResetPulseMs,
              "pulso da ISR acima do tPW do STWD100");
static_assert(board::kWdiPulseUs >= Stwd100Watchdog::kMinPulseUs,
              "pulso de kickNow abaixo da largura minima de WDI");
static_assert(board::kWdiPulseUs * 1000u > Stwd100Watchdog::kGlitchRejectNs,
              "pulso de kickNow seria tratado como glitch");
static_assert(APB_CLK_FREQ / Stwd100Watchdog::kTimerDivider / Stwd100Watchdog::kTimerTicksPerIsr ==
                  Stwd100Watchdog::kIsrTickHz,
              "divisor e alarme do timer nao dao 1 kHz na APB de 80 MHz");
static_assert(Stwd100Watchdog::kTimerIndex == 2u,
              "timer_dev[2] = {grupo 0, timer 1}: trocar o indice exige trocar o par abaixo");
static_assert(kBootGraceTicks > (kBootBlockingWorstMs + kSplashMs),
              "carencia de boot menor que setup pior caso mais splash: boot loop");
static_assert(kBootGraceTicks > kLivenessDeadlineTicks,
              "carencia de boot menor que o proprio prazo do token");
static_assert(kLastKickAfterStallMs == 750u,
              "ultimo chute apos travamento fora dos 750 ms de DECISIONS 13 item 16");
static_assert(kLastKickAfterStallMs + Stwd100Watchdog::kMaxTimeoutMs + kBootloaderDeadTimeMs +
                      kSetupToDacMs <=
                  kStaleOutputDeclaredMs,
              "saida analogica velha acima dos 3,30 s declarados (DECISIONS 13 item 16 / MAN 6.2)");

struct IsrState {
    volatile uint32_t tickMs;
    volatile uint32_t lastBeatTick;
    volatile uint32_t countdown;
    volatile uint32_t kicks;
    volatile uint32_t ledCountdown;
    volatile bool pulseHigh;
    volatile bool livenessArmed;
    volatile bool ledArmed;
    volatile bool ledOn;
};

IsrState g_isr;
hw_timer_t* g_timer;
bool g_armed;

// Spinlock em DRAM. Sem ele, kickNow()/rearmPin() e a ISR disputam o mesmo pino: um
// out_w1tc da tarefa no meio do pulso de 1 ms da ISR corta o chute abaixo dos 100 ns de
// rejeicao de glitch do STWD100 e o periodo inteiro se perde.
portMUX_TYPE g_wdiMux = portMUX_INITIALIZER_UNLOCKED;

// Pulso cru, sem contadores e sem guarda: e o unico caminho usado por begin(), quando o
// objeto ainda nao esta ready_ e a ISR ainda nao existe.
void pulseWdiBlocking() {
    GPIO.out_w1ts = kWdiMask;
    delayMicroseconds(board::kWdiPulseUs);
    GPIO.out_w1tc = kWdiMask;
}

void IRAM_ATTR wdiIsr() {
    portENTER_CRITICAL_ISR(&g_wdiMux);

    const uint32_t tick = g_isr.tickMs + 1u;
    g_isr.tickMs = tick;

    if (g_isr.pulseHigh) {
        GPIO.out_w1tc = kWdiMask;
        g_isr.pulseHigh = false;
    }

    // Portao: com token, vale o prazo; sem token, vale a carencia de boot - que TEM fim.
    // Avaliado a cada tique porque o LED LIG usa o MESMO portao do WDI: os dois tem de
    // apagar juntos quando o firmware para de dar sinal de vida.
    bool gateOpen;
    if (g_isr.livenessArmed) {
        gateOpen = (tick - g_isr.lastBeatTick) < kLivenessDeadlineTicks;
    } else {
        gateOpen = tick < kBootGraceTicks;
    }

    if (g_isr.ledArmed) {
        if (!gateOpen) {
            GPIO.out_w1tc = kLedMask;
            g_isr.ledOn = false;
            g_isr.ledCountdown = 0u;
        } else if (g_isr.ledCountdown != 0u) {
            g_isr.ledCountdown = g_isr.ledCountdown - 1u;
        } else if (g_isr.ledOn) {
            GPIO.out_w1tc = kLedMask;
            g_isr.ledOn = false;
            g_isr.ledCountdown = kLedOffTicks - 1u;
        } else {
            GPIO.out_w1ts = kLedMask;
            g_isr.ledOn = true;
            g_isr.ledCountdown = kLedOnTicks - 1u;
        }
    }

    if (g_isr.countdown != 0u) {
        g_isr.countdown = g_isr.countdown - 1u;
        portEXIT_CRITICAL_ISR(&g_wdiMux);
        return;
    }
    g_isr.countdown = kKickPeriodTicks - 1u;

    if (gateOpen) {
        GPIO.out_w1ts = kWdiMask;
        g_isr.pulseHigh = true;
        g_isr.kicks = g_isr.kicks + 1u;
    }

    portEXIT_CRITICAL_ISR(&g_wdiMux);
}

// Assinatura de timer_isr_callback_add(): o retorno false diz "nao acordar tarefa".
bool IRAM_ATTR wdiIsrCb(void*) {
    wdiIsr();
    return false;
}

}  // namespace

Stwd100Watchdog::Stwd100Watchdog() : ready_(false), manualKicks_(0), heartbeats_(0) {}

Status Stwd100Watchdog::begin() {
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
    g_isr.countdown = kKickPeriodTicks - 1u;
    g_isr.kicks = 0;
    g_isr.pulseHigh = false;
    g_isr.livenessArmed = false;
    g_isr.ledCountdown = 0;
    g_isr.ledArmed = false;
    g_isr.ledOn = false;

    // Primeiro chute, antes de existir ISR: o objeto ainda nao esta ready_, entao nao passa
    // por kickNow(). Ele conta em kickCount() porque foi um pulso real no WDI.
    pulseWdiBlocking();
    manualKicks_ = 1u;

    g_timer = timerBegin(kTimerIndex, kTimerDivider, true);
    if (g_timer == nullptr) {
        return Status(Err::HwFault);
    }

    // ESP_ERR_NOT_FOUND quando nao ha vaga de interrupcao compativel com ESP_INTR_FLAG_IRAM
    // no core que executa o setup, ESP_ERR_INVALID_STATE se ja houver callback.
    if (timer_isr_callback_add(TIMER_GROUP_0, TIMER_1, &wdiIsrCb, nullptr, ESP_INTR_FLAG_IRAM) !=
        ESP_OK) {
        timerEnd(g_timer);
        g_timer = nullptr;
        return Status(Err::HwFault);
    }

    timerAlarmWrite(g_timer, kTimerTicksPerIsr, true);
    timerAlarmEnable(g_timer);

    // PROVA DE VIDA DA ISR. Instalar sem erro nao e o mesmo que tiquetar: divisor errado,
    // alarme nao habilitado ou APB fora dos 80 MHz dariam um watchdog silenciosamente
    // parado. Em 1 kHz o primeiro tique vem em ~1 ms; o teto de 5 ms limita o bloqueio.
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
    return kOk;
}

void Stwd100Watchdog::heartbeat() {
    if (!ready_) {
        return;
    }
    portENTER_CRITICAL(&g_wdiMux);
    g_isr.lastBeatTick = g_isr.tickMs;
    g_isr.livenessArmed = true;
    portEXIT_CRITICAL(&g_wdiMux);
    heartbeats_ = heartbeats_ + 1u;
}

void Stwd100Watchdog::kickNow() {
    if (!ready_) {
        return;
    }
    portENTER_CRITICAL(&g_wdiMux);
    GPIO.out_w1ts = kWdiMask;
    delayMicroseconds(board::kWdiPulseUs);
    GPIO.out_w1tc = kWdiMask;
    // Reancora a cadencia no chute manual: sem isto um chute da ISR poderia sair poucos ms
    // depois deste e deixar um vazio de 250 ms logo atras.
    g_isr.pulseHigh = false;
    g_isr.countdown = kKickPeriodTicks - 1u;
    portEXIT_CRITICAL(&g_wdiMux);
    manualKicks_ = manualKicks_ + 1u;
}

Status Stwd100Watchdog::rearmPin() {
    if (board::kWdi == board::kNoPin) {
        return Status(Err::Param);
    }
    if (!ready_) {
        return Status(Err::NotInit);
    }
    // pinMode fora da secao critica: ele toma travas proprias do core.
    pinMode(static_cast<uint8_t>(board::kWdi), OUTPUT);
    portENTER_CRITICAL(&g_wdiMux);
    if (!g_isr.pulseHigh) {
        GPIO.out_w1tc = kWdiMask;
    }
    portEXIT_CRITICAL(&g_wdiMux);
    return kOk;
}

// Passo 14 da ordem de boot: so aqui o IO2 pode ser dirigido para valer. Antes disso ele e
// strapping e um nivel alto durante o reset quebra o modo download.
Status Stwd100Watchdog::enablePowerLed() {
    if (board::kLedTest == board::kNoPin) {
        return Status(Err::Param);
    }
    if (!ready_) {
        return Status(Err::NotInit);
    }
    // pinMode fora da secao critica: ele toma travas proprias do core.
    pinMode(static_cast<uint8_t>(board::kLedTest), OUTPUT);
    portENTER_CRITICAL(&g_wdiMux);
    GPIO.out_w1ts = kLedMask;
    g_isr.ledOn = true;
    g_isr.ledCountdown = kLedOnTicks - 1u;
    g_isr.ledArmed = true;
    portEXIT_CRITICAL(&g_wdiMux);
    return kOk;
}

bool Stwd100Watchdog::powerLedArmed() const {
    return ready_ && g_isr.ledArmed;
}

bool Stwd100Watchdog::kicking() const {
    if (!ready_) {
        return false;
    }
    portENTER_CRITICAL(&g_wdiMux);
    const uint32_t tick = g_isr.tickMs;
    const uint32_t lastBeat = g_isr.lastBeatTick;
    const bool armed = g_isr.livenessArmed;
    portEXIT_CRITICAL(&g_wdiMux);

    if (!armed) {
        return tick < kBootGraceTicks;
    }
    return (tick - lastBeat) < kLivenessDeadlineTicks;
}

uint32_t Stwd100Watchdog::kickPeriodMs() const {
    return board::kWdtKickPeriodMs;
}

uint32_t Stwd100Watchdog::heartbeatTimeoutMs() const {
    return kLivenessDeadlineMs;
}

uint32_t Stwd100Watchdog::minTimeoutMs() const {
    return board::kWdtMinTimeoutMs;
}

uint32_t Stwd100Watchdog::typTimeoutMs() const {
    return board::kWdtTypTimeoutMs;
}

uint32_t Stwd100Watchdog::kickCount() const {
    return ready_ ? (g_isr.kicks + manualKicks_) : 0u;
}

uint32_t Stwd100Watchdog::heartbeatCount() const {
    return ready_ ? heartbeats_ : 0u;
}

bool Stwd100Watchdog::livenessArmed() const {
    return ready_ && g_isr.livenessArmed;
}

uint32_t Stwd100Watchdog::livenessAgeMs() const {
    return ready_ ? (g_isr.tickMs - g_isr.lastBeatTick) : 0u;
}

uint32_t Stwd100Watchdog::isrTickMs() const {
    return ready_ ? g_isr.tickMs : 0u;
}
