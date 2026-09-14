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

SPIClass g_sclSpi(VSPI);
SpiBus g_sclBus(g_sclSpi, board::kSclSclk, board::kSclMiso, board::kSclMosi, "VSPI/SCL3300");

ExtWatchdog g_wdt;
// MODO DE OPERACAO DO SCL3300: MODO 1.
//
// REVERTIDO DO MODO 3 EM 2026-09-14, EM BANCADA, e o motivo tem de ficar escrito porque o erro
// foi meu e o argumento que o produziu parecia bom.
//
// O modo 3 foi escolhido em 2026-09-01 para fugir do fundo de escala de +-1,2 g do modo 1, que
// um impacto de carga de portico ultrapassa. O argumento usado foi verdadeiro: a Tabela 12 do
// datasheet da 182 LSB por grau na saida de INCLINACAO nos QUATRO modos, entao trocar de modo
// nao custa resolucao angular.
//
// O que esse argumento NAO cobria esta na secao 2.11.1, pagina 16, que a Tabela 12 nao
// referencia:
//
//   "Inclination ranges are limited in Mode 3 and Mode 4 to maximum +-10 degrees inclination.
//    ... If the whole 360 degrees operation is needed, then one should select either Mode 1 or
//    Mode 2 where the limitations regarding the maximum inclination angle don't exist."
//
// Este produto atua em +-90,0 graus (Tabela 2). Nos modos de inclinacao, passar de 10 graus
// levanta o bit SAT, e por 6.3 "all acceleration, inclination, and STO output data is invalid":
// a leitura inteira e recusada, os quatro reles vao a alarme por A5 e o equipamento para. Foi
// exatamente o que a bancada mostrou, com o eixo Y em 56 graus.
//
// O static_assert abaixo impede que isto se repita: ele amarra o modo escolhido a faixa de
// atuacao do produto, e qualquer volta aos modos 3 ou 4 QUEBRA A COMPILACAO em vez de reprovar
// no cais.
//
// A PREOCUPACAO ORIGINAL CONTINUA DE PE e agora tem duas saidas legitimas, as duas sem custo de
// resolucao angular:
//   modo 1 - fundo de escala +-1,2 g, filtro de 40 Hz  (em vigor)
//   modo 2 - fundo de escala +-2,4 g, filtro de 70 Hz  (o dobro de folga de choque, mais ruido)
// Qual dos dois e MEDICAO M8: e ela que diz se o espectro real da estrutura passa de 1,2 g e se
// o ruido do filtro de 70 Hz cabe na histerese.
constexpr uint8_t kSclOperationMode = 1;

// Faixa de atuacao do produto, em decimos de grau (Tabela 2: limites de -90,0 a +90,0).
constexpr int16_t kProductMaxAngleDeci = 900;

static_assert(scl::modeMaxInclinationDeci(kSclOperationMode) >= kProductMaxAngleDeci,
              "datasheet 2.11.1: modos 3 e 4 limitam a inclinacao a +-10 graus; este produto "
              "atua em +-90,0 graus e por isso so cabe nos modos 1 ou 2");

Scl3300 g_tilt(g_sclBus, board::kSclCs, Scl3300::kSpiDefaultHz, kSclOperationMode);
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

// IDENTIDADE PUBLICADA NO BOOT, e nao so dentro de publishTilt(). Segunda metade da pendencia
// P5: os campos que NAO dependem de leitura - versao de firmware e WHOAMI - tem de estar no fio
// desde o primeiro quadro Modbus, porque uma sensora que nunca consegue ler o SCL3300 nunca
// chamaria publishTilt() e ficaria anunciando versao 0x0000 para sempre. E justamente na sensora
// doente que o mestre mais precisa saber com qual firmware esta falando.
void publishIdentity() {
    g_registers[sensormap::kRegFwVersion] = sensormap::kFwVersionReg;
    g_registers[sensormap::kRegWhoAmI] = g_tilt.whoAmI();
}

void publishTilt(const Tilt& tilt, uint32_t uptimeS) {
    g_registers[sensormap::kRegAngleX] = static_cast<uint16_t>(tilt.xDeci);
    g_registers[sensormap::kRegAngleY] = static_cast<uint16_t>(tilt.yDeci);
    g_registers[sensormap::kRegAngleZ] = static_cast<uint16_t>(tilt.zDeci);
    g_registers[sensormap::kRegStatus] = tilt.status;
    g_registers[sensormap::kRegTempDeciC] = static_cast<uint16_t>(tilt.tempDeciC);
    g_registers[sensormap::kRegWhoAmI] = g_tilt.whoAmI();
    g_registers[sensormap::kRegFwVersion] = sensormap::kFwVersionReg;
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

    // ANTES do RS-485 subir, e nao depois: assim o PRIMEIRO quadro que o mestre conseguir ler ja
    // traz a versao de firmware desta sensora. Vale sobretudo quando o g_tilt.begin() acima
    // falhou - e exatamente na sensora doente que o mestre mais precisa saber com qual firmware
    // esta falando, e ela nunca chamaria publishTilt().
    publishIdentity();

    const Status linkStatus = g_link.begin(board::kRs485DefaultBaud, 8, 'N', 1);
    if (linkStatus.failed()) {
        g_io.printf("ALERTA: RS-485 nao subiu (%s)\r\n", errName(linkStatus.err));
    }

    g_console.begin();
}

void loop() {
    const uint32_t nowMs = millis();

    // TOKEN DE LIVENESS DO WATCHDOG (2026-09-14). Desde que o chute passou a sair de ISR em IRAM,
    // a ISR so pulsa o WDI enquanto este token estiver fresco. Chega a PRIMEIRA coisa do laco, e
    // de proposito: bater o token no fim significaria "o laco inteiro terminou", e um travamento
    // dentro de serviceLink() ou do console nunca renovaria - que e o comportamento desejado -,
    // mas bater no inicio prova o que este token tem de provar, que e o LACO estar rodando.
    // Vencido o prazo de 800 ms, a ISR para de pulsar e o STWD100 reseta a placa em 1,12 a 2,24 s.
    //
    // Sem esta linha a placa entra em BOOT LOOP: a carencia de boot da ISR vence em 3000 ms e o
    // cachorro morde, para sempre.
    g_wdt.heartbeat();

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
