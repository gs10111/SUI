// Console de bancada da PUSI-DI261930: eco de linha em UART0 e diagnostico do Murata SCL3300.
// Registradores publicados conforme sensor_map.h; RS e ERR_FLAG do SCL3300 (Murata 1862 rev 4).
#include "core/console.h"

#include "core/spi_probe.h"
#include "drivers/scl3300_math.h"

#include <stddef.h>
#include <string.h>

#include "board_pins.h"
#include "sensor_map.h"
#include "status.h"
#include "tilt.h"

#ifndef FW_VERSION
#define FW_VERSION "0.0.0"
#endif
#ifndef BOARD_REV
#define BOARD_REV "?"
#endif

namespace {

constexpr char kPromptText[] = "sensor> ";
constexpr char kBackspaceSeq[] = "\b \b";
constexpr char kRule[] = "================================================================";
constexpr char kPinoutWarn[] =
    "ATENCAO: pinout da PUSI-DI261930 NAO confirmado - ver sensor/include/board_pins.h";

constexpr uint16_t kWhoAmIExpected = 0x00C1;
constexpr uint16_t kStatusTextBytes = 96;

constexpr uint16_t kErrFlag1Mask = kStsSclCrcError | kStsSclStartup | kStsSclNotResponding;
constexpr uint16_t kErrFlag2Mask = kStsSclSelfTestFail | kStsSaturated | kStsWdtReset;

struct StatusBit {
    uint16_t mask;
    const char* name;
};

constexpr StatusBit kStatusBits[] = {
    {kStsDataValid, "DATA_VALID"},
    {kStsSclCrcError, "SCL_CRC"},
    {kStsSclStartup, "SCL_STARTUP"},
    {kStsSclSelfTestFail, "SELFTEST_FAIL"},
    {kStsSclNotResponding, "SEM_RESPOSTA"},
    {kStsSaturated, "SATURADO"},
    {kStsWdtReset, "WDT_RESET"},
};

constexpr size_t kStatusBitCount = sizeof(kStatusBits) / sizeof(kStatusBits[0]);

constexpr const char* kRegNames[] = {
    "ANGLE_X", "ANGLE_Y", "ANGLE_Z", "STATUS", "TEMP_DECIC", "WHOAMI", "FW_VERSION", "UPTIME_S",
};

constexpr uint16_t kRegNameCount = static_cast<uint16_t>(sizeof(kRegNames) / sizeof(kRegNames[0]));

static_assert(kRegNameCount == sensormap::kRegCount, "sensormap mudou: atualize kRegNames");

void appendText(char* out, uint16_t cap, uint16_t& used, const char* text) {
    if (out == nullptr || cap == 0 || text == nullptr) {
        return;
    }
    while (*text != '\0' && static_cast<uint32_t>(used) + 1u < static_cast<uint32_t>(cap)) {
        out[used] = *text;
        ++used;
        ++text;
    }
    out[used] = '\0';
}

void statusText(uint16_t bits, char* out, uint16_t cap) {
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    uint16_t used = 0;
    for (size_t i = 0; i < kStatusBitCount; ++i) {
        if ((bits & kStatusBits[i].mask) == 0) {
            continue;
        }
        if (used > 0) {
            appendText(out, cap, used, " ");
        }
        appendText(out, cap, used, kStatusBits[i].name);
    }
    if (used == 0) {
        appendText(out, cap, used, "(nenhum)");
    }
}

const char* rsText(const Tilt& t) {
    if ((t.status & (kStsSclCrcError | kStsSclNotResponding)) != 0) {
        return "11 ERRO";
    }
    if ((t.status & kStsSclStartup) != 0) {
        return "00 STARTUP";
    }
    if (t.valid && (t.status & kStsDataValid) != 0) {
        return "01 NORMAL";
    }
    return "10 INDEFINIDO";
}

const char* yesNo(bool v) {
    return v ? "SIM" : "NAO";
}

void printDeci(ISensorIO& out, const char* label, int16_t value, const char* unit) {
    const bool neg = value < 0;
    const int32_t mag = neg ? -static_cast<int32_t>(value) : static_cast<int32_t>(value);
    out.printf("%-6s: %s%d.%d %s\r\n", label, neg ? "-" : "", static_cast<int>(mag / 10),
               static_cast<int>(mag % 10), unit);
}

void printPin(ISensorIO& out, const char* label, board::Pin p) {
    if (p == board::kNoPin) {
        out.printf("%-13s: n/d\r\n", label);
        return;
    }
    out.printf("%-13s: IO%d\r\n", label, static_cast<int>(p));
}

const char* protoName(const ISlaveProtocol* p) {
    if (p == nullptr) {
        return "(ausente)";
    }
    const char* const n = p->name();
    return (n != nullptr) ? n : "(sem nome)";
}

}  // namespace

