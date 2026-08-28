// Item t4: display SPI da IHM no CN4. Folha 1/2 (VSPI: MOSI/SCLK/CS/DC/RESET, sem MISO).
// Sem leitura de volta: o veredito e sempre visual (ESP32-WROOM-32D, Espressif).
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "board_pins.h"
#include "build_config.h"
#include "core/ctx.h"
#include "core/test_runner.h"
#include "status.h"
#include "verdict.h"

namespace {

constexpr bool kIhmEnabled = (IHM_ENABLED != 0);

constexpr const char* kDisplayWhere = "IHM ligada no CN4";

char g_whatBuf[96];
char g_textBuf[96];
char g_noteBuf[96];

TestResult abortResult() {
    return TestResult(Verdict::Abort, "abortado pelo operador");
}

bool equalsNoCase(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = static_cast<char>(ca + 32);
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = static_cast<char>(cb + 32);
        }
        if (ca != cb) {
            return false;
        }
        ++i;
    }
    return a[i] == b[i];
}

const char* safeText(const char* text) {
    return (text != nullptr && text[0] != '\0') ? text : "(nao informado)";
}

TestResult runPatterns(Ctx& ctx) {
    const uint8_t total = ctx.display.patternCount();
    if (total == 0) {
        return TestResult(Verdict::Fail, "driver de display sem padroes de teste");
    }
    for (uint8_t i = 0; i < total; ++i) {
        const Status st = ctx.display.showPattern(i);
        if (st.failed()) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "padrao %u recusado pelo driver (%s)",
                     static_cast<unsigned>(i), errName(st.err));
            return TestResult(Verdict::Fail, g_noteBuf);
        }
        snprintf(g_whatBuf, sizeof(g_whatBuf), "observar o padrao %u de %u no display",
                 static_cast<unsigned>(i + 1), static_cast<unsigned>(total));
        ctx.op.step(g_whatBuf, kDisplayWhere, safeText(ctx.display.patternDescription(i)));
        const Verdict v = ctx.op.ask("resultado?");
        if (v == Verdict::Abort) {
            return abortResult();
        }
        if (v == Verdict::Fail) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "padrao %u errado: CN4, DC/RESET/CS ou +3V3 da IHM",
                     static_cast<unsigned>(i));
            return TestResult(Verdict::Fail, g_noteBuf);
        }
    }
    return TestResult(Verdict::Pass, "");
}

TestResult runIdentityPattern(Ctx& ctx) {
    snprintf(g_textBuf, sizeof(g_textBuf), "FW %s REV %s SN %s", safeText(ctx.fwVersion),
             safeText(ctx.boardRev), safeText(ctx.report.serial()));
    const Status st = ctx.display.writeText(g_textBuf);
    if (st.failed()) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "writeText recusado pelo driver (%s)", errName(st.err));
        return TestResult(Verdict::Fail, g_noteBuf);
    }
    ctx.op.step("conferir o texto de identificacao no display", kDisplayWhere, g_textBuf);
    const Verdict v = ctx.op.ask("texto legivel e igual ao esperado?");
    if (v == Verdict::Abort) {
        return abortResult();
    }
    if (v == Verdict::Fail) {
        return TestResult(Verdict::Fail, "texto ilegivel: SPI VSPI, DC/CS ou contraste");
    }
    return TestResult(Verdict::Pass, "");
}

class Test04Display : public ITest {
public:
    const char* id() const override { return "t4"; }
    const char* name() const override { return "Display SPI CN4"; }
    uint8_t order() const override { return 4; }

    TestResult run(Ctx& ctx) override {
        if (!kIhmEnabled) {
            ctx.op.info("build sem IHM: compile o env esp32dev-ihm (SSD1322 256x64 via U8g2)");
            return TestResult(Verdict::Skip, "IHM nao habilitada nesta build (IHM_ENABLED=0)");
        }
        const char* driver = ctx.display.driverName();
        ctx.op.info("driver de display: %s", safeText(driver));
        if (equalsNoCase(driver, "null")) {
            ctx.op.info("driver 'null' selecionado: a IHM nao esta conectada a este jig");
            return TestResult(Verdict::Skip, "driver 'null': IHM do CN4 nao conectada");
        }
        ctx.op.info("CN4: MOSI IO%d, SCLK IO%d, CS IO%d, DC IO%d, RESET IO%d, %u Hz",
                    static_cast<int>(board::kDispMosi), static_cast<int>(board::kDispSclk),
                    static_cast<int>(board::kDispCs), static_cast<int>(board::kDispDc),
                    static_cast<int>(board::kDispReset), static_cast<unsigned>(board::kDisplaySpiHz));

        Status st = ctx.display.hardReset();
        if (st.failed()) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "hardReset falhou (%s): RESET IO%d ou CN4",
                     errName(st.err), static_cast<int>(board::kDispReset));
            return TestResult(Verdict::Fail, g_noteBuf);
        }
        st = ctx.display.begin();
        if (st.failed()) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "begin do display falhou (%s): CN4 ou VSPI",
                     errName(st.err));
            return TestResult(Verdict::Fail, g_noteBuf);
        }

        const TestResult patterns = runPatterns(ctx);
        if (patterns.verdict != Verdict::Pass) {
            return patterns;
        }
        const TestResult identity = runIdentityPattern(ctx);
        if (identity.verdict != Verdict::Pass) {
            return identity;
        }
        return TestResult(Verdict::Pass, "padroes e texto de identificacao conferidos");
    }
};

}  // namespace

REGISTER_TEST(Test04Display);
