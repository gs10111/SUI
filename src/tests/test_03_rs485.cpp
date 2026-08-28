// Item t3: RS-485 em camadas (eletrica e enlace). Folha 1/2 (SN65HVD75, TVS CDSOT23-SM712, J7, CN2).
// Datasheets: SN65HVD75 (TI SLLSEA6), CDSOT23-SM712 (Bourns).
#include <stdint.h>
#include <stdio.h>

#include "board_pins.h"
#include "core/cmd_parser.h"
#include "core/ctx.h"
#include "core/test_runner.h"
#include "iface/iserial_transport.h"
#include "proto/irs485_protocol.h"
#include "status.h"
#include "verdict.h"

namespace {

constexpr uint32_t kSessionMs = 30000;
constexpr uint32_t kMinFramesOk = 500;
constexpr uint32_t kReplyTimeoutMs = 40;
constexpr uint32_t kProgressMs = 5000;
constexpr const char* kBusPoint = "RS-485 A e B no CN2 (par trancado)";

char g_noteBuf[128];
char g_answerBuf[24];

TestResult abortResult() {
    return TestResult(Verdict::Abort, "abortado pelo operador");
}

void printLowDiffCauses(Ctx& ctx) {
    ctx.op.info("causas provaveis de diferencial baixo:");
    ctx.op.info("  1) TVS CDSOT23-SM712 em curto");
    ctx.op.info("  2) terminador J7 fechado indevidamente ou resistor de 120 ohm errado");
    ctx.op.info("  3) +5 V da sensora ausente em CN2A/CN2B");
    ctx.op.info("  4) SN65HVD75 sem alimentacao ou DE (IO%d) preso em nivel baixo",
                static_cast<int>(board::kRs485De));
}

uint32_t askTerminatorCount(Ctx& ctx, bool& aborted) {
    aborted = false;
    if (!ctx.op.askLine("quantos terminadores de 120 ohm no barramento? (1 = so J7, 2 = J7 + remoto)",
                        g_answerBuf, static_cast<uint16_t>(sizeof(g_answerBuf)))) {
        aborted = true;
        return 1;
    }
    uint32_t value = 0;
    if (!cmd::parseU32(g_answerBuf, value) || value == 0) {
        ctx.op.info("resposta nao reconhecida, assumindo 1 terminador (so J7)");
        return 1;
    }
    return value;
}

TestResult runElectrical(Ctx& ctx, bool twoTerminators) {
    const char* expectedHigh =
        twoTerminators ? "A-B entre +1,5 e +3,0 V (60 ohm efetivos, 2 terminadores; VOD min do datasheet = 1,5 V)"
                       : "A-B entre +2,0 e +3,3 V (120 ohm, so o terminador local J7; o SN65HVD75 e alimentado com 3,3 V)";
    const char* expectedLow =
        twoTerminators ? "A-B entre -1,5 e -3,0 V (60 ohm efetivos, 2 terminadores; VOD min do datasheet = 1,5 V)"
                       : "A-B entre -2,0 e -3,3 V (120 ohm, so o terminador local J7; o SN65HVD75 e alimentado com 3,3 V)";

    Status st = ctx.rs485.driveStatic(true, true);
    if (st.failed()) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "drive estatico recusado (%s): DE em IO%d", errName(st.err),
                 static_cast<int>(board::kRs485De));
        return TestResult(Verdict::Fail, g_noteBuf);
    }
    ctx.op.step("medir a tensao diferencial A-B com o driver em nivel alto", kBusPoint, expectedHigh);
    Verdict v = ctx.op.ask("resultado?");
    if (v == Verdict::Abort) {
        ctx.rs485.driveStatic(false, false);
        return abortResult();
    }
    if (v == Verdict::Fail) {
        ctx.rs485.driveStatic(false, false);
        printLowDiffCauses(ctx);
        return TestResult(Verdict::Fail, "A-B alto ruim: TVS CDSOT23-SM712 em curto, J7 ou +5V CN2A/CN2B");
    }

    st = ctx.rs485.driveStatic(true, false);
    if (st.failed()) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "drive estatico recusado (%s): DE em IO%d", errName(st.err),
                 static_cast<int>(board::kRs485De));
        return TestResult(Verdict::Fail, g_noteBuf);
    }
    ctx.op.step("medir a tensao diferencial A-B com o driver em nivel baixo", kBusPoint, expectedLow);
    v = ctx.op.ask("resultado?");
    if (v == Verdict::Abort) {
        ctx.rs485.driveStatic(false, false);
        return abortResult();
    }
    if (v == Verdict::Fail) {
        ctx.rs485.driveStatic(false, false);
        printLowDiffCauses(ctx);
        return TestResult(Verdict::Fail, "A-B baixo ruim: TVS CDSOT23-SM712 em curto, J7 ou +5V CN2A/CN2B");
    }

    ctx.rs485.driveStatic(false, false);
    ctx.op.info("barramento passivo: o SN65HVD75 tem failsafe interno e a placa nao tem bias externo");
    ctx.op.info("com o driver desligado o esperado e A-B proximo de 0 V; ausencia de bias NAO reprova");
    ctx.op.step("medir A-B com o driver desligado", kBusPoint, "A-B proximo de 0 V (sem bias externo)");
    if (!ctx.op.askYes("A-B ficou proximo de 0 V?")) {
        if (ctx.op.aborted()) {
            return abortResult();
        }
        ctx.op.info("ponto apenas registrado: sem bias externo nao ha nivel de repouso a exigir");
    }
    return TestResult(Verdict::Pass, "");
}

