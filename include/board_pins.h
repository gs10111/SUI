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

constexpr float kDacFullScaleV = 5.0f;
constexpr float kXtrRGainOhms = 10000.0f;
constexpr float kXtrRSetNominalOhms = 2500.0f;
constexpr float kXtrCurrentMirrorRatio = 10.0f;

constexpr uint32_t kWdtKickPeriodMs = 250;
constexpr uint32_t kWdtMinTimeoutMs = 1120;
constexpr uint32_t kWdtTypTimeoutMs = 1600;
constexpr uint32_t kWdiPulseUs = 5;

}  // namespace board
