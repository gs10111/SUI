// Comandos de sistema do console (folhas 1/2 e 2/2): estado geral, boot, testes, relatorio e reset.
// ESP32-WROOM-32D (Espressif ESP32 datasheet v3.9); ESP.restart() do Arduino core 2.0.17.
#include <Arduino.h>

#include "board_pins.h"
#include "core/cmd_parser.h"
#include "core/command.h"
#include "core/ctx.h"
#include "status.h"
#include "verdict.h"

namespace {

char g_textBuf[2048];
Report g_lastReport;

const char* aoModeName(AoMode m) {
    return (m == AoMode::Current) ? "CORRENTE (4-20 mA)" : "TENSAO (0-10 V)";
}

const char* aoModeShort(AoMode m) {
    return (m == AoMode::Current) ? "I" : "V";
}

const char* yesNo(bool v) {
    return v ? "SIM" : "NAO";
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

void dumpHuman(Ctx& ctx, const Report& rep) {
    rep.formatHuman(g_textBuf, static_cast<uint16_t>(sizeof(g_textBuf)));
    ctx.io.write(g_textBuf);
}

void dumpCsv(Ctx& ctx, const Report& rep) {
    rep.formatCsv(g_textBuf, static_cast<uint16_t>(sizeof(g_textBuf)));
    ctx.io.write(g_textBuf);
}

class CmdStatus : public ICommand {
public:
    const char* name() const override { return "status"; }
    const char* usage() const override { return "uso: status"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        (void)argc;
        (void)argv;
        ctx.io.writeLine("---- STATUS ----");
        ctx.io.printf("Placa      : DE-PURI-DI261924 REV %s   FW %s\r\n", ctx.boardRev, ctx.fwVersion);
        ctx.io.printf("Serie/Data : %s / %s\r\n", ctx.report.serial(), ctx.report.date());
        ctx.io.printf("AO modo    : %s   OP_MODE em IO%d\r\n", aoModeName(ctx.ao.mode()),
                      static_cast<int>(board::kXtrOpMode));
        ctx.io.printf("AO SPI     : %lu Hz   fundo de escala 0x%04X\r\n",
                      static_cast<unsigned long>(ctx.ao.spiHz()),
                      static_cast<unsigned>(ctx.ao.fullScaleCode()));
        for (uint8_t axis = 0; axis < board::kAxisCount; ++axis) {
            for (uint8_t m = 0; m < kCalModeCount; ++m) {
                const AoMode md = static_cast<AoMode>(m);
                ctx.io.printf("Cal %s/%s     : %s\r\n", board::kAxisName[axis], aoModeShort(md),
                              ctx.cal.has(axis, md) ? "CALIBRADO" : "AUSENTE");
            }
        }
        const uint8_t relays = ctx.relays.count();
        for (uint8_t i = 0; i < relays; ++i) {
            bool on = false;
            const Status st = ctx.relays.get(i, on);
            const char* net = (i < board::kRelayCount) ? board::kRelayMap[i].net : "?";
            const char* ref = (i < board::kRelayCount) ? board::kRelayMap[i].relay : "?";
            if (st.ok()) {
                ctx.io.printf("Rele %u      : %-5s %-4s %s\r\n", static_cast<unsigned>(i + 1u), net, ref,
                              on ? "ON" : "OFF");
            } else {
                ctx.io.printf("Rele %u      : %-5s %-4s ERRO %s\r\n", static_cast<unsigned>(i + 1u), net, ref,
                              errName(st.err));
            }
        }
        ctx.io.printf("RS485      : %lu bps   protocolo %s\r\n",
                      static_cast<unsigned long>(ctx.rs485.baud()), ctx.proto.name());
        ctx.io.printf("Display    : %s   %u padroes\r\n", ctx.display.driverName(),
                      static_cast<unsigned>(ctx.display.patternCount()));
        ctx.io.printf("Watchdog   : chutando=%s   kicks=%lu   periodo=%lu ms\r\n", yesNo(ctx.wdt.kicking()),
                      static_cast<unsigned long>(ctx.wdt.kickCount()),
                      static_cast<unsigned long>(ctx.wdt.kickPeriodMs()));
        const uint32_t ms = ctx.io.nowMs();
        const uint32_t sec = ms / 1000u;
        ctx.io.printf("Uptime     : %lu ms (%luh %02lum %02lus)\r\n", static_cast<unsigned long>(ms),
                      static_cast<unsigned long>(sec / 3600u), static_cast<unsigned long>((sec / 60u) % 60u),
                      static_cast<unsigned long>(sec % 60u));
    }
};

class CmdBoot : public ICommand {
public:
    const char* name() const override { return "boot"; }
    const char* usage() const override { return "uso: boot"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        (void)argc;
        (void)argv;
        ctx.io.writeLine("---- BOOT ----");
        ctx.io.printf("Reset reason : %lu (%s)\r\n", static_cast<unsigned long>(ctx.boot.resetReason),
                      ctx.boot.resetReasonName);
        ctx.io.printf("MAC          : %s\r\n", ctx.boot.macText);
        ctx.io.printf("Chip ID      : 0x%08lX%08lX\r\n", static_cast<unsigned long>(ctx.boot.chipIdHigh),
                      static_cast<unsigned long>(ctx.boot.chipIdLow));
        ctx.io.printf("Flash        : %lu bytes (%lu MB)\r\n",
                      static_cast<unsigned long>(ctx.boot.flashSizeBytes),
                      static_cast<unsigned long>(ctx.boot.flashSizeBytes / (1024ul * 1024ul)));
        ctx.io.writeLine("Strapping (nivel lido no boot):");
        uint8_t n = ctx.boot.strappingCount;
        if (n > board::kStrappingCount) {
            n = board::kStrappingCount;
        }
        if (n > 8) {
            n = 8;
        }
        for (uint8_t i = 0; i < n; ++i) {
            ctx.io.printf("  IO%-2d = %u\r\n", static_cast<int>(board::kStrappingPins[i]),
                          static_cast<unsigned>(ctx.boot.strappingLevel[i]));
        }
        ctx.io.printf("WDT teste    : esperado=%s observado=%s\r\n", yesNo(ctx.boot.wdtResetExpected),
                      yesNo(ctx.boot.wdtResetObserved));
    }
};

class CmdTest : public ICommand {
public:
    const char* name() const override { return "test"; }
    const char* usage() const override { return "uso: test <id>"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        if (argc < 2) {
            ctx.io.writeLine(usage());
            return;
        }
        if (ctx.runner == nullptr) {
            ctx.io.writeLine("ERRO: executor de testes indisponivel");
            return;
        }
        if (ctx.runner->busy()) {
            ctx.io.writeLine("ERRO: executor ocupado");
            return;
        }
        TestResult r;
        if (!ctx.runner->runById(argv[1], r)) {
            ctx.io.printf("ERRO: teste '%s' nao encontrado\r\n", argv[1]);
            return;
        }
        ctx.io.printf("Teste %s: %s  %s\r\n", argv[1], verdictName(r.verdict), r.note);
    }
};

class CmdSelftest : public ICommand {
public:
    const char* name() const override { return "selftest"; }
    const char* usage() const override { return "uso: selftest"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        (void)argc;
        (void)argv;
        if (ctx.runner == nullptr) {
            ctx.io.writeLine("ERRO: executor de testes indisponivel");
            return;
        }
        if (ctx.runner->busy()) {
            ctx.io.writeLine("ERRO: executor ocupado");
            return;
        }
        ctx.runner->runSuite();
    }
};

class CmdReport : public ICommand {
public:
    const char* name() const override { return "report"; }
    const char* usage() const override { return "uso: report [show|csv|last]"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        const char* what = (argc >= 2) ? argv[1] : "show";
        if (cmd::equalsIgnoreCase(what, "show")) {
            dumpHuman(ctx, ctx.report);
            return;
        }
        if (cmd::equalsIgnoreCase(what, "csv")) {
            dumpCsv(ctx, ctx.report);
            return;
        }
        if (cmd::equalsIgnoreCase(what, "last")) {
            const Status st = g_lastReport.load(ctx.kv);
            if (st.failed()) {
                ctx.io.printf("ERRO: nenhum relatorio na NVS (%s)\r\n", errName(st.err));
                return;
            }
            g_lastReport.setMeta(ctx.fwVersion, ctx.boardRev);
            ctx.io.writeLine("---- ULTIMO RELATORIO GRAVADO ----");
            dumpHuman(ctx, g_lastReport);
            dumpCsv(ctx, g_lastReport);
            return;
        }
        ctx.io.writeLine(usage());
    }
};

class CmdSerial : public ICommand {
public:
    const char* name() const override { return "serial"; }
    const char* usage() const override { return "uso: serial <sn>"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        if (argc < 2) {
            ctx.io.printf("Serie atual: %s\r\n", ctx.report.serial());
            return;
        }
        char sn[cmd::kMaxLine];
        joinArgs(sn, static_cast<uint16_t>(sizeof(sn)), argc, argv, 1);
        ctx.report.setSerial(sn);
        const Status st = ctx.kv.putString("serial", ctx.report.serial());
        if (st.failed()) {
            ctx.io.printf("Serie: %s (AVISO: nao gravado na NVS, %s)\r\n", ctx.report.serial(), errName(st.err));
            return;
        }
        ctx.io.printf("Serie: %s (gravado na NVS)\r\n", ctx.report.serial());
    }
};

class CmdDate : public ICommand {
public:
    const char* name() const override { return "date"; }
    const char* usage() const override { return "uso: date <texto>"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        if (argc < 2) {
            ctx.io.printf("Data atual: %s\r\n", ctx.report.date());
            return;
        }
        char text[cmd::kMaxLine];
        joinArgs(text, static_cast<uint16_t>(sizeof(text)), argc, argv, 1);
        ctx.report.setDate(text);
        const Status st = ctx.kv.putString("date", ctx.report.date());
        if (st.failed()) {
            ctx.io.printf("Data: %s (AVISO: nao gravada na NVS, %s)\r\n", ctx.report.date(), errName(st.err));
            return;
        }
        ctx.io.printf("Data: %s (gravada na NVS)\r\n", ctx.report.date());
    }
};

class CmdSafe : public ICommand {
public:
    const char* name() const override { return "safe"; }
    const char* usage() const override { return "uso: safe"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        (void)argc;
        (void)argv;
        ctx.safe.enterSafeState();
        ctx.io.writeLine("Estado seguro aplicado: reles OFF, DAC em zero, OP_MODE tensao, display apagado.");
    }
};

class CmdReboot : public ICommand {
public:
    const char* name() const override { return "reboot"; }
    const char* usage() const override { return "uso: reboot"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        (void)argc;
        (void)argv;
        ctx.safe.enterSafeState();
        pinMode(static_cast<uint8_t>(board::kLedTest), OUTPUT);
        digitalWrite(static_cast<uint8_t>(board::kLedTest), LOW);
        ctx.io.writeLine("Estado seguro aplicado, LED_TEST (IO2) em nivel baixo. Reiniciando o ESP32...");
        delay(50);
        ESP.restart();
    }
};

}  // namespace

REGISTER_COMMAND(CmdStatus);
REGISTER_COMMAND(CmdBoot);
REGISTER_COMMAND(CmdTest);
REGISTER_COMMAND(CmdSelftest);
REGISTER_COMMAND(CmdReport);
REGISTER_COMMAND(CmdSerial);
REGISTER_COMMAND(CmdDate);
REGISTER_COMMAND(CmdSafe);
REGISTER_COMMAND(CmdReboot);
