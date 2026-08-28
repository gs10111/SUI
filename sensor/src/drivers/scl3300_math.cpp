// PUSI-DI261930: conversoes e conferencia das palavras de comando do SCL3300 (Murata Rev.4, doc 4921).
// Angulo: (int16)raw * 90 / 16384 no ramo COM SINAL (-180..+180). Temperatura: -273 + raw/18.9 (nao -273,15).
#include "drivers/scl3300_math.h"

#include <stdio.h>

namespace scl {
namespace {

constexpr int32_t kAngleNumerator = 900;
constexpr int32_t kAngleDenominator = 16384;
constexpr int32_t kAngleHalf = kAngleDenominator / 2;

constexpr int32_t kTempNumerator = 100;
constexpr int32_t kTempDenominator = 189;
constexpr int32_t kTempHalf = kTempDenominator / 2;
constexpr int32_t kTempOffsetDeci = 2730;

constexpr uint32_t kCrcBodyMask = 0xFFFFFF00u;

static_assert(withCrc(kCmdSwReset & kCrcBodyMask) == kCmdSwReset, "crc8 falhou em kCmdSwReset");
static_assert(withCrc(kCmdPowerDown & kCrcBodyMask) == kCmdPowerDown, "crc8 falhou em kCmdPowerDown");
static_assert(withCrc(kCmdMode1 & kCrcBodyMask) == kCmdMode1, "crc8 falhou em kCmdMode1");
static_assert(withCrc(kCmdMode2 & kCrcBodyMask) == kCmdMode2, "crc8 falhou em kCmdMode2");
static_assert(withCrc(kCmdMode3 & kCrcBodyMask) == kCmdMode3, "crc8 falhou em kCmdMode3");
static_assert(withCrc(kCmdMode4 & kCrcBodyMask) == kCmdMode4, "crc8 falhou em kCmdMode4");
static_assert(withCrc(kCmdEnableAngle & kCrcBodyMask) == kCmdEnableAngle, "crc8 falhou em kCmdEnableAngle");
static_assert(withCrc(kCmdReadAngX & kCrcBodyMask) == kCmdReadAngX, "crc8 falhou em kCmdReadAngX");
static_assert(withCrc(kCmdReadAngY & kCrcBodyMask) == kCmdReadAngY, "crc8 falhou em kCmdReadAngY");
static_assert(withCrc(kCmdReadAngZ & kCrcBodyMask) == kCmdReadAngZ, "crc8 falhou em kCmdReadAngZ");
static_assert(withCrc(kCmdReadTemp & kCrcBodyMask) == kCmdReadTemp, "crc8 falhou em kCmdReadTemp");
static_assert(withCrc(kCmdReadStatus & kCrcBodyMask) == kCmdReadStatus, "crc8 falhou em kCmdReadStatus");
static_assert(withCrc(kCmdReadErrFlag1 & kCrcBodyMask) == kCmdReadErrFlag1, "crc8 falhou em kCmdReadErrFlag1");
static_assert(withCrc(kCmdReadErrFlag2 & kCrcBodyMask) == kCmdReadErrFlag2, "crc8 falhou em kCmdReadErrFlag2");
static_assert(withCrc(kCmdReadWhoAmI & kCrcBodyMask) == kCmdReadWhoAmI, "crc8 falhou em kCmdReadWhoAmI");
static_assert(withCrc(kCmdReadAccX & kCrcBodyMask) == kCmdReadAccX, "crc8 falhou em kCmdReadAccX");
static_assert(withCrc(kCmdReadAccY & kCrcBodyMask) == kCmdReadAccY, "crc8 falhou em kCmdReadAccY");
static_assert(withCrc(kCmdReadAccZ & kCrcBodyMask) == kCmdReadAccZ, "crc8 falhou em kCmdReadAccZ");
static_assert(withCrc(kCmdReadSto & kCrcBodyMask) == kCmdReadSto, "crc8 falhou em kCmdReadSto");
static_assert(withCrc(kCmdReadCmd & kCrcBodyMask) == kCmdReadCmd, "crc8 falhou em kCmdReadCmd");
static_assert(withCrc(kCmdBank0 & kCrcBodyMask) == kCmdBank0, "crc8 falhou em kCmdBank0");
static_assert(withCrc(kCmdBank1 & kCrcBodyMask) == kCmdBank1, "crc8 falhou em kCmdBank1");
static_assert(withCrc(kCmdReadCurrentBank & kCrcBodyMask) == kCmdReadCurrentBank, "crc8 falhou em kCmdReadCurrentBank");
static_assert(withCrc(kCmdReadSerial1 & kCrcBodyMask) == kCmdReadSerial1, "crc8 falhou em kCmdReadSerial1");
static_assert(withCrc(kCmdReadSerial2 & kCrcBodyMask) == kCmdReadSerial2, "crc8 falhou em kCmdReadSerial2");
static_assert(frameCrcOk(kCmdReadStatus), "frameCrcOk falhou em palavra conhecida");
static_assert(returnStatus(kCmdReadStatus) == 0, "campo RS do MOSI tem de ser 00");
static_assert(frameData(kCmdEnableAngle) == 0x001F, "campo de dado fora de posicao");
static_assert(frameOpcode(kCmdReadWhoAmI) == 0x10, "campo de opcode fora de posicao");

}  // namespace

const char* rsName(Rs r) {
    switch (r) {
        case Rs::Startup:
            return "start-up";
        case Rs::Ok:
            return "ok";
        case Rs::Reserved:
            return "reservado";
        case Rs::Error:
            return "erro";
        default:
            break;
    }
    return "desconhecido";
}

int16_t angleDeciDegrees(uint16_t raw) {
    const int32_t scaled = static_cast<int32_t>(static_cast<int16_t>(raw)) * kAngleNumerator;
    const int32_t rounded = (scaled >= 0) ? ((scaled + kAngleHalf) / kAngleDenominator)
                                          : -(((-scaled) + kAngleHalf) / kAngleDenominator);
    return static_cast<int16_t>(rounded);
}

int16_t temperatureDeciC(uint16_t raw) {
    const int32_t scaled = static_cast<int32_t>(raw) * kTempNumerator;
    const int32_t rounded = (scaled + kTempHalf) / kTempDenominator;
    return static_cast<int16_t>(rounded - kTempOffsetDeci);
}

uint32_t modeCommand(uint8_t mode) {
    switch (mode) {
        case 2:
            return kCmdMode2;
        case 3:
            return kCmdMode3;
        case 4:
            return kCmdMode4;
        default:
            break;
    }
    return kCmdMode1;
}

uint16_t modeSettleMs(uint8_t mode) {
    switch (mode) {
        case 2:
            return kSettleMsMode2;
        case 3:
        case 4:
            return kSettleMsMode34;
        default:
            break;
    }
    return kSettleMsMode1;
}

bool commandTableOk() {
    return frameCrcOk(kCmdSwReset) && frameCrcOk(kCmdMode1) && frameCrcOk(kCmdMode2) &&
           frameCrcOk(kCmdMode3) && frameCrcOk(kCmdMode4) && frameCrcOk(kCmdEnableAngle) &&
           frameCrcOk(kCmdReadAngX) && frameCrcOk(kCmdReadAngY) && frameCrcOk(kCmdReadAngZ) &&
           frameCrcOk(kCmdReadTemp) && frameCrcOk(kCmdReadStatus) && frameCrcOk(kCmdReadErrFlag1) &&
           frameCrcOk(kCmdReadErrFlag2) && frameCrcOk(kCmdReadWhoAmI) && frameCrcOk(kCmdReadSto);
}


namespace {

void appendFlag(char* out, uint16_t cap, uint16_t& used, const char* text) {
    if (used + 1 >= cap) {
        return;
    }
    const int written = snprintf(out + used, static_cast<size_t>(cap - used), "%s%s", (used > 0) ? " " : "", text);
    if (written > 0) {
        const uint16_t grown = static_cast<uint16_t>(used + written);
        used = (grown >= cap) ? static_cast<uint16_t>(cap - 1) : grown;
    }
}

}  // namespace

void describeStatus(uint16_t value, char* out, uint16_t cap) {
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    uint16_t used = 0;
    if ((value & kStatusPinContinuity) != 0) appendFlag(out, cap, used, "PIN_CONTINUITY(conexao interna)");
    if ((value & kStatusModeChange) != 0) appendFlag(out, cap, used, "MODE_CHANGE(normal)");
    if ((value & kStatusPd) != 0) appendFlag(out, cap, used, "PD(power down)");
    if ((value & kStatusMem) != 0) appendFlag(out, cap, used, "MEM");
    if ((value & kStatusPwr) != 0) appendFlag(out, cap, used, "PWR(normal apos start-up)");
    if ((value & kStatusTemp) != 0) appendFlag(out, cap, used, "TEMP");
    if ((value & kStatusSat) != 0) appendFlag(out, cap, used, "SAT(front-end saturado)");
    if ((value & kStatusClk) != 0) appendFlag(out, cap, used, "CLK");
    if ((value & kStatusDigi2) != 0) appendFlag(out, cap, used, "DIGI2");
    if ((value & kStatusDigi1) != 0) appendFlag(out, cap, used, "DIGI1");
    if (used == 0) appendFlag(out, cap, used, "(limpo)");
}

void describeErrFlag1(uint16_t value, char* out, uint16_t cap) {
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    uint16_t used = 0;
    if ((value & kErr1Mem) != 0) appendFlag(out, cap, used, "MEM");
    if ((value & kErr1AfeSat) != 0) appendFlag(out, cap, used, "AFE_SAT(front-end analogico saturado)");
    if ((value & kErr1AdcSat) != 0) appendFlag(out, cap, used, "ADC_SAT(conversor saturado)");
    if (used == 0) appendFlag(out, cap, used, "(limpo)");
}

void describeErrFlag2(uint16_t value, char* out, uint16_t cap) {
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    uint16_t used = 0;
    if ((value & kErr2Clk) != 0) appendFlag(out, cap, used, "CLK");
    if ((value & kErr2TempSat) != 0) appendFlag(out, cap, used, "TEMP_SAT");
    if ((value & kErr2Apwr2) != 0) appendFlag(out, cap, used, "APWR_2(alimentacao analogica)");
    if ((value & kErr2Vref) != 0) appendFlag(out, cap, used, "VREF");
    if ((value & kErr2Dpwr) != 0) appendFlag(out, cap, used, "DPWR(normal apos start-up)");
    if ((value & kErr2Apwr) != 0) appendFlag(out, cap, used, "APWR(alimentacao analogica)");
    if ((value & kErr2MemoryCrc) != 0) appendFlag(out, cap, used, "MEMORY_CRC");
    if ((value & kErr2Pd) != 0) appendFlag(out, cap, used, "PD");
    if ((value & kErr2ModeChange) != 0) appendFlag(out, cap, used, "MODE_CHANGE");
    if ((value & kErr2Vdd) != 0) appendFlag(out, cap, used, "VDD(tensao de alimentacao)");
    if ((value & kErr2Agnd) != 0) appendFlag(out, cap, used, "AGND(terra analogico, pino 1)");
    if ((value & kErr2AExtC) != 0) appendFlag(out, cap, used, "A_EXT_C(capacitor 100nF do pino 2 A_EXTC)");
    if ((value & kErr2DExtC) != 0) appendFlag(out, cap, used, "D_EXT_C(capacitor 100nF do pino 10 D_EXTC)");
    if (used == 0) appendFlag(out, cap, used, "(limpo)");
}

}  // namespace scl
