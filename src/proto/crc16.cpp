// CRC16-MODBUS. Mesma funcao usada no quadro do jig, no Modbus RTU e no registro de calibracao.
#include "proto/crc16.h"

uint16_t crc16Modbus(const uint8_t* data, size_t len, uint16_t seed) {
    uint16_t crc = seed;
    if (data == nullptr) {
        return crc;
    }
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001u) {
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001u);
            } else {
                crc = static_cast<uint16_t>(crc >> 1);
            }
        }
    }
    return crc;
}
