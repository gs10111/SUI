// Item t0: boot, alimentacao e identidade. Folha 1/2 (CN1A/B/C, PTC2, D18 1N4007, CN2A/CN2B).
// Datasheets: LM2575-5 (TI SNVS106), A0515S-2WR3 (Mornsun), ESP32-WROOM-32D (Espressif).
#include <stdint.h>

#include "board_pins.h"
#include "core/ctx.h"
#include "core/test_runner.h"
#include "verdict.h"

namespace {

struct GuidedPoint {
    const char* what;
    const char* where;
    const char* expected;
    const char* failNote;
};

constexpr GuidedPoint kAcRails[] = {
    {"alimentar por 100-240 VAC em CN1A/CN1B/CN1C e medir +5 V", "saida do LM2575-5 (+5V)",
     "+5,00 V +/-5% (4,75 a 5,25 V)", "+5V em VAC fora: LM2575-5, L1 ou diodo"},
    {"medir +3V3 com a placa em 100-240 VAC", "trilho +3V3 (alimentacao do ESP32)",
     "+3,30 V +/-5% (3,14 a 3,47 V)", "+3V3 em VAC fora: regulador 3V3 ou curto"},
    {"medir +15 V na saida do conversor isolado", "saida +15V do A0515S-2WR3",
     "+15,0 V +/-5% (14,25 a 15,75 V)", "+15V fora: A0515S-2WR3 ou +5V de entrada"},
    {"medir -15 V na saida do conversor isolado", "saida -15V do A0515S-2WR3",
     "-15,0 V +/-5% (-14,25 a -15,75 V)", "-15V fora: A0515S-2WR3 (saida negativa)"},
};

constexpr GuidedPoint kDcRails[] = {
    {"desligar a VAC, alimentar com 24 VCC em CN1B/CN1C e medir +5 V", "saida do LM2575-5 (+5V)",
     "+5,00 V +/-5% (4,75 a 5,25 V)", "+5V em 24VCC: PTC2, D18 1N4007 ou LM2575"},
    {"medir +3V3 com a placa em 24 VCC", "trilho +3V3 (alimentacao do ESP32)",
     "+3,30 V +/-5% (3,14 a 3,47 V)", "+3V3 em 24VCC fora: regulador 3V3"},
    {"medir +15 V com a placa em 24 VCC", "saida +15V do A0515S-2WR3",
     "+15,0 V +/-5% (14,25 a 15,75 V)", "+15V em 24VCC: A0515S-2WR3"},
    {"medir -15 V com a placa em 24 VCC", "saida -15V do A0515S-2WR3",
     "-15,0 V +/-5% (-14,25 a -15,75 V)", "-15V em 24VCC: A0515S-2WR3"},
    {"inverter a polaridade dos 24 VCC em CN1B/CN1C por poucos segundos", "fonte de bancada em 24 VCC",
     "D18 1N4007 bloqueia: consumo zero, placa apagada, sem aquecimento",
     "D18 1N4007 nao bloqueou: invertido ou em curto"},
    {"voltar a polaridade correta dos 24 VCC", "fonte de bancada em 24 VCC",
     "placa volta a ligar normalmente", "placa nao religa: PTC2 aberto apos o evento"},
};

constexpr GuidedPoint kSensorRail[] = {
    {"conectar a placa sensora e medir o +5 V entregue a ela", "CN2A/CN2B (+5V para a sensora)",
     "+5,00 V +/-5% (4,75 a 5,25 V) com a sensora conectada",
     "+5V CN2A/CN2B ausente: trilha CN2 ou sobrecarga"},
};

constexpr GuidedPoint kCurrentDraw[] = {
    {"medir o consumo total em 100-240 VAC, placa em repouso", "entrada CN1A/CN1B/CN1C",
     "consumo estavel, sem oscilacao e sem aquecimento do LM2575-5",
     "consumo em VAC fora do esperado: fonte ou carga"},
    {"medir o consumo total em 24 VCC, placa em repouso", "entrada CN1B/CN1C",
     "consumo estavel, sem oscilacao e sem aquecimento do LM2575-5",
     "consumo em 24VCC fora do esperado: fonte ou carga"},
};

constexpr uint8_t kAcRailCount = static_cast<uint8_t>(sizeof(kAcRails) / sizeof(kAcRails[0]));
constexpr uint8_t kDcRailCount = static_cast<uint8_t>(sizeof(kDcRails) / sizeof(kDcRails[0]));
constexpr uint8_t kSensorRailCount = static_cast<uint8_t>(sizeof(kSensorRail) / sizeof(kSensorRail[0]));
constexpr uint8_t kCurrentDrawCount = static_cast<uint8_t>(sizeof(kCurrentDraw) / sizeof(kCurrentDraw[0]));

const char* safeText(const char* text) {
    return (text != nullptr && text[0] != '\0') ? text : "(nao informado)";
}

Verdict runPoints(Ctx& ctx, const GuidedPoint* points, uint8_t count, const char*& failNote) {
    for (uint8_t i = 0; i < count; ++i) {
        ctx.op.step(points[i].what, points[i].where, points[i].expected);
        const Verdict v = ctx.op.ask("resultado?");
        if (v == Verdict::Abort) {
            return Verdict::Abort;
        }
        if (v == Verdict::Fail) {
            failNote = points[i].failNote;
            return Verdict::Fail;
        }
    }
    return Verdict::Pass;
}

void printIdentity(Ctx& ctx) {
    ctx.op.info("--- identidade da placa ---");
    ctx.op.info("FW_VERSION   : %s", safeText(ctx.fwVersion));
    ctx.op.info("BOARD_REV    : %s", safeText(ctx.boardRev));
    ctx.op.info("MAC          : %s", safeText(ctx.boot.macText));
    ctx.op.info("chip id      : %08X%08X", static_cast<unsigned>(ctx.boot.chipIdHigh),
                static_cast<unsigned>(ctx.boot.chipIdLow));
    ctx.op.info("flash        : %u bytes (%u MB)", static_cast<unsigned>(ctx.boot.flashSizeBytes),
                static_cast<unsigned>(ctx.boot.flashSizeBytes / (1024u * 1024u)));
    ctx.op.info("reset reason : %s (%u)", safeText(ctx.boot.resetReasonName),
                static_cast<unsigned>(ctx.boot.resetReason));
}

void printStrapping(Ctx& ctx) {
    ctx.op.info("--- strapping pins lidos no boot ---");
    uint8_t count = board::kStrappingCount;
    if (ctx.boot.strappingCount < count) {
        count = ctx.boot.strappingCount;
    }
    if (count == 0) {
        ctx.op.info("strapping: nao lido no boot");
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {
        ctx.op.info("IO%-2d         : %d", static_cast<int>(board::kStrappingPins[i]),
                    static_cast<int>(ctx.boot.strappingLevel[i]));
    }
}

void printWdtFlags(Ctx& ctx) {
    ctx.op.info("--- flag de teste de watchdog ---");
    ctx.op.info("wdtResetExpected : %s", ctx.boot.wdtResetExpected ? "sim" : "nao");
    ctx.op.info("wdtResetObserved : %s", ctx.boot.wdtResetObserved ? "sim" : "nao");
}

class Test00Power : public ITest {
public:
    const char* id() const override { return "t0"; }
    const char* name() const override { return "Boot, alimentacao e identidade"; }
    uint8_t order() const override { return 0; }
    bool abortsSuiteOnFail() const override { return true; }

    TestResult run(Ctx& ctx) override {
        printIdentity(ctx);
        printStrapping(ctx);
        printWdtFlags(ctx);

        ctx.op.info("--- roteiro guiado: o firmware nao mede, apenas orienta ---");
        ctx.op.info("use multimetro em DC e referencia no GND da placa");

        const char* failNote = "";

        ctx.op.info("[1/4] trilhos com entrada 100-240 VAC");
        Verdict v = runPoints(ctx, kAcRails, kAcRailCount, failNote);
        if (v == Verdict::Abort) {
            return TestResult(Verdict::Abort, "abortado pelo operador");
        }
        if (v == Verdict::Fail) {
            return TestResult(Verdict::Fail, failNote);
        }

        ctx.op.info("[2/4] trilhos com entrada 24 VCC (PTC2 + D18 1N4007 + LM2575-5)");
        v = runPoints(ctx, kDcRails, kDcRailCount, failNote);
        if (v == Verdict::Abort) {
            return TestResult(Verdict::Abort, "abortado pelo operador");
        }
        if (v == Verdict::Fail) {
            return TestResult(Verdict::Fail, failNote);
        }

        ctx.op.info("[3/4] alimentacao da placa sensora");
        v = runPoints(ctx, kSensorRail, kSensorRailCount, failNote);
        if (v == Verdict::Abort) {
            return TestResult(Verdict::Abort, "abortado pelo operador");
        }
        if (v == Verdict::Fail) {
            return TestResult(Verdict::Fail, failNote);
        }

        ctx.op.info("[4/4] consumo total");
        v = runPoints(ctx, kCurrentDraw, kCurrentDrawCount, failNote);
        if (v == Verdict::Abort) {
            return TestResult(Verdict::Abort, "abortado pelo operador");
        }
        if (v == Verdict::Fail) {
            return TestResult(Verdict::Fail, failNote);
        }

        return runWorstCase(ctx);
    }

private:
    static TestResult runWorstCase(Ctx& ctx) {
        ctx.op.info("pior caso: 4 reles ligados + saida em 20 mA + display ligado");
        ctx.op.info("o firmware liga apenas os reles; deixe a saida em 20 mA e a IHM ligada");
        const Status st = ctx.relays.allOn();
        if (st.failed()) {
            return TestResult(Verdict::Fail, "reles nao acionam (allOn): ver item t2");
        }
        ctx.op.step("medir o consumo total no pior caso", "entrada CN1 (VAC e depois 24 VCC)",
                    "consumo estavel, +5 V ainda dentro de +/-5% e LM2575-5 sem aquecer");
        const Verdict v = ctx.op.ask("resultado?");
        ctx.relays.allOff();
        if (v == Verdict::Abort) {
            return TestResult(Verdict::Abort, "abortado pelo operador");
        }
        if (v == Verdict::Fail) {
            return TestResult(Verdict::Fail, "pior caso: +5V afunda com as 4 bobinas");
        }
        return TestResult(Verdict::Pass, "trilhos, identidade e consumo conferidos");
    }
};

}  // namespace

REGISTER_TEST(Test00Power);
