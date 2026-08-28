// Quadro do jig: STX | 'T' | len | payload | CRC16-MODBUS(lo,hi) | ETX.
// CRC cobre type + len + payload.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace frame {

constexpr uint8_t kStx = 0x02;
constexpr uint8_t kEtx = 0x03;
constexpr uint8_t kType = 'T';
constexpr uint8_t kMaxPayload = 32;
constexpr uint8_t kOverhead = 6;
constexpr uint8_t kMaxFrame = kMaxPayload + kOverhead;

enum class Decode : uint8_t {
    Ok = 0,
    TooShort,
    BadStx,
    BadEtx,
    BadType,
    BadLen,
    BadCrc,
};

uint16_t encode(const uint8_t* payload, uint8_t len, uint8_t* out, uint16_t cap);
Decode decode(const uint8_t* in, uint16_t len, uint8_t* payload, uint8_t cap, uint8_t& outLen);
const char* decodeName(Decode d);

}  // namespace frame
