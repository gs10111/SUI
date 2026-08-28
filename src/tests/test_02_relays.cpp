// Item t2: reles de limite RL2..RL5 e LEDs da IHM. Folha 2/2 (bobinas, BC337, jumpers J2/J8/J9/J10).
// Datasheets: AX1RC-5V (Fujitsu), BC337 (Nexperia/ON).
#include <stdint.h>
#include <stdio.h>

#include "board_pins.h"
#include "core/ctx.h"
#include "core/test_runner.h"
#include "status.h"
#include "verdict.h"

namespace {

char g_whatBuf[160];
char g_expectedBuf[160];
char g_noteBuf[96];

TestResult abortResult() {
    return TestResult(Verdict::Abort, "abortado pelo operador");
}

void printDesignNote(Ctx& ctx) {
    ctx.op.info("--- nota de projeto (acionamento das bobinas) ---");
    ctx.op.info("corrente de base de aproximadamente 0,25 mA exige hFE maior ou igual a 145");
    ctx.op.info("para a bobina de cerca de 36 mA do AX1RC-5V: dentro da faixa do BC337, sem margem");
}

TestResult runOneRelay(Ctx& ctx, uint8_t index) {
    const board::RelayMap& map = board::kRelayMap[index];
    ctx.op.info("[%u/%u] %s -> %s no IO%d, jumper %s", static_cast<unsigned>(index + 1),
                static_cast<unsigned>(board::kRelayCount), map.net, map.relay, static_cast<int>(map.pin),
                map.jumper);

    const Status stOn = ctx.relays.set(index, true);
    if (stOn.failed()) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "%s: driver recusou o acionamento (%s)", map.net,
                 errName(stOn.err));
        return TestResult(Verdict::Fail, g_noteBuf);
    }

    snprintf(g_whatBuf, sizeof(g_whatBuf), "com %s acionado, medir continuidade do contato de %s", map.net,
             map.relay);
    snprintf(g_expectedBuf, sizeof(g_expectedBuf), "contato fechado (beep) e LED aceso em %s",
             map.ihmLedLabel);
    ctx.op.step(g_whatBuf, map.screwTerminals, g_expectedBuf);
    Verdict v = ctx.op.ask("resultado?");
    ctx.relays.set(index, false);
    if (v == Verdict::Abort) {
        return abortResult();
    }
    if (v == Verdict::Fail) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "%s/%s nao fecha: BC337, bobina ou %s", map.net, map.relay,
                 map.jumper);
        return TestResult(Verdict::Fail, g_noteBuf);
    }

    snprintf(g_whatBuf, sizeof(g_whatBuf), "com %s desacionado, confirmar contato aberto e LED apagado",
             map.net);
    snprintf(g_expectedBuf, sizeof(g_expectedBuf), "contato aberto e LED apagado em %s", map.ihmLedLabel);
    ctx.op.step(g_whatBuf, map.screwTerminals, g_expectedBuf);
    v = ctx.op.ask("resultado?");
    if (v == Verdict::Abort) {
        return abortResult();
    }
    if (v == Verdict::Fail) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "%s nao desliga: contato colado ou BC337 em curto", map.net);
        return TestResult(Verdict::Fail, g_noteBuf);
    }
    return TestResult(Verdict::Pass, "");
}

class Test02Relays : public ITest {
public:
    const char* id() const override { return "t2"; }
    const char* name() const override { return "Reles de limite LIM1..LIM4"; }
    uint8_t order() const override { return 2; }

    TestResult run(Ctx& ctx) override {
        printDesignNote(ctx);

        const Status stBegin = ctx.relays.begin();
        if (stBegin.failed()) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "banco de reles nao inicializa (%s)", errName(stBegin.err));
            return TestResult(Verdict::Fail, g_noteBuf);
        }
        ctx.relays.allOff();

        uint8_t count = ctx.relays.count();
        if (count > board::kRelayCount) {
            count = board::kRelayCount;
        }
        for (uint8_t i = 0; i < count; ++i) {
            const TestResult r = runOneRelay(ctx, i);
            if (r.verdict != Verdict::Pass) {
                ctx.relays.allOff();
                return r;
            }
        }

        return runWorstCase(ctx);
    }

private:
    static TestResult runWorstCase(Ctx& ctx) {
        ctx.op.info("--- pior caso do +5 V: 4 bobinas ligadas ao mesmo tempo ---");
        const Status stAll = ctx.relays.allOn();
        if (stAll.failed()) {
            snprintf(g_noteBuf, sizeof(g_noteBuf), "allOn recusado pelo banco de reles (%s)",
                     errName(stAll.err));
            return TestResult(Verdict::Fail, g_noteBuf);
        }
        ctx.op.step("medir o +5 V com os 4 reles acionados", "saida do LM2575-5 (+5V)",
                    "+5,00 V +/-5% (4,75 a 5,25 V) e os 4 LEDs de limite acesos na IHM");
        const Verdict v = ctx.op.ask("resultado?");
        ctx.relays.allOff();
        if (v == Verdict::Abort) {
            return abortResult();
        }
        if (v == Verdict::Fail) {
            return TestResult(Verdict::Fail, "+5V afunda com 4 bobinas: LM2575-5 ou hFE baixo");
        }
        return TestResult(Verdict::Pass, "4 reles, LEDs da IHM e +5V sob carga conferidos");
    }
};

}  // namespace

REGISTER_TEST(Test02Relays);
