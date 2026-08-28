// Quadro do jig. CRC16-MODBUS cobre type + len + payload; ordem little-endian no fio.
#include "proto/frame.h"

#include "proto/crc16.h"

namespace frame {

uint16_t encode(const uint8_t* payload, uint8_t len, uint8_t* out, uint16_t cap) {
    if (out == nullptr || len > kMaxPayload) {
        return 0;
    }
    if (len > 0 && payload == nullptr) {
        return 0;
    }
    const uint16_t total = static_cast<uint16_t>(len + kOverhead);
    if (cap < total) {
        return 0;
    }
    uint8_t body[kMaxPayload + 2];
    body[0] = kType;
    body[1] = len;
    for (uint8_t i = 0; i < len; ++i) {
        body[2 + i] = payload[i];
    }
    const uint16_t crc = crc16Modbus(body, static_cast<size_t>(len) + 2u);
    out[0] = kStx;
    out[1] = kType;
    out[2] = len;
    for (uint8_t i = 0; i < len; ++i) {
        out[3 + i] = payload[i];
    }
    out[3 + len] = static_cast<uint8_t>(crc & 0xFFu);
    out[4 + len] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
    out[5 + len] = kEtx;
    return total;
}

Decode decode(const uint8_t* in, uint16_t len, uint8_t* payload, uint8_t cap, uint8_t& outLen) {
    outLen = 0;
    if (in == nullptr || len < kOverhead) {
        return Decode::TooShort;
    }
    if (in[0] != kStx) {
        return Decode::BadStx;
    }
    if (in[1] != kType) {
        return Decode::BadType;
    }
    const uint8_t plen = in[2];
    if (plen > kMaxPayload) {
        return Decode::BadLen;
    }
    const uint16_t total = static_cast<uint16_t>(plen + kOverhead);
    if (len < total) {
        return Decode::TooShort;
    }
    if (in[total - 1] != kEtx) {
        return Decode::BadEtx;
    }
    const uint16_t crc = crc16Modbus(&in[1], static_cast<size_t>(plen) + 2u);
    const uint16_t got = static_cast<uint16_t>(in[3 + plen] | (in[4 + plen] << 8));
    if (crc != got) {
        return Decode::BadCrc;
    }
    if (plen > cap) {
        return Decode::BadLen;
    }
    for (uint8_t i = 0; i < plen; ++i) {
        payload[i] = in[3 + i];
    }
    outLen = plen;
    return Decode::Ok;
}

const char* decodeName(Decode d) {
    switch (d) {
        case Decode::Ok: return "OK";
        case Decode::TooShort: return "TOO_SHORT";
        case Decode::BadStx: return "BAD_STX";
        case Decode::BadEtx: return "BAD_ETX";
        case Decode::BadType: return "BAD_TYPE";
        case Decode::BadLen: return "BAD_LEN";
        case Decode::BadCrc: return "BAD_CRC";
    }
    return "?";
}

}  // namespace frame
