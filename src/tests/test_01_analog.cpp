// Item t1: saidas analogicas X/Y. Folha 2/2 (DAC8562, XTR300, jumpers J4, J5/J6, J13/J14).
// Datasheets: DAC8562 (TI SLAS719E), XTR300 (TI SBOS332).
#include <stdint.h>
#include <stdio.h>

#include "board_pins.h"
#include "core/ctx.h"
#include "core/test_runner.h"
#include "iface/ianalog_output.h"
#include "status.h"
#include "verdict.h"

namespace {

struct AoPoint {
    float value;
    const char* expected;
};

constexpr AoPoint kVoltagePoints[] = {
    {0.0f, "0,000 V +/-0,050 V (0,5% do fundo de escala de 10 V)"},
    {2.5f, "2,500 V +/-0,050 V"},
    {5.0f, "5,000 V +/-0,050 V"},
    {7.5f, "7,500 V +/-0,050 V"},
    {10.0f, "10,000 V +/-0,050 V"},
};

constexpr AoPoint kCurrentPoints[] = {
    {4.0f, "4,00 mA +/-0,10 mA (1,000 V sobre a carga de 250 ohm)"},
    {8.0f, "8,00 mA +/-0,10 mA (2,000 V sobre a carga de 250 ohm)"},
    {12.0f, "12,00 mA +/-0,10 mA (3,000 V sobre a carga de 250 ohm)"},
    {16.0f, "16,00 mA +/-0,10 mA (4,000 V sobre a carga de 250 ohm)"},
    {20.0f, "20,00 mA +/-0,10 mA (5,000 V sobre a carga de 250 ohm)"},
};

constexpr uint8_t kVoltageCount = static_cast<uint8_t>(sizeof(kVoltagePoints) / sizeof(kVoltagePoints[0]));
constexpr uint8_t kCurrentCount = static_cast<uint8_t>(sizeof(kCurrentPoints) / sizeof(kCurrentPoints[0]));

constexpr const char* kAxisArg[board::kAxisCount] = {"x", "y"};
constexpr const char* kLedLabel[board::kAxisCount] = {
    "LD1/LD2/LD3 (EFOT/EFLD/EFCM do eixo X)",
    "LD4/LD5/LD6 (EFOT/EFLD/EFCM do eixo Y)",
};
constexpr const char* kEfldLabel[board::kAxisCount] = {
    "LD2 (EFLD do eixo X)",
    "LD5 (EFLD do eixo Y)",
};

constexpr const char* kJumpersVoltage =
    "J4 em saida de TENSAO; J5/J6 (eixo X) e J13/J14 (eixo Y) na posicao de TENSAO";
constexpr const char* kJumpersCurrent =
    "J4 em saida de CORRENTE; J5/J6 (eixo X) e J13/J14 (eixo Y) na posicao de CORRENTE";

constexpr uint16_t kAnswerCap = 16;

char g_whatBuf[128];
char g_noteBuf[96];
char g_crossValue[board::kAxisCount][kAnswerCap];

TestResult abortResult() {
    return TestResult(Verdict::Abort, "abortado pelo operador");
}

void restoreSafeOutput(Ctx& ctx) {
    ctx.ao.zeroAll();
    ctx.ao.setMode(AoMode::Voltage);
}

bool confirmJumpers(Ctx& ctx, const char* expected) {
    ctx.op.info("posicao esperada dos jumpers: %s", expected);
    return ctx.op.askYes("jumpers J4, J5/J6 e J13/J14 estao nesta posicao?");
}

TestResult runAxisPoints(Ctx& ctx, uint8_t axis, AoMode mode, const AoPoint* points, uint8_t count,
                         const char* modeArg) {
    if (!ctx.cal.has(axis, mode)) {
        ctx.op.info("eixo %s sem calibracao para este modo", board::kAxisName[axis]);
        ctx.op.info("execute no console: cal %s %s", kAxisArg[axis], modeArg);
        snprintf(g_noteBuf, sizeof(g_noteBuf), "sem calibracao: rode 'cal %s %s'", kAxisArg[axis], modeArg);
        return TestResult(Verdict::Skip, g_noteBuf);
    }

    for (uint8_t i = 0; i < count; ++i) {
        const Status st = ctx.ao.setEngineering(axis, points[i].value);
        if (st.failed()) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "eixo %s: DAC8562 recusou o ponto (%s)",
                     board::kAxisName[axis], errName(st.err));
            return TestResult(Verdict::Fail, g_noteBuf);
        }

