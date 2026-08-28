// Testes de host dos protocolos escravos da PUSI-DI261930: Modbus RTU 0x03/0x04 e o quadro do jig.
// Modbus com registros big-endian; payload do jig com X e Y em int16 little-endian (decimos de grau).
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "board_pins.h"
#include "iface/islave_protocol.h"
#include "proto/crc16.h"
#include "proto/frame.h"
#include "proto/jig_slave.h"
#include "proto/modbus_slave.h"
#include "sensor_map.h"
#include "tilt.h"

namespace {

constexpr uint8_t kSlaveId = 7;
constexpr uint8_t kOtherSlaveId = 8;
constexpr uint8_t kBroadcastId = 0;
constexpr uint8_t kFuncReadHolding = 0x03;
constexpr uint8_t kFuncReadInput = 0x04;
constexpr uint8_t kFuncWriteSingle = 0x06;
constexpr uint8_t kExceptionMask = 0x80;
constexpr uint8_t kExcIllegalFunction = 0x01;
constexpr uint8_t kExcIllegalAddress = 0x02;
constexpr uint8_t kExcIllegalValue = 0x03;
constexpr uint16_t kRequestLen = 8;
constexpr uint16_t kExceptionLen = 5;
constexpr uint16_t kReadTwoLen = 9;
constexpr uint16_t kRespCap = 64;
constexpr uint8_t kCanary = 0xA5;

constexpr int16_t kAngleXDeci = -123;
constexpr int16_t kAngleYDeci = 321;
constexpr int16_t kAngleZDeci = -7;
constexpr int16_t kTempDeci = 266;
constexpr uint16_t kWhoAmI = 0x00C1;

uint16_t buildRequest(uint8_t id, uint8_t func, uint16_t start, uint16_t count, uint8_t* out) {
    out[0] = id;
    out[1] = func;
    out[2] = static_cast<uint8_t>((start >> 8) & 0xFFu);
    out[3] = static_cast<uint8_t>(start & 0xFFu);
    out[4] = static_cast<uint8_t>((count >> 8) & 0xFFu);
    out[5] = static_cast<uint8_t>(count & 0xFFu);
    const uint16_t crc = crc16Modbus(out, 6);
    out[6] = static_cast<uint8_t>(crc & 0xFFu);
    out[7] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
    return kRequestLen;
}

bool responseCrcOk(const uint8_t* buf, uint16_t len) {
    if (buf == nullptr || len < 4u) {
        return false;
    }
    const uint16_t body = static_cast<uint16_t>(len - 2u);
    const uint16_t want = crc16Modbus(buf, body);
    const uint16_t got = static_cast<uint16_t>(static_cast<uint16_t>(buf[body]) |
                                               static_cast<uint16_t>(buf[body + 1u] << 8));
    return want == got;
}

void fillRegisters(uint16_t* regs) {
    regs[sensormap::kRegAngleX] = static_cast<uint16_t>(kAngleXDeci);
    regs[sensormap::kRegAngleY] = static_cast<uint16_t>(kAngleYDeci);
    regs[sensormap::kRegAngleZ] = static_cast<uint16_t>(kAngleZDeci);
    regs[sensormap::kRegStatus] = kStsDataValid;
    regs[sensormap::kRegTempDeciC] = static_cast<uint16_t>(kTempDeci);
    regs[sensormap::kRegWhoAmI] = kWhoAmI;
    regs[sensormap::kRegFwVersion] = 0x0100u;
    regs[sensormap::kRegUptimeS] = 0x1234u;
}

void assertException(const uint8_t* buf, uint16_t len, uint8_t func, uint8_t code) {
    TEST_ASSERT_EQUAL_UINT16(kExceptionLen, len);
    TEST_ASSERT_EQUAL_HEX8(kSlaveId, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(func | kExceptionMask), buf[1]);
    TEST_ASSERT_EQUAL_HEX8(code, buf[2]);
    TEST_ASSERT_TRUE(responseCrcOk(buf, len));
}

int16_t le16(const uint8_t* p) {
    return static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                                      static_cast<uint16_t>(p[1] << 8)));
}

