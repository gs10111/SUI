// Shell de bancada: maquina de estados, eco de linha e despacho. Nunca conhece um comando ou teste concreto.
#include "core/console.h"

#include "core/command.h"
#include "core/test_runner.h"

#ifndef FW_VERSION
#define FW_VERSION "0.0.0"
#endif
#ifndef BOARD_REV
#define BOARD_REV "?"
#endif

namespace {

constexpr uint32_t kIdleHintMs = 300000u;
constexpr uint16_t kLineCap = static_cast<uint16_t>(cmd::kMaxLine - 1);
constexpr char kPromptText[] = "jig> ";
constexpr char kBackspaceSeq[] = "\b \b";

bool isNumber(const char* text) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    return true;
}

void reportAbort(Ctx& ctx) {
    ctx.op.clearAbort();
    ctx.io.writeLine("ABORT: cancelado pelo operador");
}

}  // namespace

const char* consoleStateName(ConsoleState s) {
    switch (s) {
        case ConsoleState::Boot: return "BOOT";
        case ConsoleState::Idle: return "IDLE";
        case ConsoleState::Parsing: return "PARSING";
        case ConsoleState::TestRunning: return "TEST_RUNNING";
        case ConsoleState::AwaitVerdict: return "AWAIT_VERDICT";
        case ConsoleState::SuiteRunning: return "SUITE_RUNNING";
        case ConsoleState::Abort: return "ABORT";
    }
    return "?";
}

Console::Console(Ctx& ctx)
    : ctx_(ctx), state_(ConsoleState::Boot), line_(), len_(0), lastActivityMs_(0), overflow_(false) {}

void Console::begin() {
    state_ = ConsoleState::Boot;
    len_ = 0;
    line_[0] = '\0';
    overflow_ = false;
    printBanner();
    state_ = ConsoleState::Idle;
    lastActivityMs_ = ctx_.io.nowMs();
    prompt();
}

void Console::printBanner() {
    const char* const fw = (ctx_.fwVersion != nullptr) ? ctx_.fwVersion : FW_VERSION;
    const char* const rev = (ctx_.boardRev != nullptr) ? ctx_.boardRev : BOARD_REV;
    ctx_.io.writeLine("");
    ctx_.io.writeLine("DE-PURI-DI261924 - jig de teste de fabrica");
    ctx_.io.printf("firmware %s - placa REV %s\r\n", fw, rev);
    ctx_.io.printf("%u teste(s), %u comando(s) registrados\r\n", static_cast<unsigned>(TestRegistry::count()),
                   static_cast<unsigned>(CommandRegistry::count()));
    ctx_.io.writeLine("digite help para os comandos ou menu para os testes");
}

void Console::printHelp() {
    ctx_.io.writeLine("comandos:");
    const uint8_t total = CommandRegistry::count();
    for (uint8_t i = 0; i < total; ++i) {
        ICommand* const entry = CommandRegistry::at(i);
        if (entry == nullptr) {
            continue;
        }
        ctx_.io.printf("  %-12s %s\r\n", entry->name(), entry->usage());
    }
    if (total == 0) {
        ctx_.io.writeLine("  (nenhum comando registrado)");
    }
    ctx_.io.writeLine("  help | ?     lista os comandos");
    ctx_.io.writeLine("  menu         lista os testes");
    ctx_.io.writeLine("  <N>          executa o teste N do menu");
}

void Console::printMenu() {
    ctx_.io.writeLine("testes:");
    const uint8_t total = TestRegistry::count();
    for (uint8_t i = 0; i < total; ++i) {
        ITest* const entry = TestRegistry::at(i);
        if (entry == nullptr) {
            continue;
        }
        ctx_.io.printf("  %2u) %-10s %s\r\n", static_cast<unsigned>(i + 1), entry->id(), entry->name());
    }
    if (total == 0) {
        ctx_.io.writeLine("  (nenhum teste registrado)");
    }
}

