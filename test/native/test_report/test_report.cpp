// Teste de host do relatorio de fabrica: bloco legivel, linha CSV e persistencia via IKeyValueStore.
// Placa DE-PURI-DI261924 REV A: aqui so a consolidacao de vereditos, sem NVS real e sem Arduino.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "core/report.h"

#include "../mock_kv_store.h"

namespace {

constexpr const char* kSerial = "SN-0001";
constexpr const char* kDate = "2026-08-28";
constexpr const char* kFw = "0.1.0";
constexpr const char* kRev = "A";

uint16_t countPattern(const char* text, const char* pattern) {
    uint16_t hits = 0;
    const size_t step = strlen(pattern);
    for (const char* p = strstr(text, pattern); p != nullptr; p = strstr(p + step, pattern)) {
        ++hits;
    }
    return hits;
}

uint16_t countChar(const char* text, char wanted) {
    uint16_t hits = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == wanted) {
            ++hits;
        }
    }
    return hits;
}

const char* dataLine(const char* csv, char* line, size_t cap) {
    const char* begin = strstr(csv, kResultBegin);
    if (begin == nullptr) {
        return nullptr;
    }
    const char* head = strstr(begin, "\r\n");
    if (head == nullptr) {
        return nullptr;
    }
    head += 2;
    const char* eol = strstr(head, "\r\n");
    if (eol == nullptr) {
        return nullptr;
    }
    const size_t len = static_cast<size_t>(eol - head);
    if (len + 1u > cap) {
        return nullptr;
    }
    memcpy(line, head, len);
    line[len] = '\0';
    return eol + 2;
}

void fillReport(Report& rep) {
    rep.setMeta(kFw, kRev);
    rep.setSerial(kSerial);
    rep.setDate(kDate);
    TEST_ASSERT_TRUE(rep.record("t0", "Boot e identificacao", Verdict::Pass, "ok", 100).ok());
    TEST_ASSERT_TRUE(rep.record("t1", "Reles LIM1..LIM4", Verdict::Skip, "pulado", 200).ok());
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

static void test_recordOverwritesSameId(void) {
    Report rep;
    TEST_ASSERT_EQUAL_UINT8(0, rep.count());
    TEST_ASSERT_TRUE(rep.record("t0", "primeiro", Verdict::Pass, "ok", 10).ok());
    TEST_ASSERT_EQUAL_UINT8(1, rep.count());
    TEST_ASSERT_TRUE(rep.record("t0", "segundo", Verdict::Fail, "nok", 20).ok());
    TEST_ASSERT_EQUAL_UINT8(1, rep.count());
    TEST_ASSERT_EQUAL_STRING("t0", rep.at(0).id);
    TEST_ASSERT_EQUAL_STRING("segundo", rep.at(0).name);
    TEST_ASSERT_EQUAL_STRING("nok", rep.at(0).note);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::Fail), static_cast<uint8_t>(rep.at(0).verdict));
    TEST_ASSERT_EQUAL_UINT32(20u, rep.at(0).uptimeMs);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::Param),
                            static_cast<uint8_t>(rep.record(nullptr, "x", Verdict::Pass, "", 0).err));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::NotRun), static_cast<uint8_t>(rep.at(1).verdict));
}

static void test_anyFail(void) {
    Report rep;
    TEST_ASSERT_FALSE(rep.anyFail());
    TEST_ASSERT_TRUE(rep.record("t0", "a", Verdict::Pass, "", 0).ok());
    TEST_ASSERT_TRUE(rep.record("t1", "b", Verdict::Skip, "", 0).ok());
    TEST_ASSERT_TRUE(rep.record("t2", "c", Verdict::NotRun, "", 0).ok());
    TEST_ASSERT_FALSE(rep.anyFail());
    TEST_ASSERT_TRUE(rep.record("t3", "d", Verdict::Fail, "", 0).ok());
    TEST_ASSERT_TRUE(rep.anyFail());

    Report aborted;
    TEST_ASSERT_TRUE(aborted.record("t0", "a", Verdict::Abort, "", 0).ok());
    TEST_ASSERT_TRUE(aborted.anyFail());

    aborted.clear();
    TEST_ASSERT_FALSE(aborted.anyFail());
    TEST_ASSERT_EQUAL_UINT8(0, aborted.count());
}

