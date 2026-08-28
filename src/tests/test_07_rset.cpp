// Item t7: deriva a escala real do XTR300 e o RSET implicado (folha 2/2, TI SBOS336C eq. 1 e 3).
// V_OUT = R_GAIN/(2*R_SET) * V_IN   e   I_OUT = (10/R_SET) * V_IN. R14/R19 de 2K2 sao protecao de
// IAIN+/IAIN-, nao divisor: modela-los como divisor da o ganho errado.
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "board_pins.h"
#include "core/cmd_parser.h"
#include "core/ctx.h"
#include "core/test_runner.h"
#include "drivers/calibration.h"
#include "status.h"
#include "verdict.h"

namespace {

constexpr float kCodeSpan = 65535.0f;
constexpr float kRSetMatchTolerancePct = 5.0f;
constexpr float kRSetNominalTolerancePct = 10.0f;
constexpr float kLiveZeroDigitalMaxMa = 0.5f;
constexpr float kLiveZeroHardwareMinMa = 3.0f;
constexpr uint16_t kAnswerCap = 24;

char g_noteBuf[96];
char g_whatBuf[128];
char g_answerBuf[kAnswerCap];

TestResult abortResult() {
    return TestResult(Verdict::Abort, "abortado pelo operador");
}

float codesPerVolt() {
    return kCodeSpan / board::kDacFullScaleV;
}

bool askFloat(Ctx& ctx, const char* prompt, float& out, bool& aborted) {
    aborted = false;
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (!ctx.op.askLine(prompt, g_answerBuf, kAnswerCap)) {
            aborted = ctx.op.aborted();
            return false;
        }
        if (cmd::parseFloat(g_answerBuf, out)) {
            return true;
        }
        ctx.op.info("valor nao reconhecido: use ponto ou virgula decimal");
    }
    return false;
}

float percentDelta(float measured, float reference) {
    if (reference == 0.0f) {
        return 0.0f;
    }
    return (measured - reference) / reference * 100.0f;
}

class Test07RSet : public ITest {
public:
    const char* id() const override { return "t7"; }
    const char* name() const override { return "Escala do XTR300 e RSET implicado"; }
    uint8_t order() const override { return 6; }

