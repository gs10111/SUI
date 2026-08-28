// Traducao de Verdict para texto e para o caractere usado na linha CSV.
#include "verdict.h"

const char* verdictName(Verdict v) {
    switch (v) {
        case Verdict::NotRun: return "NOT_RUN";
        case Verdict::Pass: return "PASS";
        case Verdict::Fail: return "FAIL";
        case Verdict::Skip: return "SKIP";
        case Verdict::Abort: return "ABORT";
    }
    return "?";
}

char verdictChar(Verdict v) {
    switch (v) {
        case Verdict::NotRun: return '-';
        case Verdict::Pass: return 'P';
        case Verdict::Fail: return 'F';
        case Verdict::Skip: return 'S';
        case Verdict::Abort: return 'A';
    }
    return '?';
}
