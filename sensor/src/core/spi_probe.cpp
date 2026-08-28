// Bit-bang de 32 bits em modo 0 (dado amostrado na subida do SCK), CS alto por 12 us entre quadros,
// como manda o datasheet do SCL3300 (secao 5.1.2). Usada so em bancada, pelo comando 'spiprobe'.
#include "core/spi_probe.h"

#include <Arduino.h>

#include "board_pins.h"
#include "drivers/scl3300_math.h"

namespace spiprobe {

namespace {

constexpr int8_t kOutputPins[] = {2, 4, 5, 12, 15, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
constexpr int8_t kInputOnlyPins[] = {34, 35, 36, 39};

constexpr uint8_t kOutputCount = sizeof(kOutputPins) / sizeof(kOutputPins[0]);
constexpr uint8_t kInputOnlyCount = sizeof(kInputOnlyPins) / sizeof(kInputOnlyPins[0]);
constexpr uint8_t kInputCount = kOutputCount + kInputOnlyCount;

int8_t g_inputPins[kInputCount];
bool g_inputPinsReady = false;

constexpr uint32_t kHalfBitUs = 1;
constexpr uint32_t kCsHighUs = 12;
constexpr uint16_t kWhoAmIData = 0x00C1;

void buildInputPins() {
    if (g_inputPinsReady) {
        return;
    }
    uint8_t n = 0;
    for (uint8_t i = 0; i < kOutputCount; ++i) {
        g_inputPins[n++] = kOutputPins[i];
    }
    for (uint8_t i = 0; i < kInputOnlyCount; ++i) {
        g_inputPins[n++] = kInputOnlyPins[i];
    }
    g_inputPinsReady = true;
}

bool usable(const Pins& p) {
    const int8_t reserved[] = {board::kRs485Rx, board::kRs485Tx, board::kRs485De, board::kWdi, 1, 3};
    const int8_t used[] = {p.cs, p.sclk, p.miso, p.mosi};
    for (uint8_t i = 0; i < 4; ++i) {
        for (uint8_t r = 0; r < sizeof(reserved) / sizeof(reserved[0]); ++r) {
            if (used[i] == reserved[r]) {
                return false;
            }
        }
    }
    if (p.cs == p.sclk || p.cs == p.mosi || p.cs == p.miso) {
        return false;
    }
    if (p.sclk == p.mosi || p.sclk == p.miso) {
        return false;
    }
    if (p.mosi == p.miso) {
        return false;
    }
    return true;
}

void configure(const Pins& p) {
    pinMode(static_cast<uint8_t>(p.cs), OUTPUT);
    digitalWrite(static_cast<uint8_t>(p.cs), HIGH);
    pinMode(static_cast<uint8_t>(p.sclk), OUTPUT);
    digitalWrite(static_cast<uint8_t>(p.sclk), LOW);
    pinMode(static_cast<uint8_t>(p.mosi), OUTPUT);
    digitalWrite(static_cast<uint8_t>(p.mosi), LOW);
    pinMode(static_cast<uint8_t>(p.miso), INPUT);
}

}  // namespace

uint32_t transfer(const Pins& pins, uint32_t out) {
    uint32_t rx = 0;
    digitalWrite(static_cast<uint8_t>(pins.cs), LOW);
    delayMicroseconds(1);
    for (int8_t bit = 31; bit >= 0; --bit) {
        const uint8_t level = static_cast<uint8_t>((out >> bit) & 1u);
        digitalWrite(static_cast<uint8_t>(pins.mosi), level != 0 ? HIGH : LOW);
        delayMicroseconds(kHalfBitUs);
        digitalWrite(static_cast<uint8_t>(pins.sclk), HIGH);
        rx = (rx << 1) | static_cast<uint32_t>(digitalRead(static_cast<uint8_t>(pins.miso)) != LOW ? 1u : 0u);
        delayMicroseconds(kHalfBitUs);
        digitalWrite(static_cast<uint8_t>(pins.sclk), LOW);
    }
    digitalWrite(static_cast<uint8_t>(pins.cs), HIGH);
    delayMicroseconds(kCsHighUs);
    return rx;
}

bool whoAmIAnswers(const Pins& pins, uint32_t& response) {
    response = 0;
    if (!usable(pins)) {
        return false;
    }
    configure(pins);

    transfer(pins, scl::kCmdSwReset);
    delay(scl::kResetSettleMs);
    transfer(pins, scl::kCmdReadWhoAmI);
    const uint32_t second = transfer(pins, scl::kCmdReadWhoAmI);
    response = second;

    if (second == 0x00000000u || second == 0xFFFFFFFFu) {
        return false;
    }
    if (!scl::frameCrcOk(second)) {
        return false;
    }
    return scl::frameData(second) == kWhoAmIData;
}

void releasePins(const Pins& pins) {
    const int8_t list[] = {pins.cs, pins.sclk, pins.miso, pins.mosi};
    for (uint8_t i = 0; i < 4; ++i) {
        if (list[i] >= 0 && list[i] != board::kWdi) {
            pinMode(static_cast<uint8_t>(list[i]), INPUT);
        }
    }
}

bool scanMiso(const Pins& base, ScanResult& out) {
    buildInputPins();
    out.hitCount = 0;
    out.attempts = 0;
    out.truncated = false;
    for (uint8_t i = 0; i < kInputCount; ++i) {
        Pins probe = base;
        probe.miso = g_inputPins[i];
        if (!usable(probe)) {
            continue;
        }
        ++out.attempts;
        uint32_t response = 0;
        if (whoAmIAnswers(probe, response)) {
            if (out.hitCount < kMaxHits) {
                out.hits[out.hitCount++] = probe;
            } else {
                out.truncated = true;
            }
        }
        releasePins(probe);
    }
    return out.hitCount > 0;
}

bool scanAll(ScanResult& out) {
    buildInputPins();
    out.hitCount = 0;
    out.attempts = 0;
    out.truncated = false;
    for (uint8_t c = 0; c < kOutputCount; ++c) {
        for (uint8_t s = 0; s < kOutputCount; ++s) {
            for (uint8_t m = 0; m < kOutputCount; ++m) {
                for (uint8_t i = 0; i < kInputCount; ++i) {
                    Pins probe = {kOutputPins[c], kOutputPins[s], g_inputPins[i], kOutputPins[m]};
                    if (!usable(probe)) {
                        continue;
                    }
                    ++out.attempts;
                    uint32_t response = 0;
                    if (whoAmIAnswers(probe, response)) {
                        if (out.hitCount < kMaxHits) {
                            out.hits[out.hitCount++] = probe;
                        } else {
                            out.truncated = true;
                        }
                    }
                    releasePins(probe);
                    if (out.hitCount >= kMaxHits) {
                        return true;
                    }
                }
            }
        }
    }
    return out.hitCount > 0;
}

void togglePin(int8_t pin, uint32_t periodMs, uint32_t durationMs) {
    if (pin < 0 || pin == board::kWdi) {
        return;
    }
    pinMode(static_cast<uint8_t>(pin), OUTPUT);
    const uint32_t start = millis();
    bool level = false;
    while ((millis() - start) < durationMs) {
        level = !level;
        digitalWrite(static_cast<uint8_t>(pin), level ? HIGH : LOW);
        delay(periodMs / 2u);
    }
    digitalWrite(static_cast<uint8_t>(pin), LOW);
    pinMode(static_cast<uint8_t>(pin), INPUT);
}

const int8_t* outputCandidates(uint8_t& count) {
    count = kOutputCount;
    return kOutputPins;
}

const int8_t* inputCandidates(uint8_t& count) {
    buildInputPins();
    count = kInputCount;
    return g_inputPins;
}

}  // namespace spiprobe
