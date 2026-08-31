// Enlace de teste: o roteiro de respostas da sensora e escrito pelo teste, o tempo vem do
// FakeClock. Sem fio, sem UART, sem CRC de verdade - o que este fake reproduz e a MAQUINA DE
// VEREDITOS de ports/i_sensor_link.h, que e o que o dominio (LinkSupervisor) enxerga.
//
// SUBSTITUIVEL pelo adaptador real (src/adapters/modbus_sensor_link.h) sem que o dominio
// perceba, e o inverso tambem: um fake mais generoso que o alvo faz a suite mentir - o
// LinkSupervisor passaria no host e reprovaria na placa (ou pior, o contrario: enlace
// degradado passando por saudavel, com os quatro reles no estado normal).
// Os pontos em que os dois TEM de coincidir, e que os testes de contrato prendem:
//  1. Fresh so quando a resposta integra CHEGOU dentro de timeoutMs(). Quadro integro que
//     aterrissa em 36 ms sai como Timeout, nao como Fresh - o prazo e medido sobre o instante
//     de chegada, nao sobre o instante do poll.
//  2. O prazo conta do PRIMEIRO byte transmitido (DECISIONS.md:1846), carimbado dentro do
//     request(). timeoutMs() = 35 ms = urbase::kLinkTimeoutMs. O cabecalho da porta e
//     docs/protocolo-rs485.md 8.3 ainda dizem 30 ms: divergencia aberta, decisao do bigboss;
//     fake e adaptador seguem a base comum JUNTOS, para nao existirem duas verdades na suite.
//  3. UM veredito por request(). Falha de transmissao NAO devolve Idle: ela e latchada e o
//     poll() seguinte devolve Timeout. Idle significa "request() nem foi chamado".
//  4. Quadro ruim nao encerra a transacao: conta a estatistica, marca a transacao como suja e
//     CONTINUA ate o fim do prazo, quando sai BadFrame. Assim um byte de ruido nao mata um
//     ciclo bom, e o veredito ruim sai no mesmo tick que o Timeout sairia.
//  5. request() nao bloqueia e nao enfileira: Err::Busy enquanto houver transacao em curso ou
//     veredito nao colhido; Err::NotInit antes de begin(); begin() idempotente.
//  6. 'out' so e tocado quando o retorno e Fresh, e nao existe lastSample(): o fake nunca
//     reapresenta a amostra anterior, exatamente como a porta exige.
//
// O que o fake NAO faz, de proposito: julgar conteudo. status == 0x0001 exato, heartbeat
// parado e |angulo| <= 900 sao regra de seguranca do produto e moram no dominio. Por isso o
// roteiro permite escrever justamente os casos traicoeiros reais - angulo congelado com
// status 0x0011, selftest latchado 0x0009, heartbeat parado com quadro integro - e o fake os
// entrega como Fresh, porque no fio eles SAO transporte valido.
#pragma once

#include <stdint.h>

#include "ports/i_clock.h"
#include "ports/i_sensor_link.h"
#include "status.h"

namespace test {

class FakeSensorLink : public ISensorLink {
public:
    // Mesmos numeros do alvo (src/adapters/modbus_sensor_link.h).
    static constexpr uint32_t kTimeoutMs = 35;
    static constexpr uint32_t kBaudRate = 19200;
    static constexpr uint8_t kSlaveId = 1;
    static constexpr uint8_t kRequestLen = 8;
    static constexpr uint8_t kResponseLen = 21;

    // Como o roteiro reprova o quadro. Cada um cai num contador diferente de LinkStats, do
    // mesmo jeito que o adaptador real classifica: CRC no crcErrors, endereco/funcao/byte
    // count/comprimento no framingErrors, resposta de excecao no exceptions.
    enum class Bad : uint8_t { Crc, Framing, Exception };

    explicit FakeSensorLink(const IClock& clock)
        : clock_(clock),
          stats_{},
          scripted_{},
          sentAtMs_(0),
          sentAtUs_(0),
          replyAfterMs_(0),
          exceptionCode_(0),
          lastException_(0),
          kind_(Kind::Silence),
          begun_(false),
          beginFails_(false),
          waiting_(false),
          delivered_(false),
          failPending_(false),
          failNextRequest_(false),
          sawBad_(false) {}

    // --- roteiro do teste ---

    // Amostra integra que chega afterMs depois do primeiro byte transmitido. Vira Fresh se
    // afterMs < timeoutMs() e Timeout se nao - o mesmo criterio do alvo.
    void replySample(const SensorSample& sample, uint32_t afterMs) {
        scripted_ = sample;
        replyAfterMs_ = afterMs;
        kind_ = Kind::Sample;
    }

    // Quadro reprovado no transporte, chegando afterMs depois do inicio do pedido.
    void replyBadFrame(Bad how, uint32_t afterMs, uint8_t exceptionCode = 0x02) {
        replyAfterMs_ = afterMs;
        exceptionCode_ = exceptionCode;
        switch (how) {
            case Bad::Crc: kind_ = Kind::BadCrc; break;
            case Bad::Framing: kind_ = Kind::BadFraming; break;
            case Bad::Exception: kind_ = Kind::BadException; break;
        }
    }