        snprintf(g_whatBuf, sizeof(g_whatBuf), "medir a saida do eixo %s (ponto %u de %u)",
                 board::kAxisName[axis], static_cast<unsigned>(i + 1), static_cast<unsigned>(count));
        ctx.op.step(g_whatBuf, board::kAxisTerminals[axis], points[i].expected);
        Verdict v = ctx.op.ask("resultado?");
        if (v == Verdict::Abort) {
            return abortResult();
        }
        if (v == Verdict::Fail) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "eixo %s ponto %u fora: XTR300 ou DAC8562",
                     board::kAxisName[axis], static_cast<unsigned>(i + 1));
            return TestResult(Verdict::Fail, g_noteBuf);
        }

        ctx.op.step("inspecionar os LEDs de falha do eixo (nao voltam para o MCU)", kLedLabel[axis],
                    "EFOT, EFLD e EFCM apagados");
        v = ctx.op.ask("LEDs conforme esperado?");
        if (v == Verdict::Abort) {
            return abortResult();
        }
        if (v == Verdict::Fail) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "eixo %s: LED de falha aceso no XTR300",
                     board::kAxisName[axis]);
            return TestResult(Verdict::Fail, g_noteBuf);
        }
    }
    return TestResult(Verdict::Pass, "");
}

TestResult runOpenLoop(Ctx& ctx, uint8_t axis) {
    const Status st = ctx.ao.setEngineering(axis, kCurrentPoints[kCurrentCount - 1].value);
    if (st.failed()) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "eixo %s: DAC8562 recusou 20 mA (%s)",
                 board::kAxisName[axis], errName(st.err));
        return TestResult(Verdict::Fail, g_noteBuf);
    }
    snprintf(g_whatBuf, sizeof(g_whatBuf), "com o eixo %s em 20 mA, abrir o laco (retirar a carga de 250 ohm)",
             board::kAxisName[axis]);
    ctx.op.step(g_whatBuf, kEfldLabel[axis], "EFLD acende enquanto o laco estiver aberto");
    const Verdict v = ctx.op.ask("EFLD acendeu com o laco aberto?");
    if (v == Verdict::Abort) {
        return abortResult();
    }
    if (v == Verdict::Fail) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "eixo %s sem deteccao de laco aberto: XTR300/EFLD",
                 board::kAxisName[axis]);
        return TestResult(Verdict::Fail, g_noteBuf);
    }
    ctx.op.info("recoloque a carga de 250 ohm no eixo %s antes de seguir", board::kAxisName[axis]);
    return TestResult(Verdict::Pass, "");
}

TestResult runCrosstalk(Ctx& ctx) {
    ctx.op.info("--- diafonia entre eixos (tolerancia a definir com a primeira placa boa) ---");
    for (uint8_t driven = 0; driven < board::kAxisCount; ++driven) {
        const uint8_t quiet = static_cast<uint8_t>((driven + 1) % board::kAxisCount);
        Status st = ctx.ao.setEngineering(quiet, 0.0f);
        if (st.ok()) {
            st = ctx.ao.setEngineering(driven, kVoltagePoints[kVoltageCount - 1].value);
        }
        if (st.failed()) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "diafonia: DAC8562 recusou o ajuste (%s)", errName(st.err));
            return TestResult(Verdict::Fail, g_noteBuf);
        }
        snprintf(g_whatBuf, sizeof(g_whatBuf), "com o eixo %s em fundo de escala, medir o eixo %s em zero",
                 board::kAxisName[driven], board::kAxisName[quiet]);
        ctx.op.step(g_whatBuf, board::kAxisTerminals[quiet], "idealmente 0,000 V - anotar o valor lido");
        if (!ctx.op.askLine("valor medido no eixo em zero (V):", g_crossValue[quiet], kAnswerCap)) {
            return abortResult();
        }
    }
    ctx.ao.zeroAll();
    return TestResult(Verdict::Pass, "");
}

