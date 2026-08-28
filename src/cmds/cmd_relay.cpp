// Comando dos reles de limite RL2..RL5 (folha 2/2), acionados por LIM1..LIM4 do ESP32-WROOM-32D.
// Sequencia, acionamento individual e ensaio de margem de fecho com contato intermitente.
#include <Arduino.h>

#include "board_pins.h"
#include "core/cmd_parser.h"
#include "core/command.h"
#include "core/ctx.h"
#include "status.h"

namespace {

constexpr uint32_t kSeqOnMs = 1000;
constexpr uint32_t kSeqOffMs = 1000;
constexpr uint32_t kMarginHalfMs = 500;
constexpr uint8_t kMarginCycles = 20;

bool quitPressed(Ctx& ctx) {
    uint8_t b = 0;
    bool quit = false;
    while (ctx.io.readByte(b)) {
        if (b == 'q' || b == 'Q' || b == 0x1B) {
            quit = true;
        }
    }
    return quit;
}

bool waitOrQuit(Ctx& ctx, uint32_t ms) {
    const uint32_t start = ctx.io.nowMs();
    for (;;) {
        if (quitPressed(ctx)) {
            return true;
        }
        if (ctx.wdt.kicking()) {
            ctx.wdt.kickNow();
        }
        ctx.io.idle();
        if (ctx.io.nowMs() - start >= ms) {
            return false;
        }
        delay(5);
    }
}

void printMap(Ctx& ctx, uint8_t idx) {
    if (idx >= board::kRelayCount) {
        return;
    }
    const board::RelayMap& m = board::kRelayMap[idx];
    ctx.io.printf("Rele %u: net %s (IO%d) -> %s, bornes %s, jumper %s, LED da IHM %s\r\n",
                  static_cast<unsigned>(idx + 1u), m.net, static_cast<int>(m.pin), m.relay, m.screwTerminals,
                  m.jumper, m.ihmLedLabel);
}

bool setAndReport(Ctx& ctx, uint8_t idx, bool on) {
    const Status st = ctx.relays.set(idx, on);
    if (st.failed()) {
        ctx.io.printf("Rele %u: ERRO %s\r\n", static_cast<unsigned>(idx + 1u), errName(st.err));
        return false;
    }
    return true;
}

class CmdRelay : public ICommand {
public:
    const char* name() const override { return "relay"; }
    const char* usage() const override {
        return "uso: relay <1..4> <on|off> | relay seq | relay all <on|off> | relay margin <1..4>";
    }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        if (argc < 2) {
            ctx.io.writeLine(usage());
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "seq")) {
            doSeq(ctx);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "all")) {
            doAll(ctx, argc, argv);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "margin")) {
            doMargin(ctx, argc, argv);
            return;
        }
        doOne(ctx, argc, argv);
    }

