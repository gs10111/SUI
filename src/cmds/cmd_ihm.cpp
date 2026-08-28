// Comandos da IHM do CN3/CN4 (folha 1/2): display SPI sem MISO, LED_TEST em IO2 e botoes.
// IO2/IO15 sao pinos de strapping do ESP32-WROOM-32D (Espressif ESP32 datasheet v3.9).
#include <Arduino.h>

#include "board_pins.h"
#include "core/cmd_parser.h"
#include "core/command.h"
#include "core/ctx.h"
#include "iface/ibuttons.h"
#include "status.h"

namespace {

constexpr uint32_t kMonitorTimeoutMs = 120000;
constexpr uint32_t kMonitorTableMs = 1000;

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

void joinArgs(char* out, uint16_t cap, uint8_t argc, const char* const* argv, uint8_t from) {
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    uint16_t used = 0;
    for (uint8_t i = from; i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        if (used > 0 && used + 1u < cap) {
            out[used++] = ' ';
            out[used] = '\0';
        }
        for (const char* p = argv[i]; *p != '\0' && used + 1u < cap; ++p) {
            out[used++] = *p;
        }
        out[used] = '\0';
    }
}

class CmdDisp : public ICommand {
public:
    const char* name() const override { return "disp"; }
    const char* usage() const override {
        return "uso: disp pattern <n> | disp off | disp reset | disp text <texto>";
    }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        if (argc < 2) {
            ctx.io.writeLine(usage());
            listPatterns(ctx);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "pattern")) {
            doPattern(ctx, argc, argv);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "off")) {
            report(ctx, "disp off", ctx.display.off());
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "reset")) {
            const Status st = ctx.display.hardReset();
            report(ctx, "disp reset", st);
            ctx.io.printf("RESET em IO%d, DC em IO%d, CS em IO%d, driver %s\r\n",
                          static_cast<int>(board::kDispReset), static_cast<int>(board::kDispDc),
                          static_cast<int>(board::kDispCs), ctx.display.driverName());
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "text")) {
            doText(ctx, argc, argv);
            return;
        }
        ctx.io.writeLine(usage());
    }

private:
    void report(Ctx& ctx, const char* what, Status st) {
        if (st.ok()) {
            ctx.io.printf("%s: OK\r\n", what);
        } else {
            ctx.io.printf("%s: ERRO %s\r\n", what, errName(st.err));
        }
    }

    void listPatterns(Ctx& ctx) {
        const uint8_t total = ctx.display.patternCount();
        ctx.io.printf("Driver %s, %u padroes:\r\n", ctx.display.driverName(), static_cast<unsigned>(total));
        for (uint8_t i = 0; i < total; ++i) {
            ctx.io.printf("  %u = %s\r\n", static_cast<unsigned>(i), ctx.display.patternDescription(i));
        }
    }

    void doPattern(Ctx& ctx, uint8_t argc, const char* const* argv) {
        uint32_t n = 0;
        if (argc < 3 || !cmd::parseU32(argv[2], n)) {
            ctx.io.writeLine(usage());
            listPatterns(ctx);
            return;
        }
        const uint8_t total = ctx.display.patternCount();
        if (n >= total) {
            ctx.io.printf("ERRO: padrao %lu inexistente\r\n", static_cast<unsigned long>(n));
            listPatterns(ctx);
            return;
        }
        const uint8_t idx = static_cast<uint8_t>(n);
        const Status st = ctx.display.showPattern(idx);
        report(ctx, "disp pattern", st);
        if (st.ok()) {
            ctx.io.printf("Padrao %u: %s (sem MISO no CN4: veredito e visual)\r\n", static_cast<unsigned>(idx),
                          ctx.display.patternDescription(idx));
        }
    }

    void doText(Ctx& ctx, uint8_t argc, const char* const* argv) {
        if (argc < 3) {
            ctx.io.writeLine(usage());
            return;
        }
        char text[cmd::kMaxLine];
        joinArgs(text, static_cast<uint16_t>(sizeof(text)), argc, argv, 2);
        const Status st = ctx.display.writeText(text);
        report(ctx, "disp text", st);
        if (st.ok()) {
            ctx.io.printf("Escrito: %s\r\n", text);
        }
    }
};