static void test_csvHasExactlyOneDataLine(void) {
    Report rep;
    fillReport(rep);

    char csv[512];
    const uint16_t n = rep.formatCsv(csv, sizeof(csv));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(static_cast<size_t>(n) < sizeof(csv));
    TEST_ASSERT_EQUAL_UINT32(n, static_cast<uint32_t>(strlen(csv)));
    TEST_ASSERT_EQUAL_UINT16(1, countPattern(csv, kResultBegin));
    TEST_ASSERT_EQUAL_UINT16(1, countPattern(csv, kResultEnd));
    TEST_ASSERT_EQUAL_UINT16(3, countPattern(csv, "\r\n"));

    char line[256];
    const char* rest = dataLine(csv, line, sizeof(line));
    TEST_ASSERT_NOT_NULL(rest);
    TEST_ASSERT_EQUAL_UINT32(strlen(kResultEnd) + 2u, static_cast<uint32_t>(strlen(rest)));
    TEST_ASSERT_EQUAL_STRING_LEN(kResultEnd, rest, strlen(kResultEnd));

    TEST_ASSERT_EQUAL_STRING_LEN(kSerial, line, strlen(kSerial));
    TEST_ASSERT_NOT_NULL(strstr(line, "t0=P"));
    TEST_ASSERT_NOT_NULL(strstr(line, "t1=S"));
    TEST_ASSERT_NOT_NULL(strstr(line, ",INCOMPLETO,"));
    TEST_ASSERT_EQUAL_UINT16(6, countChar(line, ','));
}

static void test_csvSanitizesSerialComma(void) {
    Report rep;
    rep.setMeta(kFw, kRev);
    rep.setSerial("SN,0002;X");
    rep.setDate("2026-08-28,extra");
    TEST_ASSERT_TRUE(rep.record("t0", "boot", Verdict::Pass, "nota, com virgula", 5).ok());

    TEST_ASSERT_EQUAL_STRING("SN 0002 X", rep.serial());
    TEST_ASSERT_EQUAL_STRING("2026-08-28 extra", rep.date());

    char csv[512];
    TEST_ASSERT_TRUE(rep.formatCsv(csv, sizeof(csv)) > 0);
    char line[256];
    const char* rest = dataLine(csv, line, sizeof(line));
    TEST_ASSERT_NOT_NULL(rest);
    TEST_ASSERT_EQUAL_STRING_LEN("SN 0002 X", line, strlen("SN 0002 X"));
    TEST_ASSERT_EQUAL_UINT16(5, countChar(line, ','));
    TEST_ASSERT_NOT_NULL(strstr(line, "t0=P"));
    TEST_ASSERT_EQUAL_UINT16(3, countPattern(csv, "\r\n"));
}

static void test_csvKeepsEmptySerialUsable(void) {
    Report rep;
    rep.setMeta(kFw, kRev);
    rep.setSerial("");
    rep.setDate(nullptr);
    TEST_ASSERT_EQUAL_STRING("NO-SERIAL", rep.serial());
    TEST_ASSERT_EQUAL_STRING("NO-DATE", rep.date());

    char csv[256];
    TEST_ASSERT_TRUE(rep.formatCsv(csv, sizeof(csv)) > 0);
    char line[128];
    TEST_ASSERT_NOT_NULL(dataLine(csv, line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING_LEN("NO-SERIAL", line, strlen("NO-SERIAL"));
}

static void test_formatHumanFitsSmallBuffer(void) {
    Report rep;
    fillReport(rep);

    char guarded[128];
    memset(guarded, 0x5A, sizeof(guarded));
    const uint16_t cap = 24;
    const uint16_t n = rep.formatHuman(guarded, cap);
    TEST_ASSERT_TRUE(n < cap);
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(guarded[n]));
    TEST_ASSERT_EQUAL_UINT32(n, static_cast<uint32_t>(strlen(guarded)));
    for (size_t i = cap; i < sizeof(guarded); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5A, static_cast<uint8_t>(guarded[i]));
    }

    char one[1];
    one[0] = 'Z';
    TEST_ASSERT_EQUAL_UINT16(0, rep.formatHuman(one, 0));
    TEST_ASSERT_EQUAL_UINT16(0, rep.formatCsv(one, 0));
    TEST_ASSERT_EQUAL_UINT16(0, rep.formatHuman(nullptr, 32));
    TEST_ASSERT_EQUAL_UINT16(0, rep.formatCsv(nullptr, 32));
}

static void test_formatHumanFullBuffer(void) {
    Report rep;
    fillReport(rep);
    TEST_ASSERT_TRUE(rep.record("t2", "Saida analogica", Verdict::Fail, "fora de faixa", 300).ok());

    char text[1024];
    const uint16_t n = rep.formatHuman(text, sizeof(text));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(static_cast<size_t>(n) < sizeof(text));
    TEST_ASSERT_EQUAL_UINT32(n, static_cast<uint32_t>(strlen(text)));
    TEST_ASSERT_NOT_NULL(strstr(text, kSerial));
    TEST_ASSERT_NOT_NULL(strstr(text, kDate));
    TEST_ASSERT_NOT_NULL(strstr(text, "VEREDITO GERAL: FAIL"));
}

