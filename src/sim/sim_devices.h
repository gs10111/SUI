// Simulador de bancada para o host: implementacoes de mentira das interfaces do Ctx.
// Nao e codigo de producao - existe para rodar a suite inteira no PC e ver os prints.
#pragma once

#include <chrono>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "console_io.h"
#include "drivers/calibration.h"
#include "iface/ianalog_output.h"
#include "iface/ibuttons.h"
#include "iface/idigital_output_bank.h"
#include "iface/idisplay.h"
#include "iface/ioperator.h"
#include "iface/isafe_state.h"
#include "iface/iserial_transport.h"
#include "iface/iwatchdog.h"
#include "kv_store.h"
#include "proto/frame.h"

namespace sim {

inline void trace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

inline void trace(const char* fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("        [hw] %s\n", buf);
}

class SimConsoleIO : public IConsoleIO {
public:
    SimConsoleIO(const char* script, uint32_t timeScale)
        : script_(script != nullptr ? script : ""), pos_(0), scale_(timeScale == 0 ? 1 : timeScale),
          start_(std::chrono::steady_clock::now()) {}

    void write(const char* text) override {
        if (text != nullptr) {
            fputs(text, stdout);
        }
    }

    void writeLine(const char* text) override {
        write(text);
        fputs("\n", stdout);
    }

    void printf(const char* fmt, ...) override __attribute__((format(printf, 2, 3))) {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        fputs(buf, stdout);
    }

    bool readByte(uint8_t& out) override {
        if (script_[pos_] == '\0') {
            return false;
        }
        out = static_cast<uint8_t>(script_[pos_++]);
        return true;
    }

    uint32_t nowMs() const override {
        const auto delta = std::chrono::steady_clock::now() - start_;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
        return static_cast<uint32_t>(ms) * scale_;
    }

    void idle() override {}

    bool scriptExhausted() const { return script_[pos_] == '\0'; }

private:
    const char* script_;
    size_t pos_;
    uint32_t scale_;
    std::chrono::steady_clock::time_point start_;
};

class SimOperator : public IOperator {
public:
    SimOperator(IConsoleIO& io, const char* answers, const char* lineAnswer)
        : io_(io), answers_(answers != nullptr ? answers : ""), pos_(0),
          line_(lineAnswer != nullptr ? lineAnswer : "1"), aborted_(false), skipped_(false) {}

    void info(const char* fmt, ...) override __attribute__((format(printf, 2, 3))) {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        io_.printf("  %s\r\n", buf);
    }

    void step(const char* what, const char* where, const char* expected) override {
        io_.printf("  ACAO   : %s\r\n", what != nullptr ? what : "");
        io_.printf("  MEDIR  : %s\r\n", where != nullptr ? where : "");
        io_.printf("  ESPERADO: %s\r\n", expected != nullptr ? expected : "");
    }

    Verdict ask(const char* prompt) override {
        const char answer = nextAnswer();
        io_.printf("  %s [p/f/s/a] -> %c (simulado)\r\n", prompt != nullptr ? prompt : "resultado?", answer);
        switch (answer) {
            case 'f': return Verdict::Fail;
            case 's': skipped_ = true; return Verdict::Skip;
            case 'a': aborted_ = true; return Verdict::Abort;
            default: return Verdict::Pass;
        }
    }

    bool askLine(const char* prompt, char* out, uint16_t cap) override {
        if (out == nullptr || cap == 0) {
            return false;
        }
        size_t i = 0;
        while (i + 1 < cap && line_[i] != '\0') {
            out[i] = line_[i];
            ++i;
        }
        out[i] = '\0';
        io_.printf("  %s -> \"%s\" (simulado)\r\n", prompt != nullptr ? prompt : "valor?", out);
        return true;
    }

    bool askYes(const char* prompt) override {
        io_.printf("  %s [s/n] -> s (simulado)\r\n", prompt != nullptr ? prompt : "confirma?");
        return true;
    }

    bool aborted() const override { return aborted_; }
    void clearAbort() override { aborted_ = false; }
    bool skipped() const override { return skipped_; }
    void clearSkipped() override { skipped_ = false; }

private:
    char nextAnswer() {
        if (answers_[pos_] == '\0') {
            return 'p';
        }
        return answers_[pos_++];
    }

