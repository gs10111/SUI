// Maquina de estados do console: BOOT -> IDLE -> PARSING -> TEST_RUNNING -> AWAIT_VERDICT -> IDLE.
#pragma once

#include <stdint.h>

#include "core/cmd_parser.h"
#include "core/ctx.h"
#include "verdict.h"

enum class ConsoleState : uint8_t {
    Boot = 0,
    Idle,
    Parsing,
    TestRunning,
    AwaitVerdict,
    SuiteRunning,
    Abort,
};

const char* consoleStateName(ConsoleState s);

class Console {
public:
    explicit Console(Ctx& ctx);

    void begin();
    void poll();
    void printBanner();
    void printHelp();
    void printMenu();
    ConsoleState state() const { return state_; }

private:
    void handleLine(const char* line);
    void prompt();

    Ctx& ctx_;
    ConsoleState state_;
    char line_[cmd::kMaxLine];
    uint16_t len_;
    uint32_t lastActivityMs_;
    bool overflow_;
};