SensorConsole::SensorConsole(SensorCtx& ctx)
    : ctx_(ctx), line_(), len_(0), overflow_(false), lastCr_(false) {}

void SensorConsole::begin() {
    len_ = 0;
    line_[0] = '\0';
    overflow_ = false;
    lastCr_ = false;
    printBanner();
    prompt();
}

void SensorConsole::printBanner() {
    const char* const fw = (ctx_.fwVersion != nullptr) ? ctx_.fwVersion : FW_VERSION;
    const char* const rev = (ctx_.boardRev != nullptr) ? ctx_.boardRev : BOARD_REV;
    ctx_.io.writeLine("");
    ctx_.io.writeLine("PUSI-DI261930 - placa sensora de inclinacao (SCL3300)");
    ctx_.io.printf("firmware %s - placa REV %s - console %lu bps\r\n", fw, rev,
                   static_cast<unsigned long>(board::kConsoleBaud));
    if (!board::kPinoutConfirmado) {
        ctx_.io.writeLine(kRule);
        ctx_.io.writeLine(kPinoutWarn);
        ctx_.io.writeLine("Confira cada pino no esquematico antes de confiar em qualquer medida.");
        ctx_.io.writeLine(kRule);
    }
    printPinout();
    ctx_.io.writeLine("digite help para os comandos");
}

void SensorConsole::printPinout() {
    ctx_.io.writeLine("---- PINOS COMPILADOS ----");
    printPin(ctx_.io, "SCL3300 CS", board::kSclCs);
    printPin(ctx_.io, "SCL3300 SCLK", board::kSclSclk);
    printPin(ctx_.io, "SCL3300 MISO", board::kSclMiso);
    printPin(ctx_.io, "SCL3300 MOSI", board::kSclMosi);
    printPin(ctx_.io, "RS-485 RX", board::kRs485Rx);
    printPin(ctx_.io, "RS-485 TX", board::kRs485Tx);
    printPin(ctx_.io, "RS-485 DE", board::kRs485De);
    printPin(ctx_.io, "WDI", board::kWdi);
    printPin(ctx_.io, "LED", board::kStatusLed);
}

void SensorConsole::printHelp() {
    ctx_.io.writeLine("comandos:");
    ctx_.io.writeLine("  help       lista os comandos");
    ctx_.io.writeLine("  angle      X, Y, Z em graus, temperatura e status");
    ctx_.io.writeLine("  raw        registradores publicados em hex");
    ctx_.io.writeLine("  status     RS, STATUS, ERR_FLAG1, ERR_FLAG2 e contadores");
    ctx_.io.writeLine("  whoami     WHOAMI do inclinometro (esperado 0xC1)");
    ctx_.io.writeLine("  selftest   autoteste do inclinometro");
    ctx_.io.writeLine("  reinit     refaz a inicializacao do SCL3300");
    ctx_.io.writeLine("  link       estatisticas do RS-485 e o baud");
    ctx_.io.writeLine("  proto      mostra ou troca o escravo: proto [jig|modbus]");
    ctx_.io.writeLine("  wdt        watchdog externo STWD100");
    ctx_.io.writeLine("  ver        firmware, BOARD_REV e pinout");
    ctx_.io.writeLine("  spiprobe   bring-up do SPI: spiprobe miso | all | pin <n>");
    ctx_.io.writeLine("  spiraw     envia um quadro de 32 bits: spiraw <hex>");
}

