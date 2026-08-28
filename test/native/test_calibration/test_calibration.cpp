// Teste de host da calibracao de 2 pontos (folha 2/2: DAC8562 SLAS719E + XTR300 SBOS293).
// Somente matematica e persistencia via IKeyValueStore: nao toca SPI nem GPIO.
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "drivers/calibration.h"

#include "../mock_kv_store.h"

namespace {

constexpr uint16_t kFullScale = 4095;
constexpr const char* kCalKey = "cal_v1";

bool nearf(float lhs, float rhs, float tol) {
    return fabsf(lhs - rhs) <= tol;
}

calmath::Point makePoint(uint16_t code, float value) {
    calmath::Point p;
    p.code = code;
    p.value = value;
    return p;
}

void seedStore(MockKvStore& kv) {
    CalibrationStore store(kv);
    TEST_ASSERT_TRUE(store.setFromPoints(0, AoMode::Voltage, makePoint(0, 0.0f), makePoint(kFullScale, 10.0f)).ok());
    TEST_ASSERT_TRUE(store.setFromPoints(1, AoMode::Current, makePoint(410, 4.0f), makePoint(2048, 20.0f)).ok());
    TEST_ASSERT_TRUE(store.save().ok());
}

void patchStored(MockKvStore& kv, size_t byteIndex, uint8_t mask) {
    CalRecord rec;
    size_t got = 0;
    TEST_ASSERT_TRUE(kv.getBlob(kCalKey, &rec, sizeof(rec), got).ok());
    TEST_ASSERT_TRUE(got == sizeof(rec));
    uint8_t* raw = reinterpret_cast<uint8_t*>(&rec);
    raw[byteIndex] = static_cast<uint8_t>(raw[byteIndex] ^ mask);
    TEST_ASSERT_TRUE(kv.putBlob(kCalKey, &rec, sizeof(rec)).ok());
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

static void test_solveTwoPoints(void) {
    calmath::Coef c;
    TEST_ASSERT_TRUE(calmath::solve(makePoint(0, 0.0f), makePoint(kFullScale, 10.0f), c));
    TEST_ASSERT_TRUE(c.valid);
    TEST_ASSERT_TRUE(nearf(c.a, 10.0f / 4095.0f, 1.0e-9f));
    TEST_ASSERT_TRUE(nearf(c.b, 0.0f, 1.0e-6f));

    calmath::Coef d;
    TEST_ASSERT_TRUE(calmath::solve(makePoint(410, 4.0f), makePoint(2048, 20.0f), d));
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_TRUE(nearf(d.a, 16.0f / 1638.0f, 1.0e-9f));
    TEST_ASSERT_TRUE(nearf(d.b, 4.0f - (16.0f / 1638.0f) * 410.0f, 1.0e-4f));
    TEST_ASSERT_TRUE(nearf(calmath::valueFromCode(d, 410), 4.0f, 1.0e-3f));
    TEST_ASSERT_TRUE(nearf(calmath::valueFromCode(d, 2048), 20.0f, 1.0e-3f));
}

static void test_roundTripCodeValueCode(void) {
    calmath::Coef c;
    TEST_ASSERT_TRUE(calmath::solve(makePoint(0, 0.0f), makePoint(kFullScale, 10.0f), c));
    const uint16_t probes[] = {0, 1, 1000, 2047, 4094, kFullScale};
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        const float value = calmath::valueFromCode(c, probes[i]);
        uint16_t back = 0xFFFF;
        TEST_ASSERT_TRUE(calmath::codeFromValue(c, value, kFullScale, back));
        TEST_ASSERT_EQUAL_UINT16(probes[i], back);
    }
}

static void test_solveRejectsSameCode(void) {
    calmath::Coef c;
    TEST_ASSERT_FALSE(calmath::solve(makePoint(100, 1.0f), makePoint(100, 2.0f), c));
    TEST_ASSERT_FALSE(c.valid);
    TEST_ASSERT_FALSE(calmath::solve(makePoint(0, 5.0f), makePoint(0, 5.0f), c));
    TEST_ASSERT_FALSE(c.valid);
    TEST_ASSERT_FALSE(calmath::solve(makePoint(0, 1.0f), makePoint(4095, 1.0f), c));
    TEST_ASSERT_FALSE(c.valid);
}

static void test_codeFromValueOutOfRange(void) {
    calmath::Coef c;
    TEST_ASSERT_TRUE(calmath::solve(makePoint(0, 0.0f), makePoint(kFullScale, 10.0f), c));
    uint16_t code = 0x1234;
    TEST_ASSERT_FALSE(calmath::codeFromValue(c, 20.0f, kFullScale, code));
    TEST_ASSERT_EQUAL_UINT16(0, code);
    TEST_ASSERT_FALSE(calmath::codeFromValue(c, -5.0f, kFullScale, code));
    TEST_ASSERT_EQUAL_UINT16(0, code);

    calmath::Coef invalid;
    invalid.a = 1.0f;
    invalid.b = 0.0f;
    invalid.valid = false;
    TEST_ASSERT_FALSE(calmath::codeFromValue(invalid, 1.0f, kFullScale, code));
}

static void test_notCalibratedBeforeCalibration(void) {
    MockKvStore kv;
    CalibrationStore store(kv);
    calmath::Coef c;
    TEST_ASSERT_FALSE(store.has(0, AoMode::Voltage));
    TEST_ASSERT_FALSE(store.has(1, AoMode::Current));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NotCalibrated), static_cast<uint8_t>(store.coef(0, AoMode::Voltage, c).err));
    uint16_t code = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NotCalibrated),
                            static_cast<uint8_t>(store.codeFor(0, AoMode::Voltage, 1.0f, kFullScale, code).err));
    float value = 0.0f;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NotCalibrated),
                            static_cast<uint8_t>(store.valueFor(0, AoMode::Voltage, 100, value).err));
}