    IConsoleIO& io_;
    const char* answers_;
    size_t pos_;
    const char* line_;
    bool aborted_;
    bool skipped_;
};

class SimKvStore : public IKeyValueStore {
public:
    SimKvStore() : count_(0) { memset(entries_, 0, sizeof(entries_)); }

    Status putBlob(const char* key, const void* data, size_t len) override {
        if (key == nullptr || len > kValueCap) {
            return Status(Err::Param);
        }
        Entry* e = findOrCreate(key);
        if (e == nullptr) {
            return Status(Err::Storage);
        }
        memcpy(e->value, data, len);
        e->len = len;
        return kOk;
    }

    Status getBlob(const char* key, void* data, size_t cap, size_t& outLen) override {
        outLen = 0;
        Entry* e = find(key);
        if (e == nullptr) {
            return Status(Err::Storage);
        }
        if (e->len > cap) {
            return Status(Err::Range);
        }
        memcpy(data, e->value, e->len);
        outLen = e->len;
        return kOk;
    }

    Status putU8(const char* key, uint8_t value) override { return putBlob(key, &value, 1); }

    Status getU8(const char* key, uint8_t& value) override {
        size_t got = 0;
        const Status st = getBlob(key, &value, 1, got);
        if (st.failed()) {
            return st;
        }
        return (got == 1) ? kOk : Status(Err::Storage);
    }

    Status putString(const char* key, const char* value) override {
        if (value == nullptr) {
            return Status(Err::Param);
        }
        return putBlob(key, value, strlen(value) + 1);
    }

    Status getString(const char* key, char* out, size_t cap) override {
        size_t got = 0;
        const Status st = getBlob(key, out, cap, got);
        if (st.failed()) {
            return st;
        }
        out[cap - 1] = '\0';
        return kOk;
    }

    Status remove(const char* key) override {
        Entry* e = find(key);
        if (e == nullptr) {
            return Status(Err::Storage);
        }
        e->key[0] = '\0';
        e->len = 0;
        return kOk;
    }

private:
    static constexpr uint8_t kMaxEntries = 8;
    static constexpr size_t kValueCap = 2048;
    static constexpr size_t kKeyCap = 20;

    struct Entry {
        char key[kKeyCap];
        uint8_t value[kValueCap];
        size_t len;
    };

    Entry* find(const char* key) {
        if (key == nullptr) {
            return nullptr;
        }
        for (uint8_t i = 0; i < count_; ++i) {
            if (entries_[i].key[0] != '\0' && strncmp(entries_[i].key, key, kKeyCap - 1) == 0) {
                return &entries_[i];
            }
        }
        return nullptr;
    }

    Entry* findOrCreate(const char* key) {
        Entry* e = find(key);
        if (e != nullptr) {
            return e;
        }
        for (uint8_t i = 0; i < count_; ++i) {
            if (entries_[i].key[0] == '\0') {
                snprintf(entries_[i].key, kKeyCap, "%s", key);
                return &entries_[i];
            }
        }
        if (count_ >= kMaxEntries) {
            return nullptr;
        }
        Entry* fresh = &entries_[count_++];
        snprintf(fresh->key, kKeyCap, "%s", key);
        return fresh;
    }

    Entry entries_[kMaxEntries];
    uint8_t count_;
};

class SimRelayBank : public IDigitalOutputBank {
public:
    SimRelayBank() { memset(state_, 0, sizeof(state_)); }

    Status begin() override {
        memset(state_, 0, sizeof(state_));
        trace("reles: todos desenergizados no begin()");
        return kOk;
    }

    uint8_t count() const override { return board::kRelayCount; }

    Status set(uint8_t index, bool on) override {
        if (index >= board::kRelayCount) {
            return Status(Err::Param);
        }
        state_[index] = on;
        trace("%s (%s, IO%d) -> %s", board::kRelayMap[index].net, board::kRelayMap[index].relay,
              static_cast<int>(board::kRelayMap[index].pin), on ? "ON" : "OFF");
        return kOk;
    }

    Status get(uint8_t index, bool& on) const override {
        if (index >= board::kRelayCount) {
            return Status(Err::Param);
        }
        on = state_[index];
        return kOk;
    }

    Status allOff() override {
        for (uint8_t i = 0; i < board::kRelayCount; ++i) {
            state_[i] = false;
        }
        trace("reles: allOff()");
        return kOk;
    }

