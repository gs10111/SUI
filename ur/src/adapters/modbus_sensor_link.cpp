// ModbusSensorLink: UART2 do U1 ESP32-WROOM-32D em RS-485 half-duplex (folha 1/2, TX IO17,
// RX IO16, DE/RE IO14 pelo RTS do periferico), Modbus RTU FC 0x03 de 8 registradores.
// Enquadramento copiado do mestre ja validado em bancada (src/proto/modbus_rtu.cpp:
// push/dropOne/crcOk/consume, inclusive o noteBad + dropOne que CONTINUA consumindo depois de
// um quadro ruim) e do transporte de src/drivers/rs485.cpp (install() com uart_set_mode
// RS485_HALF_DUPLEX), corrigido no que a porta exige de diferente: 8 registradores em vez de
// 2, buffer de 32 bytes em vez de 16 (a resposta de 21 bytes nao cabia - pendencia P2 de
// docs/protocolo-rs485.md), poll NAO bloqueante em vez de leitura com timeout embutido, e um
// veredito explicito por transacao (LinkPoll) em vez de "ultimo angulo" cacheado.
// Base de tempo da DECISIONS.md secao 2.1: prazo de 35 ms contado do PRIMEIRO byte transmitido
// (DECISIONS.md:1846), medido pelo IClock injetado (mesma base do dominio), com aritmetica de
// wrap de i_clock.h. As pre-condicoes e o comportamento de bloqueio estao declarados no
// cabecalho e sao normativos para a tarefa ctrl da etapa 8.
#include "adapters/modbus_sensor_link.h"

#include <driver/uart.h>

#include "board_pins.h"
#include "proto/crc16.h"