void Console::poll() {
    if (ctx_.runner != nullptr && ctx_.runner->busy()) {
        if (state_ != ConsoleState::TestRunning) {
            state_ = ConsoleState::SuiteRunning;
        }
        return;
    }

    uint8_t raw = 0;
    while (ctx_.io.readByte(raw)) {
        lastActivityMs_ = ctx_.io.nowMs();
        const char c = static_cast<char>(raw);

        if (c == '\r' || c == '\n') {
            ctx_.io.writeLine("");
            if (overflow_) {
                overflow_ = false;
                len_ = 0;
                line_[0] = '\0';
                ctx_.io.writeLine("linha longa demais - descartada");
            } else {
                line_[len_] = '\0';
                handleLine(line_);
                len_ = 0;
                line_[0] = '\0';
            }
            state_ = ConsoleState::Idle;
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

        if (len_ >= kLineCap) {
            overflow_ = true;
            continue;
        }

        line_[len_] = c;
        ++len_;
        const char echo[2] = {c, '\0'};
        ctx_.io.write(echo);
    }

    if (state_ == ConsoleState::Idle) {
        const uint32_t now = ctx_.io.nowMs();
        if (static_cast<uint32_t>(now - lastActivityMs_) >= kIdleHintMs) {
            lastActivityMs_ = now;
            ctx_.io.writeLine("");
            ctx_.io.writeLine("jig ocioso - digite help ou selftest");
            prompt();
        }
    }

    ctx_.io.idle();
}

void Console::handleLine(const char* line) {
    state_ = ConsoleState::Parsing;

    cmd::Line parsed;
    if (!cmd::parse(line, parsed) || parsed.argc == 0) {
        state_ = ConsoleState::Idle;
        return;
    }
    if (parsed.truncated) {
        ctx_.io.writeLine("aviso: linha truncada");
    }

    const char* const head = parsed.argv[0];

    if (cmd::equalsIgnoreCase(head, "help") || cmd::equalsIgnoreCase(head, "?")) {
        printHelp();
        state_ = ConsoleState::Idle;
        return;
    }

    if (cmd::equalsIgnoreCase(head, "menu")) {
        printMenu();
        state_ = ConsoleState::Idle;
        return;
    }

    uint32_t picked = 0;
    if (parsed.argc == 1 && isNumber(head) && cmd::parseU32(head, picked)) {
        const uint8_t total = TestRegistry::count();
        if (picked == 0 || picked > static_cast<uint32_t>(total)) {
            ctx_.io.printf("teste inexistente: %s (menu lista 1..%u)\r\n", head, static_cast<unsigned>(total));
            state_ = ConsoleState::Idle;
            return;
        }
        ITest* const entry = TestRegistry::at(static_cast<uint8_t>(picked - 1));
        if (entry == nullptr || ctx_.runner == nullptr) {
            ctx_.io.writeLine("executor de testes indisponivel");
            state_ = ConsoleState::Idle;
            return;
        }
        state_ = ConsoleState::TestRunning;
        TestResult result;
        if (!ctx_.runner->runById(entry->id(), result)) {
            ctx_.io.printf("teste desconhecido: %s\r\n", entry->id());
        }
        state_ = ConsoleState::AwaitVerdict;
        if (ctx_.op.aborted()) {
            state_ = ConsoleState::Abort;
            reportAbort(ctx_);
        }
        state_ = ConsoleState::Idle;
        return;
    }

    ICommand* const entry = CommandRegistry::find(head);
    if (entry == nullptr) {
        ctx_.io.printf("comando desconhecido: %s (digite help)\r\n", head);
        state_ = ConsoleState::Idle;
        return;
    }

    state_ = ConsoleState::TestRunning;
    entry->execute(ctx_, parsed.argc, parsed.argv);
    state_ = ConsoleState::AwaitVerdict;
    if (ctx_.op.aborted()) {
        state_ = ConsoleState::Abort;
        reportAbort(ctx_);
    }
    state_ = ConsoleState::Idle;
}

void Console::prompt() {
    ctx_.io.write(kPromptText);
}
