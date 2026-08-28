// Comandos do console disponiveis no simulador de host (os de producao dependem de Arduino).
#include <stdio.h>

#include "core/cmd_parser.h"
#include "core/command.h"
#include "core/ctx.h"

namespace {

char g_buf[1024];

class SimSelftestCommand : public ICommand {
public:
    const char* name() const override { return "selftest"; }
    const char* usage() const override { return "selftest - executa todos os itens na ordem"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        (void)argc;
        (void)argv;
        if (ctx.runner == nullptr) {
            ctx.io.writeLine("runner indisponivel");
            return;
        }
        ctx.runner->runSuite();
    }
};

class SimTestCommand : public ICommand {
public:
    const char* name() const override { return "test"; }
    const char* usage() const override { return "test <id> - executa um item isolado (t0..t8)"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        if (argc < 2 || ctx.runner == nullptr) {
            ctx.io.writeLine(usage());
            return;
        }
        TestResult result;
        if (!ctx.runner->runById(argv[1], result)) {
            ctx.io.printf("teste desconhecido: %s\r\n", argv[1]);
        }
    }
};

class SimReportCommand : public ICommand {
public:
    const char* name() const override { return "report"; }
    const char* usage() const override { return "report [show|csv] - relatorio legivel ou linha CSV"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        const bool csv = (argc >= 2) && cmd::equalsIgnoreCase(argv[1], "csv");
        if (csv) {
            ctx.report.formatCsv(g_buf, sizeof(g_buf));
        } else {
            ctx.report.formatHuman(g_buf, sizeof(g_buf));
        }
        ctx.io.write(g_buf);
    }
};

class SimSerialCommand : public ICommand {
public:
    const char* name() const override { return "serial"; }
    const char* usage() const override { return "serial <sn> - numero de serie da placa"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        if (argc < 2) {
            ctx.io.printf("serie atual: %s\r\n", ctx.report.serial());
            return;
        }
        ctx.report.setSerial(argv[1]);
        ctx.kv.putString("serial", argv[1]);
        ctx.io.printf("serie: %s\r\n", ctx.report.serial());
    }
};

class SimStatusCommand : public ICommand {
public:
    const char* name() const override { return "status"; }
    const char* usage() const override { return "status - estado geral do jig"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        (void)argc;
        (void)argv;
        ctx.io.printf("firmware      : %s (BOARD_REV %s)\r\n", ctx.fwVersion, ctx.boardRev);
        ctx.io.printf("modo da saida : %s\r\n", ctx.ao.mode() == AoMode::Voltage ? "tensao" : "corrente");
        ctx.io.printf("SPI do DAC    : %u Hz\r\n", static_cast<unsigned>(ctx.ao.spiHz()));
        for (uint8_t axis = 0; axis < board::kAxisCount; ++axis) {
            ctx.io.printf("calibracao %s  : tensao=%s corrente=%s\r\n", board::kAxisName[axis],
                          ctx.cal.has(axis, AoMode::Voltage) ? "sim" : "nao",
                          ctx.cal.has(axis, AoMode::Current) ? "sim" : "nao");
        }
        for (uint8_t i = 0; i < ctx.relays.count(); ++i) {
            bool on = false;
            ctx.relays.get(i, on);
            ctx.io.printf("%s (%s)     : %s\r\n", board::kRelayMap[i].net, board::kRelayMap[i].relay,
                          on ? "ON" : "OFF");
        }
        ctx.io.printf("RS-485        : %u baud\r\n", static_cast<unsigned>(ctx.rs485.baud()));
        ctx.io.printf("display       : driver %s\r\n", ctx.display.driverName());
        ctx.io.printf("watchdog      : %s, %u chutes, periodo %u ms\r\n", ctx.wdt.kicking() ? "chutando" : "PARADO",
                      static_cast<unsigned>(ctx.wdt.kickCount()), static_cast<unsigned>(ctx.wdt.kickPeriodMs()));
        ctx.io.printf("uptime        : %u ms\r\n", static_cast<unsigned>(ctx.io.nowMs()));
    }
};

class SimFakeCalCommand : public ICommand {
public:
    const char* name() const override { return "calfake"; }
    const char* usage() const override { return "calfake - injeta calibracao plausivel (so no simulador)"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        (void)argc;
        (void)argv;
        const calmath::Point volt1 = {6553u, 1.0f};
        const calmath::Point volt2 = {58982u, 9.0f};
        const calmath::Point curr1 = {13107u, 4.0f};
        const calmath::Point curr2 = {65535u, 20.0f};
        for (uint8_t axis = 0; axis < board::kAxisCount; ++axis) {
            ctx.cal.setFromPoints(axis, AoMode::Voltage, volt1, volt2);
            ctx.cal.setFromPoints(axis, AoMode::Current, curr1, curr2);
        }
        const Status st = ctx.cal.save();
        ctx.io.printf("calibracao simulada gravada (%s): 0-10 V em 0x0000..0xFFFF e 4-20 mA com live\r\n"
                      "zero digital (4 mA no codigo 13107, 20 mA em 0xFFFF), o que da RSET coerente\r\n",
                      errName(st.err));
    }
};

}  // namespace

REGISTER_COMMAND(SimSelftestCommand);
REGISTER_COMMAND(SimTestCommand);
REGISTER_COMMAND(SimReportCommand);
REGISTER_COMMAND(SimSerialCommand);
REGISTER_COMMAND(SimStatusCommand);
REGISTER_COMMAND(SimFakeCalCommand);
