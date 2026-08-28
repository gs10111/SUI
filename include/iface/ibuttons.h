// Botoes do CN3. IO34/IO35 sao input-only: INPUT_PULLUP e ignorado silenciosamente.
#pragma once

#include <stdint.h>

#include "status.h"

constexpr uint8_t kButtonCount = 3;

class IButtons {
public:
    virtual ~IButtons() = default;
    virtual Status begin() = 0;
    virtual void poll() = 0;
    virtual bool level(uint8_t index) const = 0;
    virtual uint32_t pressCount(uint8_t index) const = 0;
    virtual uint32_t bounceCount(uint8_t index) const = 0;
    virtual bool takeEdge(uint8_t& index, bool& rising) = 0;
    virtual void resetCounts() = 0;
    virtual const char* name(uint8_t index) const = 0;
    virtual bool inputOnly(uint8_t index) const = 0;
    virtual bool restLevelStable(uint8_t index) const = 0;
};
