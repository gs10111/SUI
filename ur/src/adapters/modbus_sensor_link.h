// ModbusSensorLink: mestre Modbus RTU da UR sobre o RS-485 da folha 1/2 (transceptor
// SN65HVD75DR em 3V3, DE e /RE unidos, TVS CDSOT23-SM712, terminador R2 de 120 ohm atras do
// jumper J7, sensora no CN2A..CN2D). Implementa ports/i_sensor_link.h e tem de ser
// substituivel pelo fake da porta (test/fakes/fake_sensor_link.h): mesmos codigos de LinkPoll,
// mesmos limites, mesma pre-condicao (begin() antes de request(), Err::Busy enquanto a
// transacao anterior nao terminar), e o MESMO instante em que cada veredito sai.
//
// Implementa a decisao 8 (contrato de fio de docs/protocolo-rs485.md, normativo) e a base
// comum da DECISIONS.md secao 2.1: uma unica transacao por tick de 50 ms, FC 0x03, escravo 1,
// start 0, quantidade 8 - exatamente sensor/include/sensor_map.h. Pendencia P2 do contrato
// fechada aqui: 8 registradores, resposta de 21 bytes, buffer de recepcao de 32 bytes (o
// mestre do firmware de teste, src/proto/modbus_rtu.h, le 2 registradores com kRxCap = 16 e
// descarta a resposta).
//
// PRAZO - REFERENCIA NORMATIVA. kTimeoutMs = 35 ms contados do INICIO DO PRIMEIRO BYTE
// TRANSMITIDO do pedido ate o ultimo byte recebido da resposta, exatamente como manda
// DECISIONS.md:1846 (e a mesma referencia da derivacao do pior caso de 21,3 ms, que ja inclui
// os 4,17 ms do pedido no fio). NAO contar do ultimo byte transmitido: isso alargaria o
// orcamento para 39,2 ms do inicio do pedido e reduziria a folga antes do proximo pedido dos
// 15 ms afirmados na base para ~10,8 ms.
// DIVERGENCIA ABERTA, DECISAO DE PRODUTO (escalar ao bigboss, NAO resolver aqui): a base comum
// diz 35 ms (DECISIONS.md:574 e :1846, urbase::kLinkTimeoutMs), enquanto
// ports/i_sensor_link.h:97 e docs/protocolo-rs485.md secao 8.3 dizem 30 ms. Este adaptador
// segue a base comum porque ela e quem manda no tempo, e o fake da porta foi escrito com o
// MESMO numero para nao mentir na suite. Quando a divergencia for fechada, porta, fake, doc e
// adaptador mudam JUNTOS - nunca so aqui. Do mesmo modo, kTimeoutMs, kRxCap e kBaud sao copias
// locais de urbase::*: assim que a base comum virar cabecalho unico (etapa 8), trocar por
// urbase::kLinkTimeoutMs / urbase::kModbusRxCapBytes / urbase::kRs485Baud, ou passam a existir
// duas verdades.
//
// PRE-CONDICAO DE BOOT (afinidade de core). begin() TEM de ser chamada de dentro da tarefa
// ctrl (core 0, prio 5), uma unica vez, no passo de boot do enlace: uart_driver_install()
// registra a ISR da UART2 NO CORE DA TAREFA QUE CHAMA, e DECISIONS.md secao 2.1 item 5 e
// explicita ("O driver da UART2 e instalado de dentro dela, para a ISR ficar afim ao mesmo
// core"). Chamada do setup() (loopTask, core 1) NAO devolve erro - a afinidade some em
// silencio e sobra jitter. begin() e idempotente: a segunda chamada em diante so re-arma o
// estado e esvazia o anel, sem uart_driver_delete/install, porque um segundo install seria
// free/malloc depois do setup() e a regra dura proibe heap depois do boot.
//
// BLOQUEIO. request() NAO bloqueia: escreve 8 bytes num FIFO de TX de 128 bytes do periferico
// (uart_driver_install com tx_buffer_size = 0 copia direto para o FIFO) e volta. Nao ha
// uart_wait_tx_done, nao ha delay, nao ha espera de fim de transmissao - a tarefa ctrl e dona
// exclusiva de rele, DAC e token do watchdog e nao pode parar 4,2 ms tipicos (25 ms no caso
// patologico) so para carimbar o fim do pedido. poll() tambem nao bloqueia: le o anel com
// timeout zero. Pior caso declarado de ambos: microssegundos de chamada de driver, nenhuma
// espera por hardware.
//
// GRANULARIDADE DO VEREDITO (contrato com a tarefa ctrl da etapa 8). O prazo e medido sobre o
// instante de CHEGADA, nao sobre o instante do poll: um quadro integro que chega depois dos
// 35 ms sai como Timeout, e nao como Fresh. Consequencia direta: a tarefa ctrl tem de chamar
// poll() em laco curto dentro do MESMO tick que chamou request() (o round-trip tipico e
// 17,9 ms, pior caso 21,3 ms, dentro do tick de 50 ms). Um unico poll por tick faz toda
// transacao ser lida ~50 ms depois do pedido e reprovar por prazo - o adaptador prefere isso a
// declarar Fresh sobre dado velho, porque a idade maxima do dado que comanda rele e 71,3 ms
// (DECISIONS.md secao 2.1 item 3).
//
// DIVERGENCIA REGISTRADA PARA O DONO DA PORTA (ports/ nao e deste adaptador): SensorSample.atMs
// e documentado na porta como "instante do ultimo byte da resposta", mas nenhum mestre sobre
// driver de UART com anel consegue esse instante - o que existe e o instante da LEITURA que
// completou o quadro. Aqui atMs = esse instante, que e LIMITE SUPERIOR do instante do ultimo
// byte (erro para mais, isto e, o dado parece mais novo do que e), com erro limitado por
// construcao a um periodo de poll e, no total, a kTimeoutMs - porque o mesmo instante e o que
// reprova por prazo. Se o dono da porta quiser o instante exato do fio, sao necessarios eventos
// de UART (UART_DATA/UART_BREAK) com carimbo na ISR, e isso muda a porta, o fake e o custo.
//
// FRONTEIRA: este adaptador valida TRANSPORTE - prazo, endereco, funcao, byte count,
// comprimento, CRC16-MODBUS e quadro de excecao - e conta estatistica. Ele NAO julga
// conteudo: status == 0x0001 exato, heartbeat estagnado e |angulo| <= 900 sao regra de
// seguranca do produto e moram no dominio, testados em env:native sem nenhum fio.
//
// DE/RE: chaveado pelo periferico UART2 em UART_MODE_RS485_HALF_DUPLEX (pino RTS = IO14 na
// UR, board::kRs485De), nunca por software - a 19200 8N1 o turnaround por GPIO colidiria com
// o primeiro byte da resposta. Sem eco local.
#pragma once

