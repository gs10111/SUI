// Executavel de host: monta o Ctx com dispositivos simulados e alimenta o console com um roteiro.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build_config.h"
#include "core/console.h"
#include "core/ctx.h"
#include "core/test_runner.h"
#include "proto/echo_protocol.h"
#include "sim/sim_devices.h"

namespace {

constexpr const char* kDefaultScript =
    "status\n"
    "serial SN-PROTO-0001\n"
    "selftest\n"
    "report csv\n";

}  // namespace

int main(int argc, char** argv) {
    const char* script = (argc > 1) ? argv[1] : kDefaultScript;
    const uint32_t timeScale = (argc > 2) ? static_cast<uint32_t>(atoi(argv[2])) : 200u;

    sim::SimConsoleIO io(script, timeScale);
    sim::SimOperator op(io, (argc > 3) ? argv[3] : "", "1");
    sim::SimKvStore kv;
    CalibrationStore cal(kv);
    sim::SimAnalogOutput ao(cal);
    sim::SimRelayBank relays;
    sim::SimSerialTransport rs485;
    EchoProtocol proto;
    sim::SimDisplay display;
    sim::SimButtons buttons;
    sim::SimWatchdog wdt;
    sim::SimSafeState safe(relays, ao, display);
    Report report;
    BootInfo boot;

    memset(&boot, 0, sizeof(boot));
    boot.resetReason = 0;
    boot.resetReasonName = "POWERON (simulado)";
    boot.strappingCount = board::kStrappingCount;
    for (uint8_t i = 0; i < board::kStrappingCount && i < 8; ++i) {
        boot.strappingLevel[i] = 1;
    }
    boot.flashSizeBytes = 4u * 1024u * 1024u;
    boot.chipIdLow = 0x0BADC0DEu;
    boot.chipIdHigh = 0x0000C0FFu;
    snprintf(boot.macText, sizeof(boot.macText), "24:6F:28:00:00:01");

    Ctx ctx{io,  op,  ao,   relays, rs485, proto, display,    buttons,
            wdt, cal, kv,   report, safe,  boot,  nullptr,    FW_VERSION,
            BOARD_REV};

    report.setMeta(FW_VERSION, BOARD_REV);
    report.setDate("2026-08-28 (simulado)");

    wdt.begin();
    relays.begin();
    ao.begin();
    display.begin();
    buttons.begin();
    rs485.begin(board::kRs485DefaultBaud, 8, 'N', 1);
    proto.begin(rs485);
    cal.load();

    TestRunner runner(ctx);
    ctx.runner = &runner;
    safe.enterSafeState();

    Console console(ctx);
    console.begin();

    while (!io.scriptExhausted()) {
        console.poll();
        buttons.poll();
    }
    for (uint16_t i = 0; i < 64; ++i) {
        console.poll();
        buttons.poll();
    }

    printf("\n[sim] fim do roteiro. escala de tempo: %ux\n", static_cast<unsigned>(timeScale));
    return 0;
}
