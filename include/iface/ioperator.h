// Dialogo com o operador de bancada. Unica via de texto dentro de um teste.
#pragma once

#include <stdint.h>

#include "verdict.h"

class IOperator {
public:
    virtual ~IOperator() = default;
    virtual void info(const char* fmt, ...) __attribute__((format(printf, 2, 3))) = 0;
    virtual void step(const char* what, const char* where, const char* expected) = 0;
    virtual Verdict ask(const char* prompt) = 0;
    virtual bool askLine(const char* prompt, char* out, uint16_t cap) = 0;
    virtual bool askYes(const char* prompt) = 0;
    virtual bool aborted() const = 0;
    virtual void clearAbort() = 0;
    virtual bool skipped() const = 0;
    virtual void clearSkipped() = 0;
};