#include <stdint.h>

#include "ports/i_clock.h"
#include "ports/i_sensor_link.h"
#include "status.h"

namespace adapters {

class ModbusSensorLink final : public ISensorLink {
public:
    static constexpr uint32_t kBaud = 19200;          // board::kRs485DefaultBaud
    static constexpr uint8_t kSlaveId = 1;            // docs/protocolo-rs485.md secao 4
    static constexpr uint8_t kFuncReadHolding = 0x03; // a UR usa 0x03; 0x04 e alias no escravo
    static constexpr uint8_t kExceptionMask = 0x80;
    static constexpr uint16_t kStartReg = 0;          // sensormap::kRegAngleX
    static constexpr uint16_t kRegCount = 8;          // sensormap::kRegCount
    static constexpr uint8_t kRequestLen = 8;         // addr fn startHi startLo cntHi cntLo crcLo crcHi
    static constexpr uint8_t kByteCount = 2 * kRegCount;                  // 16
    static constexpr uint8_t kResponseLen = 3 + kByteCount + 2;           // 21
    static constexpr uint8_t kExceptionLen = 5;
    static constexpr uint16_t kRxCap = 32;            // urbase::kModbusRxCapBytes
    static constexpr uint32_t kTimeoutMs = 35;        // urbase::kLinkTimeoutMs, do 1o byte TX
    static constexpr uint16_t kUartRxRingBytes = 512;

