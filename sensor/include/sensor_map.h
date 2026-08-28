// Mapa de registradores publicado pela sensora. Contrato de fio: a supervisora depende desta ordem.
#pragma once

#include <stdint.h>

namespace sensormap {

constexpr uint16_t kRegAngleX = 0;
constexpr uint16_t kRegAngleY = 1;
constexpr uint16_t kRegAngleZ = 2;
constexpr uint16_t kRegStatus = 3;
constexpr uint16_t kRegTempDeciC = 4;
constexpr uint16_t kRegWhoAmI = 5;
constexpr uint16_t kRegFwVersion = 6;
constexpr uint16_t kRegUptimeS = 7;
constexpr uint16_t kRegCount = 8;

}  // namespace sensormap