uint16_t buildJigRequest(uint8_t* out, uint16_t cap) {
    const uint8_t payload[4] = {0x01, 0x00, 0x00, 0x00};
    return frame::encode(payload, sizeof(payload), out, cap);
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

static void test_readHoldingDoisRegistradores(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncReadHolding, sensormap::kRegAngleX, 2, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(kReadTwoLen, n);
    TEST_ASSERT_EQUAL_HEX8(kSlaveId, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(kFuncReadHolding, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(4u, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, resp[3]);
    TEST_ASSERT_EQUAL_HEX8(0x85u, resp[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, resp[5]);
    TEST_ASSERT_EQUAL_HEX8(0x41u, resp[6]);
    TEST_ASSERT_TRUE(responseCrcOk(resp, n));
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[kReadTwoLen]);
    TEST_ASSERT_EQUAL_UINT32(1u, slave.responses());
    TEST_ASSERT_EQUAL_UINT32(0u, slave.badFrames());
    TEST_ASSERT_TRUE(slave.requests() >= 1u);
    TEST_ASSERT_NOT_NULL(slave.name());
    TEST_ASSERT_TRUE(strlen(slave.name()) > 0u);
}

static void test_readInputDoisRegistradores(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncReadInput, sensormap::kRegAngleX, 2, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(kReadTwoLen, n);
    TEST_ASSERT_EQUAL_HEX8(kSlaveId, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(kFuncReadInput, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(4u, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, resp[3]);
    TEST_ASSERT_EQUAL_HEX8(0x85u, resp[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, resp[5]);
    TEST_ASSERT_EQUAL_HEX8(0x41u, resp[6]);
    TEST_ASSERT_TRUE(responseCrcOk(resp, n));
    TEST_ASSERT_EQUAL_UINT32(1u, slave.responses());
    TEST_ASSERT_EQUAL_UINT32(0u, slave.badFrames());
}

static void test_leituraComEnderecoInicialDeslocado(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncReadHolding, sensormap::kRegStatus, 2, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(kReadTwoLen, n);
    TEST_ASSERT_EQUAL_HEX8(4u, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>((kStsDataValid >> 8) & 0xFFu), resp[3]);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(kStsDataValid & 0xFFu), resp[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, resp[5]);
    TEST_ASSERT_EQUAL_HEX8(0x0Au, resp[6]);
    TEST_ASSERT_TRUE(responseCrcOk(resp, n));
}

static void test_leituraDeTodaATabela(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen =
        buildRequest(kSlaveId, kFuncReadHolding, 0, sensormap::kRegCount, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    const uint16_t want = static_cast<uint16_t>(5u + 2u * sensormap::kRegCount);
    TEST_ASSERT_EQUAL_UINT16(want, n);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(2u * sensormap::kRegCount), resp[2]);
    for (uint16_t i = 0; i < sensormap::kRegCount; ++i) {
        const uint16_t at = static_cast<uint16_t>(3u + 2u * i);
        TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>((regs[i] >> 8) & 0xFFu), resp[at]);
        TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(regs[i] & 0xFFu), resp[at + 1u]);
    }
    TEST_ASSERT_TRUE(responseCrcOk(resp, n));
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[want]);
}

static void test_enderecoDeOutroEscravoNaoResponde(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kOtherSlaveId, kFuncReadHolding, 0, 2, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(0u, n);
    TEST_ASSERT_EQUAL_UINT32(0u, slave.badFrames());
    TEST_ASSERT_EQUAL_UINT32(0u, slave.responses());
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[0]);
}

static void test_broadcastNaoResponde(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kBroadcastId, kFuncReadHolding, 0, 2, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(0u, n);
    TEST_ASSERT_EQUAL_UINT32(0u, slave.responses());
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[0]);
}

static void test_crcRuimContaQuadroRuim(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncReadHolding, 0, 2, req);
    req[kRequestLen - 1u] = static_cast<uint8_t>(req[kRequestLen - 1u] ^ 0xFFu);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(0u, n);
    TEST_ASSERT_EQUAL_UINT32(1u, slave.badFrames());
    TEST_ASSERT_EQUAL_UINT32(0u, slave.responses());
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[0]);
}