namespace adapters {
namespace {

constexpr uart_port_t kPort = UART_NUM_2;

uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

uint8_t hiByte(uint16_t value) { return static_cast<uint8_t>((value >> 8) & 0xFFu); }

uint8_t loByte(uint16_t value) { return static_cast<uint8_t>(value & 0xFFu); }

}  // namespace

ModbusSensorLink::ModbusSensorLink(const IClock& clock)
    : clock_(clock),
      stats_{},
      txStartUs_(0),
      sentAtMs_(0),
      lastRxMs_(0),
      lastRxUs_(0),
      rxLen_(0),
      lastException_(0),
      installed_(false),
      waiting_(false),
      failPending_(false),
      sawBad_(false),
      addrBad_(false),
      rx_{} {}

ModbusSensorLink::~ModbusSensorLink() {
    if (installed_ || uart_is_driver_installed(kPort)) {
        uart_driver_delete(kPort);
    }
    installed_ = false;
}

Status ModbusSensorLink::begin() {
    // Idempotente e sem heap novo: a partir da segunda chamada so re-arma o estado. Um segundo
    // uart_driver_delete/install seria free/malloc depois do setup(), proibido pela regra dura.
    if (installed_) {
        uart_flush_input(kPort);
        waiting_ = false;
        failPending_ = false;
        sawBad_ = false;
        addrBad_ = false;
        rxLen_ = 0;
        lastException_ = 0;
        return kOk;
    }

    uart_config_t cfg = {};
    cfg.baud_rate = static_cast<int>(kBaud);
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
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
    // Fila de eventos desativada (0, nullptr): a unica alocacao e o anel de RX, feita no boot,
    // e a ISR fica afim ao core de quem chama - por isso a pre-condicao "tarefa ctrl, core 0".
    // Erro de enquadramento do periferico nao vira contador proprio de proposito - em
    // LinkStats framingErrors significa endereco/funcao/byte count/comprimento, como a porta
    // define; byte corrompido no fio aparece como crcErrors ou timeouts.
    if (uart_driver_install(kPort, static_cast<int>(kUartRxRingBytes), 0, 0, nullptr, 0) !=
        ESP_OK) {
        return Err::Io;
    }
    if (uart_set_mode(kPort, UART_MODE_RS485_HALF_DUPLEX) != ESP_OK) {
        uart_driver_delete(kPort);
        return Err::Io;
    }
    uart_flush_input(kPort);
    installed_ = true;
    waiting_ = false;
    failPending_ = false;
    sawBad_ = false;
    addrBad_ = false;
    rxLen_ = 0;
    lastException_ = 0;
    return kOk;
}

Status ModbusSensorLink::request() {
    if (!installed_) {
        return Err::NotInit;
    }
    // Busy tambem enquanto o veredito do ciclo anterior nao foi colhido: uma falha de TX
    // latchada e transacao NAO TERMINADA para efeito de contagem do dominio - abrir outro
    // ciclo por cima faria um request() sem veredito proprio.
    if (waiting_ || failPending_) {
        return Err::Busy;
    }

    // Silencio de enquadramento: o escravo so decide num poll vazio, com t3,5 de 1,82 ms
    // (docs/protocolo-rs485.md secao 8.1). O tick de 50 ms da 27x de folga, entao o que sobra
    // no anel aqui e ruido de linha aberta ou resto de transacao abortada: descarta.
    uart_flush_input(kPort);
    rxLen_ = 0;
    lastException_ = 0;
    sawBad_ = false;
    addrBad_ = false;

    uint8_t pdu[kRequestLen];
    pdu[0] = kSlaveId;
    pdu[1] = kFuncReadHolding;
    pdu[2] = hiByte(kStartReg);
    pdu[3] = loByte(kStartReg);
    pdu[4] = hiByte(kRegCount);
    pdu[5] = loByte(kRegCount);
    const uint16_t crc = crc16Modbus(pdu, kRequestLen - 2u);
    pdu[6] = loByte(crc);
    pdu[7] = hiByte(crc);

    // O prazo conta do PRIMEIRO byte transmitido (DECISIONS.md:1846): carimbar ANTES do write.
    // Nao ha uart_wait_tx_done - 8 bytes cabem no FIFO de 128 B do periferico, o DE e solto
    // pelo proprio hardware em UART_MODE_RS485_HALF_DUPLEX e o fim do pedido no fio e
    // deterministico (4,17 ms a 19200 8N1). Parar a tarefa ctrl para carimbar esse instante
    // custaria 4,2 ms tipicos e ate 25 ms no caso patologico, com rele, DAC e token do
    // watchdog parados junto.
    sentAtMs_ = clock_.nowMs();
    txStartUs_ = clock_.nowUs();
    lastRxMs_ = sentAtMs_;
    lastRxUs_ = txStartUs_;

    // Um ciclo comecou: contar o pedido ANTES de qualquer ramo de erro, senao ficam bytes
    // transmitidos com requests == 0.
    ++stats_.requests;

    const int sent = uart_write_bytes(kPort, pdu, kRequestLen);
    if (sent > 0) {
        stats_.bytesTx += static_cast<uint32_t>(sent);
    }
    if (sent != static_cast<int>(kRequestLen)) {
        // Falha de TX latchada: o ciclo TEM de terminar com veredito explicito, senao o
        // LinkSupervisor fica preso em Idle ("request() nem foi chamado") com o enlace morto.
        failPending_ = true;
        return Err::Io;
    }

    waiting_ = true;
    return kOk;
}

void ModbusSensorLink::drainRx() {
    if (rxLen_ >= kRxCap) {
        return;
    }
    const uint16_t space = static_cast<uint16_t>(kRxCap - rxLen_);
    const int got = uart_read_bytes(kPort, &rx_[rxLen_], space, 0);
    if (got <= 0) {
        return;
    }
    rxLen_ = static_cast<uint16_t>(rxLen_ + static_cast<uint16_t>(got));
    stats_.bytesRx += static_cast<uint32_t>(got);
    lastRxMs_ = clock_.nowMs();
    lastRxUs_ = clock_.nowUs();
}

void ModbusSensorLink::dropOne() {
    uint16_t start = 1;
    while (start < rxLen_ && rx_[start] != kSlaveId) {
        ++start;
    }
    if (start >= rxLen_) {
        rxLen_ = 0;
        return;
    }
    const uint16_t rest = static_cast<uint16_t>(rxLen_ - start);
    for (uint16_t i = 0; i < rest; ++i) {
        rx_[i] = rx_[i + start];
    }
    rxLen_ = rest;
}

bool ModbusSensorLink::crcOk(uint16_t total) const {
    if (total < 3 || total > kRxCap || total > rxLen_) {
        return false;
    }
    const uint16_t body = static_cast<uint16_t>(total - 2);
    const uint16_t want = crc16Modbus(rx_, body);
    const uint16_t got = static_cast<uint16_t>(static_cast<uint16_t>(rx_[body]) |
                                               static_cast<uint16_t>(rx_[body + 1] << 8));
    return want == got;
}

// Consome o anel ate achar UM quadro integro ou ficar sem bytes. Quadro ruim nao encerra a
// transacao: conta a estatistica, marca sawBad_, descarta um byte e CONTINUA - e o que faz o
// mestre ja validado em bancada (src/proto/modbus_rtu.cpp). Sem isso, um unico 0x01 de ruido
// de linha aberta (500 m de par trancado) mata um ciclo bom, e tres desses seguidos levam os
// quatro reles ao alarme com o sensor intacto.
LinkPoll ModbusSensorLink::consume(SensorSample& out) {
    for (;;) {
        if (rxLen_ > 0 && rx_[0] != kSlaveId) {
            // Resposta integra vinda do endereco errado (jumper trocado, dois escravos no
            // tronco, cabo no equipamento vizinho) nao pode sumir em silencio: "sensora muda" e
            // "sensora falando com outro endereco" sao dois reparos diferentes em campo.
            // Uma contagem por transacao, para nao inflar a estatistica com ruido byte a byte.
            if (!addrBad_) {
                addrBad_ = true;
                sawBad_ = true;
                ++stats_.framingErrors;
            }
            while (rxLen_ > 0 && rx_[0] != kSlaveId) {
                dropOne();
            }
        }
        if (rxLen_ < 2) {
            return LinkPoll::Busy;
        }

        const uint8_t fn = rx_[1];
        if (fn == static_cast<uint8_t>(kFuncReadHolding | kExceptionMask)) {
            if (rxLen_ < kExceptionLen) {
                return LinkPoll::Busy;
            }
            if (!crcOk(kExceptionLen)) {
                ++stats_.crcErrors;
                sawBad_ = true;
                dropOne();
                continue;
            }
            lastException_ = rx_[2];
            ++stats_.exceptions;
            sawBad_ = true;
            dropOne();
            continue;
        }
        if (fn != kFuncReadHolding) {
            ++stats_.framingErrors;
            sawBad_ = true;
            dropOne();
            continue;
        }
        if (rxLen_ < 3) {
            return LinkPoll::Busy;
        }
        // Pega tambem o eco do proprio pedido (DE preso ativo): 01 03 00 ... tem byte count
        // 0x00, diferente de 16.
        if (rx_[2] != kByteCount) {
            ++stats_.framingErrors;
            sawBad_ = true;
            dropOne();
            continue;
        }
        if (rxLen_ < kResponseLen) {
            return LinkPoll::Busy;
        }
        if (!crcOk(kResponseLen)) {
            ++stats_.crcErrors;
            sawBad_ = true;
            dropOne();
            continue;
        }

        // Quadro integro: so e Fresh se CHEGOU dentro do prazo. lastRxMs_ e o carimbo da
        // leitura que completou o quadro, entao o teste usa o instante de chegada e nao o
        // instante deste poll - Fresh sobre dado velho e enlace degradado passando por
        // saudavel no LinkSupervisor.
        // Fora do prazo, a PRECEDENCIA DO VEREDITO e a mesma do fim de prazo em poll(): se
        // algum quadro ja reprovou nesta transacao, o defeito e de enquadramento e sai
        // BadFrame; so a transacao limpa que estourou o prazo sai Timeout. Sem esta linha o
        // ruido seguido de resposta atrasada sairia como Timeout aqui e como BadFrame ali, e o
        // adaptador deixaria de ser substituivel pelo fake da porta, que promete BadFrame
        // sempre que a transacao viu quadro ruim (test/fakes/fake_sensor_link.h, ponto 4).
        if (deadlineReached(sentAtMs_, lastRxMs_, kTimeoutMs)) {
            if (sawBad_) {
                return LinkPoll::BadFrame;
            }
            ++stats_.timeouts;
            return LinkPoll::Timeout;
        }

        // Registradores big-endian, na ordem congelada de sensor/include/sensor_map.h. Os tres
        // angulos e a temperatura sao int16 COM SINAL: lidos como uint16, -45,0 graus viraria
        // +315,0 (docs/protocolo-rs485.md secao 6.1).
        out.xDeci = static_cast<int16_t>(be16(&rx_[3]));
        out.yDeci = static_cast<int16_t>(be16(&rx_[5]));
        out.zDeci = static_cast<int16_t>(be16(&rx_[7]));
        out.status = be16(&rx_[9]);
        out.tempDeciC = static_cast<int16_t>(be16(&rx_[11]));
        out.whoAmI = be16(&rx_[13]);
        out.fwVersion = be16(&rx_[15]);
        out.heartbeat = be16(&rx_[17]);
        out.atMs = lastRxMs_;

        stats_.lastTurnaroundUs = static_cast<uint32_t>(lastRxUs_ - txStartUs_);
        ++stats_.fresh;
        return LinkPoll::Fresh;
    }
}

void ModbusSensorLink::finish() {
    waiting_ = false;
    rxLen_ = 0;
    sawBad_ = false;
    addrBad_ = false;
}

LinkPoll ModbusSensorLink::poll(SensorSample& out) {
    // Falha de TX do request() anterior: um veredito por ciclo, como o fake entrega.
    if (failPending_) {
        failPending_ = false;
        ++stats_.timeouts;
        return LinkPoll::Timeout;
    }
    if (!waiting_) {
        return LinkPoll::Idle;
    }
    drainRx();
    const LinkPoll verdict = consume(out);
    if (verdict != LinkPoll::Busy) {
        finish();
        return verdict;
    }
    if (deadlineReached(sentAtMs_, clock_.nowMs(), kTimeoutMs)) {
        // Fim do prazo: se algum quadro reprovou no caminho, o defeito e de enquadramento e
        // nao de silencio. O preco de continuar consumindo depois de um quadro ruim e que o
        // BadFrame sai no fim dos 35 ms - cabe folgado no tick de 50 ms e nao muda a contagem
        // de um veredito por ciclo do dominio.
        const bool bad = sawBad_;
        finish();
        if (bad) {
            return LinkPoll::BadFrame;
        }
        ++stats_.timeouts;
        return LinkPoll::Timeout;
    }
    return LinkPoll::Busy;
}

void ModbusSensorLink::abort() {
    waiting_ = false;
    failPending_ = false;
    sawBad_ = false;
    addrBad_ = false;
    rxLen_ = 0;
    lastException_ = 0;
    if (installed_) {
        uart_flush_input(kPort);
    }
}

bool ModbusSensorLink::busy() const { return waiting_ || failPending_; }

uint32_t ModbusSensorLink::timeoutMs() const { return kTimeoutMs; }

uint32_t ModbusSensorLink::baud() const { return kBaud; }

uint8_t ModbusSensorLink::slaveAddress() const { return kSlaveId; }

const char* ModbusSensorLink::protocolName() const { return "modbus-rtu"; }

const LinkStats& ModbusSensorLink::stats() const { return stats_; }

void ModbusSensorLink::resetStats() {
    stats_ = LinkStats{};
    lastException_ = 0;
}

}  // namespace adapters
