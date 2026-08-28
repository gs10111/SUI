// Comandos da cadeia analogica (folha 2/2): DAC8562 (TI SLAS719E) + XTR300 (TI SBOS326).
// Saida por eixo, varredura de monotonicidade, clock do SPI e calibracao de 2 pontos.
#include <Arduino.h>

#include "board_pins.h"
#include "core/cmd_parser.h"
#include "core/command.h"
#include "core/ctx.h"
#include "drivers/calibration.h"
#include "status.h"

namespace {

constexpr uint16_t kSweepSteps = 256;
constexpr uint32_t kSweepStepMs = 20;
constexpr uint16_t kDacRampCount = 4;
constexpr uint16_t kDacRamp[kDacRampCount] = {0x0000u, 0x5555u, 0xAAAAu, 0xFFFFu};

const char* aoModeName(AoMode m) {
    return (m == AoMode::Current) ? "CORRENTE" : "TENSAO";
}

const char* aoModeUnit(AoMode m) {
    return (m == AoMode::Current) ? "mA" : "V";
}

bool parseAoMode(const char* s, AoMode& mode) {
    if (cmd::equalsIgnoreCase(s, "v") || cmd::equalsIgnoreCase(s, "volt") || cmd::equalsIgnoreCase(s, "tensao")) {
        mode = AoMode::Voltage;
        return true;
    }
    if (cmd::equalsIgnoreCase(s, "i") || cmd::equalsIgnoreCase(s, "ma") || cmd::equalsIgnoreCase(s, "corrente")) {
        mode = AoMode::Current;
        return true;
    }
    return false;
}

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

void reportStatus(Ctx& ctx, const char* what, Status st) {
    if (st.ok()) {
        ctx.io.printf("%s: OK\r\n", what);
    } else {
        ctx.io.printf("%s: ERRO %s\r\n", what, errName(st.err));
    }
}

void printVihWarning(Ctx& ctx) {
    ctx.io.writeLine("ALERTA DE PROJETO (DAC8562, TI SLAS719E):");
    ctx.io.writeLine("  VIH minimo do DAC8562 = 0,7 x AVDD = 3,5 V com AVDD = 5 V.");
    ctx.io.writeLine("  O ESP32 aciona SCLK/SYNC/DIN com apenas 3,3 V: abaixo do VIH garantido.");
    ctx.io.writeLine("  Escrita erratica COM bordas limpas no analisador logico aponta margem de VIH,");
    ctx.io.writeLine("  nao defeito de software. Reduza o clock com 'dac spi <hz>' e repita o ensaio.");
}

class CmdAo : public ICommand {
public:
    const char* name() const override { return "ao"; }
    const char* usage() const override {
        return "uso: ao raw <x|y> <code16> | ao <x|y> <v|i> <valor> | ao sweep <x|y> | ao mode <v|i> | ao zero";
    }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        if (argc < 2) {
            ctx.io.writeLine(usage());
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "zero")) {
            reportStatus(ctx, "ao zero", ctx.ao.zeroAll());
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "mode")) {
            doMode(ctx, argc, argv);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "raw")) {
            doRaw(ctx, argc, argv);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "sweep")) {
            doSweep(ctx, argc, argv);
            return;
        }
        doEngineering(ctx, argc, argv);
    }

