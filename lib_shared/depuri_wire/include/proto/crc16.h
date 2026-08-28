// CRC16-MODBUS (poly 0xA001 refletido, seed 0xFFFF). Usado no quadro do jig e na NVS.
#pragma once

#include <stddef.h>
#include <stdint.h>

uint16_t crc16Modbus(const uint8_t* data, size_t len, uint16_t seed = 0xFFFF);