void SensorConsole::poll() {
    uint8_t raw = 0;
    while (ctx_.io.readByte(raw)) {
        const char c = static_cast<char>(raw);

        if (c == '\n' && lastCr_) {
            lastCr_ = false;
            continue;
        }
        lastCr_ = (c == '\r');

        if (c == '\r' || c == '\n') {
            ctx_.io.writeLine("");
            if (overflow_) {
                overflow_ = false;
                ctx_.io.writeLine("linha longa demais - descartada");
            } else {
                line_[len_] = '\0';
                handleLine(line_);
            }
            len_ = 0;
            line_[0] = '\0';
            prompt();
            continue;
        }

        if (c == '\b' || raw == 0x7F) {
            if (len_ > 0) {
                --len_;
                line_[len_] = '\0';
                ctx_.io.write(kBackspaceSeq);
            }
            continue;
        }

        if (raw < 0x20 || raw > 0x7E) {
            continue;
        }

        if (len_ + 1u >= kLineBytes) {
            overflow_ = true;
            continue;
        }

        line_[len_] = c;
        ++len_;
        const char echo[2] = {c, '\0'};
        ctx_.io.write(echo);
    }
}

void SensorConsole::handleLine(char* line) {
    char* head = line;
    while (*head == ' ' || *head == '\t') {
        ++head;
    }
    char* arg = head;
    while (*arg != '\0' && *arg != ' ' && *arg != '\t') {
        ++arg;
    }
    if (*arg != '\0') {
        *arg = '\0';
        ++arg;
        while (*arg == ' ' || *arg == '\t') {
            ++arg;
        }
    }

    if (*head == '\0') {
        return;
    }
    if (strcmp(head, "help") == 0 || strcmp(head, "?") == 0) {
        printHelp();
        return;
    }
    if (strcmp(head, "angle") == 0) {
        cmdAngle();
        return;
    }
    if (strcmp(head, "raw") == 0) {
        cmdRaw();
        return;
    }
    if (strcmp(head, "status") == 0) {
        cmdStatus();
        return;
    }
    if (strcmp(head, "whoami") == 0) {
        cmdWhoAmI();
        return;
    }
    if (strcmp(head, "selftest") == 0) {
        cmdSelfTest();
        return;
    }
    if (strcmp(head, "reinit") == 0) {
        cmdReinit();
        return;
    }
    if (strcmp(head, "link") == 0) {
        cmdLink();
        return;
    }
    if (strcmp(head, "proto") == 0) {
        cmdProto(arg);
        return;
    }
    if (strcmp(head, "wdt") == 0) {
        cmdWdt();
        return;
    }
    if (strcmp(head, "ver") == 0) {
        cmdVer();
        return;
    }
    if (strcmp(head, "spiprobe") == 0) {
        cmdProbe(arg);
        return;
    }
    if (strcmp(head, "spiraw") == 0) {
        cmdSpiRaw(arg);
        return;
    }
    ctx_.io.printf("comando desconhecido: %s (digite help)\r\n", head);
}

void SensorConsole::cmdProbe(const char* arg) {
    const spiprobe::Pins compiled = {board::kSclCs, board::kSclSclk, board::kSclMiso, board::kSclMosi};

    if (arg != nullptr && strncmp(arg, "pin", 3) == 0) {
        const char* number = arg + 3;
        while (*number == ' ') {
            ++number;
        }
        const long pin = strtol(number, nullptr, 10);
        if (pin < 0 || pin > 39) {
            ctx_.io.writeLine("uso: spiprobe pin <0..39>");
            return;
        }
        ctx_.io.printf("IO%ld alternando a 1 Hz por 10 s: meca com o multimetro NO PINO DO SCL3300\r\n", pin);
        ctx_.io.writeLine("se o pino do ESP32 oscila mas o do chip nao, a trilha ou a solda esta aberta");
        spiprobe::togglePin(static_cast<int8_t>(pin), 1000, 10000);
        ctx_.io.writeLine("fim");
        return;
    }

    spiprobe::ScanResult result;
    const bool full = (arg != nullptr) && (strcmp(arg, "all") == 0);
    if (full) {
        ctx_.io.writeLine("varredura completa: pode levar alguns segundos...");
        spiprobe::scanAll(result);
    } else {
        ctx_.io.writeLine("varrendo so o MISO, mantendo CS/SCLK/MOSI compilados");
        spiprobe::scanMiso(compiled, result);
    }

    ctx_.io.printf("tentativas: %lu\r\n", static_cast<unsigned long>(result.attempts));
    if (result.hitCount == 0) {
        ctx_.io.writeLine("nenhuma combinacao devolveu WHOAMI 0xC1 com CRC valido");
        ctx_.io.writeLine("com a pinagem ja conferida, isso aponta para o lado eletrico:");
        ctx_.io.writeLine("  1) VDD e DVIO do SCL3300 entre 3,0 e 3,6 V, com DVIO nunca acima de VDD");
        ctx_.io.writeLine("  2) os quatro capacitores de 100 nF (VDD, DVIO, A_EXTC, D_EXTC)");
        ctx_.io.writeLine("  3) solda dos pinos do chip, principalmente MISO e CS");
        ctx_.io.writeLine("use 'spiprobe pin <n>' e meca no pino do chip para provar a continuidade");
    } else {
        for (uint8_t i = 0; i < result.hitCount; ++i) {
            const spiprobe::Pins& p = result.hits[i];
            ctx_.io.printf("RESPONDEU: CS=IO%d SCLK=IO%d MISO=IO%d MOSI=IO%d\r\n", static_cast<int>(p.cs),
                           static_cast<int>(p.sclk), static_cast<int>(p.miso), static_cast<int>(p.mosi));
        }
        ctx_.io.writeLine("ajuste sensor/include/board_pins.h se divergir do compilado, e recompile");
    }
    ctx_.tilt.begin();
}

