// Dialogo com o operador de bancada pelo console (folha 1/2, UART0): texto ASCII sem acentos.
// Datasheet aplicavel: ESP32-WROOM-32D (UART0 115200 8N1); nao acessa GPIO.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "console_io.h"
#include "iface/ioperator.h"
#include "verdict.h"

constexpr uint32_t kOperatorTimeoutMs = 180000;
constexpr uint32_t kOperatorReminderMs = 30000;
constexpr size_t kOperatorFormatCap = 256;

class ConsoleOperator : public IOperator {
public:
    explicit ConsoleOperator(IConsoleIO& io);

    void info(const char* fmt, ...) override __attribute__((format(printf, 2, 3)));
    void step(const char* what, const char* where, const char* expected) override;
    Verdict ask(const char* prompt) override;
    bool askLine(const char* prompt, char* out, uint16_t cap) override;
    bool askYes(const char* prompt) override;
    bool aborted() const override { return aborted_; }
    void clearAbort() override { aborted_ = false; }
    bool skipped() const override { return skipped_; }
    void clearSkipped() override { skipped_ = false; }

private:
    bool waitByte(uint32_t startMs, uint32_t& lastRemindMs, uint8_t& out);
    void reportTimeout();
    void echoChar(uint8_t value);

    IConsoleIO& io_;
    bool aborted_;
    bool skipped_;
};
