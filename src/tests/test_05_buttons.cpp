// Item t5: botoes UP/DOWN/MENU da IHM no CN3. Folha 1/2 (IO15, IO34, IO35).
// IO34/IO35 sao input-only no ESP32-WROOM-32D: pull-up interno inexistente (Espressif TRM, cap. IO_MUX).
#include <stdint.h>
#include <stdio.h>

#include "board_pins.h"
#include "build_config.h"
#include "core/ctx.h"
#include "core/test_runner.h"
#include "iface/ibuttons.h"
#include "status.h"
#include "verdict.h"

namespace {

constexpr bool kIhmEnabled = (IHM_ENABLED != 0);

constexpr uint32_t kMonitorMs = 60000;
constexpr uint32_t kSettleMs = 1500;
constexpr uint32_t kProgressMs = 10000;
constexpr uint32_t kRequiredPresses = 3;

static_assert(kButtonCount == 3, "resumo de progresso assume UP/DOWN/MENU");

char g_noteBuf[96];

TestResult abortResult() {
    return TestResult(Verdict::Abort, "abortado pelo operador");
}

void pollFor(Ctx& ctx, uint32_t durationMs) {
    const uint32_t startMs = ctx.io.nowMs();
    while ((ctx.io.nowMs() - startMs) < durationMs) {
        ctx.buttons.poll();
        ctx.io.idle();
    }
}

void drainEdges(Ctx& ctx) {
    uint8_t index = 0;
    bool rising = false;
    while (ctx.buttons.takeEdge(index, rising)) {
    }
}

TestResult checkRestLevels(Ctx& ctx) {
    for (uint8_t i = 0; i < kButtonCount; ++i) {
        if (ctx.buttons.restLevelStable(i)) {
            continue;
        }
        if (ctx.buttons.inputOnly(i)) {
            ctx.op.info("botao %s esta em pino input-only (IO34/IO35)", ctx.buttons.name(i));
            ctx.op.info("nesses pinos pinMode(INPUT_PULLUP) e ignorado silenciosamente pelo ESP32");
            ctx.op.info("o nivel de repouso instavel indica falta do pull-up na placa de IHM");
            snprintf(g_noteBuf, sizeof(g_noteBuf), "%s solto: falta pull-up na IHM (IO34/IO35)",
                     ctx.buttons.name(i));
            return TestResult(Verdict::Fail, g_noteBuf);
        }
        snprintf(g_noteBuf, sizeof(g_noteBuf), "%s instavel em repouso: ruido ou pull-up fraco",
                 ctx.buttons.name(i));
        return TestResult(Verdict::Fail, g_noteBuf);
    }
    return TestResult(Verdict::Pass, "");
}

class Test05Buttons : public ITest {
public:
    const char* id() const override { return "t5"; }
    const char* name() const override { return "Botoes CN3"; }
    uint8_t order() const override { return 5; }

    TestResult run(Ctx& ctx) override {
        if (!kIhmEnabled) {
            ctx.op.info("TODO: botoes do CN3 pendentes - pull-up e nivel ativo da IHM nao confirmados");
            ctx.op.info("habilite com -DIHM_ENABLED=1");
            return TestResult(Verdict::Skip, "TODO: botoes pendentes (IHM_ENABLED=0)");
        }
        const Status stBegin = ctx.buttons.begin();
        if (stBegin.failed()) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "monitor de botoes nao inicializa (%s)",
                     errName(stBegin.err));
            return TestResult(Verdict::Fail, g_noteBuf);
        }

        ctx.op.info("--- botoes do CN3 ---");
        for (uint8_t i = 0; i < kButtonCount; ++i) {
            ctx.op.info("%s: %s", ctx.buttons.name(i),
                        ctx.buttons.inputOnly(i) ? "pino input-only (sem pull-up interno)"
                                                 : "pino com pull-up interno disponivel");
        }
        ctx.op.info("nao toque em nenhum botao durante a leitura do nivel de repouso");
        pollFor(ctx, kSettleMs);

        TestResult rest = checkRestLevels(ctx);
        if (rest.verdict != Verdict::Pass) {
            return rest;
        }

        bool restLevel[kButtonCount];
        for (uint8_t i = 0; i < kButtonCount; ++i) {
            restLevel[i] = ctx.buttons.level(i);
        }

        ctx.buttons.resetCounts();
        drainEdges(ctx);

