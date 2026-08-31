// Watchdog de teste: reproduz, no host, a MAQUINA do adaptador real (STWD100 em IO19,
// src/adapters/stwd100_watchdog.*) - a ISR de 1 kHz que chuta a cada 250 ms, o token de
// liveness que fecha o portao em 800 ms, e a carencia de boot que fecha o portao em 3000 ms
// quando o token nunca chega. O tempo so anda quando o teste manda, por advanceMs().
//
// O que este fake existe para provar (LSP): que o dominio testado contra ele encontra a MESMA
// semantica na placa. Os pontos em que um fake generoso demais faria a suite mentir:
//
// 1. INSTANCIA SEM begin() BEM-SUCEDIDO NAO MEXE NO CACHORRO. heartbeat() e kickNow() sao
//    no-op, rearmPin() devolve Err::NotInit, kicking() e false e todos os contadores sao 0.
//    No alvo o estado da ISR e global (um unico pino, um unico timer): um objeto orfao que
//    renovasse o token manteria o STWD100 real alimentado. O alvo guarda por ready_; aqui a
//    guarda e a mesma.
// 2. SINGLETON. So a primeira instancia arma; a segunda begin() recebe Err::Busy, como o
//    g_armed do alvo. DIVERGENCIA DECLARADA E UNICA: aqui o destrutor devolve a posse, para
//    que cada teste comece limpo. No alvo essa posse so morre no reset da placa - e nada em
//    producao destroi um watchdog.
// 3. begin() PODE FALHAR. setBeginResult(Status(Err::HwFault)) reproduz o alvo quando o timer
//    nao abre, quando a ISR nao pode ser instalada com ESP_INTR_FLAG_IRAM, ou quando ela nao
//    tiqueta dentro da prova de 5 ms. Um fake que so sabe devolver kOk esta chutando. A ORDEM
//    tambem e a do alvo: a posse (Busy) e testada ANTES do hardware (HwFault).
// 4. A CARENCIA DE BOOT TEM FIM. Sem nenhum heartbeat(), o chute para em kBootGraceMs e
//    wouldHaveReset() passa a valer: e o modo de falha "a tarefa ctrl nunca nasceu".
// 5. O ULTIMO CHUTE SAI NA CADENCIA, NAO NO PRAZO. Parado o heartbeat, o ultimo pulso sai no
//    maximo em 750 ms (ticks de 250, 500 e 750 ms ainda veem idade < 800 ms), e nao em 799.
//
// Os numeros sao os da base comum (DECISIONS.md Parte 2 item 6 e 2.5) e do datasheet, os
// mesmos de Stwd100Watchdog; o teste de contrato em test/native/test_fakes_watchdog/ prende
// os que a porta publica. Este cabecalho nao pode incluir o do adaptador - ele e Arduino.
#pragma once

#include <stdint.h>

#include "ports/i_watchdog.h"
#include "status.h"

namespace test {

class FakeWatchdog final : public IWatchdog {
public:
    // NAO ha include de board_pins.h aqui de proposito: o env native nao compila
    // lib_shared/depuri_board (nada do dominio o inclui, e a LDF em modo chain nao o traz
    // para o build de teste). Os numeros abaixo sao os mesmos de board_pins.h
    // (kWdtKickPeriodMs, kWdtMinTimeoutMs, kWdtTypTimeoutMs, kWdiPulseUs) e de
    // Stwd100Watchdog; quem os prende contra o alvo e o teste de contrato em
    // test/native/test_fakes_watchdog/, que afirma os valores publicados pela porta.
    static constexpr uint32_t kIsrTickHz = 1000;          // 1 tick = 1 ms
    static constexpr uint32_t kKickPeriodMs = 250;        // board::kWdtKickPeriodMs
    static constexpr uint32_t kLivenessDeadlineMs = 800;  // kCtrlLivenessDeadlineMs (DECISIONS 2.5)
    static constexpr uint32_t kBootGraceMs = 3000;        // Stwd100Watchdog::kBootGraceMs
    static constexpr uint32_t kMinTimeoutMs = 1120;       // board::kWdtMinTimeoutMs (tWD min)
    static constexpr uint32_t kTypTimeoutMs = 1600;       // board::kWdtTypTimeoutMs
    static constexpr uint32_t kMaxTimeoutMs = 2240;       // tWD max do datasheet
    static constexpr uint32_t kManualPulseUs = 5;         // board::kWdiPulseUs

    FakeWatchdog()
        : beginResult_(kOk), tickMs_(0), lastBeatTick_(0), countdown_(kKickPeriodMs - 1u),
          isrKicks_(0), manualKicks_(0), heartbeats_(0), rearms_(0), msSinceKick_(0),
          maxKickGapMs_(0), ready_(false), owner_(false), livenessArmed_(false) {}

    ~FakeWatchdog() override {
        if (owner_) {
            singletonArmed() = false;
        }
    }

    // --- porta ---