private:
    void doMode(Ctx& ctx, uint8_t argc, const char* const* argv) {
        AoMode mode = AoMode::Voltage;
        if (argc < 3 || !parseAoMode(argv[2], mode)) {
            ctx.io.writeLine(usage());
            return;
        }
        const Status st = ctx.ao.setMode(mode);
        if (st.failed()) {
            ctx.io.printf("ao mode: ERRO %s\r\n", errName(st.err));
            return;
        }
        ctx.io.printf("Modo do XTR300 = %s (OP_MODE em IO%d)\r\n", aoModeName(ctx.ao.mode()),
                      static_cast<int>(board::kXtrOpMode));
    }

    void doRaw(Ctx& ctx, uint8_t argc, const char* const* argv) {
        uint8_t axis = 0;
        uint32_t code = 0;
        if (argc < 4 || !cmd::parseAxis(argv[2], axis) || !cmd::parseU32(argv[3], code) || code > 0xFFFFu) {
            ctx.io.writeLine(usage());
            return;
        }
        const uint16_t value = static_cast<uint16_t>(code);
        const Status st = ctx.ao.setRaw(axis, value);
        if (st.failed()) {
            ctx.io.printf("ao raw: ERRO %s\r\n", errName(st.err));
            return;
        }
        ctx.io.printf("Eixo %s = 0x%04X (%u) em %s, terminais %s\r\n", board::kAxisName[axis],
                      static_cast<unsigned>(value), static_cast<unsigned>(value), aoModeName(ctx.ao.mode()),
                      board::kAxisTerminals[axis]);
    }

    void doSweep(Ctx& ctx, uint8_t argc, const char* const* argv) {
        uint8_t axis = 0;
        if (argc < 3 || !cmd::parseAxis(argv[2], axis)) {
            ctx.io.writeLine(usage());
            return;
        }
        const uint16_t full = ctx.ao.fullScaleCode();
        ctx.io.printf("Rampa no eixo %s: 0 -> 0x%04X -> 0, %u passos de %lu ms. 'q' aborta.\r\n",
                      board::kAxisName[axis], static_cast<unsigned>(full), static_cast<unsigned>(kSweepSteps),
                      static_cast<unsigned long>(kSweepStepMs));
        ctx.io.writeLine("Observe no multimetro/osciloscopio: monotonicidade e ausencia de glitch.");
        if (rampTo(ctx, axis, full, true) || rampTo(ctx, axis, full, false)) {
            const Status st = ctx.ao.setRaw(axis, 0);
            ctx.io.printf("Rampa interrompida: eixo %s zerado (%s).\r\n", board::kAxisName[axis],
                          errName(st.err));
            return;
        }
        ctx.io.writeLine("Rampa concluida.");
    }

    bool rampTo(Ctx& ctx, uint8_t axis, uint16_t full, bool rising) {
        for (uint16_t step = 0; step <= kSweepSteps; ++step) {
            const uint16_t k = rising ? step : static_cast<uint16_t>(kSweepSteps - step);
            const uint16_t code = static_cast<uint16_t>((static_cast<uint32_t>(full) * k) / kSweepSteps);
            const Status st = ctx.ao.setRaw(axis, code);
            if (st.failed()) {
                ctx.io.printf("Rampa abortada: ERRO %s no code 0x%04X\r\n", errName(st.err),
                              static_cast<unsigned>(code));
                return true;
            }
            if ((step % 32u) == 0u) {
                ctx.io.printf("  %s %3u%%  code 0x%04X\r\n", rising ? "subida " : "descida",
                              static_cast<unsigned>((step * 100u) / kSweepSteps), static_cast<unsigned>(code));
            }
            if (waitOrQuit(ctx, kSweepStepMs)) {
                ctx.io.writeLine("Rampa abortada pelo operador.");
                return true;
            }
        }
        return false;
    }

    void doEngineering(Ctx& ctx, uint8_t argc, const char* const* argv) {
        uint8_t axis = 0;
        AoMode mode = AoMode::Voltage;
        float value = 0.0f;
        if (argc < 4 || !cmd::parseAxis(argv[1], axis) || !parseAoMode(argv[2], mode) ||
            !cmd::parseFloat(argv[3], value)) {
            ctx.io.writeLine(usage());
            return;
        }
        if (!ctx.cal.has(axis, mode)) {
            ctx.io.printf("ERRO: eixo %s em %s nao esta calibrado. Rode 'cal %s %s' antes.\r\n",
                          board::kAxisName[axis], aoModeName(mode), board::kAxisName[axis],
                          (mode == AoMode::Current) ? "i" : "v");
            return;
        }
        if (ctx.ao.mode() != mode) {
            const Status stMode = ctx.ao.setMode(mode);
            if (stMode.failed()) {
                ctx.io.printf("ERRO ao trocar o modo: %s\r\n", errName(stMode.err));
                return;
            }
        }
        const Status st = ctx.ao.setEngineering(axis, value);
        if (st.failed()) {
            ctx.io.printf("ao: ERRO %s\r\n", errName(st.err));
            return;
        }
        uint16_t code = 0;
        if (ctx.ao.getRaw(axis, code).ok()) {
            ctx.io.printf("Eixo %s = %.4f %s (code 0x%04X), terminais %s\r\n", board::kAxisName[axis],
                          static_cast<double>(value), aoModeUnit(mode), static_cast<unsigned>(code),
                          board::kAxisTerminals[axis]);
        } else {
            ctx.io.printf("Eixo %s = %.4f %s\r\n", board::kAxisName[axis], static_cast<double>(value),
                          aoModeUnit(mode));
        }
    }
};

