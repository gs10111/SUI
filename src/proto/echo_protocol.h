// Protocolo do jig sobre o link RS-485 da folha 1/2 (SN65HVD75DR): quadro STX|'T'|len|payload|CRC16|ETX.
// Payload de angulo: dois int16 little-endian em decimos de grau (X, Y).
#pragma once

#include <stdint.h>

#include "iface/iserial_transport.h"
#include "proto/frame.h"
#include "proto/irs485_protocol.h"
#include "status.h"

class EchoProtocol : public IRs485Protocol {
public:
    static constexpr uint8_t kAnglePayloadLen = 4;
    static constexpr uint16_t kRxCap = frame::kMaxFrame;
    static constexpr uint32_t kDefaultPollTimeoutMs = 20;
    static constexpr uint32_t kEchoServiceTimeoutMs = 5;
    static constexpr uint16_t kEchoChunk = 64;

    EchoProtocol();

    const char* name() const override;
    Status begin(ISerialTransport& transport) override;
    Status request() override;
    bool poll(Angle& out) override;
    void serviceEcho() override;
    bool lastAngle(Angle& out) const override;
    uint32_t framesOk() const override;
    uint32_t framesBad() const override;
    void resetCounters() override;

    void setPollTimeoutMs(uint32_t timeoutMs);
    uint32_t pollTimeoutMs() const { return pollTimeoutMs_; }
    uint32_t counter() const { return counter_; }
    frame::Decode lastDecode() const { return lastDecode_; }
    bool lastEchoMatched() const { return echoMatch_; }
    uint16_t pending() const { return rxLen_; }

private:
    void push(uint8_t value);
    void shift(uint16_t count);
    void resync();
    void noteBad(bool crcError);
    bool consume(Angle& out);

    ISerialTransport* transport_;
    uint32_t framesOk_;
    uint32_t framesBad_;
    uint32_t counter_;
    uint32_t pollTimeoutMs_;
    uint16_t rxLen_;
    frame::Decode lastDecode_;
    bool echoMatch_;
    bool badCounted_;
    uint8_t txPayload_[kAnglePayloadLen];
    uint8_t rx_[kRxCap];
    Angle lastAngle_;
};
