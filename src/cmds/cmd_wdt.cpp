// Comando do watchdog externo STWD100YNYWY3F (ST DocID14134 Rev 11), folha 1/2: WDI em IO19, J15 no reset.
// Nao ha desligamento por software: o pino EN tem pull-down interno e habilita o chip.
#include "board_pins.h"
#include "core/cmd_parser.h"
#include "core/command.h"
#include "core/ctx.h"
#include "status.h"
#include "verdict.h"

namespace {

class CmdWdt : public ICommand {
public:
    const char* name() const override { return "wdt"; }
    const char* usage() const override { return "uso: wdt status | wdt kick <on|off> | wdt test"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        if (argc < 2) {
            ctx.io.writeLine(usage());
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "status")) {
            doStatus(ctx);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "kick")) {
            doKick(ctx, argc, argv);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "test")) {
            doTest(ctx);
            return;
        }
        ctx.io.writeLine(usage());
    }

private:
    void doStatus(Ctx& ctx) {
        ctx.io.writeLine("---- WATCHDOG EXTERNO STWD100 ----");
        ctx.io.printf("WDI          : IO%d, pulso de %lu us\r\n", static_cast<int>(board::kWdi),
                      static_cast<unsigned long>(board::kWdiPulseUs));
        ctx.io.printf("Periodo kick : %lu ms\r\n", static_cast<unsigned long>(ctx.wdt.kickPeriodMs()));
        ctx.io.printf("Kicks        : %lu\r\n", static_cast<unsigned long>(ctx.wdt.kickCount()));
        ctx.io.printf("tWD          : min %lu ms / tipico %lu ms\r\n",
                      static_cast<unsigned long>(ctx.wdt.minTimeoutMs()),
                      static_cast<unsigned long>(ctx.wdt.typTimeoutMs()));
        ctx.io.printf("Chutando     : %s\r\n", ctx.wdt.kicking() ? "SIM" : "NAO");
        ctx.io.writeLine("NAO existe desligar o watchdog por software: o pino EN do STWD100 tem");
        ctx.io.writeLine("pull-down interno e habilita o chip quando flutuante ou em nivel baixo.");
        ctx.io.writeLine("O unico controle de bancada e o jumper J15, na linha de reset.");
    }

    void doKick(Ctx& ctx, uint8_t argc, const char* const* argv) {
        bool on = false;
        if (argc < 3 || !cmd::parseOnOff(argv[2], on)) {
            ctx.io.writeLine(usage());
            return;
        }
        const Status st = ctx.wdt.setKicking(on);
        if (st.failed()) {
            ctx.io.printf("wdt kick: ERRO %s\r\n", errName(st.err));
            return;
        }
        if (on) {
            ctx.io.printf("Kick LIGADO: pulso em IO%d a cada %lu ms.\r\n", static_cast<int>(board::kWdi),
                          static_cast<unsigned long>(ctx.wdt.kickPeriodMs()));
            return;
        }
        ctx.io.writeLine("Kick DESLIGADO.");
        ctx.io.printf("ATENCAO: com J15 fechado a placa vai RESETAR em cerca de %lu ms (tWD tipico,\r\n",
                      static_cast<unsigned long>(ctx.wdt.typTimeoutMs()));
        ctx.io.printf("minimo %lu ms). Com J15 aberto o reset nao chega ao ESP32.\r\n",
                      static_cast<unsigned long>(ctx.wdt.minTimeoutMs()));
    }

    void doTest(Ctx& ctx) {
        if (ctx.runner == nullptr) {
            ctx.io.writeLine("ERRO: executor de testes indisponivel");
            return;
        }
        if (ctx.runner->busy()) {
            ctx.io.writeLine("ERRO: executor ocupado");
            return;
        }
        TestResult r;
        if (!ctx.runner->runById("t6", r)) {
            ctx.io.writeLine("ERRO: teste 't6' nao encontrado");
            return;
        }
        ctx.io.printf("Teste t6: %s  %s\r\n", verdictName(r.verdict), r.note);
    }
};

}  // namespace

REGISTER_COMMAND(CmdWdt);