static void test_rejectsBadAxisAndMode(void) {
    MockKvStore kv;
    CalibrationStore store(kv);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Err::Param),
        static_cast<uint8_t>(store.setFromPoints(board::kAxisCount, AoMode::Voltage, makePoint(0, 0.0f), makePoint(10, 1.0f)).err));
    calmath::Coef c;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::Param),
                            static_cast<uint8_t>(store.coef(board::kAxisCount, AoMode::Voltage, c).err));
    TEST_ASSERT_FALSE(store.has(board::kAxisCount, AoMode::Voltage));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Err::Range),
        static_cast<uint8_t>(store.setFromPoints(0, AoMode::Voltage, makePoint(7, 1.0f), makePoint(7, 2.0f)).err));
}

static void test_missingKeyIsStorage(void) {
    MockKvStore kv;
    CalibrationStore store(kv);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::Storage), static_cast<uint8_t>(store.load().err));
    TEST_ASSERT_FALSE(kv.contains(kCalKey));
}

static void test_saveLoadKeepsCoefficients(void) {
    MockKvStore kv;
    seedStore(kv);
    TEST_ASSERT_TRUE(kv.contains(kCalKey));
    TEST_ASSERT_TRUE(kv.sizeOf(kCalKey) == sizeof(CalRecord));

    CalibrationStore reloaded(kv);
    TEST_ASSERT_FALSE(reloaded.has(0, AoMode::Voltage));
    TEST_ASSERT_TRUE(reloaded.load().ok());
    TEST_ASSERT_TRUE(reloaded.has(0, AoMode::Voltage));
    TEST_ASSERT_TRUE(reloaded.has(1, AoMode::Current));
    TEST_ASSERT_FALSE(reloaded.has(0, AoMode::Current));

    calmath::Coef c;
    TEST_ASSERT_TRUE(reloaded.coef(0, AoMode::Voltage, c).ok());
    TEST_ASSERT_TRUE(nearf(c.a, 10.0f / 4095.0f, 1.0e-9f));
    TEST_ASSERT_TRUE(nearf(c.b, 0.0f, 1.0e-6f));

    uint16_t code = 0;
    TEST_ASSERT_TRUE(reloaded.codeFor(1, AoMode::Current, 12.0f, kFullScale, code).ok());
    float value = 0.0f;
    TEST_ASSERT_TRUE(reloaded.valueFor(1, AoMode::Current, code, value).ok());
    TEST_ASSERT_TRUE(nearf(value, 12.0f, 1.0e-2f));
}