class Test01Analog : public ITest {
public:
    const char* id() const override { return "t1"; }
    const char* name() const override { return "Saida analogica X/Y (DAC8562+XTR300)"; }
    uint8_t order() const override { return 1; }

    TestResult run(Ctx& ctx) override {
        for (uint8_t axis = 0; axis < board::kAxisCount; ++axis) {
            g_crossValue[axis][0] = '\0';
        }

        const Status stBegin = ctx.ao.begin();
        if (stBegin.failed()) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "DAC8562 nao inicializa (%s): SYNC/SCLK/MOSI",
                     errName(stBegin.err));
            return TestResult(Verdict::Fail, g_noteBuf);
        }
        ctx.op.info("OP_MODE do XTR300 no IO%d; SPI do DAC8562 em %u Hz",
                    static_cast<int>(board::kXtrOpMode), static_cast<unsigned>(ctx.ao.spiHz()));
        ctx.op.info("valores de corrente sao enviados em mA (unidade de engenharia da calibracao)");

        const TestResult voltageResult = runVoltageStage(ctx);
        if (voltageResult.verdict != Verdict::Pass) {
            restoreSafeOutput(ctx);
            return voltageResult;
        }

        const TestResult currentResult = runCurrentStage(ctx);
        restoreSafeOutput(ctx);
        if (currentResult.verdict != Verdict::Pass) {
            return currentResult;
        }

        snprintf(g_noteBuf, sizeof(g_noteBuf), "diafonia X=%s V Y=%s V",
                 (g_crossValue[0][0] != '\0') ? g_crossValue[0] : "?",
                 (g_crossValue[1][0] != '\0') ? g_crossValue[1] : "?");
        return TestResult(Verdict::Pass, g_noteBuf);
    }

private:
    static TestResult runVoltageStage(Ctx& ctx) {
        ctx.op.info("--- modo TENSAO ---");
        if (!confirmJumpers(ctx, kJumpersVoltage)) {
            if (ctx.op.aborted()) {
                return abortResult();
            }
            return TestResult(Verdict::Fail, "jumpers J4/J5-J6/J13-J14 fora de tensao");
        }
        ctx.ao.zeroAll();
        const Status stMode = ctx.ao.setMode(AoMode::Voltage);
        if (stMode.failed()) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "OP_MODE nao vai para tensao (%s)", errName(stMode.err));
            return TestResult(Verdict::Fail, g_noteBuf);
        }
        for (uint8_t axis = 0; axis < board::kAxisCount; ++axis) {
            const TestResult r = runAxisPoints(ctx, axis, AoMode::Voltage, kVoltagePoints, kVoltageCount, "v");
            if (r.verdict != Verdict::Pass) {
                return r;
            }
        }
        return runCrosstalk(ctx);
    }

    static TestResult runCurrentStage(Ctx& ctx) {
        ctx.op.info("--- modo CORRENTE (carga de 250 ohm em serie) ---");
        if (!confirmJumpers(ctx, kJumpersCurrent)) {
            if (ctx.op.aborted()) {
                return abortResult();
            }
            return TestResult(Verdict::Fail, "jumpers J4/J5-J6/J13-J14 fora de corrente");
        }
        ctx.ao.zeroAll();
        const Status stMode = ctx.ao.setMode(AoMode::Current);
        if (stMode.failed()) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "OP_MODE nao vai para corrente (%s)", errName(stMode.err));
            return TestResult(Verdict::Fail, g_noteBuf);
        }
        for (uint8_t axis = 0; axis < board::kAxisCount; ++axis) {
            TestResult r = runAxisPoints(ctx, axis, AoMode::Current, kCurrentPoints, kCurrentCount, "i");
            if (r.verdict != Verdict::Pass) {
                return r;
            }
            r = runOpenLoop(ctx, axis);
            if (r.verdict != Verdict::Pass) {
                return r;
            }
        }
        return TestResult(Verdict::Pass, "");
    }
};

}  // namespace

REGISTER_TEST(Test01Analog);
