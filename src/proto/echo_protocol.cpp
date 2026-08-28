// Quadro do jig sobre RS-485 half-duplex (folha 1/2): encode/decode em proto/frame.h, CRC16-MODBUS.
// Sem eco local no transceptor SN65HVD75DR: so chega o que o outro lado devolve.
#include "proto/echo_protocol.h"

namespace {

constexpr float kDeciDegreeToDegree = 0.1f;

int16_t le16(const uint8_t* p) {
    const uint16_t raw = static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                               static_cast<uint16_t>(p[1] << 8));
    return static_cast<int16_t>(raw);
}

}  // namespace

EchoProtocol::EchoProtocol()
    : transport_(nullptr),
      framesOk_(0),
      framesBad_(0),
      counter_(0),
      pollTimeoutMs_(kDefaultPollTimeoutMs),
      rxLen_(0),
      lastDecode_(frame::Decode::Ok),
      echoMatch_(false),
      badCounted_(false),
      txPayload_{0, 0, 0, 0},
      rx_{},
      lastAngle_{0.0f, 0.0f, false} {}

const char* EchoProtocol::name() const {
    return "echo";
}

Status EchoProtocol::begin(ISerialTransport& transport) {
    transport_ = &transport;
    rxLen_ = 0;
    echoMatch_ = false;
    lastDecode_ = frame::Decode::Ok;
    return kOk;
}

void EchoProtocol::setPollTimeoutMs(uint32_t timeoutMs) {
    pollTimeoutMs_ = timeoutMs;
}

void EchoProtocol::resetCounters() {
    lastAngle_.valid = false;
    framesOk_ = 0;
    framesBad_ = 0;
    rxLen_ = 0;
    echoMatch_ = false;
    lastDecode_ = frame::Decode::Ok;
}

bool EchoProtocol::lastAngle(Angle& out) const {
    out = lastAngle_;
    return lastAngle_.valid;
}

uint32_t EchoProtocol::framesOk() const {
    return framesOk_;
}

uint32_t EchoProtocol::framesBad() const {
    return framesBad_;
}

Status EchoProtocol::request() {
    if (transport_ == nullptr) {
        return Err::NotInit;
    }
    ++counter_;
    txPayload_[0] = static_cast<uint8_t>(counter_ & 0xFFu);
    txPayload_[1] = static_cast<uint8_t>((counter_ >> 8) & 0xFFu);
    txPayload_[2] = static_cast<uint8_t>((counter_ >> 16) & 0xFFu);
    txPayload_[3] = static_cast<uint8_t>((counter_ >> 24) & 0xFFu);

    uint8_t tx[frame::kMaxFrame];
    const uint16_t total = frame::encode(txPayload_, kAnglePayloadLen, tx, kRxCap);
    if (total == 0) {
        return Err::Io;
    }
    rxLen_ = 0;
    echoMatch_ = false;
    return transport_->write(tx, total);
}

void EchoProtocol::push(uint8_t value) {
    if (rxLen_ == 0 && value != frame::kStx) {
        return;
    }
    if (rxLen_ >= kRxCap) {
        rxLen_ = 0;
        if (value != frame::kStx) {
            return;
        }
    }
    rx_[rxLen_] = value;
    ++rxLen_;
}

void EchoProtocol::shift(uint16_t count) {
    if (count >= rxLen_) {
        rxLen_ = 0;
        return;
    }
    const uint16_t rest = static_cast<uint16_t>(rxLen_ - count);
    for (uint16_t i = 0; i < rest; ++i) {
        rx_[i] = rx_[i + count];
    }
    rxLen_ = rest;
}

void EchoProtocol::resync() {
    uint16_t start = 1;
    while (start < rxLen_ && rx_[start] != frame::kStx) {
        ++start;
    }
    shift(start);
}

void EchoProtocol::noteBad(bool crcError) {
    if (badCounted_) {
        return;
    }
    badCounted_ = true;
    ++framesBad_;
    if (crcError) {
        transport_->noteCrcError();
    } else {
        transport_->noteTimeout();
    }
}

bool EchoProtocol::consume(Angle& out) {
    while (rxLen_ >= 3) {
        if (rx_[1] != frame::kType) {
            lastDecode_ = frame::Decode::BadType;
            noteBad(false);
            resync();
            continue;
        }
        const uint8_t plen = rx_[2];
        if (plen > frame::kMaxPayload) {
            lastDecode_ = frame::Decode::BadLen;
            noteBad(false);
            resync();
            continue;
        }
        const uint16_t total = static_cast<uint16_t>(plen + frame::kOverhead);
        if (rxLen_ < total) {
            return false;
        }
        uint8_t payload[frame::kMaxPayload];
        uint8_t payloadLen = 0;
        const frame::Decode decoded =
            frame::decode(rx_, total, payload, frame::kMaxPayload, payloadLen);
        lastDecode_ = decoded;
        if (decoded != frame::Decode::Ok) {
            noteBad(decoded == frame::Decode::BadCrc);
            resync();
            continue;
        }
        shift(total);
        ++framesOk_;
        transport_->noteFrameOk();
        echoMatch_ = false;
        if (payloadLen != kAnglePayloadLen) {
            return false;
        }
        echoMatch_ = (payload[0] == txPayload_[0]) && (payload[1] == txPayload_[1]) &&
                     (payload[2] == txPayload_[2]) && (payload[3] == txPayload_[3]);
        out.x = static_cast<float>(le16(&payload[0])) * kDeciDegreeToDegree;
        out.y = static_cast<float>(le16(&payload[2])) * kDeciDegreeToDegree;
        out.valid = true;
        lastAngle_ = out;
        return true;
    }
    return false;
}

bool EchoProtocol::poll(Angle& out) {
    out.x = 0.0f;
    out.y = 0.0f;
    out.valid = false;
    if (transport_ == nullptr) {
        return false;
    }
    badCounted_ = false;

    uint8_t chunk[frame::kMaxFrame];
    const uint16_t got = transport_->read(chunk, kRxCap, pollTimeoutMs_);
    if (got == 0) {
        return false;
    }
    for (uint16_t i = 0; i < got; ++i) {
        push(chunk[i]);
    }
    return consume(out);
}

void EchoProtocol::serviceEcho() {
    if (transport_ == nullptr) {
        return;
    }
    uint8_t buf[kEchoChunk];
    const uint16_t got = transport_->read(buf, kEchoChunk, kEchoServiceTimeoutMs);
    if (got == 0) {
        return;
    }
    transport_->write(buf, got);
}