class CmdDac : public ICommand {
public:
    const char* name() const override { return "dac"; }
    const char* usage() const override { return "uso: dac selftest | dac spi <hz>"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        if (argc < 2) {
            ctx.io.writeLine(usage());
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "selftest")) {
            doSelftest(ctx);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "spi")) {
            doSpi(ctx, argc, argv);
            return;
        }
        ctx.io.writeLine(usage());
    }

private:
    void doSelftest(Ctx& ctx) {
        ctx.io.printf("Selftest do DAC8562 em %s, SPI a %lu Hz.\r\n", aoModeName(ctx.ao.mode()),
                      static_cast<unsigned long>(ctx.ao.spiHz()));
        char answer[32];
        for (uint16_t i = 0; i < kDacRampCount; ++i) {
            const uint16_t code = kDacRamp[i];
            bool wrote = true;
            for (uint8_t axis = 0; axis < board::kAxisCount; ++axis) {
                const Status st = ctx.ao.setRaw(axis, code);
                if (st.failed()) {
                    ctx.io.printf("Eixo %s: ERRO %s no code 0x%04X\r\n", board::kAxisName[axis], errName(st.err),
                                  static_cast<unsigned>(code));
                    wrote = false;
                }
            }
            if (!wrote) {
                printVihWarning(ctx);
                return;
            }
            ctx.io.printf("Passo %u/%u: code 0x%04X nos dois canais.\r\n", static_cast<unsigned>(i + 1u),
                          static_cast<unsigned>(kDacRampCount), static_cast<unsigned>(code));
            ctx.op.info("Terminais: eixo %s em %s | eixo %s em %s", board::kAxisName[0],
                        board::kAxisTerminals[0], board::kAxisName[1], board::kAxisTerminals[1]);
            ctx.op.step("Medir a saida dos dois eixos com o multimetro", "conector CN1, terminais acima",
                        "valor proporcional ao codigo escrito, sem salto");
            if (!ctx.op.askLine("Valores lidos (texto livre, ENTER segue)", answer,
                                static_cast<uint16_t>(sizeof(answer)))) {
                ctx.io.writeLine("Selftest abortado pelo operador.");
                ctx.ao.zeroAll();
                return;
            }
            ctx.io.printf("  registrado: %s\r\n", answer);
        }
        ctx.ao.zeroAll();
        ctx.io.writeLine("Selftest concluido, saidas zeradas.");
        printVihWarning(ctx);
    }

    void doSpi(Ctx& ctx, uint8_t argc, const char* const* argv) {
        uint32_t hz = 0;
        if (argc < 3 || !cmd::parseU32(argv[2], hz)) {
            ctx.io.writeLine(usage());
            return;
        }
        if (hz < board::kDacSpiMinHz || hz > board::kDacSpiMaxHz) {
            ctx.io.printf("ERRO: faixa aceita %lu a %lu Hz\r\n", static_cast<unsigned long>(board::kDacSpiMinHz),
                          static_cast<unsigned long>(board::kDacSpiMaxHz));
            return;
        }
        const Status st = ctx.ao.setSpiHz(hz);
        if (st.failed()) {
            ctx.io.printf("dac spi: ERRO %s\r\n", errName(st.err));
            return;
        }
        ctx.io.printf("Clock do DAC = %lu Hz (pedido %lu Hz)\r\n", static_cast<unsigned long>(ctx.ao.spiHz()),
                      static_cast<unsigned long>(hz));
    }
};

class CmdCal : public ICommand {
public:
    const char* name() const override { return "cal"; }
    const char* usage() const override { return "uso: cal <x|y> <v|i> | cal show | cal erase"; }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        if (argc < 2) {
            ctx.io.writeLine(usage());
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "show")) {
            doShow(ctx);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "erase")) {
            const Status st = ctx.cal.erase();
            reportStatus(ctx, "cal erase", st);
            return;
        }
        doRun(ctx, argc, argv);
    }

