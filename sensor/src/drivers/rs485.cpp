// Folha 1/2: SN65HVD75DR no UART2 (TX 17, RX 16, DE/RE 14). O periferico chaveia DE, nao o software.
// API do ESP-IDF 4.4 (driver/uart.h) sob o Arduino core 2.0.17; sem eco local durante a transmissao.
#include "drivers/rs485.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_timer.h>

namespace {

constexpr uart_port_t kPort = UART_NUM_2;
constexpr uint32_t kBitsPerByteWorst = 12;
constexpr uint32_t kTurnaroundMaxUs = 0xFFFFFFFFu;

constexpr gpio_num_t txGpio() { return static_cast<gpio_num_t>(board::kRs485Tx); }
constexpr gpio_num_t deGpio() { return static_cast<gpio_num_t>(board::kRs485De); }
constexpr uint8_t txPin() { return static_cast<uint8_t>(board::kRs485Tx); }
constexpr uint8_t dePin() { return static_cast<uint8_t>(board::kRs485De); }

bool mapDataBits(uint8_t bits, uart_word_length_t& outBits) {
    switch (bits) {
        case 5: outBits = UART_DATA_5_BITS; return true;
        case 6: outBits = UART_DATA_6_BITS; return true;
        case 7: outBits = UART_DATA_7_BITS; return true;
        case 8: outBits = UART_DATA_8_BITS; return true;
        default: return false;
    }
}

bool mapParity(char code, uart_parity_t& outParity) {
    switch (code) {
        case 'N':
        case 'n': outParity = UART_PARITY_DISABLE; return true;
        case 'E':
        case 'e': outParity = UART_PARITY_EVEN; return true;
        case 'O':
        case 'o': outParity = UART_PARITY_ODD; return true;
        default: return false;
    }
}

bool mapStopBits(uint8_t stops, uart_stop_bits_t& outStops) {
    switch (stops) {
        case 1: outStops = UART_STOP_BITS_1; return true;
        case 2: outStops = UART_STOP_BITS_2; return true;
        default: return false;
    }
}

}  // namespace

Rs485Transport::Rs485Transport()
    : stats_{0, 0, 0, 0, 0, 0},
      evtQueue_(nullptr),
      baud_(0),
      turnaroundUs_(0),
      txDoneUs_(-1),
      dataBits_(8),
      stopBits_(1),
      parity_('N'),
      installed_(false),
      staticDrive_(false) {}

Status Rs485Transport::install() {
    uart_word_length_t wordLength = UART_DATA_8_BITS;
    uart_parity_t parityMode = UART_PARITY_DISABLE;
    uart_stop_bits_t stopMode = UART_STOP_BITS_1;
    if (!mapDataBits(dataBits_, wordLength) || !mapParity(parity_, parityMode) ||
        !mapStopBits(stopBits_, stopMode)) {
        return Err::Param;
    }
    if (baud_ < kMinBaud || baud_ > kMaxBaud) {
        return Err::Range;
    }

    uart_config_t cfg = {};
    cfg.baud_rate = static_cast<int>(baud_);
    cfg.data_bits = wordLength;
    cfg.parity = parityMode;
    cfg.stop_bits = stopMode;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.rx_flow_ctrl_thresh = 0;
    cfg.source_clk = UART_SCLK_APB;

    if (uart_param_config(kPort, &cfg) != ESP_OK) {
        return Err::Io;
    }
    if (uart_set_pin(kPort, board::kRs485Tx, board::kRs485Rx, board::kRs485De,
                     UART_PIN_NO_CHANGE) != ESP_OK) {
        return Err::Io;
    }
    if (uart_is_driver_installed(kPort)) {
        uart_driver_delete(kPort);
    }
    evtQueue_ = nullptr;
    if (uart_driver_install(kPort, static_cast<int>(kRxBufferBytes), 0,
                            static_cast<int>(kEventQueueDepth), &evtQueue_, 0) != ESP_OK) {
        evtQueue_ = nullptr;
        return Err::Io;
    }
    if (uart_set_mode(kPort, UART_MODE_RS485_HALF_DUPLEX) != ESP_OK) {
        uart_driver_delete(kPort);
        evtQueue_ = nullptr;
        return Err::Io;
    }
    uart_flush_input(kPort);
    installed_ = true;
    txDoneUs_ = -1;
    return kOk;
}

void Rs485Transport::uninstall() {
    if (installed_ || uart_is_driver_installed(kPort)) {
        uart_driver_delete(kPort);
    }
    installed_ = false;
    evtQueue_ = nullptr;
    txDoneUs_ = -1;
}

void Rs485Transport::pumpEvents() {
    if (evtQueue_ == nullptr) {
        return;
    }
    uart_event_t ev;
    while (xQueueReceive(evtQueue_, &ev, 0) == pdTRUE) {
        switch (ev.type) {
            case UART_FRAME_ERR:
            case UART_PARITY_ERR:
            case UART_BUFFER_FULL:
            case UART_FIFO_OVF:
                ++stats_.framingErrors;
                break;
            default:
                break;
        }
    }
}

uint32_t Rs485Transport::txBudgetMs(uint16_t len) const {
    const uint32_t rate = (baud_ == 0) ? board::kRs485DefaultBaud : baud_;
    const uint32_t bits = static_cast<uint32_t>(len) * kBitsPerByteWorst;
    return ((bits * 1000u) / rate) + kTxGuardMs;
}