private:
    bool parseIndex(Ctx& ctx, const char* text, uint8_t& idx) {
        uint32_t n = 0;
        if (!cmd::parseU32(text, n) || n < 1u || n > board::kRelayCount) {
            ctx.io.writeLine(usage());
            return false;
        }
        idx = static_cast<uint8_t>(n - 1u);
        if (idx >= ctx.relays.count()) {
            ctx.io.writeLine("ERRO: indice fora do banco de saidas");
            return false;
        }
        return true;
    }

    void doOne(Ctx& ctx, uint8_t argc, const char* const* argv) {
        uint8_t idx = 0;
        bool on = false;
        if (argc < 3 || !parseIndex(ctx, argv[1], idx)) {
            return;
        }
        if (!cmd::parseOnOff(argv[2], on)) {
            ctx.io.writeLine(usage());
            return;
        }
        if (!setAndReport(ctx, idx, on)) {
            return;
        }
        ctx.io.printf("Rele %u = %s\r\n", static_cast<unsigned>(idx + 1u), on ? "ON" : "OFF");
        printMap(ctx, idx);
    }

    void doAll(Ctx& ctx, uint8_t argc, const char* const* argv) {
        bool on = false;
        if (argc < 3 || !cmd::parseOnOff(argv[2], on)) {
            ctx.io.writeLine(usage());
            return;
        }
        const Status st = on ? ctx.relays.allOn() : ctx.relays.allOff();
        if (st.failed()) {
            ctx.io.printf("relay all: ERRO %s\r\n", errName(st.err));
            return;
        }
        ctx.io.printf("Todos os reles = %s\r\n", on ? "ON" : "OFF");
    }

    void doSeq(Ctx& ctx) {
        const uint8_t total = ctx.relays.count();
        ctx.io.printf("Sequencia em %u reles, %lu ms ligado e %lu ms desligado. 'q' aborta.\r\n",
                      static_cast<unsigned>(total), static_cast<unsigned long>(kSeqOnMs),
                      static_cast<unsigned long>(kSeqOffMs));
        for (uint8_t idx = 0; idx < total; ++idx) {
            printMap(ctx, idx);
            if (!setAndReport(ctx, idx, true)) {
                ctx.relays.allOff();
                return;
            }
            ctx.io.printf("  rele %u ON\r\n", static_cast<unsigned>(idx + 1u));
            if (waitOrQuit(ctx, kSeqOnMs)) {
                ctx.relays.allOff();
                ctx.io.writeLine("Sequencia abortada pelo operador.");
                return;
            }
            if (!setAndReport(ctx, idx, false)) {
                ctx.relays.allOff();
                return;
            }
            ctx.io.printf("  rele %u OFF\r\n", static_cast<unsigned>(idx + 1u));
            if (waitOrQuit(ctx, kSeqOffMs)) {
                ctx.relays.allOff();
                ctx.io.writeLine("Sequencia abortada pelo operador.");
                return;
            }
        }
        ctx.relays.allOff();
        ctx.io.writeLine("Sequencia concluida, todos os reles OFF.");
    }

    void doMargin(Ctx& ctx, uint8_t argc, const char* const* argv) {
        uint8_t idx = 0;
        if (argc < 3 || !parseIndex(ctx, argv[2], idx)) {
            return;
        }
        printMap(ctx, idx);
        ctx.io.printf("Margem de fecho: %u ciclos de %lu ms ligado e %lu ms desligado. 'q' aborta.\r\n",
                      static_cast<unsigned>(kMarginCycles), static_cast<unsigned long>(kMarginHalfMs),
                      static_cast<unsigned long>(kMarginHalfMs));
        ctx.op.info("Acompanhe o contato e o LED da IHM e reporte QUALQUER falha de fechamento.");
        for (uint8_t cycle = 0; cycle < kMarginCycles; ++cycle) {
            if (!setAndReport(ctx, idx, true)) {
                ctx.relays.allOff();
                return;
            }
            if (waitOrQuit(ctx, kMarginHalfMs)) {
                ctx.relays.allOff();
                ctx.io.writeLine("Ensaio abortado pelo operador.");
                return;
            }
            if (!setAndReport(ctx, idx, false)) {
                ctx.relays.allOff();
                return;
            }
            if (waitOrQuit(ctx, kMarginHalfMs)) {
                ctx.relays.allOff();
                ctx.io.writeLine("Ensaio abortado pelo operador.");
                return;
            }
            ctx.io.printf("  ciclo %2u/%u\r\n", static_cast<unsigned>(cycle + 1u),
                          static_cast<unsigned>(kMarginCycles));
        }
        ctx.relays.allOff();
        const bool failed = ctx.op.askYes("Houve alguma falha de fechamento nos 20 ciclos?");
        ctx.io.writeLine("Criterio: UM unico fecho intermitente ja reprova o item.");
        ctx.io.printf("Rele %u: %s\r\n", static_cast<unsigned>(idx + 1u), failed ? "REPROVADO" : "APROVADO");
    }
};

}  // namespace

REGISTER_COMMAND(CmdRelay);
