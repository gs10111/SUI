// PUSI-DI261930: escravo do quadro do jig da supervisora DE-PURI-DI261924, usado so em bancada.
// Quadro STX | 'T' | len | payload | CRC16-MODBUS | ETX, conforme lib_shared/depuri_wire proto/frame.h.
#pragma once

#include <stdint.h>

#include "iface/islave_protocol.h"
#include "proto/frame.h"

class JigFrameSlave : public ISlaveProtocol {
public:
    static constexpr uint8_t kAnglePayloadLen = 4;
    static constexpr uint16_t kResponseLen =
        static_cast<uint16_t>(kAnglePayloadLen + frame::kOverhead);

    JigFrameSlave();

    const char* name() const override;
    void reset() override;
    uint16_t handle(const uint8_t* request, uint16_t len, const uint16_t* registers,
                    uint16_t registerCount, uint8_t* response, uint16_t cap) override;
    uint32_t requests() const override;
    uint32_t responses() const override;
    uint32_t badFrames() const override;

    frame::Decode lastDecode() const { return lastDecode_; }
    uint8_t lastRequestLen() const { return lastRequestLen_; }

private:
    uint32_t requests_;
    uint32_t responses_;
    uint32_t badFrames_;
    frame::Decode lastDecode_;
    uint8_t lastRequestLen_;
};