static void test_funcaoNaoSuportadaGeraExcecao01(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncWriteSingle, 0, 1, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    assertException(resp, n, kFuncWriteSingle, kExcIllegalFunction);
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[kExceptionLen]);
}

static void test_leituraForaDaTabelaGeraExcecao02(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    uint8_t resp[kRespCap];

    memset(resp, kCanary, sizeof(resp));
    uint16_t reqLen = buildRequest(kSlaveId, kFuncReadHolding, sensormap::kRegCount, 1, req);
    uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));
    assertException(resp, n, kFuncReadHolding, kExcIllegalAddress);

    memset(resp, kCanary, sizeof(resp));
    reqLen = buildRequest(kSlaveId, kFuncReadInput,
                          static_cast<uint16_t>(sensormap::kRegCount - 2u), 4, req);
    n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));
    assertException(resp, n, kFuncReadInput, kExcIllegalAddress);
}

static void test_contagemZeroGeraExcecao(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncReadHolding, 0, 0, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(kExceptionLen, n);
    TEST_ASSERT_EQUAL_HEX8(kSlaveId, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(kFuncReadHolding | kExceptionMask), resp[1]);
    TEST_ASSERT_TRUE(resp[2] == kExcIllegalAddress || resp[2] == kExcIllegalValue);
    TEST_ASSERT_TRUE(responseCrcOk(resp, n));
}

static void test_contagemAcimaDoLimiteGeraExcecao03(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncReadHolding, 0, 126, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    assertException(resp, n, kFuncReadHolding, kExcIllegalValue);
}

static void test_bufferDeRespostaPequenoDemais(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncReadHolding, 0, 2, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t small = static_cast<uint16_t>(kReadTwoLen - 1u);
    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, small);

    TEST_ASSERT_EQUAL_UINT16(0u, n);
    for (uint16_t i = small; i < kRespCap; ++i) {
        TEST_ASSERT_EQUAL_HEX8(kCanary, resp[i]);
    }

    memset(resp, kCanary, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT16(
        0u, slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, 0));
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[0]);
}

static void test_quadroCurtoDemaisNaoResponde(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncReadHolding, 0, 2, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, static_cast<uint16_t>(reqLen - 3u), regs,
                                    sensormap::kRegCount, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(0u, n);
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[0]);
}

static void test_resetZeraContadores(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    uint16_t reqLen = buildRequest(kSlaveId, kFuncReadHolding, 0, 2, req);
    TEST_ASSERT_EQUAL_UINT16(kReadTwoLen,
                             slave.handle(req, reqLen, regs, sensormap::kRegCount, resp,
                                          sizeof(resp)));
    reqLen = buildRequest(kSlaveId, kFuncReadHolding, 0, 2, req);
    req[kRequestLen - 1u] = static_cast<uint8_t>(req[kRequestLen - 1u] ^ 0x01u);
    TEST_ASSERT_EQUAL_UINT16(
        0u, slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp)));
    TEST_ASSERT_EQUAL_UINT32(1u, slave.responses());
    TEST_ASSERT_EQUAL_UINT32(1u, slave.badFrames());

    slave.reset();

    TEST_ASSERT_EQUAL_UINT32(0u, slave.requests());
    TEST_ASSERT_EQUAL_UINT32(0u, slave.responses());
    TEST_ASSERT_EQUAL_UINT32(0u, slave.badFrames());
}

