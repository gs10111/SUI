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


constexpr uint16_t kStatusPinContinuity = 0x0001;
constexpr uint16_t kStatusPd = 0x0004;
constexpr uint16_t kStatusMem = 0x0008;
constexpr uint16_t kStatusTemp = 0x0020;
constexpr uint16_t kStatusClk = 0x0080;
constexpr uint16_t kStatusDigi2 = 0x0100;
constexpr uint16_t kStatusDigi1 = 0x0200;

constexpr uint16_t kErr1AfeSat = 0x07FE;
constexpr uint16_t kErr1AdcSat = 0x0800;
constexpr uint16_t kErr1Mem = 0x0001;

constexpr uint16_t kErr2Clk = 0x0001;
constexpr uint16_t kErr2TempSat = 0x0002;
constexpr uint16_t kErr2Apwr2 = 0x0004;
constexpr uint16_t kErr2Vref = 0x0008;
constexpr uint16_t kErr2Dpwr = 0x0010;
constexpr uint16_t kErr2Apwr = 0x0020;
constexpr uint16_t kErr2MemoryCrc = 0x0080;
constexpr uint16_t kErr2Pd = 0x0100;
constexpr uint16_t kErr2ModeChange = 0x0200;
constexpr uint16_t kErr2Vdd = 0x0800;
constexpr uint16_t kErr2Agnd = 0x1000;
constexpr uint16_t kErr2AExtC = 0x2000;
constexpr uint16_t kErr2DExtC = 0x4000;

// Datasheet Tabela 33, bit D4 (DPWR): "[After star-up or reset] This flag is set high. No actions
// needed." Ler ERR_FLAG nao reseta nada (secao 6.4), entao DPWR fica alto para sempre depois de
// todo start-up. O bit D9 (MODE_CHANGE) sobe porque NOS pedimos o modo no passo 4 da Tabela 11.
// Estes dois - e SOMENTE estes dois - sao tolerados; todo o resto de ERR_FLAG2 reprova.
constexpr uint16_t kErr2StartupBenign = static_cast<uint16_t>(kErr2Dpwr | kErr2ModeChange);
constexpr uint16_t kErr2Fault =
    static_cast<uint16_t>(0xFFFFu & ~static_cast<uint32_t>(kErr2StartupBenign));
// ERR_FLAG1 (Tabela 31) nao tem bit benigno: MEM, AFE_SAT e ADC_SAT sao todos falha.
constexpr uint16_t kErr1Fault = 0xFFFFu;

// BYPASS DE BANCADA. Tolera EXATAMENTE os dois bits que o capacitor ausente do pino D_EXTC
// (C8) produz: PIN_CONTINUITY no STATUS e D_EXT_C no ERR_FLAG2. Serve para exercitar a cadeia
// limite -> rele -> LED -> saida analogica antes de o capacitor ser consertado, e nada alem
// disso.
//
// A_EXT_C fica DELIBERADAMENTE de fora: e o capacitor do core ANALOGICO, e sem ele o front-end
// satura e o angulo sai errado. Tolera-lo seria comandar rele com numero falso, que e
// exatamente o modo de falha que este produto existe para evitar. Um bypass que tolerasse tudo
// nao seria auxilio de bancada, seria desligar a supervisao.
//
// Quem liga isto e um comando de console, em tempo de execucao e com padrao DESLIGADO: nao
// existe binario compilado com o bypass ativo, e cada energizacao volta a recusar.
constexpr uint16_t kBenchBypassStatus = kStatusPinContinuity;
constexpr uint16_t kBenchBypassErr2 = kErr2DExtC;

// SAT sai da mascara DURA porque tem tratamento proprio no driver (kStsSaturated e Err::Range),
// e nao porque seja tolerado.
constexpr uint16_t statusHardMask(bool benchBypass) {
    const uint32_t tolerado =
        static_cast<uint32_t>(kStatusSat) | (benchBypass ? kBenchBypassStatus : 0u);
    return static_cast<uint16_t>(kStatusFault & ~tolerado);
}

// Veredito do autoteste em um lugar so, sem Arduino, para que o teste de host prenda o criterio.
// Bits reservados entram como falha de proposito: num supervisor de seguranca, bit indefinido
// subindo e motivo para desconfiar da peca, e a mesma postura ja vale para kStatusFault.
constexpr bool selfTestFaulty(uint16_t status, uint16_t flag1, uint16_t flag2,
                              bool benchBypass = false) {
    const uint16_t statusMask = static_cast<uint16_t>(
        kStatusFault & ~(benchBypass ? static_cast<uint32_t>(kBenchBypassStatus) : 0u));
    const uint16_t err2Mask = static_cast<uint16_t>(
        kErr2Fault & ~(benchBypass ? static_cast<uint32_t>(kBenchBypassErr2) : 0u));
    return ((status & statusMask) != 0u) || ((flag1 & kErr1Fault) != 0u) ||
           ((flag2 & err2Mask) != 0u);
}

// Tabela 23: limiares de EXEMPLO do STO por modo, em LSB. O datasheet manda ler STO
// continuamente depois de cada leitura XYZ e contar eventos consecutivos acima do limiar -
// limiar e tempo tolerante a falha sao "application specific". Enquanto esse contador nao
// existir, o STO e REPORTADO e nao entra no veredito: uma unica amostra fora da faixa nao e
// falha ("exceeds the threshold level continuously ... in static condition").
constexpr int16_t kStoThresholdMode1 = 1800;
constexpr int16_t kStoThresholdMode2 = 900;
constexpr int16_t kStoThresholdMode34 = 3600;

int16_t stoThreshold(uint8_t mode);
bool stoOutOfRange(uint16_t rawSto, uint8_t mode);

void describeStatus(uint16_t value, char* out, uint16_t cap);
void describeErrFlag1(uint16_t value, char* out, uint16_t cap);
void describeErrFlag2(uint16_t value, char* out, uint16_t cap);

}  // namespace scl