Status Rs485Transport::begin(uint32_t baudRate, uint8_t bits, char parityChar, uint8_t stops) {
    uart_word_length_t wordLength = UART_DATA_8_BITS;
    uart_parity_t parityMode = UART_PARITY_DISABLE;
    uart_stop_bits_t stopMode = UART_STOP_BITS_1;
    if (!mapDataBits(bits, wordLength) || !mapParity(parityChar, parityMode) ||
        !mapStopBits(stops, stopMode)) {
        return Err::Param;
    }
    if (baudRate < kMinBaud || baudRate > kMaxBaud) {
        return Err::Range;
    }

    uninstall();
    if (staticDrive_) {
        staticDrive_ = false;
        gpio_reset_pin(txGpio());
    }

    baud_ = baudRate;
    dataBits_ = bits;
    parity_ = parityChar;
    stopBits_ = stops;

    const Status st = install();
    if (st.failed()) {
        baud_ = 0;
    }
    return st;
}

Status Rs485Transport::end() {
    uninstall();
    baud_ = 0;
    return kOk;
}

Status Rs485Transport::write(const uint8_t* data, uint16_t len) {
    if (!installed_) {
        return Err::NotInit;
    }
    if (data == nullptr || len == 0) {
        return Err::Param;
    }
    const int sent = uart_write_bytes(kPort, data, len);
    if (sent < 0) {
        return Err::Io;
    }
    stats_.bytesTx += static_cast<uint32_t>(sent);
    if (uart_wait_tx_done(kPort, pdMS_TO_TICKS(txBudgetMs(len))) != ESP_OK) {
        ++stats_.timeouts;
        return Err::Timeout;
    }
    txDoneUs_ = esp_timer_get_time();
    if (sent != static_cast<int>(len)) {
        return Err::Io;
    }
    return kOk;
}

uint16_t Rs485Transport::read(uint8_t* buf, uint16_t cap, uint32_t timeoutMs) {
    if (!installed_ || buf == nullptr || cap == 0) {
        return 0;
    }
    const int got = uart_read_bytes(kPort, buf, cap, pdMS_TO_TICKS(timeoutMs));
    pumpEvents();
    if (got <= 0) {
        return 0;
    }
    stats_.bytesRx += static_cast<uint32_t>(got);
    if (txDoneUs_ >= 0) {
        const int64_t delta = esp_timer_get_time() - txDoneUs_;
        if (delta <= 0) {
            turnaroundUs_ = 0;
        } else if (delta > static_cast<int64_t>(kTurnaroundMaxUs)) {
            turnaroundUs_ = kTurnaroundMaxUs;
        } else {
            turnaroundUs_ = static_cast<uint32_t>(delta);
        }
        txDoneUs_ = -1;
    }
    return static_cast<uint16_t>(got);
}

uint16_t Rs485Transport::available() const {
    if (!installed_) {
        return 0;
    }
    size_t pending = 0;
    if (uart_get_buffered_data_len(kPort, &pending) != ESP_OK) {
        return 0;
    }
    if (pending > 0xFFFFu) {
        pending = 0xFFFFu;
    }
    return static_cast<uint16_t>(pending);
}

Status Rs485Transport::flushRx() {
    if (!installed_) {
        return Err::NotInit;
    }
    pumpEvents();
    if (uart_flush_input(kPort) != ESP_OK) {
        return Err::Io;
    }
    txDoneUs_ = -1;
    return kOk;
}

Status Rs485Transport::driveStatic(bool enable, bool level) {
    if (enable) {
        uninstall();
        gpio_reset_pin(deGpio());
        gpio_reset_pin(txGpio());
        pinMode(dePin(), OUTPUT);
        digitalWrite(dePin(), HIGH);
        pinMode(txPin(), OUTPUT);
        digitalWrite(txPin(), level ? HIGH : LOW);
        staticDrive_ = true;
        return kOk;
    }
    if (!staticDrive_) {
        return kOk;
    }
    digitalWrite(dePin(), LOW);
    gpio_reset_pin(txGpio());
    staticDrive_ = false;
    if (baud_ != 0) {
        return install();
    }
    pinMode(dePin(), OUTPUT);
    digitalWrite(dePin(), LOW);
    return kOk;
}

uint32_t Rs485Transport::lastTurnaroundUs() const {
    return turnaroundUs_;
}

const SerialStats& Rs485Transport::stats() const {
    return stats_;
}

void Rs485Transport::resetStats() {
    stats_.framesOk = 0;
    stats_.timeouts = 0;
    stats_.crcErrors = 0;
    stats_.framingErrors = 0;
    stats_.bytesRx = 0;
    stats_.bytesTx = 0;
    turnaroundUs_ = 0;
    txDoneUs_ = -1;
}

void Rs485Transport::noteCrcError() {
    ++stats_.crcErrors;
}

void Rs485Transport::noteFrameOk() {
    ++stats_.framesOk;
}

void Rs485Transport::noteTimeout() {
    ++stats_.timeouts;
}

uint32_t Rs485Transport::charTimeUs() const {
    const uint32_t bits = 1u + static_cast<uint32_t>(dataBits_) +
                          ((parity_ == 'N') ? 0u : 1u) + static_cast<uint32_t>(stopBits_);
    const uint32_t rate = (baud_ == 0) ? board::kRs485DefaultBaud : baud_;
    return (bits * 1000000u) / rate;
}

uint32_t Rs485Transport::baud() const {
    return baud_;
}
