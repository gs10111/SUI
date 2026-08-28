// Tokenizador e conversores do console. Sem dependencia de hardware: testado no host.
#pragma once

#include <stdint.h>

namespace cmd {

constexpr uint8_t kMaxTokens = 8;
constexpr uint16_t kMaxLine = 96;

struct Line {
    char buf[kMaxLine];
    const char* argv[kMaxTokens];
    uint8_t argc;
    bool truncated;
};

bool parse(const char* input, Line& out);
bool equalsIgnoreCase(const char* a, const char* b);
bool parseU32(const char* s, uint32_t& out);
bool parseI32(const char* s, int32_t& out);
bool parseFloat(const char* s, float& out);
bool parseAxis(const char* s, uint8_t& axis);
bool parseOnOff(const char* s, bool& on);
bool parseHexBytes(const char* s, uint8_t* out, uint8_t cap, uint8_t& outLen);

}  // namespace cmd
