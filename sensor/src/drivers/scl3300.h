// PUSI-DI261930: inclinometro Murata SCL3300 por SPI (Rev.4, doc 4921, Tabela 11 de inicializacao).
// Protocolo off-frame: a resposta lida no quadro N pertence ao comando do quadro N-1.
#pragma once

#include <SPI.h>
#include <stdint.h>

#include "board_pins.h"
#include "drivers/scl3300_math.h"
#include "drivers/spi_bus.h"
#include "iface/iinclinometer.h"
#include "status.h"
#include "tilt.h"

class Scl3300 : public IInclinometer {
public:
    static constexpr uint32_t kCsHighUs = 12;
    static constexpr uint32_t kSpiMinHz = 100000;
    static constexpr uint32_t kSpiMaxHz = 8000000;
    static constexpr uint32_t kSpiDefaultHz = 2000000;
    static constexpr uint8_t kTraceDepth = 24;
    static constexpr uint8_t kStatusReadsOnBegin = 3;
    static constexpr uint8_t kBurstFrames = 6;

    Scl3300(SpiBus& bus, board::Pin cs, uint32_t clockHz = kSpiDefaultHz, uint8_t mode = 1);

    Status begin() override;
    Status read(Tilt& out) override;
    Status selfTest() override;
    uint16_t whoAmI() const override;
    Status probeWhoAmI(uint16_t& out) override;
    const char* name() const override;
    uint32_t reads() const override;
    uint32_t crcErrors() const override;
    uint32_t frameErrors() const override;
    void diagnostics(InclinometerDiag& out) const override;
    uint8_t traceCount() const override;
    bool traceAt(uint8_t index, FrameTrace& out) const override;


    Status reinit();

    uint16_t lastStatus() const { return lastStatus_; }
    uint16_t lastErrFlag1() const { return lastErrFlag1_; }
    uint16_t lastErrFlag2() const { return lastErrFlag2_; }
    uint16_t lastSto() const { return lastSto_; }
    scl::Rs lastRs() const { return lastRs_; }
    bool ready() const { return ready_; }
    uint8_t mode() const { return mode_; }
    uint32_t clockHz() const { return clockHz_; }
    board::Pin csPin() const { return cs_; }
    uint32_t frames() const { return frames_; }

private:
    static uint32_t clampHz(uint32_t hz);
    void captureErrorFlags();
    void recordTrace(uint32_t command, uint32_t response);
    static uint8_t clampMode(uint8_t requested);

    void waitCsHigh();
    Status sendFrame(uint32_t command, uint32_t& previousResponse);
    Status exchange(uint32_t command, uint32_t& previousResponse);
    Status readRegister(uint32_t readCommand, uint16_t& value);

    SpiBus& bus_;
    board::Pin cs_;
    uint8_t mode_;
    uint32_t clockHz_;
    SPISettings settings_;
    uint32_t lastFrameEndUs_;
    uint32_t frames_;
    uint32_t reads_;
    uint32_t crcErrors_;
    uint32_t frameErrors_;
    uint16_t whoAmi_;
    uint16_t lastStatus_;
    uint16_t lastErrFlag1_;
    uint16_t lastErrFlag2_;
    uint16_t lastSto_;
    scl::Rs lastRs_;
    bool ready_;
    bool selfTestFailed_;
    bool flagsRead_;
    FrameTrace trace_[kTraceDepth];
    uint8_t traceFill_;
    uint8_t traceHead_;
};
