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
// Escrita MULTIPLA: a funcao que um mestre generico tenta e que este escravo tem de
// continuar recusando. Ate 2026-09-14 este papel era da 0x06, que passou a ser aceita
// para o registrador de comando de OTA - e so para ele.
constexpr uint8_t kFuncWriteMultiple = 0x10;
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
    // 0x10 (escrita multipla) e o que um mestre generico tenta quando quer mexer em varios
    // registradores de uma vez. Este escravo nao tem escrita generica e nao pode ganhar uma por
    // descuido: e o enlace que decide se quatro reles de seguranca atuam.
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncWriteMultiple, 0, 1, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    assertException(resp, n, kFuncWriteMultiple, kExcIllegalFunction);
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

// ======================= A UNICA ESCRITA DESTE ESCRAVO (2026-09-14) =======================
// Ela existe para que a supervisora possa ligar o radio desta placa a partir do painel, e o que
// estes testes protegem e o "unica": um enlace que decide se quatro reles de seguranca atuam nao
// pode ganhar escrita generica por descuido.

static void test_escrita_no_registrador_de_comando_e_aceita_e_ecoada(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncWriteSingle, sensormap::kRegCmdOta,
                                         sensormap::kCmdOtaLigar, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    // A resposta de 0x06 e o eco do proprio pedido.
    TEST_ASSERT_EQUAL_UINT16(reqLen, n);
    for (uint16_t i = 0; i < reqLen; ++i) {
        TEST_ASSERT_EQUAL_HEX8(req[i], resp[i]);
    }
    TEST_ASSERT_TRUE(responseCrcOk(resp, n));
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[reqLen]);

    uint16_t valor = 0;
    TEST_ASSERT_TRUE(slave.takeOtaCommand(valor));
    TEST_ASSERT_EQUAL_UINT16(sensormap::kCmdOtaLigar, valor);
    // Colhido uma vez so: releitura nao pode religar o radio de graca.
    TEST_ASSERT_FALSE(slave.takeOtaCommand(valor));
}

// QUALQUER OUTRO ENDERECO E RECUSADO. Sem isto, 0x06 viraria escrita generica e um mestre
// desatento (ou um teste de fabrica de outra linha) reescreveria angulo, status ou WHOAMI.
static void test_escrita_em_qualquer_outro_endereco_e_recusada(void) {
    uint16_t regs[sensormap::kRegCount];
    ModbusRtuSlave slave(kSlaveId);
    const uint16_t kEnderecos[] = {sensormap::kRegAngleX, sensormap::kRegStatus,
                                   sensormap::kRegWhoAmI, sensormap::kRegUptimeS,
                                   sensormap::kRegCmdOta + 1u, 0x00FFu};
    for (size_t i = 0; i < sizeof(kEnderecos) / sizeof(kEnderecos[0]); ++i) {
        fillRegisters(regs);
        uint8_t req[kRequestLen];
        const uint16_t reqLen = buildRequest(kSlaveId, kFuncWriteSingle, kEnderecos[i],
                                             sensormap::kCmdOtaLigar, req);
        uint8_t resp[kRespCap];
        memset(resp, kCanary, sizeof(resp));

        const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp,
                                        sizeof(resp));
        assertException(resp, n, kFuncWriteSingle, kExcIllegalAddress);

        uint16_t valor = 0;
        TEST_ASSERT_FALSE_MESSAGE(slave.takeOtaCommand(valor), "endereco errado nao pode comandar");
        // E nenhum registrador pode ter sido tocado.
        TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(kAngleXDeci), regs[sensormap::kRegAngleX]);
        TEST_ASSERT_EQUAL_UINT16(kWhoAmI, regs[sensormap::kRegWhoAmI]);
    }
}

// E QUALQUER OUTRO VALOR TAMBEM. O comando tem dois valores previstos; o resto e ruido de linha
// ou mestre errado, e ligar radio por ruido de linha e exatamente o que nao pode acontecer.
static void test_valor_fora_dos_dois_previstos_e_recusado(void) {
    uint16_t regs[sensormap::kRegCount];
    ModbusRtuSlave slave(kSlaveId);
    const uint16_t kValores[] = {1u, 1975u, 1977u, 0x07FFu, 0xFFFFu};
    for (size_t i = 0; i < sizeof(kValores) / sizeof(kValores[0]); ++i) {
        fillRegisters(regs);
        uint8_t req[kRequestLen];
        const uint16_t reqLen = buildRequest(kSlaveId, kFuncWriteSingle, sensormap::kRegCmdOta,
                                             kValores[i], req);
        uint8_t resp[kRespCap];
        memset(resp, kCanary, sizeof(resp));

        const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp,
                                        sizeof(resp));
        assertException(resp, n, kFuncWriteSingle, kExcIllegalValue);
        uint16_t valor = 0;
        TEST_ASSERT_FALSE(slave.takeOtaCommand(valor));
    }
    // Desligar CONTINUA valendo - recusar tudo passaria nos testes acima.
    fillRegisters(regs);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncWriteSingle, sensormap::kRegCmdOta,
                                         sensormap::kCmdOtaDesligar, req);
    uint8_t resp[kRespCap];
    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT16(reqLen, n);
    uint16_t valor = 0xFFFFu;
    TEST_ASSERT_TRUE(slave.takeOtaCommand(valor));
    TEST_ASSERT_EQUAL_UINT16(sensormap::kCmdOtaDesligar, valor);
}