void SensorConsole::cmdSpiRaw(const char* arg) {
    if (arg == nullptr || *arg == '\0') {
        ctx_.io.writeLine("uso: spiraw <hex de 32 bits>   ex: spiraw 40000091 (le WHOAMI)");
        return;
    }
    const uint32_t word = static_cast<uint32_t>(strtoul(arg, nullptr, 16));
    const spiprobe::Pins pins = {board::kSclCs, board::kSclSclk, board::kSclMiso, board::kSclMosi};
    const uint32_t first = spiprobe::transfer(pins, word);
    const uint32_t second = spiprobe::transfer(pins, word);
    ctx_.io.printf("enviado 0x%08lX\r\n", static_cast<unsigned long>(word));
    ctx_.io.printf("resposta 1: 0x%08lX  (do comando anterior)\r\n", static_cast<unsigned long>(first));
    ctx_.io.printf("resposta 2: 0x%08lX  RS=%s dado=0x%04X crc=%s\r\n", static_cast<unsigned long>(second),
                   scl::rsName(scl::rsOf(second)), scl::frameData(second),
                   scl::frameCrcOk(second) ? "ok" : "RUIM");
    ctx_.tilt.begin();
}

void SensorConsole::cmdAngle() {
    Tilt t;
    const Status st = ctx_.tilt.read(t);
    if (st.failed()) {
        ctx_.io.printf("angle: ERRO %s\r\n", errName(st.err));
    }
    printDeci(ctx_.io, "X", t.xDeci, "gr");
    printDeci(ctx_.io, "Y", t.yDeci, "gr");
    printDeci(ctx_.io, "Z", t.zDeci, "gr");
    printDeci(ctx_.io, "Temp", t.tempDeciC, "C");
    char decoded[kStatusTextBytes];
    statusText(t.status, decoded, kStatusTextBytes);
    ctx_.io.printf("%-6s: 0x%04X  %s\r\n", "STATUS", static_cast<unsigned>(t.status), decoded);
    ctx_.io.printf("%-6s: %s\r\n", "Valido", yesNo(t.valid));
}

void SensorConsole::cmdRaw() {
    ctx_.io.writeLine("---- REGISTRADORES PUBLICADOS ----");
    if (ctx_.registers == nullptr || ctx_.registerCount == 0) {
        ctx_.io.writeLine("(sem tabela publicada)");
        return;
    }
    for (uint16_t i = 0; i < ctx_.registerCount; ++i) {
        const char* const nm = (i < kRegNameCount) ? kRegNames[i] : "REG";
        ctx_.io.printf("  %2u %-11s 0x%04X\r\n", static_cast<unsigned>(i), nm,
                       static_cast<unsigned>(ctx_.registers[i]));
    }
}

