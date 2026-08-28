// Registro estatico de ITest e execucao. Acrescentar teste = acrescentar arquivo + REGISTER_TEST.
#pragma once

#include <stdint.h>

#include "core/itest_runner.h"
#include "verdict.h"

struct Ctx;

class ITest {
public:
    virtual ~ITest() = default;
    virtual const char* id() const = 0;
    virtual const char* name() const = 0;
    virtual uint8_t order() const = 0;
    virtual bool abortsSuiteOnFail() const { return false; }
    virtual TestResult run(Ctx& ctx) = 0;
};

class TestRegistry {
public:
    static constexpr uint8_t kMax = 12;
    static bool add(ITest* test);
    static uint8_t count();
    static ITest* at(uint8_t index);
    static ITest* find(const char* id);
};

#define REGISTER_TEST(TypeName)                                    \
    namespace {                                                    \
    TypeName g_##TypeName##_instance;                              \
    const bool g_##TypeName##_registered =                         \
        TestRegistry::add(&g_##TypeName##_instance);               \
    }                                                              \
    static_assert(true, "")

class TestRunner : public ITestRunner {
public:
    explicit TestRunner(Ctx& ctx);

    TestResult runOne(ITest& test);
    bool runById(const char* id, TestResult& out) override;
    bool runOneById(const char* id, TestResult& out);
    void runSuite() override;
    bool busy() const override { return busy_; }
    bool suiteAborted() const { return suiteAborted_; }

private:
    Ctx& ctx_;
    bool suiteAborted_;
    bool busy_;
};
