// Testes de host da matematica do SCL3300 da PUSI-DI261930 (Murata SCL3300, datasheet Rev.4 Doc 4921).
// Vetores: as 25 palavras de comando de 32 bits ja com CRC e o quadro de resposta 0x0500DC1C.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "drivers/scl3300_math.h"

namespace {

struct CmdVector {
    uint32_t word;
    const char* label;
};

constexpr CmdVector kCommandWords[] = {
    {0xB4002098u, "SW_RESET"},
    {0xB400046Bu, "POWER_DOWN"},
    {0xB400001Fu, "MODE_1"},
    {0xB4000102u, "MODE_2"},
    {0xB4000225u, "MODE_3"},
    {0xB4000338u, "MODE_4"},
    {0xB0001F6Fu, "ENABLE_ANGLE"},
    {0x240000C7u, "READ_ANG_X"},
    {0x280000CDu, "READ_ANG_Y"},
    {0x2C0000CBu, "READ_ANG_Z"},
    {0x140000EFu, "READ_TEMP"},
    {0x180000E5u, "READ_STATUS"},
    {0x1C0000E3u, "READ_ERR_FLAG1"},
    {0x200000C1u, "READ_ERR_FLAG2"},
    {0x40000091u, "READ_WHOAMI"},
    {0x040000F7u, "READ_ACC_X"},
    {0x080000FDu, "READ_ACC_Y"},
    {0x0C0000FBu, "READ_ACC_Z"},
    {0x100000E9u, "READ_STO"},
    {0x340000DFu, "READ_CMD"},
    {0xFC000073u, "BANK_0"},
    {0xFC00016Eu, "BANK_1"},
    {0x7C0000B3u, "READ_CURRENT_BANK"},
    {0x640000A7u, "READ_SERIAL1"},
    {0x680000ADu, "READ_SERIAL2"},
};

constexpr size_t kCommandWordCount = sizeof(kCommandWords) / sizeof(kCommandWords[0]);

constexpr uint32_t kBodyMask = 0xFFFFFF00u;

constexpr uint32_t kAccXReply = 0x0500DC1Cu;

constexpr uint16_t kWhoAmIData = 0x00C1u;

uint8_t crcByteOf(uint32_t value) {
    return static_cast<uint8_t>(value & 0xFFu);
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

static void test_crc8ReproduzOsVetoresDoDatasheet(void) {
    TEST_ASSERT_EQUAL_UINT32(25u, static_cast<uint32_t>(kCommandWordCount));
    for (size_t i = 0; i < kCommandWordCount; ++i) {
        const uint32_t cmd = kCommandWords[i].word;
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(crcByteOf(cmd), scl::crc8(cmd), kCommandWords[i].label);
    }
}

static void test_crc8CobreApenasOs24Msbs(void) {
    for (size_t i = 0; i < kCommandWordCount; ++i) {
        const uint32_t cmd = kCommandWords[i].word;
        const uint8_t want = crcByteOf(cmd);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(want, scl::crc8(cmd & kBodyMask), kCommandWords[i].label);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(want, scl::crc8((cmd & kBodyMask) | 0x5Au),
                                       kCommandWords[i].label);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(want, scl::crc8((cmd & kBodyMask) | 0xFFu),
                                       kCommandWords[i].label);
    }
}

static void test_withCrcMontaAPalavraCompleta(void) {
    TEST_ASSERT_EQUAL_HEX32(0xB4002098u, scl::withCrc(0xB4002000u));
    TEST_ASSERT_EQUAL_HEX32(0x240000C7u, scl::withCrc(0x24000000u));
    for (size_t i = 0; i < kCommandWordCount; ++i) {
        const uint32_t cmd = kCommandWords[i].word;
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(cmd, scl::withCrc(cmd & kBodyMask),
                                        kCommandWords[i].label);
    }
}

static void test_frameCrcOkAceitaOsVetores(void) {
    for (size_t i = 0; i < kCommandWordCount; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(scl::frameCrcOk(kCommandWords[i].word), kCommandWords[i].label);
    }
    TEST_ASSERT_TRUE(scl::frameCrcOk(kAccXReply));
}

static void test_frameCrcOkRejeitaBitAlterado(void) {
    TEST_ASSERT_FALSE(scl::frameCrcOk(0x0500DD1Cu));
    TEST_ASSERT_FALSE(scl::frameCrcOk(kAccXReply ^ 0x00000100u));
    TEST_ASSERT_FALSE(scl::frameCrcOk(kAccXReply ^ 0x00800000u));
    TEST_ASSERT_FALSE(scl::frameCrcOk(kAccXReply ^ 0x00000001u));
    TEST_ASSERT_FALSE(scl::frameCrcOk(0xB4002099u));
    TEST_ASSERT_FALSE(scl::frameCrcOk(0xB4002198u));
}

static void test_rsEstaDentroDaAreaCobertaPeloCrc(void) {
    TEST_ASSERT_FALSE(scl::frameCrcOk(0x0400DC1Cu));
    TEST_ASSERT_FALSE(scl::frameCrcOk(0x0600DC1Cu));
    TEST_ASSERT_FALSE(scl::frameCrcOk(0x0700DC1Cu));
    TEST_ASSERT_EQUAL_HEX32(0x0700DC1Fu, scl::withCrc(0x0700DC00u));
    TEST_ASSERT_TRUE(scl::frameCrcOk(0x0700DC1Fu));
}

static void test_extracaoDeCampos(void) {
    TEST_ASSERT_EQUAL_HEX8(0x01u, scl::frameOpcode(kAccXReply));
    TEST_ASSERT_EQUAL_UINT8(1u, scl::returnStatus(kAccXReply));
    TEST_ASSERT_EQUAL_HEX16(0x00DCu, scl::frameData(kAccXReply));

    TEST_ASSERT_EQUAL_HEX8(0x01u, scl::frameOpcode(0x040000F7u));
    TEST_ASSERT_EQUAL_HEX8(0x04u, scl::frameOpcode(0x100000E9u));
    TEST_ASSERT_EQUAL_HEX8(0x05u, scl::frameOpcode(0x140000EFu));
    TEST_ASSERT_EQUAL_HEX8(0x06u, scl::frameOpcode(0x180000E5u));
    TEST_ASSERT_EQUAL_HEX8(0x09u, scl::frameOpcode(0x240000C7u));
    TEST_ASSERT_EQUAL_HEX8(0x0Au, scl::frameOpcode(0x280000CDu));
    TEST_ASSERT_EQUAL_HEX8(0x0Bu, scl::frameOpcode(0x2C0000CBu));
    TEST_ASSERT_EQUAL_HEX8(0x10u, scl::frameOpcode(0x40000091u));

    TEST_ASSERT_EQUAL_UINT8(0u, scl::returnStatus(0x240000C7u));
    TEST_ASSERT_EQUAL_UINT8(0u, scl::returnStatus(0xB4002098u));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, scl::frameData(0x240000C7u));
    TEST_ASSERT_EQUAL_HEX16(0x0020u, scl::frameData(0xB4002098u));
    TEST_ASSERT_EQUAL_HEX16(0x001Fu, scl::frameData(0xB0001F6Fu));
    TEST_ASSERT_EQUAL_HEX16(0x0003u, scl::frameData(0xB4000338u));

    const uint32_t whoAmIReply = scl::withCrc(0x41000000u | (static_cast<uint32_t>(kWhoAmIData) << 8));
    TEST_ASSERT_TRUE(scl::frameCrcOk(whoAmIReply));
    TEST_ASSERT_EQUAL_HEX16(kWhoAmIData, scl::frameData(whoAmIReply));
    TEST_ASSERT_EQUAL_UINT8(1u, scl::returnStatus(whoAmIReply));
}

static void test_rsOfNosQuatroValores(void) {
    TEST_ASSERT_EQUAL_UINT8(0u, static_cast<uint8_t>(scl::Rs::Startup));
    TEST_ASSERT_EQUAL_UINT8(1u, static_cast<uint8_t>(scl::Rs::Ok));
    TEST_ASSERT_EQUAL_UINT8(2u, static_cast<uint8_t>(scl::Rs::Reserved));
    TEST_ASSERT_EQUAL_UINT8(3u, static_cast<uint8_t>(scl::Rs::Error));

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(scl::Rs::Startup),
                            static_cast<uint8_t>(scl::rsOf(0x04000000u)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(scl::Rs::Ok),
                            static_cast<uint8_t>(scl::rsOf(0x05000000u)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(scl::Rs::Reserved),
                            static_cast<uint8_t>(scl::rsOf(0x06000000u)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(scl::Rs::Error),
                            static_cast<uint8_t>(scl::rsOf(0x07000000u)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(scl::Rs::Ok),
                            static_cast<uint8_t>(scl::rsOf(kAccXReply)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(scl::Rs::Error),
                            static_cast<uint8_t>(scl::rsOf(0xFFFFFFFFu)));
}

static void test_rsNameNosQuatroValores(void) {
    const char* names[4];
    names[0] = scl::rsName(scl::Rs::Startup);
    names[1] = scl::rsName(scl::Rs::Ok);
    names[2] = scl::rsName(scl::Rs::Reserved);
    names[3] = scl::rsName(scl::Rs::Error);
    for (size_t i = 0; i < 4u; ++i) {
        TEST_ASSERT_NOT_NULL(names[i]);
        TEST_ASSERT_TRUE(strlen(names[i]) > 0u);
    }
    for (size_t i = 0; i < 4u; ++i) {
        for (size_t j = i + 1u; j < 4u; ++j) {
            TEST_ASSERT_TRUE(strcmp(names[i], names[j]) != 0);
        }
    }
}

static void test_angleDeciDegrees(void) {
    TEST_ASSERT_EQUAL_INT16(0, scl::angleDeciDegrees(0x0000u));
    TEST_ASSERT_EQUAL_INT16(900, scl::angleDeciDegrees(0x4000u));
    TEST_ASSERT_EQUAL_INT16(218, scl::angleDeciDegrees(0x0F88u));
    TEST_ASSERT_EQUAL_INT16(450, scl::angleDeciDegrees(0x2000u));
    TEST_ASSERT_EQUAL_INT16(-450, scl::angleDeciDegrees(0xE000u));
    TEST_ASSERT_EQUAL_INT16(-900, scl::angleDeciDegrees(0xC000u));
    TEST_ASSERT_EQUAL_INT16(-1800, scl::angleDeciDegrees(0x8000u));
}

static void test_temperatureDeciC(void) {
    TEST_ASSERT_EQUAL_INT16(266, scl::temperatureDeciC(0x161Eu));
    TEST_ASSERT_EQUAL_INT16(-2730, scl::temperatureDeciC(0x0000u));
    TEST_ASSERT_EQUAL_INT16(-190, scl::temperatureDeciC(4800u));
    TEST_ASSERT_EQUAL_INT16(-84, scl::temperatureDeciC(5000u));
}

static void test_statusSatEhOBit6(void) {
    TEST_ASSERT_EQUAL_HEX16(0x0040u, static_cast<uint16_t>(scl::kStatusSat));
    TEST_ASSERT_EQUAL_HEX16(static_cast<uint16_t>(1u << 6), static_cast<uint16_t>(scl::kStatusSat));
}

static void test_constantesDeComando(void) {
    TEST_ASSERT_EQUAL_HEX32(0xB4002098u, static_cast<uint32_t>(scl::kCmdSwReset));
    TEST_ASSERT_EQUAL_HEX32(0xB400001Fu, static_cast<uint32_t>(scl::kCmdMode1));
    TEST_ASSERT_EQUAL_HEX32(0xB4000102u, static_cast<uint32_t>(scl::kCmdMode2));
    TEST_ASSERT_EQUAL_HEX32(0xB4000225u, static_cast<uint32_t>(scl::kCmdMode3));
    TEST_ASSERT_EQUAL_HEX32(0xB4000338u, static_cast<uint32_t>(scl::kCmdMode4));
    TEST_ASSERT_EQUAL_HEX32(0xB0001F6Fu, static_cast<uint32_t>(scl::kCmdEnableAngle));
    TEST_ASSERT_EQUAL_HEX32(0x240000C7u, static_cast<uint32_t>(scl::kCmdReadAngX));
    TEST_ASSERT_EQUAL_HEX32(0x280000CDu, static_cast<uint32_t>(scl::kCmdReadAngY));
    TEST_ASSERT_EQUAL_HEX32(0x2C0000CBu, static_cast<uint32_t>(scl::kCmdReadAngZ));
    TEST_ASSERT_EQUAL_HEX32(0x140000EFu, static_cast<uint32_t>(scl::kCmdReadTemp));
    TEST_ASSERT_EQUAL_HEX32(0x180000E5u, static_cast<uint32_t>(scl::kCmdReadStatus));
    TEST_ASSERT_EQUAL_HEX32(0x40000091u, static_cast<uint32_t>(scl::kCmdReadWhoAmI));
    TEST_ASSERT_EQUAL_HEX32(0x1C0000E3u, static_cast<uint32_t>(scl::kCmdReadErrFlag1));
    TEST_ASSERT_EQUAL_HEX32(0x200000C1u, static_cast<uint32_t>(scl::kCmdReadErrFlag2));
    TEST_ASSERT_EQUAL_HEX32(0xFC000073u, static_cast<uint32_t>(scl::kCmdBank0));
    TEST_ASSERT_EQUAL_HEX32(0xFC00016Eu, static_cast<uint32_t>(scl::kCmdBank1));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_crc8ReproduzOsVetoresDoDatasheet);
    RUN_TEST(test_crc8CobreApenasOs24Msbs);
    RUN_TEST(test_withCrcMontaAPalavraCompleta);
    RUN_TEST(test_frameCrcOkAceitaOsVetores);
    RUN_TEST(test_frameCrcOkRejeitaBitAlterado);
    RUN_TEST(test_rsEstaDentroDaAreaCobertaPeloCrc);
    RUN_TEST(test_extracaoDeCampos);
    RUN_TEST(test_rsOfNosQuatroValores);
    RUN_TEST(test_rsNameNosQuatroValores);
    RUN_TEST(test_angleDeciDegrees);
    RUN_TEST(test_temperatureDeciC);
    RUN_TEST(test_statusSatEhOBit6);
    RUN_TEST(test_constantesDeComando);
    return UNITY_END();
}
