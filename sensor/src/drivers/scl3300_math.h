// PUSI-DI261930: regras de quadro do Murata SCL3300 (Rev.4, doc 4921) sem Arduino nem SPI, compila no host.
// Angulo tomado DELIBERADAMENTE no ramo com sinal (int16, -180..+180 graus): a Tabela 18 se contradiz.
#pragma once

#include <stdint.h>

namespace scl {

constexpr uint8_t kCrcPoly = 0x1D;
constexpr uint8_t kCrcInit = 0xFF;
constexpr uint8_t kFrameFirstCrcBit = 31;
constexpr uint8_t kFrameLastCrcBit = 8;

constexpr uint32_t kCmdSwReset = 0xB4002098;
constexpr uint32_t kCmdPowerDown = 0xB400046B;
constexpr uint32_t kCmdMode1 = 0xB400001F;
constexpr uint32_t kCmdMode2 = 0xB4000102;
constexpr uint32_t kCmdMode3 = 0xB4000225;
constexpr uint32_t kCmdMode4 = 0xB4000338;
constexpr uint32_t kCmdWakeUp = kCmdMode1;
constexpr uint32_t kCmdEnableAngle = 0xB0001F6F;
constexpr uint32_t kCmdReadAngX = 0x240000C7;
constexpr uint32_t kCmdReadAngY = 0x280000CD;
constexpr uint32_t kCmdReadAngZ = 0x2C0000CB;
constexpr uint32_t kCmdReadTemp = 0x140000EF;
constexpr uint32_t kCmdReadStatus = 0x180000E5;
constexpr uint32_t kCmdReadErrFlag1 = 0x1C0000E3;
constexpr uint32_t kCmdReadErrFlag2 = 0x200000C1;
constexpr uint32_t kCmdReadWhoAmI = 0x40000091;
constexpr uint32_t kCmdReadAccX = 0x040000F7;
constexpr uint32_t kCmdReadAccY = 0x080000FD;
constexpr uint32_t kCmdReadAccZ = 0x0C0000FB;
constexpr uint32_t kCmdReadSto = 0x100000E9;
constexpr uint32_t kCmdReadCmd = 0x340000DF;
constexpr uint32_t kCmdBank0 = 0xFC000073;
constexpr uint32_t kCmdBank1 = 0xFC00016E;
constexpr uint32_t kCmdReadCurrentBank = 0x7C0000B3;
constexpr uint32_t kCmdReadSerial1 = 0x640000A7;
constexpr uint32_t kCmdReadSerial2 = 0x680000AD;

constexpr uint16_t kWhoAmIValue = 0x00C1;

constexpr uint16_t kStatusModeChange = 0x0002;
constexpr uint16_t kStatusPwr = 0x0010;
constexpr uint16_t kStatusSat = 0x0040;
constexpr uint16_t kStatusStartupBenign = static_cast<uint16_t>(kStatusPwr | kStatusModeChange);
constexpr uint16_t kStatusFault = static_cast<uint16_t>(0xFFFFu & ~static_cast<uint32_t>(kStatusStartupBenign));

constexpr uint8_t kModeMin = 1;
constexpr uint8_t kModeMax = 4;
constexpr uint16_t kSettleMsMode1 = 25;
constexpr uint16_t kSettleMsMode2 = 15;
constexpr uint16_t kSettleMsMode34 = 100;
constexpr uint16_t kResetSettleMs = 3;

enum class Rs : uint8_t { Startup = 0, Ok = 1, Reserved = 2, Error = 3 };

constexpr uint8_t crc8(uint32_t frame) {
    uint8_t crc = kCrcInit;
    for (int bit = kFrameFirstCrcBit; bit >= kFrameLastCrcBit; --bit) {
        const uint8_t frameBit = static_cast<uint8_t>((frame >> bit) & 0x01u);
        const uint8_t crcBit = static_cast<uint8_t>((crc >> 7) & 0x01u);
        crc = static_cast<uint8_t>(crc << 1);
        if (crcBit != frameBit) {
            crc = static_cast<uint8_t>(crc ^ kCrcPoly);
        }
    }
    return static_cast<uint8_t>(~crc);
}

constexpr uint32_t withCrc(uint32_t frameWithoutCrc) {
    const uint32_t body = frameWithoutCrc & 0xFFFFFF00u;
    return body | static_cast<uint32_t>(crc8(body));
}

constexpr bool frameCrcOk(uint32_t frame) {
    return crc8(frame) == static_cast<uint8_t>(frame & 0xFFu);
}

constexpr uint8_t returnStatus(uint32_t frame) {
    return static_cast<uint8_t>((frame >> 24) & 0x03u);
}

constexpr uint16_t frameData(uint32_t frame) {
    return static_cast<uint16_t>((frame >> 8) & 0xFFFFu);
}

constexpr uint8_t frameOpcode(uint32_t frame) {
    return static_cast<uint8_t>((frame >> 26) & 0x3Fu);
}

constexpr Rs rsOf(uint32_t frame) {
    return static_cast<Rs>(returnStatus(frame));
}

const char* rsName(Rs r);

int16_t angleDeciDegrees(uint16_t raw);
int16_t temperatureDeciC(uint16_t raw);

uint32_t modeCommand(uint8_t mode);
uint16_t modeSettleMs(uint8_t mode);

bool commandTableOk();

}  // namespace scl
