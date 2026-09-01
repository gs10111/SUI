// Composition root da PUSI-DI261930: SCL3300 por SPI, escravo RS-485 e console de diagnostico.
// Unico lugar que constroi objetos concretos e o unico que conhece temporizacao de enquadramento.
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_system.h>

#include "board_pins.h"
#include "core/console.h"
#include "core/sensor_ctx.h"
#include "drivers/ext_wdt.h"
#include "drivers/rs485.h"
#include "drivers/scl3300.h"
#include "drivers/spi_bus.h"
#include "platform/serial_sensor_io.h"
#include "proto/jig_slave.h"
#include "proto/modbus_slave.h"
#include "sensor_map.h"
#include "status.h"
#include "tilt.h"

namespace {

constexpr uint16_t kRxCapacity = 256;
constexpr uint32_t kTiltPeriodMs = 10;
constexpr uint32_t kLedPeriodMs = 500;
constexpr uint32_t kReadPollMs = 2;
constexpr uint32_t kMinGapUs = 750;
constexpr uint16_t kFwMajor = 0;
constexpr uint16_t kFwMinor = 1;

SPIClass g_sclSpi(VSPI);
SpiBus g_sclBus(g_sclSpi, board::kSclSclk, board::kSclMiso, board::kSclMosi, "VSPI/SCL3300");

ExtWatchdog g_wdt;
Scl3300 g_tilt(g_sclBus, board::kSclCs);
Rs485Transport g_link;
JigFrameSlave g_jigSlave;
ModbusRtuSlave g_modbusSlave(board::kModbusSlaveId);
// PADRAO DE BOOT = MODBUS RTU, que e o protocolo do PRODUTO: e o que a Unidade Remota
// DE-PURI-DI261924 fala em operacao (FC03, id 1, 8 registradores de sensor_map.h, 19200 8N1).
// O quadro do jig existe so para o firmware de teste de fabrica da supervisora, no item t3, e
// entra por comando de console. O padrao anterior era o jig, e o efeito era este: sensora e
// Unidade Remota subiam falando idiomas diferentes e a UR mostrava falha de comunicacao com o
// cabo perfeito. "proto" e ponteiro em RAM e nao sobrevive ao ciclo de energia, entao trocar
// pelo console resolvia ate a proxima energizacao - o padrao e que tinha de mudar.
ISlaveProtocol* g_activeProtocol = &g_modbusSlave;
SerialSensorIO g_io;

uint16_t g_registers[sensormap::kRegCount];

uint8_t g_rx[kRxCapacity];
uint16_t g_rxLen = 0;
uint32_t g_lastByteUs = 0;
uint32_t g_lastTiltMs = 0;
uint32_t g_lastLedMs = 0;
bool g_ledOn = false;

SensorCtx g_ctx{g_io,
                g_tilt,
                g_link,
                g_wdt,
                &g_activeProtocol,
                &g_jigSlave,
                &g_modbusSlave,
                g_registers,
                sensormap::kRegCount,
                FW_VERSION,
                BOARD_REV};

SensorConsole g_console(g_ctx);

void publishTilt(const Tilt& tilt, uint32_t uptimeS) {
    g_registers[sensormap::kRegAngleX] = static_cast<uint16_t>(tilt.xDeci);
    g_registers[sensormap::kRegAngleY] = static_cast<uint16_t>(tilt.yDeci);
    g_registers[sensormap::kRegAngleZ] = static_cast<uint16_t>(tilt.zDeci);
    g_registers[sensormap::kRegStatus] = tilt.status;
    g_registers[sensormap::kRegTempDeciC] = static_cast<uint16_t>(tilt.tempDeciC);
    g_registers[sensormap::kRegWhoAmI] = g_tilt.whoAmI();
    g_registers[sensormap::kRegFwVersion] = static_cast<uint16_t>((kFwMajor << 8) | kFwMinor);
    g_registers[sensormap::kRegUptimeS] = static_cast<uint16_t>(uptimeS & 0xFFFFu);
}

uint32_t interFrameGapUs() {
    const uint32_t gap = (g_link.charTimeUs() * 7u) / 2u;
    return (gap < kMinGapUs) ? kMinGapUs : gap;
}

void serviceLink() {
    uint8_t chunk[64];
    const uint16_t got = g_link.read(chunk, sizeof(chunk), kReadPollMs);
    if (got > 0) {
        for (uint16_t i = 0; i < got; ++i) {
            if (g_rxLen < kRxCapacity) {
                g_rx[g_rxLen++] = chunk[i];
            }
        }
        g_lastByteUs = micros();
        return;
    }
    if (g_rxLen == 0) {
        return;
    }
    if ((micros() - g_lastByteUs) < interFrameGapUs()) {
        return;
    }

    uint8_t response[kRxCapacity];
    ISlaveProtocol* const protocol = g_activeProtocol;
    const uint16_t replyLen =
        protocol->handle(g_rx, g_rxLen, g_registers, sensormap::kRegCount, response, sizeof(response));
    g_rxLen = 0;
    if (replyLen > 0) {
        g_link.write(response, replyLen);
        g_link.noteFrameOk();
    }
}

}  // namespace

