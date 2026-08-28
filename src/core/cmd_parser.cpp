// Tokenizador do console. Sem alocacao dinamica, sem dependencia de Arduino.
#include "core/cmd_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace cmd {

namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool isHexSeparator(char c) {
    return c == ' ' || c == ',' || c == ':' || c == '-' || c == '_';
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

bool parse(const char* input, Line& out) {
    out.argc = 0;
    out.truncated = false;
    out.buf[0] = '\0';
    for (uint8_t i = 0; i < kMaxTokens; ++i) {
        out.argv[i] = nullptr;
    }
    if (input == nullptr) {
        return false;
    }

    uint16_t n = 0;
    while (input[n] != '\0' && n < kMaxLine - 1) {
        out.buf[n] = input[n];
        ++n;
    }
    out.buf[n] = '\0';
    if (input[n] != '\0') {
        out.truncated = true;
    }

    uint16_t i = 0;
    while (i < n) {
        while (i < n && isSpace(out.buf[i])) {
            out.buf[i] = '\0';
            ++i;
        }
        if (i >= n) {
            break;
        }
        if (out.argc >= kMaxTokens) {
            out.truncated = true;
            break;
        }
        out.argv[out.argc++] = &out.buf[i];
        while (i < n && !isSpace(out.buf[i])) {
            ++i;
        }
        if (i < n) {
            out.buf[i] = '\0';
            ++i;
        }
    }
    return out.argc > 0;
}

bool equalsIgnoreCase(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        const char ca = static_cast<char>(tolower(static_cast<unsigned char>(*a)));
        const char cb = static_cast<char>(tolower(static_cast<unsigned char>(*b)));
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

bool parseU32(const char* s, uint32_t& out) {
    if (s == nullptr || *s == '\0') {
        return false;
    }
    char* end = nullptr;
    const int base = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10;
    const unsigned long v = strtoul(s, &end, base);
    if (end == s || (end != nullptr && *end != '\0')) {
        return false;
    }
    if (v > 0xFFFFFFFFul) {
        return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}

bool parseI32(const char* s, int32_t& out) {
    if (s == nullptr || *s == '\0') {
        return false;
    }
    char* end = nullptr;
    const long v = strtol(s, &end, 10);
    if (end == s || (end != nullptr && *end != '\0')) {
        return false;
    }
    if (v > 2147483647L || v < -2147483647L - 1) {
        return false;
    }
    out = static_cast<int32_t>(v);
    return true;
}

bool parseFloat(const char* s, float& out) {
    if (s == nullptr || *s == '\0') {
        return false;
    }
    char normalized[32];
    size_t i = 0;
    while (i + 1 < sizeof(normalized) && s[i] != '\0') {
        normalized[i] = (s[i] == ',') ? '.' : s[i];
        ++i;
    }
    if (s[i] != '\0') {
        return false;
    }
    normalized[i] = '\0';
    char* end = nullptr;
    const double v = strtod(normalized, &end);
    if (end == normalized || (end != nullptr && *end != '\0')) {
        return false;
    }
    out = static_cast<float>(v);
    return true;
}

bool parseAxis(const char* s, uint8_t& axis) {
    if (s == nullptr || s[0] == '\0' || s[1] != '\0') {
        return false;
    }
    const char c = static_cast<char>(tolower(static_cast<unsigned char>(s[0])));
    if (c == 'x' || c == '0') {
        axis = 0;
        return true;
    }
    if (c == 'y' || c == '1') {
        axis = 1;
        return true;
    }
    return false;
}

bool parseOnOff(const char* s, bool& on) {
    if (equalsIgnoreCase(s, "on") || equalsIgnoreCase(s, "1") || equalsIgnoreCase(s, "true")) {
        on = true;
        return true;
    }
    if (equalsIgnoreCase(s, "off") || equalsIgnoreCase(s, "0") || equalsIgnoreCase(s, "false")) {
        on = false;
        return true;
    }
    return false;
}

bool parseHexBytes(const char* s, uint8_t* out, uint8_t cap, uint8_t& outLen) {
    outLen = 0;
    if (s == nullptr || out == nullptr) {
        return false;
    }
    int high = -1;
    for (const char* p = s; *p != '\0'; ++p) {
        if (isHexSeparator(*p)) {
            if (high >= 0) {
                return false;
            }
            continue;
        }
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && high < 0) {
            ++p;
            continue;
        }
        const int d = hexDigit(*p);
        if (d < 0) {
            return false;
        }
        if (high < 0) {
            high = d;
        } else {
            if (outLen >= cap) {
                return false;
            }
            out[outLen++] = static_cast<uint8_t>((high << 4) | d);
            high = -1;
        }
    }
    if (high >= 0) {
        return false;
    }
    return outLen > 0;
}

}  // namespace cmd