// EM BROADCAST O COMANDO VALE E NAO HA RESPOSTA. E o modo que a supervisora usa: transmite e
// segue, sem esperar, porque esperar resposta dentro do tick de 50 ms do ciclo de seguranca
// custaria o dobro do orcamento por um comando administrativo.
static void test_broadcast_comanda_sem_responder(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(0 /*broadcast*/, kFuncWriteSingle, sensormap::kRegCmdOta,
                                         sensormap::kCmdOtaLigar, req);
    uint8_t resp[kRespCap];
    memset(resp, kCanary, sizeof(resp));

    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, n, "broadcast nao responde, nem com excecao");
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[0]);
    uint16_t valor = 0;
    TEST_ASSERT_TRUE_MESSAGE(slave.takeOtaCommand(valor), "mas o comando tem de valer");
    TEST_ASSERT_EQUAL_UINT16(sensormap::kCmdOtaLigar, valor);
}

// Broadcast com endereco ou valor errado: sem resposta E sem comando. O silencio nao pode ser
// confundido com aceitacao.
static void test_broadcast_invalido_nao_comanda_e_continua_mudo(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t resp[kRespCap];
    uint16_t valor = 0;

    uint8_t req1[kRequestLen];
    const uint16_t n1 = slave.handle(req1,
                                     buildRequest(0, kFuncWriteSingle, sensormap::kRegAngleX,
                                                  sensormap::kCmdOtaLigar, req1),
                                     regs, sensormap::kRegCount, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT16(0, n1);
    TEST_ASSERT_FALSE(slave.takeOtaCommand(valor));

    uint8_t req2[kRequestLen];
    const uint16_t n2 = slave.handle(req2,
                                     buildRequest(0, kFuncWriteSingle, sensormap::kRegCmdOta,
                                                  4242u, req2),
                                     regs, sensormap::kRegCount, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT16(0, n2);
    TEST_ASSERT_FALSE(slave.takeOtaCommand(valor));
}

// CRC ERRADO NAO COMANDA. Um quadro corrompido num cabo de 500 m nao pode ligar radio nenhum.
static void test_crc_errado_nao_comanda(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncWriteSingle, sensormap::kRegCmdOta,
                                         sensormap::kCmdOtaLigar, req);
    req[reqLen - 1] ^= 0x01u;
    uint8_t resp[kRespCap];
    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT16(0, n);
    uint16_t valor = 0;
    TEST_ASSERT_FALSE(slave.takeOtaCommand(valor));
}

// A LEITURA NAO PODE TER MUDADO. Este e o teste que protege a frota durante o rollout: se o
// registrador de comando entrasse na faixa de leitura, a supervisora atualizada pediria 9
// registradores e toda sensora ainda nao atualizada responderia "endereco ilegal" - transacao
// invalida, e em 150 ms os quatro reles em alarme, por causa da ORDEM em que as placas foram
// atualizadas.
static void test_a_faixa_de_leitura_nao_mudou(void) {
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(8, sensormap::kRegCount,
                                     "mexer nisto poe a frota em alarme durante o rollout");
    TEST_ASSERT_TRUE_MESSAGE(sensormap::kRegCmdOta >= sensormap::kRegCount,
                             "o registrador de comando tem de ficar FORA da faixa de leitura");

    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t req[kRequestLen];
    const uint16_t reqLen = buildRequest(kSlaveId, kFuncReadHolding, 0, sensormap::kRegCount, req);
    uint8_t resp[kRespCap];
    const uint16_t n = slave.handle(req, reqLen, regs, sensormap::kRegCount, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT16(5u + 2u * sensormap::kRegCount, n);
    TEST_ASSERT_TRUE(responseCrcOk(resp, n));

    // E ler o registrador de comando tem de dar excecao, nao valor.
    uint8_t req2[kRequestLen];
    const uint16_t reqLen2 = buildRequest(kSlaveId, kFuncReadHolding, sensormap::kRegCmdOta, 1,
                                          req2);
    const uint16_t n2 = slave.handle(req2, reqLen2, regs, sensormap::kRegCount, resp, sizeof(resp));
    assertException(resp, n2, kFuncReadHolding, kExcIllegalAddress);
}


// QUADRO 0x06 DE TAMANHO ERRADO. Sem a conferencia de comprimento, o decodificador leria os
// bytes 2..5 de um quadro que so tem 4 ou 6 - leitura fora dos limites do buffer de recepcao,
// dentro do caminho que liga radio. Um mestre de outra linha, ou lixo de linha num cabo de 500 m,
// produz exatamente isso.
static void test_quadro_de_escrita_com_tamanho_errado_e_descartado(void) {
    uint16_t regs[sensormap::kRegCount];
    fillRegisters(regs);
    ModbusRtuSlave slave(kSlaveId);
    uint8_t resp[kRespCap];

    // Curto: [addr][func][regHi][regLo][crcLo][crcHi] - falta o valor.
    uint8_t curto[6] = {kSlaveId, kFuncWriteSingle, 0x00, sensormap::kRegCmdOta, 0, 0};
    const uint16_t crcCurto = crc16Modbus(curto, 4);
    curto[4] = static_cast<uint8_t>(crcCurto & 0xFFu);
    curto[5] = static_cast<uint8_t>((crcCurto >> 8) & 0xFFu);
    memset(resp, kCanary, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT16(0, slave.handle(curto, sizeof(curto), regs, sensormap::kRegCount,
                                             resp, sizeof(resp)));
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[0]);

    // Longo: um byte a mais antes do CRC.
    uint8_t longo[9] = {kSlaveId, kFuncWriteSingle, 0x00, sensormap::kRegCmdOta,
                        0x07,     0xB8,             0x00, 0,    0};
    const uint16_t crcLongo = crc16Modbus(longo, 7);
    longo[7] = static_cast<uint8_t>(crcLongo & 0xFFu);
    longo[8] = static_cast<uint8_t>((crcLongo >> 8) & 0xFFu);
    memset(resp, kCanary, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT16(0, slave.handle(longo, sizeof(longo), regs, sensormap::kRegCount,
                                             resp, sizeof(resp)));
    TEST_ASSERT_EQUAL_HEX8(kCanary, resp[0]);

    uint16_t valor = 0;
    TEST_ASSERT_FALSE_MESSAGE(slave.takeOtaCommand(valor),
                              "quadro malformado nao pode ligar radio nenhum");
}

// O VALOR NO FIO E CONSTANTE DE PROTOCOLO, e esta prendido aqui pelo numero literal de proposito.
//
// Ele tem os mesmos digitos do codigo que o tecnico digita no painel porque e mais facil de
// lembrar, mas NAO e a mesma coisa e nao pode passar a ser: o codigo do painel e assunto da
// interface da supervisora e pode mudar; este numero e contrato de fio entre duas placas que
// podem estar em versoes diferentes de firmware durante um rollout. Se um dia o painel mudar,
// ISTO NAO MUDA - e este teste e o que garante que a mudanca nao escorregue de um lado para o
// outro sem ninguem ver.
static void test_o_comando_de_fio_esta_prendido_por_numero_literal(void) {
    TEST_ASSERT_EQUAL_UINT16(1976, sensormap::kCmdOtaLigar);
    TEST_ASSERT_EQUAL_UINT16(0, sensormap::kCmdOtaDesligar);
    TEST_ASSERT_EQUAL_UINT16(8, sensormap::kRegCmdOta);
    TEST_ASSERT_EQUAL_UINT8(0x06, kFuncWriteSingle);
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
    RUN_TEST(test_escrita_no_registrador_de_comando_e_aceita_e_ecoada);
    RUN_TEST(test_escrita_em_qualquer_outro_endereco_e_recusada);
    RUN_TEST(test_valor_fora_dos_dois_previstos_e_recusado);
    RUN_TEST(test_broadcast_comanda_sem_responder);
    RUN_TEST(test_broadcast_invalido_nao_comanda_e_continua_mudo);
    RUN_TEST(test_crc_errado_nao_comanda);
    RUN_TEST(test_a_faixa_de_leitura_nao_mudou);
    RUN_TEST(test_quadro_de_escrita_com_tamanho_errado_e_descartado);
    RUN_TEST(test_o_comando_de_fio_esta_prendido_por_numero_literal);
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
