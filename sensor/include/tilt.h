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
// STO (Self-Test Output) fora da faixa da Tabela 23 do datasheet por kStoFaultRun leituras
// CONSECUTIVAS. A secao 6.2 manda ler o STO continuamente depois de cada leitura XYZ e contar
// eventos subsequentes acima do limiar - uma amostra isolada nao e falha ("exceeds the threshold
// level continuously ... in static condition"), e por isso o bit so sobe com a rajada inteira.
// Sobe SEPARADO do autoteste porque a causa e outra: o autoteste reprova por registrador de
// erro, este bit reprova pela propria medida do elemento sensor.
constexpr uint16_t kStsStoOutOfRange = 0x0080;