static void test_idPadraoDaPlaca(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(board::kModbusSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(board::kModbusSlaveId, kFuncReadHolding, 0, 2, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(kReadTwoLen, n);
    TEST_ASSERT_EQUAL_HEX8(board::kModbusSlaveId, resp[0]);
    TEST_ASSERT_TRUE(responseCrcOk(resp, n));
}

static void test_jigRespondeComXeYLittleEndian(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    JigFrameSlave jig;
    uint8_t wire[frame::kMaxFrame];
    const uint16_t reqLen = buildJigRequest(wire, sizeof(wire));
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(4u + frame::kOverhead), reqLen);

    uint8_t resp[frame::kMaxFrame];
    memset(resp, kCanary, sizeof(resp));
    const uint16_t n = jig.handle(wire, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(4u + frame::kOverhead), n);
    uint8_t payload[frame::kMaxPayload];
    uint8_t payloadLen = 0xFF;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(frame::Decode::Ok),
        static_cast<uint8_t>(frame::decode(resp, n, payload, sizeof(payload), payloadLen)));
    TEST_ASSERT_EQUAL_UINT8(4u, payloadLen);
    TEST_ASSERT_EQUAL_HEX8(0x85u, payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0x41u, payload[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, payload[3]);
    TEST_ASSERT_EQUAL_INT16(kAngleXDeci, le16(&payload[0]));
    TEST_ASSERT_EQUAL_INT16(kAngleYDeci, le16(&payload[2]));
    TEST_ASSERT_EQUAL_UINT32(1u, jig.responses());
    TEST_ASSERT_EQUAL_UINT32(0u, jig.badFrames());
    TEST_ASSERT_TRUE(jig.requests() >= 1u);
    TEST_ASSERT_NOT_NULL(jig.name());
    TEST_ASSERT_TRUE(strlen(jig.name()) > 0u);
}

static void test_jigSegueOsRegistradoresCorrentes(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    regs[sensormap::kRegAngleX] = static_cast<uint16_t>(static_cast<int16_t>(-1));
    regs[sensormap::kRegAngleY] = static_cast<uint16_t>(static_cast<int16_t>(-1800));
    JigFrameSlave jig;
    uint8_t wire[frame::kMaxFrame];
    const uint16_t reqLen = buildJigRequest(wire, sizeof(wire));
    uint8_t resp[frame::kMaxFrame];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = jig.handle(wire, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    uint8_t payload[frame::kMaxPayload];
    uint8_t payloadLen = 0xFF;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(frame::Decode::Ok),
        static_cast<uint8_t>(frame::decode(resp, n, payload, sizeof(payload), payloadLen)));
    TEST_ASSERT_EQUAL_UINT8(4u, payloadLen);
    TEST_ASSERT_EQUAL_INT16(-1, le16(&payload[0]));
    TEST_ASSERT_EQUAL_INT16(-1800, le16(&payload[2]));
}

static void test_jigCrcCorrompidoNaoResponde(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    JigFrameSlave jig;
    uint8_t wire[frame::kMaxFrame];
    const uint16_t reqLen = buildJigRequest(wire, sizeof(wire));
    wire[3 + 4] = static_cast<uint8_t>(wire[3 + 4] ^ 0xFFu);
    uint8_t resp[frame::kMaxFrame];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = jig.handle(wire, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16(0u, n);
    TEST_ASSERT_EQUAL_UINT32(1u, jig.badFrames());
    TEST_ASSERT_EQUAL_UINT32(0u, jig.responses());
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[0]);
}

static void test_jigQuadroMalFormadoNaoResponde(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    JigFrameSlave jig;
    uint8_t wire[frame::kMaxFrame];
    const uint16_t reqLen = buildJigRequest(wire, sizeof(wire));
    uint8_t resp[frame::kMaxFrame];

    memset(resp, kCanary, sizeof(resp));
    wire[0] = 0x99;
    TEST_ASSERT_EQUAL_UINT16(
        0u, jig.handle(wire, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp)));
    wire[0] = frame::kStx;

    memset(resp, kCanary, sizeof(resp));
    wire[1] = 'X';
    TEST_ASSERT_EQUAL_UINT16(
        0u, jig.handle(wire, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp)));
    wire[1] = frame::kType;

    memset(resp, kCanary, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT16(0u, jig.handle(wire, static_cast<uint16_t>(reqLen - 2u), regs,
                                            sensormap::kRegCount, resp, sizeof(resp)));
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, jig.responses());

    memset(resp, kCanary, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT16(
        static_cast<uint16_t>(4u + frame::kOverhead),
        jig.handle(wire, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp)));
}