void setup() {
    const Status wdtStatus = g_wdt.begin();

    pinMode(static_cast<uint8_t>(board::kStatusLed), OUTPUT);
    digitalWrite(static_cast<uint8_t>(board::kStatusLed), LOW);

    WiFi.mode(WIFI_OFF);
    btStop();

    g_io.begin();
    if (wdtStatus.failed()) {
        g_io.printf("ALERTA: timer de chute do watchdog nao subiu (%s): reset a cada ~%u ms\r\n",
                    errName(wdtStatus.err), static_cast<unsigned>(board::kWdtTypTimeoutMs));
    }

    for (uint16_t i = 0; i < sensormap::kRegCount; ++i) {
        g_registers[i] = 0;
    }

    g_sclBus.begin();
    const Status tiltStatus = g_tilt.begin();
    if (tiltStatus.failed()) {
        g_io.printf("ALERTA: SCL3300 nao inicializou (%s): use 'status' e 'reinit'\r\n", errName(tiltStatus.err));
    }

    const Status linkStatus = g_link.begin(board::kRs485DefaultBaud, 8, 'N', 1);
    if (linkStatus.failed()) {
        g_io.printf("ALERTA: RS-485 nao subiu (%s)\r\n", errName(linkStatus.err));
    }

    g_console.begin();
}

void loop() {
    const uint32_t nowMs = millis();

    if ((nowMs - g_lastTiltMs) >= kTiltPeriodMs) {
        g_lastTiltMs = nowMs;
        // PUBLICA SEMPRE, e nao so na leitura boa.
        //
        // Scl3300::read() preenche o Tilt e monta o status completo em TODOS os caminhos - ate no
        // "nao inicializado", que ja sai como kStsSclNotResponding - e kStsDataValid so entra
        // quando a leitura vale. Descartar o resultado da leitura reprovada tinha dois efeitos, e
        // os dois apareceram em bancada:
        //
        // 1. O angulo NUNCA chegava ao mestre. A sensora tinha o numero (o console 'angle'
        //    mostrava 49,5 graus) e os registradores do RS-485 ficavam zerados. A Unidade Remota
        //    nao pode exibir, nem marcado pela Emenda 2, aquilo que nunca recebe.
        // 2. O status virava ambiguo: o kStsDataValid de uma publicacao ANTERIOR ficava no
        //    registrador e o ramo de falha so acrescentava kStsSclNotResponding por cima,
        //    produzindo 0x0011 - DATA_VALID e SCL_NOT_RESPONDING juntos. E a pendencia P3 de
        //    docs/protocolo-rs485.md, e um mestre que teste por mascara de bit aceitaria angulo
        //    velho de sensor morto indefinidamente. Reescrever a palavra inteira a cada ciclo
        //    fecha isso: nao existe mais estado em que os dois bits coexistam.
        //
        // O valor publicado numa leitura reprovada e diagnostico, nao decisao: quem consome
        // decide pelo status, e o mestre deste produto exige status == 0x0001 exato.
        Tilt tilt = {0, 0, 0, 0, 0, false};
        g_tilt.read(tilt);
        publishTilt(tilt, nowMs / 1000u);
    }

    serviceLink();
    g_console.poll();

    if ((nowMs - g_lastLedMs) >= kLedPeriodMs) {
        g_lastLedMs = nowMs;
        const bool valid = (g_registers[sensormap::kRegStatus] & kStsDataValid) != 0;
        g_ledOn = valid ? !g_ledOn : true;
        digitalWrite(static_cast<uint8_t>(board::kStatusLed), g_ledOn ? HIGH : LOW);
    }
}
