// Porta do executor de testes vista pelos comandos do console.
#pragma once

#include "verdict.h"

class ITestRunner {
public:
    virtual ~ITestRunner() = default;
    virtual bool runById(const char* id, TestResult& out) = 0;
    virtual void runSuite() = 0;
    virtual bool busy() const = 0;
};
