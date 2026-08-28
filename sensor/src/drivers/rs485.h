// Folha 1/2: transceptor SN65HVD75DR (DE e /RE unidos), TVS CDSOT23-SM712, terminador 120R em J7.
// UART2 do ESP32 em RS-485 half-duplex; DE no pino RTS do periferico (ESP32 TRM, cap. UART).
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>

#include "board_pins.h"
#include "iface/iserial_transport.h"
#include "status.h"

class Rs485Transport : public ISerialTransport {
public:
    static constexpr uint16_t kRxBufferBytes = 512;
    static constexpr uint8_t kEventQueueDepth = 8;
    static constexpr uint32_t kTxGuardMs = 50;
    static constexpr uint32_t kMinBaud = 300;
    static constexpr uint32_t kMaxBaud = 921600;

    Rs485Transport();

    Status begin(uint32_t baudRate, uint8_t bits, char parityChar, uint8_t stops) override;
    Status end() override;
    Status write(const uint8_t* data, uint16_t len) override;
    uint16_t read(uint8_t* buf, uint16_t cap, uint32_t timeoutMs) override;
    uint16_t available() const override;
    Status flushRx() override;
    Status driveStatic(bool enable, bool level) override;
    uint32_t lastTurnaroundUs() const override;
    const SerialStats& stats() const override;
    void resetStats() override;
    void noteCrcError() override;
    void noteFrameOk() override;
    void noteTimeout() override;
    uint32_t baud() const override;
    uint32_t charTimeUs() const override;

    bool installed() const { return installed_; }
    bool staticDriveActive() const { return staticDrive_; }
    uint8_t dataBits() const { return dataBits_; }
    char parity() const { return parity_; }
    uint8_t stopBits() const { return stopBits_; }

private:
    Status install();
    void uninstall();
    void pumpEvents();
    uint32_t txBudgetMs(uint16_t len) const;

    SerialStats stats_;
    QueueHandle_t evtQueue_;
    uint32_t baud_;
    uint32_t turnaroundUs_;
    int64_t txDoneUs_;
    uint8_t dataBits_;
    uint8_t stopBits_;
    char parity_;
    bool installed_;
    bool staticDrive_;
};
