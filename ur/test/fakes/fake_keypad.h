// Teclado de teste: o roteiro de bordas e escrito pelo teste, o tempo vem do FakeClock.
//
// Substituivel pelo ButtonMonitor real sem que o dominio perceba (LSP): mesma fila de 16
// bordas, mesmo descarte por fila cheia contado em droppedEvents(), mesmo heldMs valido
// somente em Release, e o mesmo hasInternalPullup() falso para DOWN e MENU, que sao
// input-only na placa. Um fake mais permissivo que o alvo esconderia justamente os defeitos
// que o alvo produz.
//
// press() e release() usam o instante corrente do relogio injetado. E assim que o teste
// escreve gesto: avanca o relogio, prensa, avanca, solta - sem carimbar instante na mao e
// sem poder produzir um roteiro que o hardware nao geraria.
#pragma once

#include <stdint.h>

#include "fakes/fake_clock.h"
#include "ports/i_keypad.h"

namespace test {

class FakeKeypad : public IKeypad {
public:
    static constexpr uint8_t kQueueCap = 16;
    static constexpr uint16_t kDebounceMs = 20;

    explicit FakeKeypad(FakeClock& clock)
        : clock_(clock), head_(0), count_(0), dropped_(0), begun_(false) {
        for (uint8_t i = 0; i < kKeyCount; ++i) {
            down_[i] = false;
            sinceMs_[i] = 0;
            bounces_[i] = 0;
        }
    }

    // --- roteiro do teste ---

    void press(Key key) {
        const uint8_t i = index(key);
        if (down_[i]) {
            ++bounces_[i];
            return;
        }
        down_[i] = true;
        sinceMs_[i] = clock_.nowMs();
        push(KeyEvent{key, KeyEdge::Press, clock_.nowMs(), 0});
    }

    void release(Key key) {
        const uint8_t i = index(key);
        if (!down_[i]) {
            ++bounces_[i];
            return;
        }
        const uint32_t held = elapsedMs(sinceMs_[i], clock_.nowMs());
        down_[i] = false;
        sinceMs_[i] = 0;
        push(KeyEvent{key, KeyEdge::Release, clock_.nowMs(),
                      static_cast<uint16_t>(held > 65535u ? 65535u : held)});
    }

    // Toque completo de duracao dada, avancando o relogio.
    void tap(Key key, uint32_t heldMs = 60u) {
        press(key);
        clock_.advanceMs(heldMs);
        release(key);
    }

    // --- IKeypad ---

    Status begin() override {
        begun_ = true;
        return kOk;
    }

    void poll() override {}

    bool takeEvent(KeyEvent& out) override {
        if (count_ == 0) {
            return false;
        }
        out = queue_[head_];
        head_ = static_cast<uint8_t>((head_ + 1) % kQueueCap);
        --count_;
        return true;
    }

    bool pressed(Key key) const override { return down_[index(key)]; }

    uint8_t pressedMask() const override {
        uint8_t mask = 0;
        for (uint8_t i = 0; i < kKeyCount; ++i) {
            if (down_[i]) {
                mask = static_cast<uint8_t>(mask | (1u << i));
            }
        }
        return mask;
    }

    uint32_t pressedForMs(Key key) const override {
        const uint8_t i = index(key);
        return down_[i] ? elapsedMs(sinceMs_[i], clock_.nowMs()) : 0u;
    }

    void flush() override {
        head_ = 0;
        count_ = 0;
    }

    uint16_t debounceMs() const override { return kDebounceMs; }

    // Espelha a placa: so o UP (IO15) tem pull-up interno.
    bool hasInternalPullup(Key key) const override { return key == Key::Up; }

    uint32_t bounceCount(Key key) const override { return bounces_[index(key)]; }
    uint32_t droppedEvents() const override { return dropped_; }

    void resetCounters() override {
        dropped_ = 0;
        for (uint8_t i = 0; i < kKeyCount; ++i) {
            bounces_[i] = 0;
        }
    }

    const char* keyName(Key key) const override {
        switch (key) {
            case Key::Menu: return "MENU";
            case Key::Up: return "UP";
            case Key::Down: return "DOWN";
        }
        return "?";
    }

    bool begun() const { return begun_; }
    uint8_t queued() const { return count_; }

private:
    static uint8_t index(Key key) { return static_cast<uint8_t>(key); }

    void push(const KeyEvent& event) {
        if (count_ >= kQueueCap) {
            ++dropped_;
            return;
        }
        queue_[(head_ + count_) % kQueueCap] = event;
        ++count_;
    }

    FakeClock& clock_;
    KeyEvent queue_[kQueueCap];
    uint8_t head_;
    uint8_t count_;
    uint32_t dropped_;
    bool down_[kKeyCount];
    uint32_t sinceMs_[kKeyCount];
    uint32_t bounces_[kKeyCount];
    bool begun_;
};

}  // namespace test
