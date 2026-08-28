// Sonda de bring-up do SPI do SCL3300: descobre a fiacao real por forca bruta, sem depender do
// pinout compilado. Bit-bang proprio (modo 0) para nao brigar com o SPIClass ja inicializado.
#pragma once

#include <stdint.h>

namespace spiprobe {

struct Pins {
    int8_t cs;
    int8_t sclk;
    int8_t miso;
    int8_t mosi;
};

constexpr uint8_t kMaxHits = 4;

struct ScanResult {
    Pins hits[kMaxHits];
    uint8_t hitCount;
    uint32_t attempts;
    bool truncated;
};

uint32_t transfer(const Pins& pins, uint32_t out);
bool whoAmIAnswers(const Pins& pins, uint32_t& response);

bool scanMiso(const Pins& base, ScanResult& out);
bool scanAll(ScanResult& out);

void releasePins(const Pins& pins);
void togglePin(int8_t pin, uint32_t periodMs, uint32_t durationMs);

const int8_t* outputCandidates(uint8_t& count);
const int8_t* inputCandidates(uint8_t& count);

}  // namespace spiprobe