    Status begin() override {
        if (ready_) {
            return kOk;
        }
        // ORDEM DE CHECAGEM DO ALVO, e nao a mais conveniente: Stwd100Watchdog::begin() testa
        // g_armed (posse do pino e do timer) ANTES de tocar em qualquer hardware, entao a
        // segunda instancia recebe Busy mesmo que o hardware fosse falhar. Inverter aqui faria
        // a suite prometer HwFault onde a placa devolve Busy.
        if (singletonArmed()) {
            return Status(Err::Busy);
        }
        if (beginResult_.failed()) {
            return beginResult_;  // timer/ISR que nao subiu: NADA fica meio armado
        }

        tickMs_ = 0;
        lastBeatTick_ = 0;
        countdown_ = kKickPeriodMs - 1u;
        isrKicks_ = 0;
        heartbeats_ = 0;
        livenessArmed_ = false;
        msSinceKick_ = 0;
        maxKickGapMs_ = 0;

        manualKicks_ = 1u;  // o pulso imediato do passo 1 da ordem de boot conta
        singletonArmed() = true;
        owner_ = true;
        ready_ = true;
        return kOk;
    }

    void heartbeat() override {
        if (!ready_) {
            return;
        }
        lastBeatTick_ = tickMs_;
        livenessArmed_ = true;
        heartbeats_ += 1u;
    }

    void kickNow() override {
        if (!ready_) {
            return;
        }
        manualKicks_ += 1u;
        countdown_ = kKickPeriodMs - 1u;  // o alvo reancora a cadencia no chute manual
        msSinceKick_ = 0;
    }

    Status rearmPin() override {
        if (!ready_) {
            return Status(Err::NotInit);
        }
        rearms_ += 1u;
        return kOk;
    }

    bool kicking() const override {
        if (!ready_) {
            return false;
        }
        return gateOpen();
    }

    uint32_t kickPeriodMs() const override { return kKickPeriodMs; }
    uint32_t heartbeatTimeoutMs() const override { return kLivenessDeadlineMs; }
    uint32_t minTimeoutMs() const override { return kMinTimeoutMs; }
    uint32_t typTimeoutMs() const override { return kTypTimeoutMs; }
    uint32_t kickCount() const override { return ready_ ? (isrKicks_ + manualKicks_) : 0u; }
    uint32_t heartbeatCount() const override { return ready_ ? heartbeats_ : 0u; }

    // --- controle e inspecao de teste ---

    // Anda o tempo tique a tique, como a ISR de 1 kHz do alvo.
    void advanceMs(uint32_t deltaMs) {
        for (uint32_t i = 0; i < deltaMs; ++i) {
            tick();
        }
    }

    // Injeta a falha de begin() do alvo (timer ausente, ISR nao instalada, ISR sem tique).
    void setBeginResult(Status result) { beginResult_ = result; }

    bool ready() const { return ready_; }
    bool livenessArmed() const { return ready_ && livenessArmed_; }
    uint32_t livenessAgeMs() const { return ready_ ? (tickMs_ - lastBeatTick_) : 0u; }
    uint32_t isrTickMs() const { return ready_ ? tickMs_ : 0u; }
    uint32_t isrKickCount() const { return ready_ ? isrKicks_ : 0u; }
    uint32_t manualKickCount() const { return ready_ ? manualKicks_ : 0u; }
    uint32_t rearmCount() const { return rearms_; }
    uint32_t bootGraceMs() const { return kBootGraceMs; }
    uint32_t maxTimeoutMs() const { return kMaxTimeoutMs; }

    // Maior intervalo ja observado entre dois pulsos no WDI, e a pergunta que a porta pede ao
    // fake: o STWD100 ja teria resetado a placa? (tWD minimo do datasheet.)
    uint32_t maxKickGapMs() const { return maxKickGapMs_; }
    bool wouldHaveReset() const { return maxKickGapMs_ >= kMinTimeoutMs; }

private:
    static bool& singletonArmed() {
        static bool armed = false;  // um unico WDI e um unico timer, como na placa
        return armed;
    }

    bool gateOpen() const {
        if (livenessArmed_) {
            return (tickMs_ - lastBeatTick_) < kLivenessDeadlineMs;
        }
        return tickMs_ < kBootGraceMs;  // carencia de boot: incondicional, mas com fim
    }

    void tick() {
        if (!ready_) {
            return;  // sem begin() nao existe ISR
        }
        tickMs_ += 1u;
        msSinceKick_ += 1u;
        if (msSinceKick_ > maxKickGapMs_) {
            maxKickGapMs_ = msSinceKick_;
        }

        if (countdown_ != 0u) {
            countdown_ -= 1u;
            return;
        }
        countdown_ = kKickPeriodMs - 1u;

        if (gateOpen()) {
            isrKicks_ += 1u;
            msSinceKick_ = 0;
        }
    }

    Status beginResult_;
    uint32_t tickMs_;
    uint32_t lastBeatTick_;
    uint32_t countdown_;
    uint32_t isrKicks_;
    uint32_t manualKicks_;
    uint32_t heartbeats_;
    uint32_t rearms_;
    uint32_t msSinceKick_;
    uint32_t maxKickGapMs_;
    bool ready_;
    bool owner_;
    bool livenessArmed_;
};

}  // namespace test