    Status allOn() override {
        for (uint8_t i = 0; i < board::kRelayCount; ++i) {
            state_[i] = true;
        }
        trace("reles: allOn() - pior caso de carga do +5 V");
        return kOk;
    }

private:
    bool state_[board::kRelayCount];
};

class SimAnalogOutput : public IAnalogOutput {
public:
    explicit SimAnalogOutput(CalibrationStore& cal)
        : cal_(cal), mode_(AoMode::Voltage), spiHz_(board::kDacSpiDefaultHz), ready_(false) {
        code_[0] = 0;
        code_[1] = 0;
    }

    Status begin() override {
        ready_ = true;
        mode_ = AoMode::Voltage;
        code_[0] = 0;
        code_[1] = 0;
        trace("DAC8562: reset, power-up A+B, ref interna + ganho 2, canais em 0x0000");
        return kOk;
    }

    Status setRaw(uint8_t axis, uint16_t value) override {
        if (axis >= board::kAxisCount) {
            return Status(Err::Param);
        }
        if (!ready_) {
            return Status(Err::NotInit);
        }
        code_[axis] = value;
        trace("DAC %s <- 0x%04X", board::kAxisName[axis], value);
        return kOk;
    }

    Status getRaw(uint8_t axis, uint16_t& value) const override {
        if (axis >= board::kAxisCount) {
            return Status(Err::Param);
        }
        value = code_[axis];
        return kOk;
    }

    Status setEngineering(uint8_t axis, float value) override {
        uint16_t code = 0;
        const Status st = cal_.codeFor(axis, mode_, value, fullScaleCode(), code);
        if (st.failed()) {
            return st;
        }
        return setRaw(axis, code);
    }

    Status setMode(AoMode desired) override {
        if (desired == mode_) {
            return kOk;
        }
        zeroAll();
        mode_ = desired;
        trace("OP_MODE (IO%d) -> %s", static_cast<int>(board::kXtrOpMode),
              desired == AoMode::Voltage ? "L (tensao)" : "H (corrente)");
        return kOk;
    }

    AoMode mode() const override { return mode_; }

    Status zeroAll() override {
        code_[0] = 0;
        code_[1] = 0;
        trace("DAC: os dois canais em 0x0000");
        return kOk;
    }

    Status setSpiHz(uint32_t hz) override {
        if (hz < board::kDacSpiMinHz || hz > board::kDacSpiMaxHz) {
            return Status(Err::Range);
        }
        spiHz_ = hz;
        return kOk;
    }

    uint32_t spiHz() const override { return spiHz_; }
    uint16_t fullScaleCode() const override { return 0xFFFF; }

private:
    CalibrationStore& cal_;
    AoMode mode_;
    uint32_t spiHz_;
    uint16_t code_[board::kAxisCount];
    bool ready_;
};

class SimSerialTransport : public ISerialTransport {
public:
    SimSerialTransport() : stats_(), baud_(0), head_(0), tail_(0), staticDrive_(false), turnaroundUs_(0) {
        memset(&stats_, 0, sizeof(stats_));
        memset(rx_, 0, sizeof(rx_));
    }

    Status begin(uint32_t baudRate, uint8_t bits, char parityChar, uint8_t stops) override {
        baud_ = baudRate;
        trace("RS-485: UART2 %u %u%c%u, DE em IO%d, half-duplex", static_cast<unsigned>(baudRate),
              static_cast<unsigned>(bits), parityChar, static_cast<unsigned>(stops),
              static_cast<int>(board::kRs485De));
        return kOk;
    }

    Status end() override {
        baud_ = 0;
        return kOk;
    }

    Status write(const uint8_t* data, uint16_t len) override {
        if (data == nullptr) {
            return Status(Err::Param);
        }
        stats_.bytesTx += len;
        if (staticDrive_) {
            return kOk;
        }
        replyTo(data, len);
        turnaroundUs_ = 120;
        return kOk;
    }

    uint16_t read(uint8_t* buf, uint16_t cap, uint32_t timeoutMs) override {
        (void)timeoutMs;
        uint16_t n = 0;
        while (n < cap && head_ != tail_) {
            buf[n++] = rx_[tail_];
            tail_ = static_cast<uint16_t>((tail_ + 1) % kRxCap);
        }
        stats_.bytesRx += n;
        return n;
    }