    TestResult run(Ctx& ctx) override {
        ctx.op.info("--- escala real da malha do XTR300 ---");
        ctx.op.info("este item nao mede: ele DERIVA os componentes a partir da calibracao ja levantada");
        ctx.op.info("V_OUT = R_GAIN/(2*R_SET) * V_IN   e   I_OUT = (10/R_SET) * V_IN  (SBOS336C)");
        ctx.op.info("R14/R19 de 2K2 sao protecao de IAIN+/IAIN-, nao divisor de realimentacao");

        float rGain = board::kXtrRGainOhms;
        snprintf(g_whatBuf, sizeof(g_whatBuf), "R_GAIN medido em R17 (esperado %.0f ohm), em ohm",
                 static_cast<double>(board::kXtrRGainOhms));
        bool aborted = false;
        float typed = 0.0f;
        if (askFloat(ctx, g_whatBuf, typed, aborted)) {
            if (typed > 100.0f) {
                rGain = typed;
            } else {
                ctx.op.info("valor fora do plausivel: seguindo com %.0f ohm do esquematico",
                            static_cast<double>(rGain));
            }
        } else if (aborted) {
            return abortResult();
        } else {
            ctx.op.info("sem leitura: seguindo com %.0f ohm do esquematico", static_cast<double>(rGain));
        }

        float rSetFromVoltage = 0.0f;
        float rSetFromCurrent = 0.0f;
        uint8_t derived = 0;

        for (uint8_t axis = 0; axis < board::kAxisCount; ++axis) {
            calmath::Coef cv;
            if (ctx.cal.coef(axis, AoMode::Voltage, cv).ok()) {
                const float gainV = cv.a * codesPerVolt();
                const float rSet = (gainV != 0.0f) ? (rGain / (2.0f * gainV)) : 0.0f;
                ctx.op.info("eixo %s tensao : ganho medido %.4f V/V -> R_SET implicado %.0f ohm",
                            board::kAxisName[axis], static_cast<double>(gainV), static_cast<double>(rSet));
                ctx.op.info("  fundo de escala derivado: %.3f V em 0xFFFF",
                            static_cast<double>(cv.a * kCodeSpan + cv.b));
                if (rSetFromVoltage == 0.0f && rSet > 0.0f) {
                    rSetFromVoltage = rSet;
                    ++derived;
                }
            } else {
                ctx.op.info("eixo %s sem calibracao de tensao: rode 'cal %s v'", board::kAxisName[axis],
                            (axis == 0) ? "x" : "y");
            }

            calmath::Coef ci;
            if (ctx.cal.coef(axis, AoMode::Current, ci).ok()) {
                const float gmMaPerV = ci.a * codesPerVolt();
                const float gmAPerV = gmMaPerV / 1000.0f;
                const float rSet = (gmAPerV != 0.0f) ? (board::kXtrCurrentMirrorRatio / gmAPerV) : 0.0f;
                ctx.op.info("eixo %s corrente: gm medido %.4f mA/V -> R_SET implicado %.0f ohm",
                            board::kAxisName[axis], static_cast<double>(gmMaPerV), static_cast<double>(rSet));
                ctx.op.info("  corrente em codigo 0: %.3f mA   fundo de escala: %.3f mA",
                            static_cast<double>(ci.b), static_cast<double>(ci.a * kCodeSpan + ci.b));
                if (ci.b < kLiveZeroDigitalMaxMa) {
                    ctx.op.info("  live zero de 4 mA vem do CODIGO do DAC (R_OS ausente): 4 mA = 1,000 V de V_IN");
                } else if (ci.b >= kLiveZeroHardwareMinMa) {
                    ctx.op.info("  live zero de 4 mA vem do HARDWARE (R_OS/V_REF ou R_SET referido a V_REF)");
                } else {
                    ctx.op.info("  offset intermediario: conferir a topologia do R_SET no esquematico");
                }
                if (rSetFromCurrent == 0.0f && rSet > 0.0f) {
                    rSetFromCurrent = rSet;
                    ++derived;
                }
            } else {
                ctx.op.info("eixo %s sem calibracao de corrente: rode 'cal %s i'", board::kAxisName[axis],
                            (axis == 0) ? "x" : "y");
            }
        }

        if (derived == 0) {
            return TestResult(Verdict::Skip, "sem calibracao: rode 'cal x v' e 'cal x i' antes deste item");
        }

        if (rSetFromVoltage > 0.0f && rSetFromCurrent > 0.0f) {
            const float mismatch = percentDelta(rSetFromVoltage, rSetFromCurrent);
            ctx.op.info("--- coerencia entre os dois modos ---");
            ctx.op.info("R_SET pela tensao %.0f ohm   R_SET pela corrente %.0f ohm   diferenca %.1f%%",
                        static_cast<double>(rSetFromVoltage), static_cast<double>(rSetFromCurrent),
                        static_cast<double>(mismatch));
            if (fabsf(mismatch) > kRSetMatchTolerancePct) {
                ctx.op.info("e o MESMO resistor fisico nos dois modos: divergencia acima de %.0f%% indica",
                            static_cast<double>(kRSetMatchTolerancePct));
                ctx.op.info("componente errado na malha, R_GAIN diferente do esperado, ou topologia de");
                ctx.op.info("R_SET referida a V_REF em vez de GND (variante TIDU434)");
                snprintf(g_noteBuf, sizeof(g_noteBuf), "R_SET incoerente: %.0f ohm (V) contra %.0f ohm (I), %.1f%%",
                         static_cast<double>(rSetFromVoltage), static_cast<double>(rSetFromCurrent),
                         static_cast<double>(mismatch));
                return TestResult(Verdict::Fail, g_noteBuf);
            }
        }

        const float rSet = (rSetFromCurrent > 0.0f) ? rSetFromCurrent : rSetFromVoltage;
        const float deltaNominal = percentDelta(rSet, board::kXtrRSetNominalOhms);
        ctx.op.info("R_SET implicado da placa: %.0f ohm (%.1f%% do nominal de %.0f ohm)",
                    static_cast<double>(rSet), static_cast<double>(deltaNominal),
                    static_cast<double>(board::kXtrRSetNominalOhms));
        ctx.op.info("com R_GAIN %.0f ohm isso da fundo de escala de %.2f V e %.2f mA",
                    static_cast<double>(rGain),
                    static_cast<double>(rGain / (2.0f * rSet) * board::kDacFullScaleV),
                    static_cast<double>(board::kXtrCurrentMirrorRatio / rSet * board::kDacFullScaleV * 1000.0f));

        ctx.op.step("conferir no esquematico o valor impresso de RSET e a que no a ponta fria dele vai",
                    "malha do XTR300 na folha 2/2", "valor proximo do derivado acima, ponta fria em 0V ou em VREF");
        const Verdict v = ctx.op.ask("o valor derivado bate com o componente montado?");
        if (v == Verdict::Abort) {
            return abortResult();
        }
        if (v == Verdict::Fail) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "RSET derivado %.0f ohm nao bate com o montado", static_cast<double>(rSet));
            return TestResult(Verdict::Fail, g_noteBuf);
        }

        if (fabsf(deltaNominal) > kRSetNominalTolerancePct) {
            ctx.op.info("atencao: a placa nao usa o ponto de projeto de %.0f ohm; registre isso na engenharia",
                        static_cast<double>(board::kXtrRSetNominalOhms));
        }

        snprintf(g_noteBuf, sizeof(g_noteBuf), "RSET %.0f ohm, RGAIN %.0f ohm, FE %.2f V / %.2f mA",
                 static_cast<double>(rSet), static_cast<double>(rGain),
                 static_cast<double>(rGain / (2.0f * rSet) * board::kDacFullScaleV),
                 static_cast<double>(board::kXtrCurrentMirrorRatio / rSet * board::kDacFullScaleV * 1000.0f));
        return TestResult(Verdict::Pass, g_noteBuf);
    }
};

}  // namespace

REGISTER_TEST(Test07RSet);