static void test_loadDetectsCorruption(void) {
    MockKvStore kv;
    seedStore(kv);
    patchStored(kv, offsetof(CalRecord, a), 0xFF);
    CalibrationStore reloaded(kv);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::Crc), static_cast<uint8_t>(reloaded.load().err));
    TEST_ASSERT_FALSE(reloaded.has(0, AoMode::Voltage));

    MockKvStore other;
    seedStore(other);
    patchStored(other, sizeof(CalRecord) - 1u, 0x01);
    CalibrationStore tail(other);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::Crc), static_cast<uint8_t>(tail.load().err));
}

static void test_loadRejectsWrongVersionAndMagic(void) {
    MockKvStore kv;
    seedStore(kv);
    patchStored(kv, offsetof(CalRecord, version), 0x02);
    CalibrationStore wrongVersion(kv);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::Unsupported), static_cast<uint8_t>(wrongVersion.load().err));

    MockKvStore other;
    seedStore(other);
    patchStored(other, offsetof(CalRecord, magic), 0xFF);
    CalibrationStore wrongMagic(other);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::Storage), static_cast<uint8_t>(wrongMagic.load().err));
}

static void test_shortBlobIsStorage(void) {
    MockKvStore kv;
    const uint8_t stub[8] = {0};
    TEST_ASSERT_TRUE(kv.putBlob(kCalKey, stub, sizeof(stub)).ok());
    CalibrationStore store(kv);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::Storage), static_cast<uint8_t>(store.load().err));
}

static void test_eraseClearsRecordAndKey(void) {
    MockKvStore kv;
    seedStore(kv);
    CalibrationStore store(kv);
    TEST_ASSERT_TRUE(store.load().ok());
    TEST_ASSERT_TRUE(store.erase().ok());
    TEST_ASSERT_FALSE(store.has(0, AoMode::Voltage));
    TEST_ASSERT_FALSE(kv.contains(kCalKey));
    TEST_ASSERT_TRUE(kv.entryCount() == 0u);
    TEST_ASSERT_TRUE(store.erase().ok());
}

static void test_recordCrcMatchesSavedBlob(void) {
    MockKvStore kv;
    seedStore(kv);
    CalibrationStore store(kv);
    TEST_ASSERT_TRUE(store.load().ok());
    const CalRecord& rec = store.record();
    TEST_ASSERT_EQUAL_UINT32(kCalMagic, rec.magic);
    TEST_ASSERT_EQUAL_UINT16(kCalVersion, rec.version);
    TEST_ASSERT_EQUAL_UINT16(calRecordCrc(rec), rec.crc);
}


void test_endpointsReachableAfterTenNinetyCalibration() {
    const calmath::Point p1 = {6553u, 1.0f};
    const calmath::Point p2 = {58982u, 9.0f};
    calmath::Coef c;
    TEST_ASSERT_TRUE(calmath::solve(p1, p2, c));

    uint16_t code = 0xFFFFu;
    TEST_ASSERT_TRUE(calmath::codeFromValue(c, 0.0f, 0xFFFFu, code));
    TEST_ASSERT_EQUAL_UINT16(0u, code);

    TEST_ASSERT_TRUE(calmath::codeFromValue(c, 10.0f, 0xFFFFu, code));
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, code);

    TEST_ASSERT_FALSE(calmath::codeFromValue(c, 11.0f, 0xFFFFu, code));
    TEST_ASSERT_FALSE(calmath::codeFromValue(c, -1.0f, 0xFFFFu, code));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_solveTwoPoints);
    RUN_TEST(test_endpointsReachableAfterTenNinetyCalibration);
    RUN_TEST(test_roundTripCodeValueCode);
    RUN_TEST(test_solveRejectsSameCode);
    RUN_TEST(test_codeFromValueOutOfRange);
    RUN_TEST(test_notCalibratedBeforeCalibration);
    RUN_TEST(test_rejectsBadAxisAndMode);
    RUN_TEST(test_missingKeyIsStorage);
    RUN_TEST(test_saveLoadKeepsCoefficients);
    RUN_TEST(test_loadDetectsCorruption);
    RUN_TEST(test_loadRejectsWrongVersionAndMagic);
    RUN_TEST(test_shortBlobIsStorage);
    RUN_TEST(test_eraseClearsRecordAndKey);
    RUN_TEST(test_recordCrcMatchesSavedBlob);
    return UNITY_END();
}