TestResult runLink(Ctx& ctx, const char* targetText) {
    const Status stBegin = ctx.rs485.begin(board::kRs485DefaultBaud, static_cast<uint8_t>(8), 'N',
                                           static_cast<uint8_t>(1));
    if (stBegin.failed()) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "UART do RS-485 nao inicializa (%s)", errName(stBegin.err));
        return TestResult(Verdict::Fail, g_noteBuf);
    }
    const Status stProto = ctx.proto.begin(ctx.rs485);
    if (stProto.failed()) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "protocolo %s nao inicializa (%s)", ctx.proto.name(),
                 errName(stProto.err));
        return TestResult(Verdict::Fail, g_noteBuf);
    }

    ctx.rs485.flushRx();
    ctx.rs485.resetStats();
    ctx.proto.resetCounters();
    ctx.op.info("sessao de %u s a %u bps 8N1, protocolo %s, alvo %s", static_cast<unsigned>(kSessionMs / 1000u),
                static_cast<unsigned>(ctx.rs485.baud()), ctx.proto.name(), targetText);

    Angle angle = {0.0f, 0.0f, false};
    const uint32_t startMs = ctx.io.nowMs();
    uint32_t nextProgressMs = kProgressMs;
    uint32_t attempts = 0;
    uint32_t localTimeouts = 0;
    uint32_t txErrors = 0;

    while ((ctx.io.nowMs() - startMs) < kSessionMs) {
        if (ctx.op.aborted()) {
            return abortResult();
        }
        ++attempts;
        const Status stReq = ctx.proto.request();
        if (stReq.failed()) {
            ++txErrors;
        } else {
            const uint32_t waitStart = ctx.io.nowMs();
            bool got = false;
            do {
                if (ctx.proto.poll(angle)) {
                    got = true;
                    break;
                }
                ctx.io.idle();
            } while ((ctx.io.nowMs() - waitStart) < kReplyTimeoutMs);
            if (!got) {
                ++localTimeouts;
                ctx.rs485.noteTimeout();
            }
        }
        const uint32_t elapsed = ctx.io.nowMs() - startMs;
        if (elapsed >= nextProgressMs) {
            ctx.op.info("  %u s: tentativas=%u quadros OK=%u", static_cast<unsigned>(elapsed / 1000u),
                        static_cast<unsigned>(attempts), static_cast<unsigned>(ctx.proto.framesOk()));
            nextProgressMs += kProgressMs;
        }
    }

    const SerialStats& counters = ctx.rs485.stats();
    uint32_t framesOk = ctx.proto.framesOk();
    if (counters.framesOk > framesOk) {
        framesOk = counters.framesOk;
    }
    uint32_t timeouts = localTimeouts;
    if (counters.timeouts > timeouts) {
        timeouts = counters.timeouts;
    }
    const uint32_t badFrames = ctx.proto.framesBad();

    ctx.op.info("--- resultado do enlace ---");
    ctx.op.info("tentativas=%u quadros OK=%u", static_cast<unsigned>(attempts),
                static_cast<unsigned>(framesOk));
    ctx.op.info("timeout=%u crc=%u enquadramento=%u tx=%u quadros invalidos=%u",
                static_cast<unsigned>(timeouts), static_cast<unsigned>(counters.crcErrors),
                static_cast<unsigned>(counters.framingErrors), static_cast<unsigned>(txErrors),
                static_cast<unsigned>(badFrames));
    ctx.op.info("bytes rx=%u tx=%u ultimo turnaround=%u us", static_cast<unsigned>(counters.bytesRx),
                static_cast<unsigned>(counters.bytesTx), static_cast<unsigned>(ctx.rs485.lastTurnaroundUs()));

    const uint32_t errors = timeouts + counters.crcErrors + counters.framingErrors + txErrors + badFrames;
    if (framesOk < kMinFramesOk) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "so %u quadros OK em %u s: DE IO%d, baud ou fiacao A/B",
                 static_cast<unsigned>(framesOk), static_cast<unsigned>(kSessionMs / 1000u),
                 static_cast<int>(board::kRs485De));
        return TestResult(Verdict::Fail, g_noteBuf);
    }
    if (errors != 0) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "erros no link: tmo=%u crc=%u enq=%u tx=%u",
                 static_cast<unsigned>(timeouts), static_cast<unsigned>(counters.crcErrors),
                 static_cast<unsigned>(counters.framingErrors),
                 static_cast<unsigned>(txErrors + badFrames));
        return TestResult(Verdict::Fail, g_noteBuf);
    }

    snprintf(g_noteBuf, sizeof(g_noteBuf), "%u quadros OK 0 erro, alvo: %s", static_cast<unsigned>(framesOk),
             targetText);
    return TestResult(Verdict::Pass, g_noteBuf);
}

