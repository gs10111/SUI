// Composition root do jig DE-PURI-DI261924 REV A (folhas 1/2 e 2/2). Unico lugar que constroi objetos concretos.
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_mac.h>
#include <esp_system.h>

#include "board_pins.h"
#include "build_config.h"
#include "core/console.h"
#include "core/console_operator.h"
#include "core/ctx.h"
#include "core/report.h"
#include "core/test_runner.h"
#include "drivers/buttons.h"
#include "drivers/calibration.h"
#include "drivers/dac8562.h"
#include "drivers/display.h"
#include "drivers/ext_wdt.h"
#include "drivers/relays.h"
#include "drivers/rs485.h"
#include "drivers/spi_bus.h"
#include "drivers/xtr300.h"
#include "platform/nvs_store.h"
#include "platform/serial_console_io.h"
#include "proto/echo_protocol.h"

namespace {

constexpr const char* kNvsWdtFlag = "wdt_expect";
constexpr const char* kNvsSerial = "serial";
constexpr const char* kNvsDate = "date";

SPIClass g_hspi(HSPI);
SPIClass g_vspi(VSPI);

SpiBus g_dacBus(g_hspi, board::kDacSclk, board::kDacMiso, board::kDacMosi, "HSPI/DAC");
SpiBus g_dispBus(g_vspi, board::kDispSclk, board::kDispMiso, board::kDispMosi, "VSPI/DISP");

ExtWatchdog g_wdt;
NvsStore g_nvs;
CalibrationStore g_cal(g_nvs);
Dac8562 g_dac(g_dacBus, board::kDacSync, board::kDacSpiDefaultHz);
Xtr300AnalogOutput g_ao(g_dac, board::kXtrOpMode, g_cal);
RelayBank g_relays;
Rs485Transport g_rs485;
EchoProtocol g_proto;
ButtonMonitor g_buttons;

#if DISPLAY_DRIVER == DISPLAY_DRIVER_RAW
RawSpiDisplay g_display(g_dispBus);
#else
NullDisplay g_display;
#endif

SerialConsoleIO g_io;
ConsoleOperator g_operator(g_io);
Report g_report;
BootInfo g_boot;

class JigSafeState : public ISafeState {
public:
    JigSafeState(IDigitalOutputBank& relays, IAnalogOutput& ao, IDisplay& display, ISerialTransport& rs485)
        : relays_(relays), ao_(ao), display_(display), rs485_(rs485) {}

    void enterSafeState() override {
        relays_.allOff();
        ao_.zeroAll();
        ao_.setMode(AoMode::Voltage);
        display_.off();
        rs485_.driveStatic(false, false);
    }

private:
    IDigitalOutputBank& relays_;
    IAnalogOutput& ao_;
    IDisplay& display_;
    ISerialTransport& rs485_;
};

JigSafeState g_safe(g_relays, g_ao, g_display, g_rs485);

Ctx g_ctx{g_io,      g_operator, g_ao,     g_relays, g_rs485, g_proto, g_display, g_buttons,
          g_wdt,     g_cal,      g_nvs,    g_report, g_safe,   g_boot,  nullptr,   FW_VERSION,
          BOARD_REV};

TestRunner g_runner(g_ctx);
Console g_console(g_ctx);

const char* resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_UNKNOWN: return "UNKNOWN";
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXT";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "?";
    }
}

void captureBootInfo() {
    const esp_reset_reason_t reason = esp_reset_reason();
    g_boot.resetReason = static_cast<uint32_t>(reason);
    g_boot.resetReasonName = resetReasonName(reason);
    g_boot.strappingCount = board::kStrappingCount;
    for (uint8_t i = 0; i < board::kStrappingCount && i < 8; ++i) {
        const board::Pin pin = board::kStrappingPins[i];
        pinMode(pin, INPUT);
        g_boot.strappingLevel[i] = static_cast<uint8_t>(digitalRead(pin));
    }
    g_boot.flashSizeBytes = ESP.getFlashChipSize();
    const uint64_t chip = ESP.getEfuseMac();
    g_boot.chipIdLow = static_cast<uint32_t>(chip & 0xFFFFFFFFull);
    g_boot.chipIdHigh = static_cast<uint32_t>((chip >> 32) & 0xFFFFFFFFull);
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    esp_efuse_mac_get_default(mac);
    snprintf(g_boot.macText, sizeof(g_boot.macText), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    g_boot.wdtResetExpected = false;
    g_boot.wdtResetObserved = false;
}

void restoreNvsState() {
    uint8_t flag = 0;
    if (g_nvs.getU8(kNvsWdtFlag, flag).ok() && flag != 0) {
        g_boot.wdtResetExpected = true;
        g_boot.wdtResetObserved = (g_boot.resetReason == static_cast<uint32_t>(ESP_RST_POWERON));
    }
    char serial[kReportSerialLen] = {0};
    if (g_nvs.getString(kNvsSerial, serial, sizeof(serial)).ok() && serial[0] != '\0') {
        g_report.setSerial(serial);
    }
    char stamp[kReportDateLen] = {0};
    if (g_nvs.getString(kNvsDate, stamp, sizeof(stamp)).ok() && stamp[0] != '\0') {
        g_report.setDate(stamp);
    }
    g_cal.load();
}

}  // namespace

void setup() {
    const Status wdtStatus = g_wdt.begin();

    captureBootInfo();

    g_relays.begin();
    g_relays.allOff();

    WiFi.mode(WIFI_OFF);
    btStop();

    g_io.begin();
    if (wdtStatus.failed()) {
        g_io.printf("ALERTA: timer de chute do watchdog nao subiu (%s): a placa vai resetar a cada ~%u ms\r\n",
                    errName(wdtStatus.err), static_cast<unsigned>(board::kWdtTypTimeoutMs));
    }
    g_report.setMeta(FW_VERSION, BOARD_REV);

    g_nvs.begin();
    restoreNvsState();

    g_dacBus.begin();
    g_dispBus.begin();
    g_ao.begin();
    g_display.begin();
    g_buttons.begin();
    g_rs485.begin(board::kRs485DefaultBaud, 8, 'N', 1);
    g_proto.begin(g_rs485);

    g_ctx.runner = &g_runner;
    g_safe.enterSafeState();

    g_console.begin();
}

void loop() {
    g_console.poll();
    g_buttons.poll();
}
