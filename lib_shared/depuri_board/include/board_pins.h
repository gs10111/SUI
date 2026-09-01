// DE-PURI-DI261924 REV A - folhas 1/2 e 2/2. Fonte unica de verdade de pinos.
#pragma once

#include <stdint.h>

namespace board {

using Pin = int8_t;
constexpr Pin kNoPin = -1;

constexpr Pin kDacSync = 12;
constexpr Pin kDacMosi = 13;
constexpr Pin kDacSclk = 21;
constexpr Pin kDacMiso = 36;

constexpr Pin kXtrOpMode = 22;

constexpr Pin kDispMosi = 23;
constexpr Pin kDispSclk = 18;
constexpr Pin kDispDc = 4;
constexpr Pin kDispReset = 27;
constexpr Pin kDispCs = 5;
constexpr Pin kDispMiso = 39;

constexpr Pin kLedTest = 2;

constexpr Pin kBtnUp = 15;
constexpr Pin kBtnDown = 34;
constexpr Pin kBtnMenu = 35;

constexpr Pin kLim1 = 32;
constexpr Pin kLim2 = 26;
constexpr Pin kLim3 = 25;
constexpr Pin kLim4 = 33;

constexpr Pin kRs485Rx = 16;
constexpr Pin kRs485Tx = 17;
constexpr Pin kRs485De = 14;

constexpr Pin kWdi = 19;

constexpr Pin kFreeTestpoint = 0;

constexpr Pin kStrappingPins[] = {0, 2, 5, 12, 15};
constexpr uint8_t kStrappingCount = sizeof(kStrappingPins) / sizeof(kStrappingPins[0]);

constexpr uint8_t kRelayCount = 4;
constexpr Pin kRelayPins[kRelayCount] = {kLim1, kLim2, kLim3, kLim4};

struct RelayMap {
    const char* net;
    Pin pin;
    const char* relay;
    const char* screwTerminals;
    const char* jumper;
    const char* ihmLedLabel;
};

constexpr RelayMap kRelayMap[kRelayCount] = {
    {"LIM1", kLim1, "RL5", "CN1D/CN1E", "J10", "CN3-6 (serigrafia \"LED LIM3\")"},
    {"LIM2", kLim2, "RL4", "CN1F/CN1G", "J9", "CN3-8 (serigrafia \"LED LIM1\")"},
    {"LIM3", kLim3, "RL3", "CN1H/CN1I", "J8", "CN3-7 (serigrafia \"LED LIM2\")"},
    {"LIM4", kLim4, "RL2", "CN1J/CN1K", "J2", "CN3-5 (serigrafia \"LED LIM4\")"},
};

constexpr uint8_t kAxisCount = 2;
constexpr const char* kAxisName[kAxisCount] = {"X", "Y"};
constexpr const char* kAxisTerminals[kAxisCount] = {"CN1L(+)/CN1M(-)", "CN1N(+)/CN1O(-)"};

constexpr uint32_t kConsoleBaud = 115200;
constexpr uint32_t kDacSpiDefaultHz = 1000000;
constexpr uint32_t kDacSpiMinHz = 100000;
constexpr uint32_t kDacSpiMaxHz = 10000000;
constexpr uint32_t kDisplaySpiHz = 4000000;
constexpr uint32_t kRs485DefaultBaud = 19200;

// Cadeia analogica da folha 2/2, na configuracao de saida bidirecional do XTR300
// (TI SBOS336C figura 2 e equacoes 2 e 3): o pino SET recebe R_OS = R12/R25 = 1K vindo
// do VREF de 2,5 V do DAC8562, R_GAIN = R17/R29 = 10K entre RG1 e RG2, e NAO existe R_SET.
//   V_OUT = (R_GAIN / 2) * (V_DAC - V_REF) / R_OS = 5 * (V_DAC - 2,5 V)
//   I_OUT = 10 * (V_DAC - V_REF) / R_OS         = 10 mA/V * (V_DAC - 2,5 V)
// Com V_DAC = 5,0 V * D / 65536, a saida e bipolar e o zero cai no meio da escala do DAC.
constexpr float kDacFullScaleV = 5.0f;
constexpr float kXtrRGainOhms = 10000.0f;
constexpr float kXtrROsOhms = 1000.0f;
constexpr float kXtrVrefV = 2.5f;
constexpr float kXtrCurrentMirrorRatio = 10.0f;

// Codigo de saida nula. 0x0000 NAO e zero nesta placa: vale -12,5 V, saturado pelo trilho
// de -15 V. Escrever 0x0000 no boot, na troca de modo ou no estado seguro coloca as duas
// saidas no fundo de escala negativo, que o sistema a jusante le como inclinacao maxima.
constexpr uint16_t kDacZeroCode = 0x8000;

// Limites uteis publicados no manual (secao 5.7): a saida satura em +/-10,00 V, bem antes
// do limite eletrico de +/-12 V do XTR300 com trilhos de +/-15 V.
constexpr float kXtrOutputLimitV = 10.0f;
constexpr uint16_t kDacMinUsefulCode = 6554;   // -10,00 V
constexpr uint16_t kDacMaxUsefulCode = 58982;  // +10,00 V

constexpr uint32_t kWdtKickPeriodMs = 250;
constexpr uint32_t kWdtMinTimeoutMs = 1120;
constexpr uint32_t kWdtTypTimeoutMs = 1600;
constexpr uint32_t kWdiPulseUs = 5;

}  // namespace board
