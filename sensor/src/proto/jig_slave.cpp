// PUSI-DI261930: responde ao quadro do jig com X e Y em decimos de grau, dois int16 little-endian.
// Codigo puro de host: sem Arduino, sem SPI, sem tempo; encode/decode e CRC vem de depuri_wire.
#include "proto/jig_slave.h"

#include "sensor_map.h"

namespace {

void writeLe16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value & 0xFFu);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

}  // namespace

JigFrameSlave::JigFrameSlave()
    : requests_(0),
      responses_(0),
      badFrames_(0),
      lastDecode_(frame::Decode::Ok),
      lastRequestLen_(0) {}

const char* JigFrameSlave::name() const {
    return "jig-frame";
}

void JigFrameSlave::reset() {
    requests_ = 0;
    responses_ = 0;
    badFrames_ = 0;
    lastDecode_ = frame::Decode::Ok;
    lastRequestLen_ = 0;
}

uint32_t JigFrameSlave::requests() const {
    return requests_;
}

uint32_t JigFrameSlave::responses() const {
    return responses_;
}

uint32_t JigFrameSlave::badFrames() const {
    return badFrames_;
}

uint16_t JigFrameSlave::handle(const uint8_t* request, uint16_t len, const uint16_t* registers,
                               uint16_t registerCount, uint8_t* response, uint16_t cap) {
    if (request == nullptr || response == nullptr) {
        return 0;
    }
    ++requests_;

    uint8_t payload[frame::kMaxPayload];
    uint8_t payloadLen = 0;
    const frame::Decode decoded =
        frame::decode(request, len, payload, frame::kMaxPayload, payloadLen);
    lastDecode_ = decoded;
    if (decoded != frame::Decode::Ok) {
        lastRequestLen_ = 0;
        ++badFrames_;
        return 0;
    }
    lastRequestLen_ = payloadLen;

    if (registers == nullptr || registerCount <= sensormap::kRegAngleY) {
        return 0;
    }

    uint8_t out[kAnglePayloadLen];
    writeLe16(&out[0], registers[sensormap::kRegAngleX]);
    writeLe16(&out[2], registers[sensormap::kRegAngleY]);

    const uint16_t total = frame::encode(out, kAnglePayloadLen, response, cap);
    if (total == 0) {
        return 0;
    }
    ++responses_;
    return total;
}
