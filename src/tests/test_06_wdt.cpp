// Item t6: watchdog externo STWD100YNYWY3F com WDI em IO19 e habilitacao por J15. Folha 1/2.
// Datasheet: STWD100 (ST DocID14134 Rev 11) - tWD 1,12 s min / 1,6 s tip.
#include <stdint.h>
#include <stdio.h>

#include "board_pins.h"
#include "core/ctx.h"
#include "core/test_runner.h"
#include "status.h"
#include "verdict.h"

namespace {

constexpr const char* kNvsWdtFlag = "wdt_expect";
constexpr uint32_t kWaitResetMs = 3000;
constexpr uint32_t kTickMs = 500;
constexpr const char* kFailNote =
    "watchdog nao atua - verificar J15, pull-up de WDO (open-drain) e o pino EN do STWD100";

char g_noteBuf[96];

void printTimings(Ctx& ctx) {
    ctx.op.info("--- watchdog externo STWD100 (WDI em IO%d, habilitado por J15) ---",
                static_cast<int>(board::kWdi));
    ctx.op.info("periodo de chute : %u ms", static_cast<unsigned>(ctx.wdt.kickPeriodMs()));
    ctx.op.info("timeout minimo   : %u ms", static_cast<unsigned>(ctx.wdt.minTimeoutMs()));
    ctx.op.info("timeout tipico   : %u ms", static_cast<unsigned>(ctx.wdt.typTimeoutMs()));
    ctx.op.info("chutes ate agora : %u", static_cast<unsigned>(ctx.wdt.kickCount()));
}

class Test06Wdt : public ITest {
public:
    const char* id() const override { return "t6"; }
    const char* name() const override { return "Watchdog externo STWD100"; }
    uint8_t order() const override { return 6; }

    TestResult run(Ctx& ctx) override {
        if (ctx.boot.wdtResetExpected && ctx.boot.wdtResetObserved) {
            ctx.op.info("reset por watchdog confirmado no boot (motivo: %s)",
                        (ctx.boot.resetReasonName != nullptr) ? ctx.boot.resetReasonName : "?");
            const Status stClear = ctx.kv.remove(kNvsWdtFlag);
            if (stClear.failed()) {
                ctx.op.info("aviso: nao foi possivel limpar a chave '%s' (%s)", kNvsWdtFlag,
                            errName(stClear.err));
            }
            ctx.boot.wdtResetExpected = false;
            ctx.boot.wdtResetObserved = false;
            return TestResult(Verdict::Pass, "reset por watchdog confirmado no boot");
        }

        printTimings(ctx);
        if (ctx.boot.wdtResetExpected && !ctx.boot.wdtResetObserved) {
            ctx.op.info("havia flag de teste pendente sem reset observado: repetindo o ensaio");
        }
        ctx.op.info("ATENCAO: a placa VAI RESETAR em cerca de %u ms se J15 estiver fechado",
                    static_cast<unsigned>(ctx.wdt.typTimeoutMs()));
        ctx.op.info("o relatorio parcial e salvo na NVS antes do reset");
        if (!ctx.op.askYes("provocar o reset por watchdog agora?")) {
            if (ctx.op.aborted()) {
                return TestResult(Verdict::Abort, "abortado pelo operador");
            }
            return TestResult(Verdict::Skip, "ensaio de reset nao autorizado pelo operador");
        }

        ctx.safe.enterSafeState();

        const Status stSave = ctx.report.save(ctx.kv);
        if (stSave.failed()) {
            ctx.op.info("aviso: relatorio parcial nao foi salvo (%s)", errName(stSave.err));
        }
        const Status stFlag = ctx.kv.putU8(kNvsWdtFlag, 1);
        if (stFlag.failed()) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "NVS nao grava '%s' (%s): ensaio cancelado", kNvsWdtFlag,
                     errName(stFlag.err));
            return TestResult(Verdict::Fail, g_noteBuf);
        }

        const Status stStop = ctx.wdt.setKicking(false);
        if (stStop.failed()) {
            ctx.kv.remove(kNvsWdtFlag);
            snprintf(g_noteBuf, sizeof(g_noteBuf), "nao foi possivel parar o chute do WDI (%s)",
                     errName(stStop.err));
            return TestResult(Verdict::Fail, g_noteBuf);
        }

        ctx.op.info("chute do WDI interrompido, aguardando o reset...");
        waitForReset(ctx);

        ctx.wdt.setKicking(true);
        ctx.kv.remove(kNvsWdtFlag);
        ctx.op.info("%s", kFailNote);
        return TestResult(Verdict::Fail, kFailNote);
    }

private:
    static void waitForReset(Ctx& ctx) {
        const uint32_t startMs = ctx.io.nowMs();
        uint32_t nextTickMs = kTickMs;
        uint32_t elapsed = ctx.io.nowMs() - startMs;
        while (elapsed < kWaitResetMs) {
            if (elapsed >= nextTickMs) {
                ctx.op.info("  %u ms sem chute no WDI", static_cast<unsigned>(elapsed));
                nextTickMs += kTickMs;
            }
            ctx.io.idle();
            elapsed = ctx.io.nowMs() - startMs;
        }
    }
};

}  // namespace

REGISTER_TEST(Test06Wdt);
