// Modbus RTU (stub) sobre RS-485 half-duplex da folha 1/2: PDU 0x03/0x04 + CRC16-MODBUS.
// Registros big-endian, int16 em decimos de grau (X no primeiro registro, Y no segundo).
#include "proto/modbus_rtu.h"

#include "proto/crc16.h"

namespace {

constexpr float kDeciDegreeToDegree = 0.1f;
constexpr uint8_t kExceptionMask = 0x80;

int16_t be16(const uint8_t* p) {
    const uint16_t raw = static_cast<uint16_t>(static_cast<uint16_t>(p[0] << 8) |
                                               static_cast<uint16_t>(p[1]));
    return static_cast<int16_t>(raw);
}

uint8_t hiByte(uint16_t value) {
    return static_cast<uint8_t>((value >> 8) & 0xFFu);
}

uint8_t loByte(uint16_t value) {
    return static_cast<uint8_t>(value & 0xFFu);
}

}  // namespace

ModbusRtuProtocol::ModbusRtuProtocol()
    : transport_(nullptr),
      framesOk_(0),
      framesBad_(0),
      pollTimeoutMs_(kDefaultPollTimeoutMs),
      startAddr_(kDefaultStartAddr),
      rxLen_(0),
      slaveId_(kDefaultSlaveId),
      function_(kFuncReadHolding),
      lastException_(0),
      badCounted_(false),
      rx_{} {}

const char* ModbusRtuProtocol::name() const {
    return "modbus-rtu-stub";
}

Status ModbusRtuProtocol::begin(ISerialTransport& transport) {
    transport_ = &transport;
    rxLen_ = 0;
    lastException_ = 0;
    return kOk;
}

Status ModbusRtuProtocol::configure(uint8_t slaveId, uint8_t function, uint16_t startAddr) {
    if (slaveId == 0 || slaveId > kMaxSlaveId) {
        return Err::Param;
    }
    if (function != kFuncReadHolding && function != kFuncReadInput) {
        return Err::Unsupported;
    }
    if (startAddr > static_cast<uint16_t>(0xFFFFu - kRegisterCount)) {
        return Err::Range;
    }
    slaveId_ = slaveId;
    function_ = function;
    startAddr_ = startAddr;
    rxLen_ = 0;
    lastException_ = 0;
    return kOk;
}

void ModbusRtuProtocol::setPollTimeoutMs(uint32_t timeoutMs) {
    pollTimeoutMs_ = timeoutMs;
}

void ModbusRtuProtocol::resetCounters() {
    framesOk_ = 0;
    framesBad_ = 0;
    rxLen_ = 0;
    lastException_ = 0;
}

uint32_t ModbusRtuProtocol::framesOk() const {
    return framesOk_;
}

uint32_t ModbusRtuProtocol::framesBad() const {
    return framesBad_;
}

Status ModbusRtuProtocol::request() {
    if (transport_ == nullptr) {
        return Err::NotInit;
    }
    uint8_t pdu[kRequestLen];
    pdu[0] = slaveId_;
    pdu[1] = function_;
    pdu[2] = hiByte(startAddr_);
    pdu[3] = loByte(startAddr_);
    pdu[4] = hiByte(kRegisterCount);
    pdu[5] = loByte(kRegisterCount);
    const uint16_t crc = crc16Modbus(pdu, kRequestLen - 2u);
    pdu[6] = loByte(crc);
    pdu[7] = hiByte(crc);
    rxLen_ = 0;
    lastException_ = 0;
    return transport_->write(pdu, kRequestLen);
}

void ModbusRtuProtocol::push(uint8_t value) {
    if (rxLen_ == 0 && value != slaveId_) {
        return;
    }
    if (rxLen_ >= kRxCap) {
        rxLen_ = 0;
        if (value != slaveId_) {
            return;
        }
    }
    rx_[rxLen_] = value;
    ++rxLen_;
}

void ModbusRtuProtocol::shift(uint16_t count) {
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

void ModbusRtuProtocol::dropOne() {
    uint16_t start = 1;
    while (start < rxLen_ && rx_[start] != slaveId_) {
        ++start;
    }
    shift(start);
}

void ModbusRtuProtocol::noteBad(bool crcError) {
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

bool ModbusRtuProtocol::crcOk(uint16_t total) const {
    if (total < 3 || total > kRxCap) {
        return false;
    }
    const uint16_t body = static_cast<uint16_t>(total - 2);
    const uint16_t want = crc16Modbus(rx_, body);
    const uint16_t got = static_cast<uint16_t>(static_cast<uint16_t>(rx_[body]) |
                                               static_cast<uint16_t>(rx_[body + 1] << 8));
    return want == got;
}

bool ModbusRtuProtocol::consume(Angle& out) {
    while (rxLen_ >= 2) {
        if (rx_[0] != slaveId_) {
            dropOne();
            continue;
        }
        const uint8_t fn = rx_[1];
        if (fn == static_cast<uint8_t>(function_ | kExceptionMask)) {
            if (rxLen_ < kExceptionLen) {
                return false;
            }
            if (!crcOk(kExceptionLen)) {
                noteBad(true);
                dropOne();
                continue;
            }
            lastException_ = rx_[2];
            if (!badCounted_) {
                badCounted_ = true;
                ++framesBad_;
            }
            transport_->noteFrameOk();
            shift(kExceptionLen);
            return false;
        }
        if (fn != function_) {
            noteBad(false);
            dropOne();
            continue;
        }
        if (rxLen_ < 3) {
            return false;
        }
        if (rx_[2] != kDataBytes) {
            noteBad(false);
            dropOne();
            continue;
        }
        if (rxLen_ < kResponseLen) {
            return false;
        }
        if (!crcOk(kResponseLen)) {
            noteBad(true);
            dropOne();
            continue;
        }
        out.x = static_cast<float>(be16(&rx_[3])) * kDeciDegreeToDegree;
        out.y = static_cast<float>(be16(&rx_[5])) * kDeciDegreeToDegree;
        out.valid = true;
        lastException_ = 0;
        ++framesOk_;
        transport_->noteFrameOk();
        shift(kResponseLen);
        return true;
    }
    return false;
}

bool ModbusRtuProtocol::poll(Angle& out) {
    out.x = 0.0f;
    out.y = 0.0f;
    out.valid = false;
    if (transport_ == nullptr) {
        return false;
    }
    badCounted_ = false;
    uint8_t chunk[kRxCap];
    const uint16_t got = transport_->read(chunk, kRxCap, pollTimeoutMs_);
    if (got == 0) {
        if (rxLen_ > 0) {
            noteBad(false);
            rxLen_ = 0;
        }
        return false;
    }
    for (uint16_t i = 0; i < got; ++i) {
        push(chunk[i]);
    }
    return consume(out);
}

void ModbusRtuProtocol::serviceEcho() {
    if (transport_ == nullptr) {
        return;
    }
    uint8_t buf[kRxCap];
    (void)transport_->read(buf, kRxCap, 0);
}
