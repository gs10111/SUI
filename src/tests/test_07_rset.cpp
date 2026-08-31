// Item t7: deriva a escala real da malha do XTR300 (folha 2/2, TI SBOS336C figura 2 e eq. 2 e 3).
// A placa usa a configuracao de saida BIDIRECIONAL: o pino SET recebe R_OS = R12/R25 = 1K vindo do
// VREF de 2,5 V do DAC8562, R_GAIN = R17/R29 = 10K entre RG1 e RG2, e NAO existe R_SET para 0 V.
//   V_OUT = (R_GAIN / 2) * (V_DAC - V_REF) / R_OS   e   I_OUT = 10 * (V_DAC - V_REF) / R_OS
// Consequencia que este item confere: a saida cruza o zero no MEIO da escala do DAC, nao em 0x0000.
// R14/R19 de 2K2 sao protecao de IAIN+/IAIN-, nao divisor: modela-los como divisor da o ganho errado.
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
constexpr float kROsMatchTolerancePct = 5.0f;
constexpr float kROsNominalTolerancePct = 10.0f;
constexpr float kVrefTolerancePct = 4.0f;
constexpr float kZeroCodeToleranceLsb = 2000.0f;
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

// Codigo do DAC em que a reta levantada pela calibracao cruza o zero da grandeza de saida.
float zeroCrossingCode(const calmath::Coef& c) {
    return (c.a != 0.0f) ? (-c.b / c.a) : 0.0f;
}

class Test07RSet : public ITest {
public:
    const char* id() const override { return "t7"; }
    const char* name() const override { return "Escala do XTR300 e ROS implicado"; }
    uint8_t order() const override { return 6; }