class CmdLed : public ICommand {
public:
    const char* name() const override { return "led"; }
    const char* usage() const override { return "uso: led <on|off>"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        bool on = false;
        if (argc < 2 || !cmd::parseOnOff(argv[1], on)) {
            ctx.io.writeLine(usage());
            return;
        }
        const uint8_t pin = static_cast<uint8_t>(board::kLedTest);
        pinMode(pin, OUTPUT);
        digitalWrite(pin, on ? HIGH : LOW);
        ctx.io.printf("LED_TEST (IO%d) = %s\r\n", static_cast<int>(board::kLedTest), on ? "ON" : "OFF");
        if (on) {
            ctx.io.writeLine("AVISO: IO2 e pino de strapping. Deixe em 'led off' antes de resetar a placa.");
        }
    }
};

class CmdBtn : public ICommand {
public:
    const char* name() const override { return "btn"; }
    const char* usage() const override { return "uso: btn"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        (void)argc;
        (void)argv;
        ctx.io.writeLine("Monitor de botoes do CN3. Envie 'q' pelo console para sair.");
        ctx.io.printf("UP em IO%d, DOWN em IO%d, MENU em IO%d.\r\n", static_cast<int>(board::kBtnUp),
                      static_cast<int>(board::kBtnDown), static_cast<int>(board::kBtnMenu));
        ctx.io.writeLine("IO34/IO35 sao input-only: nao tem pull-up interno, dependem do resistor externo.");
        const uint32_t start = ctx.io.nowMs();
        uint32_t lastTable = 0;
        for (;;) {
            ctx.buttons.poll();
            uint8_t which = 0;
            bool rising = false;
            while (ctx.buttons.takeEdge(which, rising)) {
                if (which >= kButtonCount) {
                    continue;
                }
                ctx.io.printf("[%8lu ms] %-4s borda %-7s nivel=%u presses=%lu bounces=%lu\r\n",
                              static_cast<unsigned long>(ctx.io.nowMs()), ctx.buttons.name(which),
                              rising ? "SUBIDA" : "DESCIDA",
                              static_cast<unsigned>(ctx.buttons.level(which) ? 1u : 0u),
                              static_cast<unsigned long>(ctx.buttons.pressCount(which)),
                              static_cast<unsigned long>(ctx.buttons.bounceCount(which)));
            }
            const uint32_t now = ctx.io.nowMs();
            if (now - lastTable >= kMonitorTableMs) {
                lastTable = now;
                printTable(ctx);
            }
            if (quitPressed(ctx)) {
                ctx.io.writeLine("Monitor encerrado pelo operador.");
                break;
            }
            if (now - start >= kMonitorTimeoutMs) {
                ctx.io.writeLine("Monitor encerrado por tempo limite.");
                break;
            }
            if (ctx.wdt.kicking()) {
                ctx.wdt.kickNow();
            }
            ctx.io.idle();
            delay(2);
        }
    }

private:
    void printTable(Ctx& ctx) {
        for (uint8_t i = 0; i < kButtonCount; ++i) {
            const bool stable = ctx.buttons.restLevelStable(i);
            ctx.io.printf("  %-4s nivel=%u presses=%-4lu bounces=%-4lu%s%s\r\n", ctx.buttons.name(i),
                          static_cast<unsigned>(ctx.buttons.level(i) ? 1u : 0u),
                          static_cast<unsigned long>(ctx.buttons.pressCount(i)),
                          static_cast<unsigned long>(ctx.buttons.bounceCount(i)),
                          ctx.buttons.inputOnly(i) ? " [input-only]" : "",
                          stable ? "" : " AVISO: nivel de repouso instavel");
        }
    }
};

}  // namespace

REGISTER_COMMAND(CmdDisp);
REGISTER_COMMAND(CmdLed);
REGISTER_COMMAND(CmdBtn);