void SensorConsole::cmdStatus() {
    Tilt t;
    const Status st = ctx_.tilt.read(t);
    ctx_.io.writeLine("---- ESTADO DA SENSORA ----");
    ctx_.io.printf("Inclinometro : %s\r\n", ctx_.tilt.name());
    ctx_.io.printf("Leitura      : %s\r\n", st.ok() ? "OK" : errName(st.err));
    ctx_.io.printf("RS atual     : %s\r\n", rsText(t));

    char decoded[kStatusTextBytes];
    statusText(t.status, decoded, kStatusTextBytes);
    ctx_.io.printf("STATUS       : 0x%04X  %s\r\n", static_cast<unsigned>(t.status), decoded);

    statusText(static_cast<uint16_t>(t.status & kErrFlag1Mask), decoded, kStatusTextBytes);
    ctx_.io.printf("ERR_FLAG1    : 0x%04X  %s\r\n", static_cast<unsigned>(t.status & kErrFlag1Mask),
                   decoded);

    statusText(static_cast<uint16_t>(t.status & kErrFlag2Mask), decoded, kStatusTextBytes);
    ctx_.io.printf("ERR_FLAG2    : 0x%04X  %s\r\n", static_cast<unsigned>(t.status & kErrFlag2Mask),
                   decoded);

    ctx_.io.printf("Leituras     : %lu\r\n", static_cast<unsigned long>(ctx_.tilt.reads()));
    ctx_.io.printf("Erros CRC    : %lu\r\n", static_cast<unsigned long>(ctx_.tilt.crcErrors()));
    ctx_.io.printf("Erros quadro : %lu\r\n", static_cast<unsigned long>(ctx_.tilt.frameErrors()));
    ctx_.io.printf("Uptime       : %lu s\r\n", static_cast<unsigned long>(ctx_.io.nowMs() / 1000u));
    ctx_.io.writeLine("ERR_FLAG1 e ERR_FLAG2 sao a parte de comunicacao e a parte de medida do");
    ctx_.io.writeLine("STATUS publicado; o registrador bruto do SCL3300 fica dentro do driver.");
}

void SensorConsole::cmdWhoAmI() {
    const uint16_t id = ctx_.tilt.whoAmI();
    ctx_.io.printf("WHOAMI: 0x%04X (esperado 0x%02X) %s\r\n", static_cast<unsigned>(id),
                   static_cast<unsigned>(kWhoAmIExpected),
                   (id == kWhoAmIExpected) ? "OK" : "DIVERGENTE");
    if (id != kWhoAmIExpected) {
        ctx_.io.writeLine("Confira alimentacao, CS e a fiacao do SPI antes de trocar o componente.");
    }
}

void SensorConsole::cmdSelfTest() {
    const Status st = ctx_.tilt.selfTest();
    if (st.ok()) {
        ctx_.io.writeLine("selftest: OK");
        return;
    }
    ctx_.io.printf("selftest: FALHA (%s)\r\n", errName(st.err));
}

void SensorConsole::cmdReinit() {
    const Status st = ctx_.tilt.begin();
    if (st.failed()) {
        ctx_.io.printf("reinit: FALHA (%s)\r\n", errName(st.err));
        return;
    }
    ctx_.io.printf("reinit: OK - %s, WHOAMI 0x%04X\r\n", ctx_.tilt.name(),
                   static_cast<unsigned>(ctx_.tilt.whoAmI()));
}

void SensorConsole::cmdLink() {
    const SerialStats& s = ctx_.link.stats();
    ctx_.io.writeLine("---- RS-485 ----");
    ctx_.io.printf("Baud         : %lu bps (RX IO%d, TX IO%d, DE IO%d)\r\n",
                   static_cast<unsigned long>(ctx_.link.baud()), static_cast<int>(board::kRs485Rx),
                   static_cast<int>(board::kRs485Tx), static_cast<int>(board::kRs485De));
    ctx_.io.printf("Frames OK    : %lu\r\n", static_cast<unsigned long>(s.framesOk));
    ctx_.io.printf("Erros CRC    : %lu\r\n", static_cast<unsigned long>(s.crcErrors));
    ctx_.io.printf("Timeouts     : %lu\r\n", static_cast<unsigned long>(s.timeouts));
    ctx_.io.printf("Erros frame  : %lu\r\n", static_cast<unsigned long>(s.framingErrors));
    ctx_.io.printf("Bytes RX/TX  : %lu / %lu\r\n", static_cast<unsigned long>(s.bytesRx),
                   static_cast<unsigned long>(s.bytesTx));
    ctx_.io.printf("Tempo de char: %lu us  virada %lu us\r\n",
                   static_cast<unsigned long>(ctx_.link.charTimeUs()),
                   static_cast<unsigned long>(ctx_.link.lastTurnaroundUs()));
    showProtocol();
}

