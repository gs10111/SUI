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
    // O protocolo ativo no banner: sensora e supervisora falando idiomas diferentes aparece como
    // "falha de comunicacao" com o cabo perfeito, e nada na tela da UR diz qual e a causa. Uma
    // linha aqui custa nada e resolve o diagnostico na primeira energizacao.
    if (ctx_.protocol != nullptr && *ctx_.protocol != nullptr) {
        ctx_.io.printf("protocolo    : %s a %lu bps (troca com 'proto')\r\n",
                       protoName(*ctx_.protocol),
                       static_cast<unsigned long>(board::kRs485DefaultBaud));
    }
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
    ctx_.io.writeLine("  bypass     bancada: bypass [on|off] - tolera SO o capacitor D_EXTC");
    ctx_.io.writeLine("  link       estatisticas do RS-485 e o baud");
    ctx_.io.writeLine("  proto      mostra ou troca o escravo: proto [jig|modbus]");
    ctx_.io.writeLine("  wdt        watchdog externo STWD100");
    ctx_.io.writeLine("  ver        firmware, BOARD_REV e pinout");
    ctx_.io.writeLine("  spiprobe   bring-up do SPI: spiprobe miso | all | pin <n>");
    ctx_.io.writeLine("  spiraw     envia um quadro de 32 bits: spiraw <hex>");
    ctx_.io.writeLine("  trace      ultimos quadros trocados com o SCL3300");
    ctx_.io.writeLine("  spiloop    clock continuo para osciloscopio: spiloop [hex] [segundos]");
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
    if (strcmp(head, "bypass") == 0) {
        cmdBypass(arg);
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
    if (strcmp(head, "trace") == 0) {
        cmdTrace();
        return;
    }
    if (strcmp(head, "spiloop") == 0) {
        cmdSpiLoop(arg);
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

// STO e o Self-Test Output, valor COM SINAL (Tabela 21): em hex cru, 0xFFEE parece enorme e e
// -18. O limiar de comparacao muda com o modo de operacao (Tabela 23), entao imprimir o numero
// sem o limiar do modo ATIVO deixa o operador sem referencia para julgar o que esta vendo.
void SensorConsole::printSto(const InclinometerDiag& diag) {
    const int sto = static_cast<int>(static_cast<int16_t>(diag.sto));
    const int limit = static_cast<int>(scl::stoThreshold(diag.mode));
    const bool out = scl::stoOutOfRange(diag.sto, diag.mode);
    ctx_.io.printf("STO          : 0x%04X  (%d com sinal)  limiar modo %u: +-%d LSB  [%s]\r\n",
                   static_cast<unsigned>(diag.sto), sto, static_cast<unsigned>(diag.mode), limit,
                   out ? "FORA DA FAIXA" : "dentro");
}

void SensorConsole::cmdStatus() {
    Tilt t;
    const Status st = ctx_.tilt.read(t);
    InclinometerDiag diag;
    ctx_.tilt.diagnostics(diag);

    ctx_.io.writeLine("---- ESTADO DA SENSORA ----");
    if (diag.benchBypass) {
        ctx_.io.writeLine("!! BYPASS DE BANCADA LIGADO - leitura publicada SEM credito real !!");
    }
    ctx_.io.printf("Inclinometro : %s  (%s)\r\n", ctx_.tilt.name(), diag.ready ? "inicializado" : "NAO INICIALIZADO");
    ctx_.io.printf("Leitura      : %s\r\n", st.ok() ? "OK" : errName(st.err));

    ctx_.io.writeLine("-- registradores do SCL3300 (datasheet Rev.4) --");
    // RS_SCL, e nao "RS": o Return Status do quadro SPI do inclinometro nao tem relacao nenhuma
    // com o RS-485 do cabo. Os dois aparecem no mesmo console, e ler "RS erro" logo abaixo de
    // "RS-485" ja custou uma sessao de bancada procurando defeito no cabo que estava perfeito.
    ctx_.io.printf("RS_SCL       : %u  %s   (Return Status do SPI, NAO e o RS-485)\r\n",
                   static_cast<unsigned>(diag.returnStatus),
                   scl::rsName(static_cast<scl::Rs>(diag.returnStatus)));
    char flags[160];
    scl::describeStatus(diag.status, flags, sizeof(flags));
    ctx_.io.printf("STATUS       : 0x%04X  %s\r\n", static_cast<unsigned>(diag.status), flags);
    if (!diag.flagsRead) {
        ctx_.io.writeLine("ERR_FLAG1    : NAO LIDO neste ciclo");
        ctx_.io.writeLine("ERR_FLAG2    : NAO LIDO neste ciclo");
    } else {
        scl::describeErrFlag1(diag.errFlag1, flags, sizeof(flags));
        ctx_.io.printf("ERR_FLAG1    : 0x%04X  %s\r\n", static_cast<unsigned>(diag.errFlag1), flags);
        scl::describeErrFlag2(diag.errFlag2, flags, sizeof(flags));
        ctx_.io.printf("ERR_FLAG2    : 0x%04X  %s\r\n", static_cast<unsigned>(diag.errFlag2), flags);
        if ((diag.errFlag2 & (scl::kErr2AExtC | scl::kErr2DExtC | scl::kErr2Agnd)) != 0) {
            ctx_.io.writeLine("DIAGNOSTICO: erro de conexao externa do componente.");
            ctx_.io.writeLine("Conferir os capacitores de 100 nF X7R colados no chip (A_EXTC pino 2,");
            ctx_.io.writeLine("D_EXTC pino 10) e o terra analogico AVSS (pino 1). Sem o capacitor do");
            ctx_.io.writeLine("core analogico o front-end satura, e por isso SAT acende parado.");
            // Tabela 48 do datasheet: a faixa e ESTREITA. Nao basta o capacitor estar la e ter
            // continuidade - fora de 70..130 nF ou com ESR acima de 100 mohm o chip acusa do
            // mesmo jeito, e um 100 nF de dieletrico errado (Y5V) cai fora com facilidade.
            ctx_.io.writeLine("Faixa aceita (Tabela 48): 70 a 130 nF, ESR ate 100 mohm, o mais");
            ctx_.io.writeLine("perto possivel do pino. Recomendado GCM155R71C104KA55 0402 16V X7R.");
        }
    }
    // STO e o Self-Test Output do SCL3300, e e um valor COM SINAL: em hexadecimal cru, 0xFFEE
    // parece enorme e e -18. Impresso so em hex, o unico registrador que mede de verdade o
    // autoteste vira ruido visual.
    printSto(diag);
    if (diag.status == 0 && diag.errFlag1 == 0 && diag.errFlag2 == 0 && !diag.ready) {
        ctx_.io.writeLine("registradores zerados e driver nao inicializado: nenhum quadro valido ainda");
        ctx_.io.writeLine("veja docs/bringup_sensora.md - a essa altura o suspeito e alimentacao/solda");
    }

    ctx_.io.writeLine("-- estado publicado no RS-485 (bits de tilt.h) --");
    char decoded[kStatusTextBytes];
    statusText(t.status, decoded, kStatusTextBytes);
    ctx_.io.printf("status       : 0x%04X  %s\r\n", static_cast<unsigned>(t.status), decoded);

    ctx_.io.printf("Leituras     : %lu\r\n", static_cast<unsigned long>(ctx_.tilt.reads()));
    ctx_.io.printf("Erros CRC    : %lu\r\n", static_cast<unsigned long>(ctx_.tilt.crcErrors()));
    ctx_.io.printf("Erros quadro : %lu\r\n", static_cast<unsigned long>(ctx_.tilt.frameErrors()));
    ctx_.io.printf("Uptime       : %lu s\r\n", static_cast<unsigned long>(ctx_.io.nowMs() / 1000u));
}

void SensorConsole::cmdWhoAmI() {
    uint32_t first = 0;
    uint32_t second = 0;
    const Status st1 = ctx_.tilt.exchangeRaw(scl::kCmdReadWhoAmI, first);
    const Status st2 = ctx_.tilt.exchangeRaw(scl::kCmdReadWhoAmI, second);
    if (st1.failed() || st2.failed()) {
        ctx_.io.printf("WHOAMI: nao foi possivel enviar o quadro (%s)\r\n",
                       errName(st1.failed() ? st1.err : st2.err));
        return;
    }

    ctx_.io.printf("enviado    : 0x%08lX (duas vezes, pipeline off-frame)\r\n",
                   static_cast<unsigned long>(scl::kCmdReadWhoAmI));
    ctx_.io.printf("resposta 1 : 0x%08lX  RS=%s dado=0x%04X crc=%s\r\n", static_cast<unsigned long>(first),
                   scl::rsName(scl::rsOf(first)), static_cast<unsigned>(scl::frameData(first)),
                   scl::frameCrcOk(first) ? "ok" : "RUIM");
    ctx_.io.printf("resposta 2 : 0x%08lX  RS=%s dado=0x%04X crc=%s   <- esta e a valida\r\n",
                   static_cast<unsigned long>(second), scl::rsName(scl::rsOf(second)),
                   static_cast<unsigned>(scl::frameData(second)), scl::frameCrcOk(second) ? "ok" : "RUIM");

    const uint16_t id = scl::frameData(second);
    if (second == 0x00000000u) {
        ctx_.io.writeLine("MISO em zero constante: nada esta dirigindo a linha");
        return;
    }
    if (second == 0xFFFFFFFFu) {
        ctx_.io.writeLine("MISO em um constante: linha presa em alto ou sem retorno");
        return;
    }
    if (!scl::frameCrcOk(second)) {
        ctx_.io.writeLine("chegou quadro mas o CRC nao fecha: temporizacao (CS alto < 10 us) ou modo SPI");
        return;
    }
    ctx_.io.printf("WHOAMI = 0x%04X (esperado 0x%02X) %s\r\n", static_cast<unsigned>(id),
                   static_cast<unsigned>(kWhoAmIExpected), (id == kWhoAmIExpected) ? "OK" : "DIVERGENTE");
}

// BYPASS DE BANCADA. Existe para exercitar a cadeia limite -> rele -> LED -> saida analogica
// antes de o capacitor do pino D_EXTC (C8) ser consertado: sem ele a UR recusa toda leitura e
// os quatro reles ficam em alarme por A5, entao nenhum ajuste de limite pode ser observado.
//
// Runtime e volatil de proposito. Nao existe binario compilado com ele ligado, e cada
// energizacao volta a recusar - um bypass que sobrevive ao reboot vira bypass esquecido.
void SensorConsole::cmdBypass(const char* arg) {
    if (arg != nullptr && strcmp(arg, "on") == 0) {
        ctx_.tilt.setBenchBypass(true);
    } else if (arg != nullptr && strcmp(arg, "off") == 0) {
        ctx_.tilt.setBenchBypass(false);
    } else if (arg != nullptr && *arg != '\0') {
        ctx_.io.writeLine("uso: bypass [on|off]");
        return;
    }

    InclinometerDiag diag;
    ctx_.tilt.diagnostics(diag);
    if (!diag.benchBypass) {
        ctx_.io.writeLine("bypass de bancada: DESLIGADO (leitura estrita)");
        return;
    }
    ctx_.io.writeLine("!! BYPASS DE BANCADA LIGADO - NAO E CONDICAO DE OPERACAO !!");
    ctx_.io.writeLine("Tolerados SO: PIN_CONTINUITY no STATUS e D_EXT_C no ERR_FLAG2,");
    ctx_.io.writeLine("que sao os dois bits do capacitor do pino 10 (D_EXTC, C8).");
    ctx_.io.writeLine("A_EXT_C, SAT, MEM, CLK e todo o resto CONTINUAM invalidando: sem o");
    ctx_.io.writeLine("capacitor do core analogico o angulo sai errado, e numero errado");
    ctx_.io.writeLine("comandando rele e o defeito que este produto existe para evitar.");
    ctx_.io.writeLine("Some na proxima energizacao. Conserte o C8.");
}

void SensorConsole::cmdSelfTest() {
    const Status st = ctx_.tilt.selfTest();
    if (st.ok()) {
        ctx_.io.writeLine("selftest: OK");
        return;
    }
    ctx_.io.printf("selftest: FALHA (%s)\r\n", errName(st.err));

    // Um comando que reprova tem de dizer POR QUE na mesma tela. O selftest ja LEU os quatro
    // registradores que explicam a reprovacao; exigir um 'status' depois e transformar um passo
    // de bancada em dois, e foi assim que ERR_FLAG1 e ERR_FLAG2 ficaram invisiveis por uma
    // sessao inteira.
    InclinometerDiag diag;
    ctx_.tilt.diagnostics(diag);
    char flags[160];
    ctx_.io.writeLine("-- por que reprovou (registradores lidos pelo proprio selftest) --");
    scl::describeStatus(diag.status, flags, sizeof(flags));
    ctx_.io.printf("STATUS       : 0x%04X  %s\r\n", static_cast<unsigned>(diag.status), flags);
    scl::describeErrFlag1(diag.errFlag1, flags, sizeof(flags));
    ctx_.io.printf("ERR_FLAG1    : 0x%04X  %s\r\n", static_cast<unsigned>(diag.errFlag1), flags);
    scl::describeErrFlag2(diag.errFlag2, flags, sizeof(flags));
    ctx_.io.printf("ERR_FLAG2    : 0x%04X  %s\r\n", static_cast<unsigned>(diag.errFlag2), flags);
    printSto(diag);
    ctx_.io.printf("RS_SCL       : %u  %s\r\n", static_cast<unsigned>(diag.returnStatus),
                   scl::rsName(static_cast<scl::Rs>(diag.returnStatus)));
    ctx_.io.writeLine("criterio atual: reprova se STATUS tiver bit fora de PWR|MODE_CHANGE,");
    ctx_.io.writeLine("se ERR_FLAG1 for diferente de zero, ou se ERR_FLAG2 tiver bit fora de");
    ctx_.io.writeLine("DPWR|MODE_CHANGE (Tabela 33: DPWR alto apos start-up e normal).");
    ctx_.io.writeLine("STO e reportado e NAO entra no veredito: o datasheet pede contador de");
    ctx_.io.writeLine("eventos consecutivos, e uma amostra isolada fora da faixa nao e falha.");
}

void SensorConsole::cmdSpiLoop(const char* arg) {
    uint32_t word = scl::kCmdReadWhoAmI;
    uint32_t seconds = 10;
    if (arg != nullptr && *arg != '\0') {
        char* end = nullptr;
        const unsigned long parsed = strtoul(arg, &end, 16);
        if (end != arg) {
            word = static_cast<uint32_t>(parsed);
        }
        while (end != nullptr && *end == ' ') {
            ++end;
        }
        if (end != nullptr && *end != '\0') {
            const unsigned long secs = strtoul(end, nullptr, 10);
            if (secs > 0 && secs <= 120) {
                seconds = static_cast<uint32_t>(secs);
            }
        }
    }
    ctx_.io.printf("repetindo 0x%08lX por %lu s no caminho real do driver (2 MHz, modo 0)\r\n",
                   static_cast<unsigned long>(word), static_cast<unsigned long>(seconds));
    ctx_.io.writeLine("ponta no SCLK para medir a frequencia, no CS para ver o enquadramento,");
    ctx_.io.writeLine("e no MISO para ver se o SCL3300 esta dirigindo alguma coisa");

    const uint32_t startMs = ctx_.io.nowMs();
    uint32_t frames = 0;
    uint32_t nonZero = 0;
    uint32_t crcOk = 0;
    uint32_t last = 0;
    while ((ctx_.io.nowMs() - startMs) < (seconds * 1000u)) {
        uint32_t response = 0;
        if (ctx_.tilt.exchangeRaw(word, response).failed()) {
            break;
        }
        ++frames;
        last = response;
        if (response != 0x00000000u && response != 0xFFFFFFFFu) {
            ++nonZero;
        }
        if (scl::frameCrcOk(response)) {
            ++crcOk;
        }
    }
    ctx_.io.printf("quadros: %lu   respostas nao triviais: %lu   com CRC valido: %lu\r\n",
                   static_cast<unsigned long>(frames), static_cast<unsigned long>(nonZero),
                   static_cast<unsigned long>(crcOk));
    ctx_.io.printf("ultima resposta: 0x%08lX  RS=%s dado=0x%04X\r\n", static_cast<unsigned long>(last),
                   scl::rsName(scl::rsOf(last)), static_cast<unsigned>(scl::frameData(last)));
    if (nonZero == 0) {
        ctx_.io.writeLine("MISO nunca saiu de nivel constante: o chip nao esta respondendo");
    }
}

void SensorConsole::cmdTrace() {
    const uint8_t total = ctx_.tilt.traceCount();
    if (total == 0) {
        ctx_.io.writeLine("nenhum quadro registrado: rode 'reinit'");
        return;
    }
    ctx_.io.writeLine("-- quadros trocados com o SCL3300 (lembre do pipeline: a resposta e do comando ANTERIOR) --");
    // A coluna "op env>eco" e o diagnostico mais barato deste console. O SCL3300 ecoa na resposta
    // o opcode do comando que RECEBEU: os dois numeros iguais significam que o comando chegou;
    // diferentes, que nao chegou. Sem ela e preciso decodificar dois hexadecimais de 32 bits a
    // mao para descobrir isso, e ninguem faz no meio de um bring-up.
    ctx_.io.writeLine("  n  enviado     recebido    op env>eco  RS_SCL     dado    CRC");
    uint8_t opDivergente = 0;
    for (uint8_t i = 0; i < total; ++i) {
        FrameTrace f;
        if (!ctx_.tilt.traceAt(i, f)) {
            break;
        }
        const bool crcOk = scl::frameCrcOk(f.response);
        const uint8_t opTx = scl::frameOpcode(f.command);
        const uint8_t opRx = scl::frameOpcode(f.response);
        if (opTx != opRx) {
            ++opDivergente;
        }
        ctx_.io.printf(" %2u  0x%08lX  0x%08lX  %02X>%02X %-5s %-9s 0x%04X  %s\r\n",
                       static_cast<unsigned>(i), static_cast<unsigned long>(f.command),
                       static_cast<unsigned long>(f.response), static_cast<unsigned>(opTx),
                       static_cast<unsigned>(opRx), (opTx == opRx) ? "" : "DIF",
                       scl::rsName(scl::rsOf(f.response)),
                       static_cast<unsigned>(scl::frameData(f.response)),
                       crcOk ? "ok" : "RUIM");
    }
    if (opDivergente == total && total > 1u) {
        ctx_.io.writeLine("");
        ctx_.io.writeLine("!! TODOS os quadros ecoam opcode diferente do enviado.");
        ctx_.io.writeLine("   O chip responde, com CRC valido, ao comando que NAO foi o enviado:");
        ctx_.io.printf("   o MOSI (IO%d) nao esta chegando ao pino do U2.\r\n",
                       static_cast<int>(board::kSclMosi));
        ctx_.io.writeLine("   Meca com 'spiprobe pin 23' NO PINO DO CHIP, nao no ESP32.");
        ctx_.io.writeLine("   Ate isso fechar, nenhum bit de STATUS ou ERR_FLAG vale nada.");
    }
    ctx_.io.writeLine("RS_SCL = Return Status do quadro SPI do SCL3300. NAO e o RS-485 do cabo.");
    ctx_.io.writeLine("resposta 0x00000000 = barramento mudo; 0xFFFFFFFF = linha presa em alto");
    // Resposta identica em quadros com comandos DIFERENTES nao e um chip doente: e um chip que
    // nao esta recebendo comando. O opcode que ele ecoa e o que ele recebeu, entao opcode 0 em
    // toda linha aponta o MOSI, e enquanto isso durar nenhum bit de STATUS ou ERR_FLAG vale.
    ctx_.io.writeLine("resposta IGUAL para comandos diferentes = MOSI nao chega ao chip:");
    ctx_.io.writeLine("  compare o opcode enviado (bits 31:26) com o ecoado na resposta.");
}

void SensorConsole::cmdReinit() {
    const Status st = ctx_.tilt.begin();
    if (st.failed()) {
        ctx_.io.printf("reinit: FALHA (%s)\r\n", errName(st.err));
        cmdTrace();
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
