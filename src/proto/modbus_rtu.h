// Stub de Modbus RTU sobre o RS-485 da folha 1/2 (SN65HVD75DR): funcao 0x03/0x04, 2 registros.
// Mapa de registradores e formato da PUSI-DI261930 ainda em aberto; padrao 19200 8N1.
#pragma once

#include <stdint.h>

#include "iface/iserial_transport.h"
#include "proto/irs485_protocol.h"
#include "status.h"

class ModbusRtuProtocol : public IRs485Protocol {
public:
    static constexpr uint8_t kDefaultSlaveId = 1;
    static constexpr uint8_t kFuncReadHolding = 0x03;
    static constexpr uint8_t kFuncReadInput = 0x04;
    static constexpr uint16_t kDefaultStartAddr = 0;
    static constexpr uint16_t kRegisterCount = 2;
    static constexpr uint8_t kRequestLen = 8;
    static constexpr uint8_t kDataBytes = 4;
    static constexpr uint8_t kResponseLen = 9;
    static constexpr uint8_t kExceptionLen = 5;
    static constexpr uint16_t kRxCap = 16;
    static constexpr uint32_t kDefaultPollTimeoutMs = 50;
    static constexpr uint8_t kMaxSlaveId = 247;

    ModbusRtuProtocol();

    const char* name() const override;
    Status begin(ISerialTransport& transport) override;
    Status request() override;
    bool poll(Angle& out) override;
    void serviceEcho() override;
    uint32_t framesOk() const override;
    uint32_t framesBad() const override;
    void resetCounters() override;

    Status configure(uint8_t slaveId, uint8_t function, uint16_t startAddr);
    void setPollTimeoutMs(uint32_t timeoutMs);
    uint8_t slaveId() const { return slaveId_; }
    uint8_t function() const { return function_; }
    uint16_t startAddr() const { return startAddr_; }
    uint8_t lastException() const { return lastException_; }
    uint16_t pending() const { return rxLen_; }

private:
    void push(uint8_t value);
    void shift(uint16_t count);
    void dropOne();
    void noteBad(bool crcError);
    bool crcOk(uint16_t total) const;
    bool consume(Angle& out);

    ISerialTransport* transport_;
    uint32_t framesOk_;
    uint32_t framesBad_;
    uint32_t pollTimeoutMs_;
    uint16_t startAddr_;
    uint16_t rxLen_;
    uint8_t slaveId_;
    uint8_t function_;
    uint8_t lastException_;
    bool badCounted_;
    uint8_t rx_[kRxCap];
};
