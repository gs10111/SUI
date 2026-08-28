// Comando do link RS-485 half-duplex (folha 1/2): SN65HVD75D (TI SLLSEA9) com DE/RE em IO14.
// Acionamento estatico do barramento, eco, ping, escuta e contadores de erro.
#include "board_pins.h"
#include "core/cmd_parser.h"
#include "core/command.h"
#include "core/ctx.h"
#include "status.h"

namespace {

constexpr uint32_t kLoopTimeoutMs = 120000;
constexpr uint32_t kPingTimeoutMs = 200;
constexpr uint32_t kSniffPollMs = 50;
constexpr uint32_t kBaudMin = 1200;
constexpr uint32_t kBaudMax = 921600;
constexpr uint8_t kTxMax = 32;
constexpr uint16_t kRxMax = 64;

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

void serviceIdle(Ctx& ctx) {
    if (ctx.wdt.kicking()) {
        ctx.wdt.kickNow();
    }
    ctx.io.idle();
}

void dumpHex(Ctx& ctx, const uint8_t* data, uint16_t len) {
    for (uint16_t i = 0; i < len; ++i) {
        ctx.io.printf("%02X ", static_cast<unsigned>(data[i]));
    }
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

class CmdRs485 : public ICommand {
public:
    const char* name() const override { return "rs485"; }
    const char* usage() const override {
        return "uso: rs485 drive <0|1> | rs485 idle | rs485 echo <baud> | rs485 ping <hex> | rs485 sniff | "
               "rs485 stats [reset]";
    }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        if (argc < 2) {
            ctx.io.writeLine(usage());
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "drive")) {
            doDrive(ctx, argc, argv);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "idle")) {
            doIdle(ctx);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "echo")) {
            doEcho(ctx, argc, argv);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "ping")) {
            doPing(ctx, argc, argv);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "sniff")) {
            doSniff(ctx);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "stats")) {
            doStats(ctx, argc, argv);
            return;
        }
        ctx.io.writeLine(usage());
    }