    // Sensora muda (desligada, cabo rompido, endereco errado sem responder, P1 do contrato
    // aberta: a sensora boota falando o quadro do jig e nao Modbus).
    void replySilence() {
        kind_ = Kind::Silence;
        replyAfterMs_ = 0;
    }

    // O periferico recusa a transmissao no proximo request(): Err::Io com veredito latchado.
    void failNextRequest() { failNextRequest_ = true; }

    // begin() reprova (driver nao instalou).
    void failBegin(bool fails) { beginFails_ = fails; }

    // Amostra de transporte valido e conteudo saudavel, ponto de partida do roteiro. O teste
    // do dominio estraga o campo que quiser: status = 0x0011 (angulo congelado), 0x0009
    // (selftest latchado), heartbeat repetido, |angulo| > 900.
    static SensorSample goodSample(int16_t xDeci, int16_t yDeci, uint16_t heartbeat) {
        SensorSample sample{};
        sample.xDeci = xDeci;
        sample.yDeci = yDeci;
        sample.zDeci = 0;
        sample.status = kStsDataValid;
        sample.tempDeciC = 250;
        sample.whoAmI = 0x00C1;
        sample.fwVersion = 0x0002;
        sample.heartbeat = heartbeat;
        sample.atMs = 0;
        return sample;
    }

    uint8_t lastException() const { return lastException_; }

    // --- porta ---

    Status begin() override {
        if (beginFails_) {
            return Err::Io;
        }
        // Idempotente e sem alocar nada, como o alvo: a segunda chamada so re-arma o estado.
        begun_ = true;
        waiting_ = false;
        delivered_ = false;
        failPending_ = false;
        sawBad_ = false;
        lastException_ = 0;
        return kOk;
    }

    Status request() override {
        if (!begun_) {
            return Err::NotInit;
        }
        if (waiting_ || failPending_) {
            return Err::Busy;
        }

        sentAtMs_ = clock_.nowMs();
        sentAtUs_ = clock_.nowUs();
        delivered_ = false;
        sawBad_ = false;
        lastException_ = 0;
        ++stats_.requests;

        if (failNextRequest_) {
            failNextRequest_ = false;
            failPending_ = true;  // o ciclo ainda deve um veredito ao dominio
            return Err::Io;
        }

        stats_.bytesTx += kRequestLen;
        waiting_ = true;
        return kOk;
    }

    LinkPoll poll(SensorSample& out) override {
        if (failPending_) {
            failPending_ = false;
            ++stats_.timeouts;
            return LinkPoll::Timeout;
        }
        if (!waiting_) {
            return LinkPoll::Idle;
        }

        const uint32_t nowMs = clock_.nowMs();
        if (!delivered_ && kind_ != Kind::Silence &&
            elapsedMs(sentAtMs_, nowMs) >= replyAfterMs_) {
            delivered_ = true;
            stats_.bytesRx += kResponseLen;
            // O instante de chegada visivel e o do poll que observou o quadro - limite
            // superior do instante do ultimo byte, exatamente como no alvo, onde o carimbo sai
            // da leitura do anel da UART.
            switch (kind_) {
                case Kind::Sample:
                    if (deadlineReached(sentAtMs_, nowMs, kTimeoutMs)) {
                        ++stats_.timeouts;
                        finish();
                        return LinkPoll::Timeout;
                    }
                    out = scripted_;
                    out.atMs = nowMs;
                    stats_.lastTurnaroundUs = static_cast<uint32_t>(clock_.nowUs() - sentAtUs_);
                    ++stats_.fresh;
                    finish();
                    return LinkPoll::Fresh;
                case Kind::BadCrc:
                    ++stats_.crcErrors;
                    sawBad_ = true;
                    break;
                case Kind::BadFraming:
                    ++stats_.framingErrors;
                    sawBad_ = true;
                    break;
                case Kind::BadException:
                    ++stats_.exceptions;
                    lastException_ = exceptionCode_;
                    sawBad_ = true;
                    break;
                case Kind::Silence:
                    break;
            }
        }

        if (deadlineReached(sentAtMs_, nowMs, kTimeoutMs)) {
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

    void abort() override {
        waiting_ = false;
        delivered_ = false;
        failPending_ = false;
        sawBad_ = false;
        lastException_ = 0;
    }

    bool busy() const override { return waiting_ || failPending_; }
    uint32_t timeoutMs() const override { return kTimeoutMs; }
    uint32_t baud() const override { return kBaudRate; }
    uint8_t slaveAddress() const override { return kSlaveId; }
    const char* protocolName() const override { return "modbus-rtu"; }

    const LinkStats& stats() const override { return stats_; }
    void resetStats() override {
        stats_ = LinkStats{};
        lastException_ = 0;
    }

private:
    enum class Kind : uint8_t { Silence, Sample, BadCrc, BadFraming, BadException };

    void finish() {
        waiting_ = false;
        delivered_ = false;
        sawBad_ = false;
    }

    const IClock& clock_;
    LinkStats stats_;
    SensorSample scripted_;
    uint32_t sentAtMs_;
    uint32_t sentAtUs_;
    uint32_t replyAfterMs_;
    uint8_t exceptionCode_;
    uint8_t lastException_;
    Kind kind_;
    bool begun_;
    bool beginFails_;
    bool waiting_;
    bool delivered_;
    bool failPending_;
    bool failNextRequest_;
    bool sawBad_;
};

}  // namespace test
