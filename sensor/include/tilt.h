// Leitura de inclinacao publicada pela PUSI-DI261930. Decimos de grau para caber em int16 no fio.
#pragma once

#include <stdint.h>

struct Tilt {
    int16_t xDeci;
    int16_t yDeci;
    int16_t zDeci;
    int16_t tempDeciC;
    uint16_t status;
    bool valid;
};

constexpr uint16_t kStsDataValid = 0x0001;
constexpr uint16_t kStsSclCrcError = 0x0002;
constexpr uint16_t kStsSclStartup = 0x0004;
constexpr uint16_t kStsSclSelfTestFail = 0x0008;
constexpr uint16_t kStsSclNotResponding = 0x0010;
constexpr uint16_t kStsSaturated = 0x0020;
constexpr uint16_t kStsWdtReset = 0x0040;
