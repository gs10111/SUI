// Teste de host do CRC16-MODBUS e do quadro do jig RS485 (folha 1/2: MAX3485 no CN2).
// Vetor de referencia do padrao Modbus over Serial Line V1.02: "123456789" -> 0x4B37.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "proto/crc16.h"
#include "proto/frame.h"

namespace {

constexpr uint8_t kSampleLen = 8;

uint16_t buildFrame(uint8_t len, uint8_t* payload, uint8_t* out, uint16_t cap) {
    for (uint8_t i = 0; i < len; ++i) {
        payload[i] = static_cast<uint8_t>(0xA0u + i);
    }
    return frame::encode(payload, len, out, cap);
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

static void test_knownVector(void) {
    const char digits[] = "123456789";
    const uint16_t crc = crc16Modbus(reinterpret_cast<const uint8_t*>(digits), sizeof(digits) - 1u);
    TEST_ASSERT_EQUAL_HEX16(0x4B37, crc);
}

static void test_seedAndEmptyInput(void) {
    const uint8_t data[] = {0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16Modbus(nullptr, 4));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16Modbus(data, 0));
    TEST_ASSERT_EQUAL_HEX16(0x1234, crc16Modbus(nullptr, 0, 0x1234));
}

static void test_incrementalSeedMatchesSinglePass(void) {
    const char digits[] = "123456789";
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(digits);
    const uint16_t head = crc16Modbus(raw, 4);
    const uint16_t full = crc16Modbus(raw + 4, 5, head);
    TEST_ASSERT_EQUAL_HEX16(0x4B37, full);
}

static void test_encodeDecodeRoundTrip(void) {
    uint8_t payload[frame::kMaxPayload];
    uint8_t wire[frame::kMaxFrame];
    const uint16_t total = buildFrame(kSampleLen, payload, wire, sizeof(wire));
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(kSampleLen + frame::kOverhead), total);
    TEST_ASSERT_EQUAL_HEX8(frame::kStx, wire[0]);
    TEST_ASSERT_EQUAL_HEX8(frame::kType, wire[1]);
    TEST_ASSERT_EQUAL_HEX8(kSampleLen, wire[2]);
    TEST_ASSERT_EQUAL_HEX8(frame::kEtx, wire[total - 1]);

    uint8_t got[frame::kMaxPayload];
    uint8_t gotLen = 0xFF;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::Ok),
                            static_cast<uint8_t>(frame::decode(wire, total, got, sizeof(got), gotLen)));
    TEST_ASSERT_EQUAL_UINT8(kSampleLen, gotLen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, got, kSampleLen);
}

static void test_encodeDecodeEmptyPayload(void) {
    uint8_t wire[frame::kMaxFrame];
    const uint16_t total = frame::encode(nullptr, 0, wire, sizeof(wire));
    TEST_ASSERT_EQUAL_UINT16(frame::kOverhead, total);
    uint8_t got[frame::kMaxPayload];
    uint8_t gotLen = 0xFF;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::Ok),
                            static_cast<uint8_t>(frame::decode(wire, total, got, sizeof(got), gotLen)));
    TEST_ASSERT_EQUAL_UINT8(0, gotLen);
}

static void test_encodeRejectsBadArguments(void) {
    uint8_t payload[frame::kMaxPayload + 8];
    uint8_t wire[frame::kMaxFrame + 8];
    memset(payload, 0x5A, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT16(0, frame::encode(payload, frame::kMaxPayload + 1u, wire, sizeof(wire)));
    TEST_ASSERT_EQUAL_UINT16(0, frame::encode(payload, 4, nullptr, sizeof(wire)));
    TEST_ASSERT_EQUAL_UINT16(0, frame::encode(nullptr, 4, wire, sizeof(wire)));
    TEST_ASSERT_EQUAL_UINT16(0, frame::encode(payload, 4, wire, 9));
    TEST_ASSERT_EQUAL_UINT16(10, frame::encode(payload, 4, wire, 10));
    TEST_ASSERT_EQUAL_UINT16(frame::kMaxFrame,
                             frame::encode(payload, frame::kMaxPayload, wire, sizeof(wire)));
}

static void test_decodeBadCrc(void) {
    uint8_t payload[frame::kMaxPayload];
    uint8_t wire[frame::kMaxFrame];
    const uint16_t total = buildFrame(kSampleLen, payload, wire, sizeof(wire));
    uint8_t got[frame::kMaxPayload];
    uint8_t gotLen = 0xFF;

    wire[4] = static_cast<uint8_t>(wire[4] ^ 0x01u);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::BadCrc),
                            static_cast<uint8_t>(frame::decode(wire, total, got, sizeof(got), gotLen)));
    TEST_ASSERT_EQUAL_UINT8(0, gotLen);

    wire[4] = static_cast<uint8_t>(wire[4] ^ 0x01u);
    wire[3 + kSampleLen] = static_cast<uint8_t>(wire[3 + kSampleLen] ^ 0x80u);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::BadCrc),
                            static_cast<uint8_t>(frame::decode(wire, total, got, sizeof(got), gotLen)));
}

