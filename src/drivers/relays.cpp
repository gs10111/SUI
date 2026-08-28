// RelayBank: LIM1..LIM4 nos bornes CN1D..CN1K (folha 2/2), jumpers J10/J9/J8/J2.
// Nivel ALTO satura o BC337 e energiza a bobina do AX1RC-5V; nivel BAIXO e o estado seguro.
#include "drivers/relays.h"

#include <Arduino.h>

RelayBank::RelayBank() : state_{}, ready_(false) {}

bool RelayBank::indexOk(uint8_t index) {
    return index < board::kRelayCount;
}

Status RelayBank::begin() {
    for (uint8_t i = 0; i < board::kRelayCount; ++i) {
        const uint8_t pinNum = static_cast<uint8_t>(board::kRelayPins[i]);
        digitalWrite(pinNum, LOW);
        pinMode(pinNum, OUTPUT);
        digitalWrite(pinNum, LOW);
        state_[i] = false;
    }
    ready_ = true;
    return kOk;
}

uint8_t RelayBank::count() const {
    return board::kRelayCount;
}

Status RelayBank::set(uint8_t index, bool on) {
    if (!indexOk(index)) {
        return Status(Err::Param);
    }
    if (!ready_) {
        return Status(Err::NotInit);
    }
    digitalWrite(static_cast<uint8_t>(board::kRelayPins[index]), on ? HIGH : LOW);
    state_[index] = on;
    return kOk;
}

Status RelayBank::get(uint8_t index, bool& on) const {
    if (!indexOk(index)) {
        return Status(Err::Param);
    }
    if (!ready_) {
        return Status(Err::NotInit);
    }
    on = state_[index];
    return kOk;
}

Status RelayBank::writeAll(bool on) {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    for (uint8_t i = 0; i < board::kRelayCount; ++i) {
        digitalWrite(static_cast<uint8_t>(board::kRelayPins[i]), on ? HIGH : LOW);
        state_[i] = on;
    }
    return kOk;
}

Status RelayBank::allOff() {
    return writeAll(false);
}

Status RelayBank::allOn() {
    return writeAll(true);
}

const board::RelayMap& RelayBank::info(uint8_t index) const {
    const uint8_t safe = indexOk(index) ? index : 0;
    return board::kRelayMap[safe];
}

board::Pin RelayBank::pin(uint8_t index) const {
    if (!indexOk(index)) {
        return board::kNoPin;
    }
    return board::kRelayPins[index];
}