static void test_jigBufferPequenoDemais(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    JigFrameSlave jig;
    uint8_t wire[frame::kMaxFrame];
    const uint16_t reqLen = buildJigRequest(wire, sizeof(wire));
    uint8_t resp[frame::kMaxFrame];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t small = static_cast<uint16_t>(4u + frame::kOverhead - 1u);
    TEST_ASSERT_EQUAL_UINT16(
        0u, jig.handle(wire, reqLen, regs, sensormap::kRegCount, resp, small));
    for (uint16_t i = small; i < frame::kMaxFrame; ++i) {
        TEST_ASSERT_EQUAL_HEX8(kCanary, resp[i]);
    }
}

static void test_jigResetZeraContadores(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    JigFrameSlave jig;
    uint8_t wire[frame::kMaxFrame];
    const uint16_t reqLen = buildJigRequest(wire, sizeof(wire));
    uint8_t resp[frame::kMaxFrame];
    memset(resp, kCanary, sizeof(resp));

    TEST_ASSERT_TRUE(jig.handle(wire, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp)) > 0u);
    TEST_ASSERT_EQUAL_UINT32(1u, jig.responses());

    jig.reset();

    TEST_ASSERT_EQUAL_UINT32(0u, jig.requests());
    TEST_ASSERT_EQUAL_UINT32(0u, jig.responses());
    TEST_ASSERT_EQUAL_UINT32(0u, jig.badFrames());
}

static void test_ambosImplementamAInterface(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave modbus(kSlaveId);
    JigFrameSlave jig;
    ISlaveProtocol* slaves[2];
    slaves[0] = &modbus;
    slaves[1] = &jig;
    for (size_t i = 0; i < 2u; ++i) {
        TEST_ASSERT_NOT_NULL(slaves[i]->name());
        slaves[i]->reset();
        TEST_ASSERT_EQUAL_UINT32(0u, slaves[i]->requests());
        TEST_ASSERT_EQUAL_UINT32(0u, slaves[i]->responses());
        TEST_ASSERT_EQUAL_UINT32(0u, slaves[i]->badFrames());
    }
    TEST_ASSERT_TRUE(strcmp(slaves[0]->name(), slaves[1]->name()) != 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_readHoldingDoisRegistradores);
    RUN_TEST(test_readInputDoisRegistradores);
    RUN_TEST(test_leituraComEnderecoInicialDeslocado);
    RUN_TEST(test_leituraDeTodaATabela);
    RUN_TEST(test_enderecoDeOutroEscravoNaoResponde);
    RUN_TEST(test_broadcastNaoResponde);
    RUN_TEST(test_crcRuimContaQuadroRuim);
    RUN_TEST(test_funcaoNaoSuportadaGeraExcecao01);
    RUN_TEST(test_leituraForaDaTabelaGeraExcecao02);
    RUN_TEST(test_contagemZeroGeraExcecao);
    RUN_TEST(test_contagemAcimaDoLimiteGeraExcecao03);
    RUN_TEST(test_bufferDeRespostaPequenoDemais);
    RUN_TEST(test_quadroCurtoDemaisNaoResponde);
    RUN_TEST(test_resetZeraContadores);
    RUN_TEST(test_idPadraoDaPlaca);
    RUN_TEST(test_jigRespondeComXeYLittleEndian);
    RUN_TEST(test_jigSegueOsRegistradoresCorrentes);
    RUN_TEST(test_jigCrcCorrompidoNaoResponde);
    RUN_TEST(test_jigQuadroMalFormadoNaoResponde);
    RUN_TEST(test_jigBufferPequenoDemais);
    RUN_TEST(test_jigResetZeraContadores);
    RUN_TEST(test_ambosImplementamAInterface);
    return UNITY_END();
}