static void test_decodeFramingErrors(void) {
    uint8_t payload[frame::kMaxPayload];
    uint8_t wire[frame::kMaxFrame];
    uint8_t got[frame::kMaxPayload];
    uint8_t gotLen = 0xFF;
    const uint16_t total = buildFrame(kSampleLen, payload, wire, sizeof(wire));

    wire[0] = 0x99;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::BadStx),
                            static_cast<uint8_t>(frame::decode(wire, total, got, sizeof(got), gotLen)));
    wire[0] = frame::kStx;

    wire[1] = 'X';
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::BadType),
                            static_cast<uint8_t>(frame::decode(wire, total, got, sizeof(got), gotLen)));
    wire[1] = frame::kType;

    wire[total - 1] = 0x99;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::BadEtx),
                            static_cast<uint8_t>(frame::decode(wire, total, got, sizeof(got), gotLen)));
    wire[total - 1] = frame::kEtx;

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::Ok),
                            static_cast<uint8_t>(frame::decode(wire, total, got, sizeof(got), gotLen)));
}

static void test_decodeTooShortAndBadLen(void) {
    uint8_t payload[frame::kMaxPayload];
    uint8_t wire[frame::kMaxFrame];
    uint8_t got[frame::kMaxPayload];
    uint8_t gotLen = 0xFF;
    const uint16_t total = buildFrame(kSampleLen, payload, wire, sizeof(wire));

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::TooShort),
                            static_cast<uint8_t>(frame::decode(wire, frame::kOverhead - 1u, got, sizeof(got), gotLen)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::TooShort),
                            static_cast<uint8_t>(frame::decode(wire, static_cast<uint16_t>(total - 1u), got, sizeof(got), gotLen)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::TooShort),
                            static_cast<uint8_t>(frame::decode(nullptr, total, got, sizeof(got), gotLen)));

    wire[2] = frame::kMaxPayload + 1u;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::BadLen),
                            static_cast<uint8_t>(frame::decode(wire, total, got, sizeof(got), gotLen)));
    wire[2] = kSampleLen;

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(frame::Decode::BadLen),
                            static_cast<uint8_t>(frame::decode(wire, total, got, kSampleLen - 1u, gotLen)));
    TEST_ASSERT_EQUAL_UINT8(0, gotLen);
}

static void test_decodeNamesAreStable(void) {
    TEST_ASSERT_EQUAL_STRING("OK", frame::decodeName(frame::Decode::Ok));
    TEST_ASSERT_EQUAL_STRING("BAD_CRC", frame::decodeName(frame::Decode::BadCrc));
    TEST_ASSERT_EQUAL_STRING("TOO_SHORT", frame::decodeName(frame::Decode::TooShort));
    TEST_ASSERT_EQUAL_STRING("BAD_STX", frame::decodeName(frame::Decode::BadStx));
    TEST_ASSERT_EQUAL_STRING("BAD_ETX", frame::decodeName(frame::Decode::BadEtx));
    TEST_ASSERT_EQUAL_STRING("BAD_TYPE", frame::decodeName(frame::Decode::BadType));
    TEST_ASSERT_EQUAL_STRING("BAD_LEN", frame::decodeName(frame::Decode::BadLen));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_knownVector);
    RUN_TEST(test_seedAndEmptyInput);
    RUN_TEST(test_incrementalSeedMatchesSinglePass);
    RUN_TEST(test_encodeDecodeRoundTrip);
    RUN_TEST(test_encodeDecodeEmptyPayload);
    RUN_TEST(test_encodeRejectsBadArguments);
    RUN_TEST(test_decodeBadCrc);
    RUN_TEST(test_decodeFramingErrors);
    RUN_TEST(test_decodeTooShortAndBadLen);
    RUN_TEST(test_decodeNamesAreStable);
    return UNITY_END();
}