    uint16_t available() const override {
        return static_cast<uint16_t>((head_ + kRxCap - tail_) % kRxCap);
    }

    Status flushRx() override {
        head_ = 0;
        tail_ = 0;
        return kOk;
    }

    Status driveStatic(bool enable, bool level) override {
        staticDrive_ = enable;
        trace("RS-485: drive estatico %s, TX em nivel %s", enable ? "LIGADO" : "desligado",
              level ? "ALTO" : "BAIXO");
        return kOk;
    }

    uint32_t lastTurnaroundUs() const override { return turnaroundUs_; }
    const SerialStats& stats() const override { return stats_; }
    void resetStats() override { memset(&stats_, 0, sizeof(stats_)); }
    void noteCrcError() override { ++stats_.crcErrors; }
    void noteFrameOk() override { ++stats_.framesOk; }
    void noteTimeout() override { ++stats_.timeouts; }
    uint32_t baud() const override { return baud_; }

private:
    static constexpr uint16_t kRxCap = 512;

    void push(uint8_t value) {
        const uint16_t next = static_cast<uint16_t>((head_ + 1) % kRxCap);
        if (next == tail_) {
            return;
        }
        rx_[head_] = value;
        head_ = next;
    }

    void replyTo(const uint8_t* data, uint16_t len) {
        uint8_t payload[frame::kMaxPayload];
        uint8_t plen = 0;
        if (frame::decode(data, len, payload, sizeof(payload), plen) != frame::Decode::Ok) {
            for (uint16_t i = 0; i < len; ++i) {
                push(data[i]);
            }
            return;
        }
        const int16_t angleX = 123;
        const int16_t angleY = -45;
        uint8_t reply[4];
        reply[0] = static_cast<uint8_t>(angleX & 0xFF);
        reply[1] = static_cast<uint8_t>((angleX >> 8) & 0xFF);
        reply[2] = static_cast<uint8_t>(angleY & 0xFF);
        reply[3] = static_cast<uint8_t>((angleY >> 8) & 0xFF);
        uint8_t out[frame::kMaxFrame];
        const uint16_t n = frame::encode(reply, sizeof(reply), out, sizeof(out));
        for (uint16_t i = 0; i < n; ++i) {
            push(out[i]);
        }
    }

    SerialStats stats_;
    uint32_t baud_;
    uint8_t rx_[kRxCap];
    uint16_t head_;
    uint16_t tail_;
    bool staticDrive_;
    uint32_t turnaroundUs_;
};

class SimDisplay : public IDisplay {
public:
    SimDisplay() : ready_(false) {}

    Status begin() override {
        ready_ = true;
        trace("display: begin() no VSPI (CS IO%d, DC IO%d, RESET IO%d)", static_cast<int>(board::kDispCs),
              static_cast<int>(board::kDispDc), static_cast<int>(board::kDispReset));
        return kOk;
    }

    Status hardReset() override {
        trace("display: pulso de RESET");
        return kOk;
    }

    Status showPattern(uint8_t index) override {
        if (index >= patternCount()) {
            return Status(Err::Param);
        }
        trace("display: padrao %u (%s)", static_cast<unsigned>(index), patternDescription(index));
        return kOk;
    }

    uint8_t patternCount() const override { return 6; }

    const char* patternDescription(uint8_t index) const override {
        static const char* kDesc[] = {"tudo aceso",     "tudo apagado",     "tabuleiro de xadrez",
                                      "texto com versao e serie", "contraste minimo", "contraste maximo"};
        return (index < 6) ? kDesc[index] : "?";
    }

    Status writeText(const char* text) override {
        trace("display: texto \"%s\"", text != nullptr ? text : "");
        return kOk;
    }

    Status setContrast(uint8_t value) override {
        trace("display: contraste %u", static_cast<unsigned>(value));
        return kOk;
    }

    Status off() override { return kOk; }
    const char* driverName() const override { return "sim"; }
    bool ready() const { return ready_; }

private:
    bool ready_;
};

class SimButtons : public IButtons {
public:
    SimButtons() : polls_(0), cursor_(0), pendingRelease_(false), fill_(0), out_(0) {
        memset(press_, 0, sizeof(press_));
        memset(edges_, 0, sizeof(edges_));
    }

