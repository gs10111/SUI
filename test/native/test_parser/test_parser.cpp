// Teste de host do tokenizador do console (115200 8N1, folha 1/2: USB-UART do modulo ESP32).
// So logica de texto: nada de Serial nem de hardware.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "core/cmd_parser.h"

namespace {

bool nearf(float lhs, float rhs, float tol) {
    const float diff = (lhs > rhs) ? (lhs - rhs) : (rhs - lhs);
    return diff <= tol;
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

static void test_parseSplitsOnSpacesAndTabs(void) {
    cmd::Line line;
    TEST_ASSERT_TRUE(cmd::parse("  ao \t set   x  2.5 \r\n", line));
    TEST_ASSERT_FALSE(line.truncated);
    TEST_ASSERT_EQUAL_UINT8(4, line.argc);
    TEST_ASSERT_EQUAL_STRING("ao", line.argv[0]);
    TEST_ASSERT_EQUAL_STRING("set", line.argv[1]);
    TEST_ASSERT_EQUAL_STRING("x", line.argv[2]);
    TEST_ASSERT_EQUAL_STRING("2.5", line.argv[3]);
    TEST_ASSERT_NULL(line.argv[4]);
}

static void test_parseEmptyAndBlankLines(void) {
    cmd::Line line;
    TEST_ASSERT_FALSE(cmd::parse("", line));
    TEST_ASSERT_EQUAL_UINT8(0, line.argc);
    TEST_ASSERT_FALSE(line.truncated);

    TEST_ASSERT_FALSE(cmd::parse("   \t \r\n", line));
    TEST_ASSERT_EQUAL_UINT8(0, line.argc);
    TEST_ASSERT_FALSE(line.truncated);

    TEST_ASSERT_FALSE(cmd::parse(nullptr, line));
    TEST_ASSERT_EQUAL_UINT8(0, line.argc);
}

static void fillTokens(char* out, uint8_t count) {
    uint16_t n = 0;
    for (uint8_t i = 0; i < count; ++i) {
        out[n++] = 'a';
        out[n++] = ' ';
    }
    out[n - 1] = '\0';
}

static void test_parseTooManyTokensSetsTruncated(void) {
    cmd::Line line;
    char many[cmd::kMaxLine];
    char exact[cmd::kMaxLine];
    fillTokens(many, static_cast<uint8_t>(cmd::kMaxTokens + 2));
    fillTokens(exact, cmd::kMaxTokens);

    TEST_ASSERT_TRUE(cmd::parse(many, line));
    TEST_ASSERT_TRUE(line.truncated);
    TEST_ASSERT_TRUE(line.tokenLimit);
    TEST_ASSERT_EQUAL_UINT8(cmd::kMaxTokens, line.argc);
    TEST_ASSERT_EQUAL_STRING("a", line.argv[0]);
    TEST_ASSERT_EQUAL_STRING("a", line.argv[cmd::kMaxTokens - 1]);

    TEST_ASSERT_TRUE(cmd::parse(exact, line));
    TEST_ASSERT_FALSE(line.truncated);
    TEST_ASSERT_FALSE(line.tokenLimit);
    TEST_ASSERT_EQUAL_UINT8(cmd::kMaxTokens, line.argc);
}

// Regressao: 'rs485 ping' com o quadro do jig byte a byte cabia em 12 palavras, mas o
// limite antigo de 8 cortava a linha e so 6 bytes iam para o barramento.
static void test_parsePingFrameKeepsEveryHexByte(void) {
    cmd::Line line;
    TEST_ASSERT_TRUE(cmd::parse("rs485 ping 02 54 04 01 00 00 00 FD F3 03", line));
    TEST_ASSERT_FALSE(line.truncated);
    TEST_ASSERT_FALSE(line.tokenLimit);
    TEST_ASSERT_EQUAL_UINT8(12, line.argc);
    TEST_ASSERT_EQUAL_STRING("02", line.argv[2]);
    TEST_ASSERT_EQUAL_STRING("03", line.argv[11]);
}

static void test_parseLongLineSetsTruncated(void) {
    char exact[cmd::kMaxLine];
    memset(exact, 'a', sizeof(exact));
    exact[cmd::kMaxLine - 1] = '\0';

    cmd::Line line;
    TEST_ASSERT_TRUE(cmd::parse(exact, line));
    TEST_ASSERT_FALSE(line.truncated);
    TEST_ASSERT_EQUAL_UINT8(1, line.argc);
    TEST_ASSERT_EQUAL_UINT32(cmd::kMaxLine - 1u, static_cast<uint32_t>(strlen(line.argv[0])));

    char big[cmd::kMaxLine * 2];
    memset(big, 'b', sizeof(big));
    big[sizeof(big) - 1] = '\0';

    TEST_ASSERT_TRUE(cmd::parse(big, line));
    TEST_ASSERT_TRUE(line.truncated);
    TEST_ASSERT_EQUAL_UINT8(1, line.argc);
    TEST_ASSERT_EQUAL_UINT32(cmd::kMaxLine - 1u, static_cast<uint32_t>(strlen(line.argv[0])));
}

static void test_equalsIgnoreCase(void) {
    TEST_ASSERT_TRUE(cmd::equalsIgnoreCase("Relay", "relay"));
    TEST_ASSERT_TRUE(cmd::equalsIgnoreCase("AO", "ao"));
    TEST_ASSERT_FALSE(cmd::equalsIgnoreCase("ao", "aos"));
    TEST_ASSERT_FALSE(cmd::equalsIgnoreCase("ao", nullptr));
    TEST_ASSERT_FALSE(cmd::equalsIgnoreCase(nullptr, "ao"));
}

static void test_parseU32(void) {
    uint32_t v = 0xDEADBEEFu;
    TEST_ASSERT_TRUE(cmd::parseU32("0", v));
    TEST_ASSERT_EQUAL_UINT32(0u, v);
    TEST_ASSERT_TRUE(cmd::parseU32("4095", v));
    TEST_ASSERT_EQUAL_UINT32(4095u, v);
    TEST_ASSERT_TRUE(cmd::parseU32("4294967295", v));
    TEST_ASSERT_EQUAL_UINT32(4294967295u, v);
    TEST_ASSERT_TRUE(cmd::parseU32("0x1F", v));
    TEST_ASSERT_EQUAL_UINT32(31u, v);
    TEST_ASSERT_TRUE(cmd::parseU32("0XFF", v));
    TEST_ASSERT_EQUAL_UINT32(255u, v);

    TEST_ASSERT_FALSE(cmd::parseU32("12x", v));
    TEST_ASSERT_FALSE(cmd::parseU32("0x", v));
    TEST_ASSERT_FALSE(cmd::parseU32("", v));
    TEST_ASSERT_FALSE(cmd::parseU32(nullptr, v));
    TEST_ASSERT_FALSE(cmd::parseU32("abc", v));
}

static void test_parseI32(void) {
    int32_t v = 7;
    TEST_ASSERT_TRUE(cmd::parseI32("-100", v));
    TEST_ASSERT_EQUAL_INT32(-100, v);
    TEST_ASSERT_TRUE(cmd::parseI32("0", v));
    TEST_ASSERT_EQUAL_INT32(0, v);
    TEST_ASSERT_TRUE(cmd::parseI32("2147483647", v));
    TEST_ASSERT_EQUAL_INT32(2147483647, v);
    TEST_ASSERT_TRUE(cmd::parseI32("-2147483648", v));
    TEST_ASSERT_EQUAL_INT32(-2147483647 - 1, v);

    TEST_ASSERT_FALSE(cmd::parseI32("-", v));
    TEST_ASSERT_FALSE(cmd::parseI32("12x", v));
    TEST_ASSERT_FALSE(cmd::parseI32("0x10", v));
    TEST_ASSERT_FALSE(cmd::parseI32("", v));
    TEST_ASSERT_FALSE(cmd::parseI32(nullptr, v));
}

static void test_parseFloat(void) {
    float v = 99.0f;
    TEST_ASSERT_TRUE(cmd::parseFloat("2.5", v));
    TEST_ASSERT_TRUE(nearf(v, 2.5f, 1.0e-6f));
    TEST_ASSERT_TRUE(cmd::parseFloat("-0.125", v));
    TEST_ASSERT_TRUE(nearf(v, -0.125f, 1.0e-6f));
    TEST_ASSERT_TRUE(cmd::parseFloat("20", v));
    TEST_ASSERT_TRUE(nearf(v, 20.0f, 1.0e-6f));
    TEST_ASSERT_TRUE(cmd::parseFloat("1e3", v));
    TEST_ASSERT_TRUE(nearf(v, 1000.0f, 1.0e-3f));

    TEST_ASSERT_FALSE(cmd::parseFloat("2.5v", v));
    TEST_ASSERT_FALSE(cmd::parseFloat("abc", v));
    TEST_ASSERT_FALSE(cmd::parseFloat("", v));
    TEST_ASSERT_FALSE(cmd::parseFloat(nullptr, v));
}

static void test_parseAxis(void) {
    uint8_t axis = 9;
    TEST_ASSERT_TRUE(cmd::parseAxis("x", axis));
    TEST_ASSERT_EQUAL_UINT8(0, axis);
    TEST_ASSERT_TRUE(cmd::parseAxis("X", axis));
    TEST_ASSERT_EQUAL_UINT8(0, axis);
    TEST_ASSERT_TRUE(cmd::parseAxis("0", axis));
    TEST_ASSERT_EQUAL_UINT8(0, axis);
    TEST_ASSERT_TRUE(cmd::parseAxis("y", axis));
    TEST_ASSERT_EQUAL_UINT8(1, axis);
    TEST_ASSERT_TRUE(cmd::parseAxis("Y", axis));
    TEST_ASSERT_EQUAL_UINT8(1, axis);
    TEST_ASSERT_TRUE(cmd::parseAxis("1", axis));
    TEST_ASSERT_EQUAL_UINT8(1, axis);

    TEST_ASSERT_FALSE(cmd::parseAxis("z", axis));
    TEST_ASSERT_FALSE(cmd::parseAxis("xx", axis));
    TEST_ASSERT_FALSE(cmd::parseAxis("2", axis));
    TEST_ASSERT_FALSE(cmd::parseAxis("", axis));
    TEST_ASSERT_FALSE(cmd::parseAxis(nullptr, axis));
}

static void test_parseOnOff(void) {
    bool on = false;
    TEST_ASSERT_TRUE(cmd::parseOnOff("on", on));
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_TRUE(cmd::parseOnOff("ON", on));
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_TRUE(cmd::parseOnOff("1", on));
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_TRUE(cmd::parseOnOff("True", on));
    TEST_ASSERT_TRUE(on);

    TEST_ASSERT_TRUE(cmd::parseOnOff("off", on));
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_TRUE(cmd::parseOnOff("OFF", on));
    TEST_ASSERT_FALSE(on);
    on = true;
    TEST_ASSERT_TRUE(cmd::parseOnOff("0", on));
    TEST_ASSERT_FALSE(on);
    on = true;
    TEST_ASSERT_TRUE(cmd::parseOnOff("false", on));
    TEST_ASSERT_FALSE(on);

    TEST_ASSERT_FALSE(cmd::parseOnOff("maybe", on));
    TEST_ASSERT_FALSE(cmd::parseOnOff("", on));
    TEST_ASSERT_FALSE(cmd::parseOnOff(nullptr, on));
}

static void test_parseHexBytes(void) {
    uint8_t out[8];
    uint8_t len = 0xFF;
    const uint8_t expected[] = {0x01, 0x02, 0xFF};

    TEST_ASSERT_TRUE(cmd::parseHexBytes("01 02 FF", out, sizeof(out), len));
    TEST_ASSERT_EQUAL_UINT8(3, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 3);

    TEST_ASSERT_TRUE(cmd::parseHexBytes("01:02-ff", out, sizeof(out), len));
    TEST_ASSERT_EQUAL_UINT8(3, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 3);

    TEST_ASSERT_TRUE(cmd::parseHexBytes("01,02_FF", out, sizeof(out), len));
    TEST_ASSERT_EQUAL_UINT8(3, len);

    TEST_ASSERT_TRUE(cmd::parseHexBytes("0x0A0x0B", out, sizeof(out), len));
    TEST_ASSERT_EQUAL_UINT8(2, len);
    TEST_ASSERT_EQUAL_HEX8(0x0A, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0B, out[1]);

    TEST_ASSERT_FALSE(cmd::parseHexBytes("0AB", out, sizeof(out), len));
    TEST_ASSERT_FALSE(cmd::parseHexBytes("1 2", out, sizeof(out), len));
    TEST_ASSERT_FALSE(cmd::parseHexBytes("GG", out, sizeof(out), len));
    TEST_ASSERT_FALSE(cmd::parseHexBytes("", out, sizeof(out), len));
    TEST_ASSERT_EQUAL_UINT8(0, len);
    TEST_ASSERT_FALSE(cmd::parseHexBytes(nullptr, out, sizeof(out), len));
    TEST_ASSERT_FALSE(cmd::parseHexBytes("0102", nullptr, sizeof(out), len));
    TEST_ASSERT_FALSE(cmd::parseHexBytes("0102030405", out, 3, len));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_parseSplitsOnSpacesAndTabs);
    RUN_TEST(test_parseEmptyAndBlankLines);
    RUN_TEST(test_parseTooManyTokensSetsTruncated);
    RUN_TEST(test_parsePingFrameKeepsEveryHexByte);
    RUN_TEST(test_parseLongLineSetsTruncated);
    RUN_TEST(test_equalsIgnoreCase);
    RUN_TEST(test_parseU32);
    RUN_TEST(test_parseI32);
    RUN_TEST(test_parseFloat);
    RUN_TEST(test_parseAxis);
    RUN_TEST(test_parseOnOff);
    RUN_TEST(test_parseHexBytes);
    return UNITY_END();
}