static void test_saveLoadKeepsSerialDateAndVerdicts(void) {
    MockKvStoreBig kv;
    Report saved;
    fillReport(saved);
    TEST_ASSERT_TRUE(saved.record("t2", "Saida analogica", Verdict::Fail, "fora de faixa", 300).ok());
    TEST_ASSERT_TRUE(saved.save(kv).ok());
    TEST_ASSERT_TRUE(kv.contains("report"));

    Report loaded;
    TEST_ASSERT_EQUAL_STRING("NO-SERIAL", loaded.serial());
    TEST_ASSERT_TRUE(loaded.load(kv).ok());
    TEST_ASSERT_EQUAL_STRING(kSerial, loaded.serial());
    TEST_ASSERT_EQUAL_STRING(kDate, loaded.date());
    TEST_ASSERT_EQUAL_UINT8(saved.count(), loaded.count());
    for (uint8_t i = 0; i < saved.count(); ++i) {
        TEST_ASSERT_EQUAL_STRING(saved.at(i).id, loaded.at(i).id);
        TEST_ASSERT_EQUAL_STRING(saved.at(i).name, loaded.at(i).name);
        TEST_ASSERT_EQUAL_STRING(saved.at(i).note, loaded.at(i).note);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(saved.at(i).verdict), static_cast<uint8_t>(loaded.at(i).verdict));
        TEST_ASSERT_EQUAL_UINT32(saved.at(i).uptimeMs, loaded.at(i).uptimeMs);
    }
    TEST_ASSERT_TRUE(loaded.anyFail());
}

static void test_loadRejectsMissingAndBadBlob(void) {
    MockKvStoreBig kv;
    Report rep;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::Storage), static_cast<uint8_t>(rep.load(kv).err));

    const uint8_t stub[16] = {0};
    TEST_ASSERT_TRUE(kv.putBlob("report", stub, sizeof(stub)).ok());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::Storage), static_cast<uint8_t>(rep.load(kv).err));
}

static void test_overflowReturnsRange(void) {
    Report rep;
    char id[kReportIdLen];
    for (uint8_t i = 0; i < kReportMaxItems; ++i) {
        id[0] = 'i';
        id[1] = static_cast<char>('a' + i);
        id[2] = '\0';
        TEST_ASSERT_TRUE(rep.record(id, "teste", Verdict::Pass, "", i).ok());
    }
    TEST_ASSERT_EQUAL_UINT8(kReportMaxItems, rep.count());

    id[0] = 'z';
    id[1] = 'z';
    id[2] = '\0';
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::Range),
                            static_cast<uint8_t>(rep.record(id, "excedente", Verdict::Pass, "", 0).err));
    TEST_ASSERT_EQUAL_UINT8(kReportMaxItems, rep.count());

    id[0] = 'i';
    id[1] = 'a';
    id[2] = '\0';
    TEST_ASSERT_TRUE(rep.record(id, "regravado", Verdict::Fail, "", 0).ok());
    TEST_ASSERT_EQUAL_UINT8(kReportMaxItems, rep.count());
    TEST_ASSERT_EQUAL_STRING("regravado", rep.at(0).name);
}


static void test_overallRequiresEveryItemExecuted(void) {
    Report empty;
    TEST_ASSERT_EQUAL_STRING("INCOMPLETO", empty.overallText());

    Report rep;
    rep.record("t0", "power", Verdict::Pass, "", 1);
    rep.record("t1", "analog", Verdict::Skip, "sem calibracao", 2);
    TEST_ASSERT_EQUAL_STRING("INCOMPLETO", rep.overallText());

    Report good;
    good.record("t0", "power", Verdict::Pass, "", 1);
    good.record("t1", "analog", Verdict::Pass, "", 2);
    TEST_ASSERT_EQUAL_STRING("PASS", good.overallText());

    Report bad;
    bad.record("t0", "power", Verdict::Fail, "trilho fora", 1);
    bad.record("t1", "analog", Verdict::Skip, "", 2);
    TEST_ASSERT_EQUAL_STRING("FAIL", bad.overallText());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_recordOverwritesSameId);
    RUN_TEST(test_overallRequiresEveryItemExecuted);
    RUN_TEST(test_anyFail);
    RUN_TEST(test_csvHasExactlyOneDataLine);
    RUN_TEST(test_csvSanitizesSerialComma);
    RUN_TEST(test_csvKeepsEmptySerialUsable);
    RUN_TEST(test_formatHumanFitsSmallBuffer);
    RUN_TEST(test_formatHumanFullBuffer);
    RUN_TEST(test_saveLoadKeepsSerialDateAndVerdicts);
    RUN_TEST(test_loadRejectsMissingAndBadBlob);
    RUN_TEST(test_overflowReturnsRange);
    return UNITY_END();
}