    TestResult run(Ctx& ctx) override {
        ctx.op.info("--- escala real da malha do XTR300 ---");
        ctx.op.info("este item nao mede: ele DERIVA os componentes a partir da calibracao ja levantada");
        ctx.op.info("V_OUT = R_GAIN/2 * (V_DAC - V_REF)/R_OS   e   I_OUT = 10 * (V_DAC - V_REF)/R_OS");
        ctx.op.info("saida BIDIRECIONAL (SBOS336C fig. 2): o zero cai no meio da escala do DAC, nao em 0x0000");
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

        float rOsFromVoltage = 0.0f;
        float rOsFromCurrent = 0.0f;
        float vrefFromVoltage = 0.0f;
        uint8_t derived = 0;

        for (uint8_t axis = 0; axis < board::kAxisCount; ++axis) {
            calmath::Coef cv;
            if (ctx.cal.coef(axis, AoMode::Voltage, cv).ok()) {
                const float gainV = cv.a * codesPerVolt();
                const float rOs = (gainV != 0.0f) ? (rGain / (2.0f * gainV)) : 0.0f;
                const float zeroCode = zeroCrossingCode(cv);
                const float vref = zeroCode / codesPerVolt();
                ctx.op.info("eixo %s tensao : ganho medido %.4f V/V -> R_OS implicado %.0f ohm",
                            board::kAxisName[axis], static_cast<double>(gainV), static_cast<double>(rOs));
                ctx.op.info("  cruza 0,00 V no codigo %.0f (esperado %u) -> V_REF implicado %.3f V",
                            static_cast<double>(zeroCode), static_cast<unsigned>(board::kDacZeroCode),
                            static_cast<double>(vref));
                ctx.op.info("  extremos da reta: %.3f V em 0x0000 e %.3f V em 0xFFFF",
                            static_cast<double>(cv.b), static_cast<double>(cv.a * kCodeSpan + cv.b));
                if (rOsFromVoltage == 0.0f && rOs > 0.0f) {
                    rOsFromVoltage = rOs;
                    vrefFromVoltage = vref;
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
                const float rOs = (gmAPerV != 0.0f) ? (board::kXtrCurrentMirrorRatio / gmAPerV) : 0.0f;
                ctx.op.info("eixo %s corrente: gm medido %.4f mA/V -> R_OS implicado %.0f ohm",
                            board::kAxisName[axis], static_cast<double>(gmMaPerV), static_cast<double>(rOs));
                ctx.op.info("  cruza 0,00 mA no codigo %.0f (esperado %u)",
                            static_cast<double>(zeroCrossingCode(ci)),
                            static_cast<unsigned>(board::kDacZeroCode));
                ctx.op.info("  extremos da reta: %.3f mA em 0x0000 e %.3f mA em 0xFFFF",
                            static_cast<double>(ci.b), static_cast<double>(ci.a * kCodeSpan + ci.b));
                ctx.op.info("  a malha e bipolar: nao existe live zero de 4 mA nesta placa");
                if (rOsFromCurrent == 0.0f && rOs > 0.0f) {
                    rOsFromCurrent = rOs;
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

        if (rOsFromVoltage > 0.0f && rOsFromCurrent > 0.0f) {
            const float mismatch = percentDelta(rOsFromVoltage, rOsFromCurrent);
            ctx.op.info("--- coerencia entre os dois modos ---");
            ctx.op.info("R_OS pela tensao %.0f ohm   R_OS pela corrente %.0f ohm   diferenca %.1f%%",
                        static_cast<double>(rOsFromVoltage), static_cast<double>(rOsFromCurrent),
                        static_cast<double>(mismatch));
            if (fabsf(mismatch) > kROsMatchTolerancePct) {
                ctx.op.info("e o MESMO resistor fisico nos dois modos: divergencia acima de %.0f%% indica",
                            static_cast<double>(kROsMatchTolerancePct));
                ctx.op.info("componente errado na malha, R_GAIN diferente do esperado, ou um R_SET");
                ctx.op.info("montado para 0 V que nao deveria existir nesta topologia");
                snprintf(g_noteBuf, sizeof(g_noteBuf), "R_OS incoerente: %.0f ohm (V) contra %.0f ohm (I), %.1f%%",
                         static_cast<double>(rOsFromVoltage), static_cast<double>(rOsFromCurrent),
                         static_cast<double>(mismatch));
                return TestResult(Verdict::Fail, g_noteBuf);
            }
        }

        if (vrefFromVoltage > 0.0f) {
            const float deltaVref = percentDelta(vrefFromVoltage, board::kXtrVrefV);
            const float zeroCode = vrefFromVoltage * codesPerVolt();
            const float codeError = zeroCode - static_cast<float>(board::kDacZeroCode);
            ctx.op.info("--- ponto de zero da saida ---");
            ctx.op.info("V_REF implicado %.3f V (%.1f%% do nominal de %.2f V), zero em %.0f LSB do DAC",
                        static_cast<double>(vrefFromVoltage), static_cast<double>(deltaVref),
                        static_cast<double>(board::kXtrVrefV), static_cast<double>(zeroCode));
            if (fabsf(deltaVref) > kVrefTolerancePct || fabsf(codeError) > kZeroCodeToleranceLsb) {
                ctx.op.info("o zero nao cai onde a topologia manda: conferir o VREF do DAC8562 (2,500 V no");
                ctx.op.info("pino VREF do CI7) e o R_OS de 1K entre o pino SET e o net VREF");
                snprintf(g_noteBuf, sizeof(g_noteBuf), "zero em %.0f LSB, esperado %u (V_REF %.3f V)",
                         static_cast<double>(zeroCode), static_cast<unsigned>(board::kDacZeroCode),
                         static_cast<double>(vrefFromVoltage));
                return TestResult(Verdict::Fail, g_noteBuf);
            }
        }

        const float rOs = (rOsFromCurrent > 0.0f) ? rOsFromCurrent : rOsFromVoltage;
        const float deltaNominal = percentDelta(rOs, board::kXtrROsOhms);
        ctx.op.info("R_OS implicado da placa: %.0f ohm (%.1f%% do nominal de %.0f ohm)",
                    static_cast<double>(rOs), static_cast<double>(deltaNominal),
                    static_cast<double>(board::kXtrROsOhms));
        ctx.op.info("com R_GAIN %.0f ohm isso da excursao de +/-%.2f V e +/-%.2f mA em torno do zero",
                    static_cast<double>(rGain),
                    static_cast<double>(rGain / (2.0f * rOs) * board::kXtrVrefV),
                    static_cast<double>(board::kXtrCurrentMirrorRatio / rOs * board::kXtrVrefV * 1000.0f));
        ctx.op.info("o trilho de +/-15 V limita a excursao util a cerca de +/-12 V");

        ctx.op.step("conferir no esquematico que o R_OS de 1K vai do pino SET ao net VREF e que",
                    "malha do XTR300 na folha 2/2", "nao ha resistor do pino SET para 0 V (R_SET ausente)");
        const Verdict v = ctx.op.ask("a topologia montada bate com o derivado acima?");
        if (v == Verdict::Abort) {
            return abortResult();
        }
        if (v == Verdict::Fail) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "R_OS derivado %.0f ohm nao bate com o montado",
                     static_cast<double>(rOs));
            return TestResult(Verdict::Fail, g_noteBuf);
        }

        if (fabsf(deltaNominal) > kROsNominalTolerancePct) {
            ctx.op.info("atencao: a placa nao usa o ponto de projeto de %.0f ohm; registre isso na engenharia",
                        static_cast<double>(board::kXtrROsOhms));
        }

        snprintf(g_noteBuf, sizeof(g_noteBuf), "ROS %.0f ohm, RGAIN %.0f ohm, +/-%.2f V em torno de 0x%04X",
                 static_cast<double>(rOs), static_cast<double>(rGain),
                 static_cast<double>(rGain / (2.0f * rOs) * board::kXtrVrefV),
                 static_cast<unsigned>(board::kDacZeroCode));
        return TestResult(Verdict::Pass, g_noteBuf);
    }
};

}  // namespace

REGISTER_TEST(Test07RSet);
