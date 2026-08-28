// Veredito de item de teste. Tipo puro, compilavel no host.
#pragma once

#include <stdint.h>

enum class Verdict : uint8_t {
    NotRun = 0,
    Pass,
    Fail,
    Skip,
    Abort,
};

struct TestResult {
    Verdict verdict;
    const char* note;

    constexpr TestResult() : verdict(Verdict::NotRun), note("") {}
    constexpr TestResult(Verdict v, const char* n) : verdict(v), note(n ? n : "") {}
};

const char* verdictName(Verdict v);
char verdictChar(Verdict v);
