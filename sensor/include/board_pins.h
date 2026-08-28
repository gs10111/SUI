// PUSI-DI261930 REV A. Pinos lidos do esquematico DiEletrons (PUSI-DI261930, folha 1/1).
// SCL3300 no VSPI, RS-485 no SN65HVD75DR (VCC 3V3), STWD100 com WDO -> J1 -> EN do ESP32.
#pragma once

#include <stdint.h>

namespace board {

using Pin = int8_t;
constexpr Pin kNoPin = -1;

constexpr bool kPinoutConfirmado = true;

constexpr Pin kSclCs = 5;
constexpr Pin kSclSclk = 18;
constexpr Pin kSclMiso = 19;
constexpr Pin kSclMosi = 23;

constexpr Pin kRs485Rx = 16;
constexpr Pin kRs485Tx = 17;
constexpr Pin kRs485De = 13;

constexpr Pin kWdi = 14;

constexpr Pin kStatusLed = 2;

constexpr uint32_t kConsoleBaud = 115200;
constexpr uint32_t kRs485DefaultBaud = 19200;

constexpr uint32_t kWdtKickPeriodMs = 250;
constexpr uint32_t kWdtMinTimeoutMs = 1120;
constexpr uint32_t kWdtTypTimeoutMs = 1600;
constexpr uint32_t kWdiPulseUs = 5;

constexpr uint8_t kModbusSlaveId = 1;

constexpr bool kRs485TerminatorOnBoard = true;
constexpr bool kRs485ExternalBias = false;
constexpr bool kWatchdogEnablePinWired = false;

}  // namespace board