private:
    void doShow(Ctx& ctx) {
        ctx.io.writeLine("---- CALIBRACAO (valor = a * code + b) ----");
        for (uint8_t axis = 0; axis < board::kAxisCount; ++axis) {
            for (uint8_t m = 0; m < kCalModeCount; ++m) {
                const AoMode mode = static_cast<AoMode>(m);
                calmath::Coef c;
                const Status st = ctx.cal.coef(axis, mode, c);
                if (st.ok()) {
                    ctx.io.printf("%s %-8s: a=%.8f b=%.6f  valido=SIM\r\n", board::kAxisName[axis],
                                  aoModeName(mode), static_cast<double>(c.a), static_cast<double>(c.b));
                } else {
                    ctx.io.printf("%s %-8s: valido=NAO (%s)\r\n", board::kAxisName[axis], aoModeName(mode),
                                  errName(st.err));
                }
            }
        }
    }

    void doRun(Ctx& ctx, uint8_t argc, const char* const* argv) {
        uint8_t axis = 0;
        AoMode mode = AoMode::Voltage;
        if (argc < 3 || !cmd::parseAxis(argv[1], axis) || !parseAoMode(argv[2], mode)) {
            ctx.io.writeLine(usage());
            return;
        }
        const Status stMode = ctx.ao.setMode(mode);
        if (stMode.failed()) {
            ctx.io.printf("ERRO ao trocar o modo: %s\r\n", errName(stMode.err));
            return;
        }
        const uint16_t full = ctx.ao.fullScaleCode();
        const uint16_t codeLow = static_cast<uint16_t>((static_cast<uint32_t>(full) * 10u) / 100u);
        const uint16_t codeHigh = static_cast<uint16_t>((static_cast<uint32_t>(full) * 90u) / 100u);
        ctx.io.printf("Calibracao de 2 pontos, eixo %s em %s, terminais %s.\r\n", board::kAxisName[axis],
                      aoModeName(mode), board::kAxisTerminals[axis]);
        calmath::Point p1 = {0u, 0.0f};
        calmath::Point p2 = {0u, 0.0f};
        if (!measure(ctx, axis, mode, codeLow, p1)) {
            ctx.ao.zeroAll();
            return;
        }
        if (!measure(ctx, axis, mode, codeHigh, p2)) {
            ctx.ao.zeroAll();
            return;
        }
        const Status stSet = ctx.cal.setFromPoints(axis, mode, p1, p2);
        if (stSet.failed()) {
            ctx.io.printf("ERRO: pontos invalidos (%s). Nada gravado.\r\n", errName(stSet.err));
            ctx.ao.zeroAll();
            return;
        }
        const Status stSave = ctx.cal.save();
        calmath::Coef c;
        ctx.cal.coef(axis, mode, c);
        ctx.io.printf("Resultado: a=%.8f  b=%.6f  (%s por code)\r\n", static_cast<double>(c.a),
                      static_cast<double>(c.b), aoModeUnit(mode));
        reportStatus(ctx, "gravacao na NVS", stSave);
        ctx.ao.zeroAll();
    }

    bool measure(Ctx& ctx, uint8_t axis, AoMode mode, uint16_t code, calmath::Point& out) {
        out.code = code;
        out.value = 0.0f;
        const Status st = ctx.ao.setRaw(axis, code);
        if (st.failed()) {
            ctx.io.printf("ERRO ao escrever 0x%04X: %s\r\n", static_cast<unsigned>(code), errName(st.err));
            return false;
        }
        if (waitOrQuit(ctx, 300)) {
            ctx.io.writeLine("Calibracao abortada pelo operador.");
            return false;
        }
        char answer[32];
        ctx.io.printf("Code 0x%04X escrito no eixo %s.\r\n", static_cast<unsigned>(code), board::kAxisName[axis]);
        if (!ctx.op.askLine("Valor medido no multimetro", answer, static_cast<uint16_t>(sizeof(answer)))) {
            ctx.io.writeLine("Calibracao abortada pelo operador.");
            return false;
        }
        float value = 0.0f;
        if (!cmd::parseFloat(answer, value)) {
            ctx.io.printf("ERRO: '%s' nao e um numero. Calibracao cancelada.\r\n", answer);
            return false;
        }
        out.value = value;
        ctx.io.printf("  ponto: code 0x%04X = %.4f %s\r\n", static_cast<unsigned>(code),
                      static_cast<double>(value), aoModeUnit(mode));
        return true;
    }
};

}  // namespace

REGISTER_COMMAND(CmdAo);
REGISTER_COMMAND(CmdDac);
REGISTER_COMMAND(CmdCal);
