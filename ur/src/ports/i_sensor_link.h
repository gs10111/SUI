// src/ports/i_sensor_link.h
// Link com a placa sensora PUSI-DI261930 (SI-DI141389XY): RS-485 half duplex,
// 19200 8N1, Modbus RTU, escravo 1, FC 0x03, start 0, quantidade 8.
// COMO O DOMINIO SABE QUE O LINK CAIU: nao e por ausencia de chamada e nao e por
// dado velho. Cada ciclo tem um RESULTADO EXPLICITO (LinkPoll). A porta so devolve
// Fresh quando uma resposta integra chegou DENTRO do timeout, e nunca reapresenta
// a amostra anterior - nao existe lastSample() nesta interface, de proposito. O
// dominio (LinkSupervisor, puro) aplica a regra de aceitacao sobre a amostra e
// conta ciclos consecutivos: 3 reprovados declaram falha, 10 aprovados restabelecem.
// A porta valida TRANSPORTE (prazo, endereco, funcao, byte count, CRC). A porta
// NAO julga o conteudo: quem exige status == 0x0001 exato, heartbeat avancado e
// |angulo| <= 900 e o dominio, porque isso e regra de seguranca do produto e tem
// de ser testavel em env:native sem nenhum fio.
// Alvo: ModbusRtuLink (src/proto/modbus_rtu.cpp) sobre Rs485Transport.
// Fake: FakeSensorLink (test/native) - roteiro de respostas, inclusive os casos
//       traicoeiros reais: angulo congelado com status 0x0011, selftest latchado
//       0x0009, heartbeat parado com CRC bom, e silencio total.
// REQ:  MAN-2.1-L27/L34 (sensor remoto ate 500 m, RS485), MAN-4-L58, MAN-5.5-L130,
//       MAN-7-L296..299 (falha de comunicacao), decisao 7, decisao 8, decisao 11.
#pragma once

#include <stdint.h>

#include "status.h"

// Bits do registrador 3 publicado pela sensora (contrato de fio congelado).
constexpr uint16_t kStsDataValid = 0x0001;
constexpr uint16_t kStsSclCrcError = 0x0002;
constexpr uint16_t kStsSclStartup = 0x0004;
constexpr uint16_t kStsSclSelfTestFail = 0x0008;
constexpr uint16_t kStsSclNotResponding = 0x0010;
constexpr uint16_t kStsSaturated = 0x0020;
constexpr uint16_t kStsWdtReset = 0x0040;

// Unico valor de status aceitavel. Mascarar kStsDataValid NAO serve: 0x0011
// (angulo congelado) e 0x0009 (selftest reprovado latchado) tambem contem o bit.
constexpr uint16_t kStsAcceptedExact = kStsDataValid;

// Faixa mecanica valida da leitura crua, em decimos de grau.
constexpr int16_t kAngleDeciMin = -900;
constexpr int16_t kAngleDeciMax = 900;

struct SensorSample {
    int16_t xDeci;      // reg 0, decimos de grau, com sinal
    int16_t yDeci;      // reg 1
    int16_t zDeci;      // reg 2, diagnostico; nao decide rele
    uint16_t status;    // reg 3, bitfield cru, sem interpretacao
    int16_t tempDeciC;  // reg 4, decimos de grau Celsius
    uint16_t whoAmI;    // reg 5, 0x00C1 quando o SCL3300 respondeu
    uint16_t fwVersion; // reg 6, (major << 8) | minor
    uint16_t heartbeat; // reg 7, avanca a cada ciclo da sensora; envolve em 2^16
    uint32_t atMs;      // instante do ultimo byte da resposta (base IClock)
};

enum class LinkPoll : uint8_t {
    Idle = 0,   // nenhuma transacao em curso; request() ainda nao foi chamado
    Busy,       // transacao em curso, resposta incompleta: nao e falha nem sucesso
    Fresh,      // resposta integra e dentro do prazo; 'out' preenchido
    Timeout,    // nenhuma resposta completa dentro de timeoutMs()
    BadFrame,   // CRC, endereco, funcao, byte count, comprimento ou excecao Modbus
};

struct LinkStats {
    uint32_t requests;
    uint32_t fresh;
    uint32_t timeouts;
    uint32_t crcErrors;
    uint32_t framingErrors;   // endereco/funcao/byte count/comprimento
    uint32_t exceptions;      // resposta de excecao Modbus (func | 0x80)
    uint32_t bytesRx;
    uint32_t bytesTx;
    uint32_t lastTurnaroundUs;
};

class ISensorLink {
public:
    virtual ~ISensorLink() = default;
    ISensorLink(const ISensorLink&) = delete;
    ISensorLink& operator=(const ISensorLink&) = delete;

    virtual Status begin() = 0;

    // Emite UMA transacao. Err::Busy se a anterior ainda nao terminou - a porta
    // nunca enfileira pedidos e nunca sobrepoe transacoes no barramento.
    virtual Status request() = 0;

    // Avanca a maquina de estados sem bloquear e devolve o resultado do ciclo.
    // 'out' so e tocado quando o retorno e Fresh. Cada request() produz no maximo
    // um Fresh; depois disso o retorno volta a Idle ate o proximo request().
    virtual LinkPoll poll(SensorSample& out) = 0;

    // Cancela a transacao pendente e limpa o buffer de recepcao (troca de modo,
    // reconfiguracao). Depois disto o proximo poll() devolve Idle.
    virtual void abort() = 0;

    virtual bool busy() const = 0;
    virtual uint32_t timeoutMs() const = 0;   // 30 ms
    virtual uint32_t baud() const = 0;        // 19200
    virtual uint8_t slaveAddress() const = 0; // 1
    virtual const char* protocolName() const = 0;

    virtual const LinkStats& stats() const = 0;
    virtual void resetStats() = 0;

protected:
    ISensorLink() = default;
};