    Status begin() override {
        trace("botoes: UP IO%d (pull-up interno), DOWN IO%d e MENU IO%d (input-only, sem pull interno)",
              static_cast<int>(board::kBtnUp), static_cast<int>(board::kBtnDown),
              static_cast<int>(board::kBtnMenu));
        return kOk;
    }

    void poll() override {
        ++polls_;
        if (polls_ % kPollsPerEvent != 0) {
            return;
        }
        if (cursor_ >= kButtonCount * kPressesPerButton) {
            return;
        }
        const uint8_t index = static_cast<uint8_t>(cursor_ / kPressesPerButton);
        if (!pendingRelease_) {
            pushEdge(index, false);
            pendingRelease_ = true;
            return;
        }
        pushEdge(index, true);
        ++press_[index];
        pendingRelease_ = false;
        ++cursor_;
    }

    bool level(uint8_t index) const override {
        (void)index;
        return true;
    }

    uint32_t pressCount(uint8_t index) const override {
        return (index < kButtonCount) ? press_[index] : 0;
    }

    uint32_t bounceCount(uint8_t index) const override {
        (void)index;
        return 0;
    }

    bool takeEdge(uint8_t& index, bool& rising) override {
        if (fill_ == 0) {
            return false;
        }
        index = edges_[out_].index;
        rising = edges_[out_].rising;
        out_ = static_cast<uint8_t>((out_ + 1) % kQueue);
        --fill_;
        return true;
    }

    void resetCounts() override {
        memset(press_, 0, sizeof(press_));
        cursor_ = 0;
        fill_ = 0;
        out_ = 0;
        pendingRelease_ = false;
    }

    const char* name(uint8_t index) const override {
        static const char* kNames[] = {"UP", "DOWN", "MENU"};
        return (index < kButtonCount) ? kNames[index] : "?";
    }

    bool inputOnly(uint8_t index) const override { return index != 0; }
    bool restLevelStable(uint8_t index) const override {
        (void)index;
        return true;
    }

private:
    static constexpr uint8_t kQueue = 16;
    static constexpr uint32_t kPollsPerEvent = 200;
    static constexpr uint8_t kPressesPerButton = 3;

    struct Edge {
        uint8_t index;
        bool rising;
    };

    void pushEdge(uint8_t index, bool rising) {
        if (fill_ >= kQueue) {
            return;
        }
        const uint8_t in = static_cast<uint8_t>((out_ + fill_) % kQueue);
        edges_[in].index = index;
        edges_[in].rising = rising;
        ++fill_;
    }

    uint32_t polls_;
    uint8_t cursor_;
    bool pendingRelease_;
    uint32_t press_[kButtonCount];
    Edge edges_[kQueue];
    uint8_t fill_;
    uint8_t out_;
};

class SimWatchdog : public IWatchdog {
public:
    SimWatchdog() : kicks_(0), kicking_(false) {}

    Status begin() override {
        kicking_ = true;
        trace("watchdog: esp_timer periodico de %u ms armado em IO%d",
              static_cast<unsigned>(board::kWdtKickPeriodMs), static_cast<int>(board::kWdi));
        return kOk;
    }

    void kickNow() override { ++kicks_; }

    Status setKicking(bool enable) override {
        kicking_ = enable;
        trace("watchdog: chute %s", enable ? "LIGADO" : "PARADO");
        return kOk;
    }

    bool kicking() const override { return kicking_; }
    uint32_t kickPeriodMs() const override { return board::kWdtKickPeriodMs; }
    uint32_t kickCount() const override { return kicks_; }
    uint32_t minTimeoutMs() const override { return board::kWdtMinTimeoutMs; }
    uint32_t typTimeoutMs() const override { return board::kWdtTypTimeoutMs; }

private:
    uint32_t kicks_;
    bool kicking_;
};

class SimSafeState : public ISafeState {
public:
    SimSafeState(IDigitalOutputBank& relays, IAnalogOutput& ao, IDisplay& display)
        : relays_(relays), ao_(ao), display_(display) {}

    void enterSafeState() override {
        relays_.allOff();
        ao_.zeroAll();
        ao_.setMode(AoMode::Voltage);
        display_.off();
    }

private:
    IDigitalOutputBank& relays_;
    IAnalogOutput& ao_;
    IDisplay& display_;
};

}  // namespace sim
