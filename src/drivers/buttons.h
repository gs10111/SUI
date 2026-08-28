// Botoes UP/DOWN/MENU da IHM no CN3 (folha 1/2), assumidos ativos em nivel BAIXO.
// IO34/IO35 sao input-only: INPUT_PULLUP e ignorado, o pull-up tem de vir da IHM.
#pragma once

#include <stdint.h>

#include "board_pins.h"
#include "iface/ibuttons.h"
#include "status.h"

constexpr bool kBtnActiveLevel = false;
constexpr bool kBtnRestLevel = !kBtnActiveLevel;
constexpr uint32_t kBtnDebounceMs = 20;
constexpr uint32_t kBtnRestWindowMs = 1000;
constexpr uint8_t kBtnRestNoiseLimit = 3;
constexpr uint8_t kBtnEdgeQueueLen = 16;

struct ButtonEdge {
    uint8_t index;
    bool rising;
};

class ButtonMonitor : public IButtons {
public:
    ButtonMonitor();

    Status begin() override;
    void poll() override;
    bool level(uint8_t index) const override;
    uint32_t pressCount(uint8_t index) const override;
    uint32_t bounceCount(uint8_t index) const override;
    bool takeEdge(uint8_t& index, bool& rising) override;
    void resetCounts() override;
    const char* name(uint8_t index) const override;
    bool inputOnly(uint8_t index) const override;
    bool restLevelStable(uint8_t index) const override;

    bool pressed(uint8_t index) const;
    board::Pin pin(uint8_t index) const;
    uint32_t restNoiseCount(uint8_t index) const;
    uint32_t edgeOverflowCount() const { return edgeOverflow_; }
    uint32_t debounceMs() const { return kBtnDebounceMs; }
    uint8_t pendingEdges() const { return fill_; }
    bool ready() const { return ready_; }

private:
    static bool indexOk(uint8_t index);
    void pushEdge(uint8_t index, bool rising);
    void noteRestTransition(uint8_t index, uint32_t nowMs);

    bool stable_[kButtonCount];
    bool raw_[kButtonCount];
    uint32_t changeMs_[kButtonCount];
    uint32_t press_[kButtonCount];
    uint32_t bounce_[kButtonCount];
    uint32_t restNoise_[kButtonCount];
    uint32_t restWindowMs_[kButtonCount];
    uint8_t restWindowHits_[kButtonCount];
    bool restNoisy_[kButtonCount];
    ButtonEdge queue_[kBtnEdgeQueueLen];
    uint8_t head_;
    uint8_t tail_;
    uint8_t fill_;
    uint32_t edgeOverflow_;
    bool ready_;
};
