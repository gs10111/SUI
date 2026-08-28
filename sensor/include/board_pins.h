// PUSI-DI261930 REV A. ATENCAO: o esquematico desta placa nao foi fornecido.
// Todos os pinos abaixo sao PENDENTES de confirmacao e estao reunidos aqui para troca em um lugar so.
#pragma once

#include <stdint.h>

namespace board {

using Pin = int8_t;
constexpr Pin kNoPin = -1;

constexpr bool kPinoutConfirmado = false;

constexpr Pin kSclCs = 5;
constexpr Pin kSclSclk = 18;
constexpr Pin kSclMiso = 19;
constexpr Pin kSclMosi = 23;

constexpr Pin kRs485Rx = 16;
constexpr Pin kRs485Tx = 17;
constexpr Pin kRs485De = 4;

constexpr Pin kWdi = 13;

constexpr Pin kStatusLed = 2;

constexpr uint32_t kConsoleBaud = 115200;
constexpr uint32_t kRs485DefaultBaud = 19200;

constexpr uint32_t kWdtKickPeriodMs = 250;
constexpr uint32_t kWdtMinTimeoutMs = 1120;
constexpr uint32_t kWdtTypTimeoutMs = 1600;
constexpr uint32_t kWdiPulseUs = 5;

constexpr uint8_t kModbusSlaveId = 1;

}  // namespace board