void SensorConsole::showProtocol() {
    if (ctx_.protocol == nullptr || *ctx_.protocol == nullptr) {
        ctx_.io.writeLine("Protocolo    : (nenhum ativo)");
        return;
    }
    const ISlaveProtocol* const active = *ctx_.protocol;
    ctx_.io.printf("Protocolo    : %s  req=%lu resp=%lu ruins=%lu\r\n", protoName(active),
                   static_cast<unsigned long>(active->requests()),
                   static_cast<unsigned long>(active->responses()),
                   static_cast<unsigned long>(active->badFrames()));
}

void SensorConsole::cmdProto(const char* arg) {
    if (ctx_.protocol == nullptr) {
        ctx_.io.writeLine("proto: nao ha protocolo trocavel neste firmware");
        return;
    }
    if (arg == nullptr || *arg == '\0') {
        showProtocol();
        ctx_.io.printf("Disponiveis  : jig=%s  modbus=%s\r\n", protoName(ctx_.jigProtocol),
                       protoName(ctx_.modbusProtocol));
        ctx_.io.writeLine("uso: proto [jig|modbus]");
        return;
    }

    ISlaveProtocol* chosen = nullptr;
    if (strcmp(arg, "jig") == 0) {
        chosen = ctx_.jigProtocol;
    } else if (strcmp(arg, "modbus") == 0) {
        chosen = ctx_.modbusProtocol;
    } else {
        ctx_.io.writeLine("uso: proto [jig|modbus]");
        return;
    }

    if (chosen == nullptr) {
        ctx_.io.printf("proto: '%s' nao esta compilado neste firmware\r\n", arg);
        return;
    }

    chosen->reset();
    *ctx_.protocol = chosen;
    ctx_.io.printf("Protocolo ativo agora: %s (recepcao zerada)\r\n", protoName(chosen));
    ctx_.io.printf("A supervisora precisa falar o mesmo protocolo a %lu bps.\r\n",
                   static_cast<unsigned long>(ctx_.link.baud()));
}

void SensorConsole::cmdWdt() {
    ctx_.io.writeLine("---- WATCHDOG EXTERNO STWD100 ----");
    ctx_.io.printf("WDI          : IO%d, pulso de %lu us\r\n", static_cast<int>(board::kWdi),
                   static_cast<unsigned long>(board::kWdiPulseUs));
    ctx_.io.printf("Periodo kick : %lu ms\r\n", static_cast<unsigned long>(ctx_.wdt.kickPeriodMs()));
    ctx_.io.printf("Kicks        : %lu\r\n", static_cast<unsigned long>(ctx_.wdt.kickCount()));
    ctx_.io.printf("tWD          : min %lu ms / tipico %lu ms\r\n",
                   static_cast<unsigned long>(ctx_.wdt.minTimeoutMs()),
                   static_cast<unsigned long>(ctx_.wdt.typTimeoutMs()));
    ctx_.io.printf("Chutando     : %s\r\n", yesNo(ctx_.wdt.kicking()));
    ctx_.io.writeLine("NAO existe desligar o watchdog por software: o pino EN do STWD100 tem");
    ctx_.io.writeLine("pull-down interno e habilita o chip quando flutuante ou em nivel baixo.");
    ctx_.io.writeLine("Parar o kick reseta a placa; para isolar o reset, so no hardware.");
}

void SensorConsole::cmdVer() {
    const char* const fw = (ctx_.fwVersion != nullptr) ? ctx_.fwVersion : FW_VERSION;
    const char* const rev = (ctx_.boardRev != nullptr) ? ctx_.boardRev : BOARD_REV;
    ctx_.io.printf("Firmware     : %s\r\n", fw);
    ctx_.io.printf("Placa        : PUSI-DI261930 REV %s\r\n", rev);
    ctx_.io.printf("Registradores: %u publicados\r\n", static_cast<unsigned>(ctx_.registerCount));
    if (board::kPinoutConfirmado) {
        ctx_.io.writeLine("Pinout       : CONFIRMADO");
    } else {
        ctx_.io.writeLine(kRule);
        ctx_.io.writeLine(kPinoutWarn);
        ctx_.io.writeLine(kRule);
    }
    printPinout();
}

void SensorConsole::prompt() {
    ctx_.io.write(kPromptText);
}