class Test03Rs485 : public ITest {
public:
    const char* id() const override { return "t3"; }
    const char* name() const override { return "RS-485 (SN65HVD75)"; }
    uint8_t order() const override { return 3; }

    TestResult run(Ctx& ctx) override {
        ctx.op.info("--- camada 1: eletrica ---");
        bool aborted = false;
        const uint32_t terminators = askTerminatorCount(ctx, aborted);
        if (aborted) {
            return abortResult();
        }
        ctx.op.info("terminadores informados: %u", static_cast<unsigned>(terminators));

        const TestResult electrical = runElectrical(ctx, terminators >= 2);
        if (electrical.verdict != Verdict::Pass) {
            return electrical;
        }

        ctx.op.info("--- camada 2: enlace e aplicacao ---");
        if (!ctx.op.askLine("alvo do link: 1 = placa sensora PUSI-DI261930, 2 = adaptador USB-RS485 do PC",
                            g_answerBuf, static_cast<uint16_t>(sizeof(g_answerBuf)))) {
            return abortResult();
        }
        uint32_t choice = 1;
        if (!cmd::parseU32(g_answerBuf, choice)) {
            choice = 1;
        }
        const char* targetText = (choice == 2) ? "adaptador USB-RS485" : "placa sensora PUSI-DI261930";
        return runLink(ctx, targetText);
    }
};

}  // namespace

REGISTER_TEST(Test03Rs485);
