// PUSI-DI261930: Modbus RTU escravo, [addr][func][dados][CRC16-MODBUS lo,hi], registrador big-endian.
// Codigo puro de host: sem Arduino, sem SPI, sem tempo; a temporizacao t3.5 fica no main.
#include "proto/modbus_slave.h"

#include "proto/crc16.h"

namespace {

uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

void appendCrc(uint8_t* buf, uint16_t len) {
    const uint16_t crc = crc16Modbus(buf, static_cast<size_t>(len));
    buf[len] = static_cast<uint8_t>(crc & 0xFFu);
    buf[len + 1] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
}

}  // namespace

ModbusRtuSlave::ModbusRtuSlave(uint8_t id)
    : requests_(0), responses_(0), badFrames_(0), exceptions_(0), slaveId_(id) {}

const char* ModbusRtuSlave::name() const {
    return "modbus-rtu";
}

void ModbusRtuSlave::reset() {
    requests_ = 0;
    responses_ = 0;
    badFrames_ = 0;
    exceptions_ = 0;
}

uint32_t ModbusRtuSlave::requests() const {
    return requests_;
}

uint32_t ModbusRtuSlave::responses() const {
    return responses_;
}

uint32_t ModbusRtuSlave::badFrames() const {
    return badFrames_;
}

void ModbusRtuSlave::setSlaveId(uint8_t id) {
    slaveId_ = id;
}

uint16_t ModbusRtuSlave::buildException(uint8_t func, uint8_t code, uint8_t* response,
                                        uint16_t cap) {
    if (cap < kExceptionLen) {
        return 0;
    }
    response[0] = slaveId_;
    response[1] = static_cast<uint8_t>(func | kExceptionMask);
    response[2] = code;
    appendCrc(response, 3);
    ++exceptions_;
    ++responses_;
    return kExceptionLen;
}

uint16_t ModbusRtuSlave::readRegisters(uint8_t func, const uint8_t* request,
                                       const uint16_t* registers, uint16_t registerCount,
                                       uint8_t* response, uint16_t cap) {
    const uint16_t start = be16(&request[2]);
    const uint16_t count = be16(&request[4]);

    if (count == 0) {
        return buildException(func, kExIllegalDataAddress, response, cap);
    }
    if (count > kMaxReadCount) {
        return buildException(func, kExIllegalDataValue, response, cap);
    }
    const uint32_t last = static_cast<uint32_t>(start) + static_cast<uint32_t>(count);
    if (registers == nullptr || last > static_cast<uint32_t>(registerCount)) {
        return buildException(func, kExIllegalDataAddress, response, cap);
    }

    const uint16_t byteCount = static_cast<uint16_t>(count * 2u);
    const uint16_t total = static_cast<uint16_t>(byteCount + kReadOverhead);
    if (cap < total) {
        return 0;
    }
    response[0] = slaveId_;
    response[1] = func;
    response[2] = static_cast<uint8_t>(byteCount);
    for (uint16_t i = 0; i < count; ++i) {
        const uint16_t value = registers[start + i];
        response[3 + i * 2] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        response[4 + i * 2] = static_cast<uint8_t>(value & 0xFFu);
    }
    appendCrc(response, static_cast<uint16_t>(byteCount + 3u));
    ++responses_;
    return total;
}

uint16_t ModbusRtuSlave::handle(const uint8_t* request, uint16_t len, const uint16_t* registers,
                                uint16_t registerCount, uint8_t* response, uint16_t cap) {
    if (request == nullptr || response == nullptr) {
        return 0;
    }
    if (len < kMinRequestLen) {
        return 0;
    }

    const uint16_t crc = crc16Modbus(request, static_cast<size_t>(len - 2));
    const uint16_t got = le16(&request[len - 2]);
    if (crc != got) {
        ++badFrames_;
        return 0;
    }

    const uint8_t addr = request[0];
    const bool broadcast = (addr == kBroadcastAddr);
    if (!broadcast && addr != slaveId_) {
        return 0;
    }
    ++requests_;

    const uint8_t func = request[1];
    if (func != kFuncReadHolding && func != kFuncReadInput) {
        if (broadcast) {
            return 0;
        }
        return buildException(func, kExIllegalFunction, response, cap);
    }
    if (len != kReadRequestLen) {
        return 0;
    }
    if (broadcast) {
        return 0;
    }
    return readRegisters(func, request, registers, registerCount, response, cap);
}