    explicit ModbusSensorLink(const IClock& clock);
    ~ModbusSensorLink() override;

    // Instala a UART2. Chamar de dentro da tarefa ctrl (core 0) - ver PRE-CONDICAO DE BOOT.
    // Idempotente e sem heap novo a partir da segunda chamada.
    Status begin() override;

    // Nao bloqueia. Err::NotInit sem begin(); Err::Busy enquanto houver transacao em curso OU
    // veredito de ciclo ainda nao colhido por poll(); Err::Io se o periferico nao aceitou os
    // 8 bytes - e nesse caso o ciclo AINDA TEM VEREDITO: o proximo poll() devolve Timeout,
    // nunca Idle (falha de TX nao pode virar "nada aconteceu" para o LinkSupervisor). Todo
    // request() que chega a mexer no barramento conta em stats().requests, inclusive o que
    // termina em Err::Io - bytesTx > 0 com requests == 0 seria estatistica incoerente.
    Status request() override;

    // Nao bloqueia. Fresh so com quadro integro CHEGADO dentro de kTimeoutMs; BadFrame e
    // Timeout saem no fim do prazo, um veredito por request(). Precedencia unica no fim do
    // prazo: transacao que viu QUALQUER quadro reprovado sai BadFrame (mesmo que a resposta
    // integra tenha aterrissado depois do prazo); so a transacao limpa sai Timeout.
    LinkPoll poll(SensorSample& out) override;
    void abort() override;

    // Verdadeiro enquanto o ciclo corrente nao entregou veredito - inclui a falha de TX
    // latchada, que ainda deve um Timeout ao dominio.
    bool busy() const override;
    uint32_t timeoutMs() const override;
    uint32_t baud() const override;
    uint8_t slaveAddress() const override;
    const char* protocolName() const override;

    // stats().lastTurnaroundUs = round-trip COMPLETO, do primeiro byte transmitido do pedido
    // ate a leitura que completou a resposta (mesma referencia do prazo e da medicao M4:
    // 17,9 ms tipico, 21,3 ms pior caso). NAO e a latencia do escravo (2,05 a 4,55 ms), que so
    // sai do osciloscopio no DE das duas placas.
    const LinkStats& stats() const override;
    void resetStats() override;

    // Diagnostico de bancada (comando de console): codigo da ultima excecao Modbus recebida,
    // 0 quando nenhuma. Uma excecao em regime e erro de programacao do mestre, nao defeito de
    // campo (docs/protocolo-rs485.md secao 9.2).
    uint8_t lastException() const { return lastException_; }
    bool installed() const { return installed_; }

private:
    void drainRx();
    LinkPoll consume(SensorSample& out);
    void dropOne();
    bool crcOk(uint16_t total) const;
    void finish();

    const IClock& clock_;
    LinkStats stats_;
    uint32_t txStartUs_;   // carimbo do PRIMEIRO byte transmitido (base do turnaround)
    uint32_t sentAtMs_;    // idem em ms; e a origem do prazo de kTimeoutMs
    uint32_t lastRxMs_;    // leitura que trouxe o ultimo byte do anel; limite superior de atMs
    uint32_t lastRxUs_;
    uint16_t rxLen_;
    uint8_t lastException_;
    bool installed_;
    bool waiting_;
    bool failPending_;     // falha de TX latchada: o proximo poll() devolve Timeout
    bool sawBad_;          // houve quadro reprovado nesta transacao: veredito de fim = BadFrame
    bool addrBad_;         // ja contei um descarte por endereco nesta transacao
    uint8_t rx_[kRxCap];
};

}  // namespace adapters
