// ButtonMonitor: UP=IO15 (pull-up interno), DOWN=IO34 e MENU=IO35 (input-only) no CN3, folha 1/2.
// Debounce de 20 ms nao bloqueante; level()/takeEdge() usam o nivel eletrico do pino (HIGH = repouso).
#include "drivers/buttons.h"

#include <Arduino.h>

namespace {

struct ButtonDesc {
    const char* label;
    board::Pin pin;
    bool inputOnly;
};

constexpr ButtonDesc kButtons[kButtonCount] = {
    {"UP", board::kBtnUp, false},
    {"DOWN", board::kBtnDown, true},
    {"MENU", board::kBtnMenu, true},
};

}  // namespace

ButtonMonitor::ButtonMonitor()
    : stable_{},
      raw_{},
      changeMs_{},
      press_{},
      bounce_{},
      restNoise_{},
      restWindowMs_{},
      restWindowHits_{},
      restNoisy_{},
      queue_{},
      head_(0),
      tail_(0),
      fill_(0),
      edgeOverflow_(0),
      ready_(false) {
    for (uint8_t i = 0; i < kButtonCount; ++i) {
        stable_[i] = kBtnRestLevel;
        raw_[i] = kBtnRestLevel;
    }
}

bool ButtonMonitor::indexOk(uint8_t index) {
    return index < kButtonCount;
}

Status ButtonMonitor::begin() {
    const uint32_t nowMs = static_cast<uint32_t>(millis());
    for (uint8_t i = 0; i < kButtonCount; ++i) {
        const uint8_t pinNum = static_cast<uint8_t>(kButtons[i].pin);
        if (kButtons[i].inputOnly) {
            pinMode(pinNum, INPUT);
        } else {
            pinMode(pinNum, INPUT_PULLUP);
        }
        const bool rawHigh = digitalRead(pinNum) != LOW;
        stable_[i] = rawHigh;
        raw_[i] = rawHigh;
        changeMs_[i] = nowMs;
        press_[i] = 0;
        bounce_[i] = 0;
        restNoise_[i] = 0;
        restWindowMs_[i] = nowMs;
        restWindowHits_[i] = 0;
        restNoisy_[i] = false;
    }
    head_ = 0;
    tail_ = 0;
    fill_ = 0;
    edgeOverflow_ = 0;
    ready_ = true;
    return kOk;
}

void ButtonMonitor::pushEdge(uint8_t index, bool rising) {
    if (fill_ >= kBtnEdgeQueueLen) {
        tail_ = static_cast<uint8_t>((tail_ + 1) % kBtnEdgeQueueLen);
        fill_ = static_cast<uint8_t>(fill_ - 1);
        edgeOverflow_++;
    }
    queue_[head_].index = index;
    queue_[head_].rising = rising;
    head_ = static_cast<uint8_t>((head_ + 1) % kBtnEdgeQueueLen);
    fill_ = static_cast<uint8_t>(fill_ + 1);
}

void ButtonMonitor::noteRestTransition(uint8_t index, uint32_t nowMs) {
    if ((nowMs - restWindowMs_[index]) >= kBtnRestWindowMs) {
        restWindowMs_[index] = nowMs;
        restWindowHits_[index] = 0;
    }
    if (restWindowHits_[index] < 0xFF) {
        restWindowHits_[index] = static_cast<uint8_t>(restWindowHits_[index] + 1);
    }
    restNoise_[index]++;
    if (restWindowHits_[index] >= kBtnRestNoiseLimit) {
        restNoisy_[index] = true;
    }
}

void ButtonMonitor::poll() {
    if (!ready_) {
        return;
    }
    const uint32_t nowMs = static_cast<uint32_t>(millis());
    for (uint8_t i = 0; i < kButtonCount; ++i) {
        const bool rawHigh = digitalRead(static_cast<uint8_t>(kButtons[i].pin)) != LOW;
        if (rawHigh != raw_[i]) {
            raw_[i] = rawHigh;
            changeMs_[i] = nowMs;
            if (rawHigh == stable_[i]) {
                bounce_[i]++;
            }
            if (stable_[i] == kBtnRestLevel) {
                noteRestTransition(i, nowMs);
            }
            continue;
        }
        if (rawHigh != stable_[i] && (nowMs - changeMs_[i]) >= kBtnDebounceMs) {
            stable_[i] = rawHigh;
            pushEdge(i, rawHigh);
            if (rawHigh == kBtnActiveLevel) {
                press_[i]++;
            }
        }
    }
}

bool ButtonMonitor::level(uint8_t index) const {
    if (!indexOk(index)) {
        return kBtnRestLevel;
    }
    return stable_[index];
}

bool ButtonMonitor::pressed(uint8_t index) const {
    if (!indexOk(index)) {
        return false;
    }
    return stable_[index] == kBtnActiveLevel;
}

uint32_t ButtonMonitor::pressCount(uint8_t index) const {
    if (!indexOk(index)) {
        return 0;
    }
    return press_[index];
}

uint32_t ButtonMonitor::bounceCount(uint8_t index) const {
    if (!indexOk(index)) {
        return 0;
    }
    return bounce_[index];
}

uint32_t ButtonMonitor::restNoiseCount(uint8_t index) const {
    if (!indexOk(index)) {
        return 0;
    }
    return restNoise_[index];
}

bool ButtonMonitor::takeEdge(uint8_t& index, bool& rising) {
    if (fill_ == 0) {
        return false;
    }
    index = queue_[tail_].index;
    rising = queue_[tail_].rising;
    tail_ = static_cast<uint8_t>((tail_ + 1) % kBtnEdgeQueueLen);
    fill_ = static_cast<uint8_t>(fill_ - 1);
    return true;
}

void ButtonMonitor::resetCounts() {
    const uint32_t nowMs = static_cast<uint32_t>(millis());
    for (uint8_t i = 0; i < kButtonCount; ++i) {
        press_[i] = 0;
        bounce_[i] = 0;
        restNoise_[i] = 0;
        restWindowMs_[i] = nowMs;
        restWindowHits_[i] = 0;
        restNoisy_[i] = false;
    }
    head_ = 0;
    tail_ = 0;
    fill_ = 0;
    edgeOverflow_ = 0;
}

const char* ButtonMonitor::name(uint8_t index) const {
    if (!indexOk(index)) {
        return "?";
    }
    return kButtons[index].label;
}

bool ButtonMonitor::inputOnly(uint8_t index) const {
    if (!indexOk(index)) {
        return false;
    }
    return kButtons[index].inputOnly;
}

board::Pin ButtonMonitor::pin(uint8_t index) const {
    if (!indexOk(index)) {
        return board::kNoPin;
    }
    return kButtons[index].pin;
}

bool ButtonMonitor::restLevelStable(uint8_t index) const {
    if (!indexOk(index)) {
        return false;
    }
    return !restNoisy_[index];
}