        ctx.op.info("pressione cada botao %u vezes, um de cada vez, em ate %u s",
                    static_cast<unsigned>(kRequiredPresses), static_cast<unsigned>(kMonitorMs / 1000u));
        const TestResult monitor = runMonitor(ctx, restLevel);
        if (monitor.verdict != Verdict::Pass) {
            return monitor;
        }

        pollFor(ctx, kSettleMs);
        rest = checkRestLevels(ctx);
        if (rest.verdict != Verdict::Pass) {
            return rest;
        }
        return TestResult(Verdict::Pass, "3 pressionamentos por botao, sem repique/cruzado");
    }

private:
    static TestResult runMonitor(Ctx& ctx, const bool* restLevel) {
        const uint32_t startMs = ctx.io.nowMs();
        uint32_t nextProgressMs = kProgressMs;
        uint32_t crossCount = 0;
        bool complete = false;

        while (!complete && (ctx.io.nowMs() - startMs) < kMonitorMs) {
            if (ctx.op.aborted()) {
                return abortResult();
            }
            ctx.buttons.poll();

            uint8_t index = 0;
            bool rising = false;
            while (ctx.buttons.takeEdge(index, rising)) {
                if (index >= kButtonCount) {
                    continue;
                }
                ctx.op.info("%s: borda %s nivel=%d presses=%u repiques=%u", ctx.buttons.name(index),
                            rising ? "SUBIDA" : "DESCIDA", ctx.buttons.level(index) ? 1 : 0,
                            static_cast<unsigned>(ctx.buttons.pressCount(index)),
                            static_cast<unsigned>(ctx.buttons.bounceCount(index)));
                for (uint8_t j = 0; j < kButtonCount; ++j) {
                    if (j == index) {
                        continue;
                    }
                    if (ctx.buttons.level(j) != restLevel[j]) {
                        ++crossCount;
                        ctx.op.info("acionamento cruzado: %s saiu do repouso junto com %s",
                                    ctx.buttons.name(j), ctx.buttons.name(index));
                    }
                }
            }

            complete = true;
            for (uint8_t i = 0; i < kButtonCount; ++i) {
                if (ctx.buttons.pressCount(i) < kRequiredPresses) {
                    complete = false;
                }
            }

            const uint32_t elapsed = ctx.io.nowMs() - startMs;
            if (elapsed >= nextProgressMs) {
                ctx.op.info("  %u s decorridos: %s=%u %s=%u %s=%u",
                            static_cast<unsigned>(elapsed / 1000u), ctx.buttons.name(0),
                            static_cast<unsigned>(ctx.buttons.pressCount(0)), ctx.buttons.name(1),
                            static_cast<unsigned>(ctx.buttons.pressCount(1)), ctx.buttons.name(2),
                            static_cast<unsigned>(ctx.buttons.pressCount(2)));
                nextProgressMs += kProgressMs;
            }
            ctx.io.idle();
        }

        for (uint8_t i = 0; i < kButtonCount; ++i) {
            ctx.op.info("%s: presses=%u repiques=%u", ctx.buttons.name(i),
                        static_cast<unsigned>(ctx.buttons.pressCount(i)),
                        static_cast<unsigned>(ctx.buttons.bounceCount(i)));
        }

        for (uint8_t i = 0; i < kButtonCount; ++i) {
            if (ctx.buttons.pressCount(i) < kRequiredPresses) {
                snprintf(g_noteBuf, sizeof(g_noteBuf), "%s com %u de %u toques: contato ou CN3",
                         ctx.buttons.name(i), static_cast<unsigned>(ctx.buttons.pressCount(i)),
                         static_cast<unsigned>(kRequiredPresses));
                return TestResult(Verdict::Fail, g_noteBuf);
            }
        }
        for (uint8_t i = 0; i < kButtonCount; ++i) {
            if (ctx.buttons.bounceCount(i) != 0) {
                snprintf(g_noteBuf, sizeof(g_noteBuf), "%s com repique (%u): chave ou filtro RC do CN3",
                         ctx.buttons.name(i), static_cast<unsigned>(ctx.buttons.bounceCount(i)));
                return TestResult(Verdict::Fail, g_noteBuf);
            }
        }
        if (crossCount != 0) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "acionamento cruzado (%u): fiacao do CN3 ou curto",
                     static_cast<unsigned>(crossCount));
            return TestResult(Verdict::Fail, g_noteBuf);
        }
        return TestResult(Verdict::Pass, "");
    }
};

}  // namespace

REGISTER_TEST(Test05Buttons);