private:
    void doDrive(Ctx& ctx, uint8_t argc, const char* const* argv) {
        bool level = false;
        if (argc < 3 || !cmd::parseOnOff(argv[2], level)) {
            ctx.io.writeLine(usage());
            return;
        }
        const Status st = ctx.rs485.driveStatic(true, level);
        if (st.failed()) {
            ctx.io.printf("rs485 drive: ERRO %s\r\n", errName(st.err));
            return;
        }
        ctx.io.printf("DE ligado (IO%d), barramento forcado em nivel %u.\r\n", static_cast<int>(board::kRs485De),
                      static_cast<unsigned>(level ? 1u : 0u));
        ctx.io.writeLine("Antes de julgar a medida, confira:");
        ctx.io.writeLine("  1. J7 (terminador de 120 ohm da placa) esta fechado ou aberto?");
        ctx.io.writeLine("  2. Quantos terminadores existem no cabo inteiro (placa + sensora)?");
        ctx.io.writeLine("  3. O +5 V da sensora chega em CN2A/CN2B?");
        ctx.io.writeLine("Esperado em A-B: 3,5 a 5,0 V com UM terminador; 2,0 a 3,0 V com DOIS.");
        ctx.io.writeLine("Nivel 1 da A-B positivo; nivel 0 da a mesma amplitude com o sinal invertido.");
    }

    void doIdle(Ctx& ctx) {
        const Status st = ctx.rs485.driveStatic(false, false);
        if (st.failed()) {
            ctx.io.printf("rs485 idle: ERRO %s\r\n", errName(st.err));
            return;
        }
        ctx.io.writeLine("DE desligado: transceptor apenas em recepcao.");
        ctx.io.writeLine("Com o failsafe interno do SN65HVD75 e sem bias externo, o esperado e A-B");
        ctx.io.writeLine("proximo de 0 V e a saida do receptor em nivel alto (linha ociosa).");
    }

    void doEcho(Ctx& ctx, uint8_t argc, const char* const* argv) {
        uint32_t baud = 0;
        if (argc < 3 || !cmd::parseU32(argv[2], baud)) {
            ctx.io.writeLine(usage());
            return;
        }
        if (baud < kBaudMin || baud > kBaudMax) {
            ctx.io.printf("ERRO: baud fora da faixa %lu a %lu\r\n", static_cast<unsigned long>(kBaudMin),
                          static_cast<unsigned long>(kBaudMax));
            return;
        }
        const uint32_t previous = ctx.rs485.baud();
        const Status st = ctx.rs485.begin(baud, 8, 'N', 1);
        if (st.failed()) {
            ctx.io.printf("rs485 echo: ERRO %s\r\n", errName(st.err));
            return;
        }
        ctx.io.printf("Eco puro a %lu bps 8N1. Envie 'q' pelo console para sair.\r\n",
                      static_cast<unsigned long>(baud));
        const uint32_t start = ctx.io.nowMs();
        for (;;) {
            ctx.proto.serviceEcho();
            if (quitPressed(ctx)) {
                ctx.io.writeLine("Eco encerrado pelo operador.");
                break;
            }
            if (ctx.io.nowMs() - start >= kLoopTimeoutMs) {
                ctx.io.writeLine("Eco encerrado por tempo limite.");
                break;
            }
            serviceIdle(ctx);
        }
        const Status back = ctx.rs485.begin(previous, 8, 'N', 1);
        if (back.failed()) {
            ctx.io.printf("AVISO: nao foi possivel voltar para %lu bps (%s)\r\n",
                          static_cast<unsigned long>(previous), errName(back.err));
            return;
        }
        ctx.io.printf("Baud restaurado para %lu bps.\r\n", static_cast<unsigned long>(ctx.rs485.baud()));
    }

    void doPing(Ctx& ctx, uint8_t argc, const char* const* argv) {
        if (argc < 3) {
            ctx.io.writeLine(usage());
            return;
        }
        char text[cmd::kMaxLine];
        joinArgs(text, static_cast<uint16_t>(sizeof(text)), argc, argv, 2);
        uint8_t tx[kTxMax];
        uint8_t txLen = 0;
        if (!cmd::parseHexBytes(text, tx, kTxMax, txLen)) {
            ctx.io.printf("ERRO: '%s' nao e uma sequencia hex valida (max %u bytes)\r\n", text,
                          static_cast<unsigned>(kTxMax));
            ctx.io.writeLine(usage());
            return;
        }
        ctx.rs485.flushRx();
        ctx.io.write("TX: ");
        dumpHex(ctx, tx, txLen);
        ctx.io.printf("(%u bytes)\r\n", static_cast<unsigned>(txLen));
        const Status st = ctx.rs485.write(tx, txLen);
        if (st.failed()) {
            ctx.io.printf("rs485 ping: ERRO %s\r\n", errName(st.err));
            return;
        }
        uint8_t rx[kRxMax];
        const uint16_t got = ctx.rs485.read(rx, kRxMax, kPingTimeoutMs);
        if (got == 0) {
            ctx.io.printf("RX: nada em %lu ms\r\n", static_cast<unsigned long>(kPingTimeoutMs));
        } else {
            ctx.io.write("RX: ");
            dumpHex(ctx, rx, got);
            ctx.io.printf("(%u bytes)\r\n", static_cast<unsigned>(got));
        }
        ctx.io.printf("Virada TX->RX: %lu us\r\n", static_cast<unsigned long>(ctx.rs485.lastTurnaroundUs()));
    }

    void doSniff(Ctx& ctx) {
        const Status st = ctx.rs485.driveStatic(false, false);
        if (st.failed()) {
            ctx.io.printf("rs485 sniff: ERRO %s\r\n", errName(st.err));
            return;
        }
        ctx.io.writeLine("Escuta apenas em recepcao. Envie 'q' pelo console para sair.");
        const uint32_t start = ctx.io.nowMs();
        uint8_t rx[kRxMax];
        for (;;) {
            const uint16_t got = ctx.rs485.read(rx, kRxMax, kSniffPollMs);
            if (got > 0) {
                ctx.io.printf("[%8lu ms] %3u: ", static_cast<unsigned long>(ctx.io.nowMs()),
                              static_cast<unsigned>(got));
                dumpHex(ctx, rx, got);
                ctx.io.writeLine("");
            }
            if (quitPressed(ctx)) {
                ctx.io.writeLine("Escuta encerrada pelo operador.");
                break;
            }
            if (ctx.io.nowMs() - start >= kLoopTimeoutMs) {
                ctx.io.writeLine("Escuta encerrada por tempo limite.");
                break;
            }
            serviceIdle(ctx);
        }
    }

    void doStats(Ctx& ctx, uint8_t argc, const char* const* argv) {
        if (argc >= 3 && cmd::equalsIgnoreCase(argv[2], "reset")) {
            ctx.rs485.resetStats();
            ctx.proto.resetCounters();
            ctx.io.writeLine("Contadores do RS-485 zerados.");
            return;
        }
        if (argc >= 3) {
            ctx.io.writeLine(usage());
            return;
        }
        const SerialStats& s = ctx.rs485.stats();
        ctx.io.writeLine("---- RS-485 ----");
        ctx.io.printf("Baud        : %lu bps 8N1 (RX IO%d, TX IO%d, DE IO%d)\r\n",
                      static_cast<unsigned long>(ctx.rs485.baud()), static_cast<int>(board::kRs485Rx),
                      static_cast<int>(board::kRs485Tx), static_cast<int>(board::kRs485De));
        ctx.io.printf("Frames OK   : %lu\r\n", static_cast<unsigned long>(s.framesOk));
        ctx.io.printf("Timeouts    : %lu\r\n", static_cast<unsigned long>(s.timeouts));
        ctx.io.printf("Erros CRC   : %lu\r\n", static_cast<unsigned long>(s.crcErrors));
        ctx.io.printf("Erros frame : %lu\r\n", static_cast<unsigned long>(s.framingErrors));
        ctx.io.printf("Bytes RX/TX : %lu / %lu\r\n", static_cast<unsigned long>(s.bytesRx),
                      static_cast<unsigned long>(s.bytesTx));
        ctx.io.printf("Protocolo   : %s  ok=%lu bad=%lu\r\n", ctx.proto.name(),
                      static_cast<unsigned long>(ctx.proto.framesOk()),
                      static_cast<unsigned long>(ctx.proto.framesBad()));
        ctx.io.writeLine("Zere com 'rs485 stats reset'.");
    }
};

}  // namespace

REGISTER_COMMAND(CmdRs485);
