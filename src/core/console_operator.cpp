// Perguntas ao operador pelo console (folha 1/2, UART0): timeout de 180 s com lembrete a cada 30 s.
// Datasheet aplicavel: ESP32-WROOM-32D (UART0 115200 8N1); o kick do watchdog externo e feito por esp_timer.
#include "core/console_operator.h"

#include <stdarg.h>
#include <stdio.h>

namespace {

constexpr uint8_t kAsciiEtx = 0x03;
constexpr uint8_t kAsciiBackspace = 0x08;
constexpr uint8_t kAsciiEsc = 0x1B;
constexpr uint8_t kAsciiSpace = 0x20;
constexpr uint8_t kAsciiDelete = 0x7F;

}  // namespace

ConsoleOperator::ConsoleOperator(IConsoleIO& io) : io_(io), aborted_(false), skipped_(false) {}

void ConsoleOperator::info(const char* fmt, ...) {
    if (fmt == nullptr) {
        return;
    }
    char buffer[kOperatorFormatCap];
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = '\0';
    io_.writeLine(buffer);
}

void ConsoleOperator::step(const char* what, const char* where, const char* expected) {
    io_.printf("  ACAO   : %s\r\n", what != nullptr ? what : "-");
    io_.printf("  MEDIR  : %s\r\n", where != nullptr ? where : "-");
    io_.printf("  ESPERADO: %s\r\n", expected != nullptr ? expected : "-");
}

void ConsoleOperator::echoChar(uint8_t value) {
    const char text[2] = {static_cast<char>(value), '\0'};
    io_.write(text);
}

void ConsoleOperator::reportTimeout() {
    io_.writeLine("");
    io_.writeLine("timeout: sem resposta do operador em 180 s");
}

bool ConsoleOperator::waitByte(uint32_t startMs, uint32_t& lastRemindMs, uint8_t& out) {
    for (;;) {
        uint8_t value = 0;
        if (io_.readByte(value)) {
            out = value;
            return true;
        }
        const uint32_t now = io_.nowMs();
        if ((now - startMs) >= kOperatorTimeoutMs) {
            return false;
        }
        if ((now - lastRemindMs) >= kOperatorReminderMs) {
            lastRemindMs = now;
            io_.writeLine("");
            io_.writeLine("aguardando operador...");
        }
        io_.idle();
    }
}

Verdict ConsoleOperator::ask(const char* prompt) {
    io_.printf("%s [p=pass f=fail s=skip a=abort] ", prompt != nullptr ? prompt : "resultado?");
    const uint32_t start = io_.nowMs();
    uint32_t remind = start;
    uint8_t value = 0;
    while (waitByte(start, remind, value)) {
        switch (value) {
            case 'p':
            case 'P':
                io_.writeLine("p");
                return Verdict::Pass;
            case 'f':
            case 'F':
                io_.writeLine("f");
                return Verdict::Fail;
            case 's':
            case 'S':
                io_.writeLine("s");
                skipped_ = true;
                return Verdict::Skip;
            case 'a':
            case 'A':
                io_.writeLine("a");
                aborted_ = true;
                return Verdict::Abort;
            default:
                break;
        }
    }
    reportTimeout();
    io_.writeLine("item marcado como SKIP");
    skipped_ = true;
    return Verdict::Skip;
}

bool ConsoleOperator::askYes(const char* prompt) {
    io_.printf("%s [s/n] ", prompt != nullptr ? prompt : "confirma?");
    const uint32_t start = io_.nowMs();
    uint32_t remind = start;
    uint8_t value = 0;
    while (waitByte(start, remind, value)) {
        switch (value) {
            case 's':
            case 'S':
                io_.writeLine("s");
                return true;
            case 'n':
            case 'N':
                io_.writeLine("n");
                return false;
            default:
                break;
        }
    }
    reportTimeout();
    io_.writeLine("resposta assumida como nao");
    return false;
}

bool ConsoleOperator::askLine(const char* prompt, char* out, uint16_t cap) {
    if (out == nullptr || cap == 0) {
        return false;
    }
    out[0] = '\0';
    if (aborted_) {
        return false;
    }
    io_.printf("%s ", prompt != nullptr ? prompt : ">");
    const uint32_t start = io_.nowMs();
    uint32_t remind = start;
    uint16_t len = 0;
    uint8_t value = 0;
    while (waitByte(start, remind, value)) {
        if (value == '\r' || value == '\n') {
            io_.writeLine("");
            out[len] = '\0';
            return true;
        }
        if (value == kAsciiEtx || value == kAsciiEsc) {
            aborted_ = true;
            io_.writeLine("");
            io_.writeLine("entrada abortada pelo operador");
            out[0] = '\0';
            return false;
        }
        if (value == kAsciiBackspace || value == kAsciiDelete) {
            if (len > 0) {
                --len;
                out[len] = '\0';
                io_.write("\b \b");
            }
            continue;
        }
        if (value >= kAsciiSpace && value < kAsciiDelete) {
            if ((static_cast<uint32_t>(len) + 1u) < static_cast<uint32_t>(cap)) {
                out[len] = static_cast<char>(value);
                ++len;
                out[len] = '\0';
                echoChar(value);
            }
        }
    }
    reportTimeout();
    out[0] = '\0';
    return false;
}
