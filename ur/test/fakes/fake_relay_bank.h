// Banco de reles de teste: guarda a mascara comandada e o HISTORICO DE TRANSICOES com carimbo de
// tempo, como o cabecalho de ports/i_relay_bank.h anuncia, para que o dominio possa afirmar
// latencia ("o rele sinalizou dentro de N ms do quadro invalido") e permanencia ("continuou
// sinalizado por 3000 ms") sem esperar por elas.
//
// Este fake existe para PRENDER a semantica que o adaptador real (src/adapters/relay_bank_gpio.cpp)
// entrega. Um fake mais generoso que o alvo faz a suite inteira mentir: o dominio passa no host e
// encontra outro codigo de erro na placa. Os pontos presos aqui, um a um, sao os do alvo:
//
//  1. UMA transicao por applyMask(). A porta promete escrita ATOMICA dos quatro canais, e o
//     adaptador cumpre escrevendo registrador de GPIO (dois bancos, ate quatro stores, skew de
//     dezenas de ns) em vez de quatro digitalWrite(). Se o alvo voltar a escrever canal a canal,
//     este fake passa a mentir - e o teste de contrato que o acompanha e que denuncia.
//  2. Mascara com bit acima de 0x0F -> Err::Param (nao Range).
//  3. applyMask()/set() antes de begin() -> Err::NotInit.
//  4. A checagem de Param vem ANTES da de NotInit: com a placa nao inicializada E mascara suja, o
//     chamador recebe Param. E a ordem do alvo.
//  5. set() com RelayState fora da enumeracao -> Err::Param. Recusa explicita, nunca
//     interpretacao: set() e a via de fabrica e de diagnostico, a que recebe valor de console.
//  6. Canal fora de faixa: state() devolve Signalled (direcao segura) e channelName() devolve
//     "??"; nenhum dos dois e erro, porque a porta nao tem como devolve-lo.
//  7. mask() logo apos o construtor ja e kRelayMaskAllSignalled: a UR nasce em alarme.
//  8. feedbackAvailable() e sempre false (decisao 16). state()/mask() sao COMANDO, nunca medicao,
//     e por isso este fake nao tem nenhum "setContato()" - fingir realimentacao aqui seria
//     autorizar o dominio a depender de algo que a placa nao tem.
//  9. begin() pode REPROVAR (Err::HwFault), como no alvo, que so devolve kOk depois de reler o
//     latch de saida dos quatro pinos. Com begin() reprovado, ready() fica false, o comando fica
//     em Signalled e toda escrita posterior devolve NotInit. injectBeginFault() cobre esse ramo.
// 10. failSafeCoil() devolve exatamente o argumento do construtor (decisao A1 pendente): o fake
//     nao escolhe polaridade, como o adaptador tambem nao escolhe.
//
// Os rotulos "X1, X2, Y1, Y2" sao a nomenclatura do manual 5.9 L202 e estao aqui em copia
// deliberada do alvo, para provar substituibilidade. Quando domain::limitChannelLabel() existir
// em src/domain/limit_rule.h, alvo e fake apontam para ela e as copias somem.
#pragma once

#include <stdint.h>

#include "ports/i_clock.h"
#include "ports/i_relay_bank.h"
#include "status.h"

namespace test {

class FakeRelayBank : public IRelayBank {
public:
    static constexpr uint8_t kHistoryCap = 32;

    struct Transition {
        RelayMask mask;
        uint32_t atMs;
    };

    explicit FakeRelayBank(const IClock& clock, bool failSafePolarity = true)
        : clock_(clock),
          recorded_(0),
          transitions_(0),
          dropped_(0),
          history_{},
          mask_(kRelayMaskAllSignalled),
          failSafePolarity_(failSafePolarity),
          beginFault_(false),
          ready_(false) {}

    // --- IRelayBank ---

    Status begin() override {
        // Como no alvo: o passo 2 do boot escreve o NIVEL DE BOOT da base comum (LOW nas duas
        // polaridades, urbase::kRelayBootLevel), nao o nivel de "Signalled" - os dois so
        // coincidem quando failSafeCoil() e true. Na polaridade do manual, LOW e "Clear", e um
        // fake que insistisse em AllSignalled prometeria um boot com as quatro bobinas
        // energizadas que o alvo nao produz. A janela do passo 2 e SEMPRE uma transicao
        // registrada, mesmo que a mascara ja fosse a mesma - e o instante que a medicao de
        // inrush procura.
        const RelayMask bootMask =
            failSafePolarity_ ? kRelayMaskAllSignalled : kRelayMaskAllClear;
        mask_ = bootMask;
        record(bootMask);
        if (beginFault_) {
            ready_ = false;
            return Status(Err::HwFault);
        }
        ready_ = true;
        return kOk;
    }

    uint8_t count() const override { return kLimitChannelCount; }

    Status applyMask(RelayMask wanted) override {
        if ((wanted & static_cast<RelayMask>(~kRelayMaskAllSignalled)) != 0) {
            return Status(Err::Param);
        }
        if (!ready_) {
            return Status(Err::NotInit);
        }
        write(wanted);
        return kOk;
    }

    Status set(LimitChannel channel, RelayState relayState) override {
        uint8_t index = 0;
        if (!indexOf(channel, index)) {
            return Status(Err::Param);
        }
        if (relayState != RelayState::Clear && relayState != RelayState::Signalled) {
            return Status(Err::Param);
        }
        if (!ready_) {
            return Status(Err::NotInit);
        }
        const RelayMask wanted =
            (relayState == RelayState::Signalled)
                ? static_cast<RelayMask>(mask_ | bitOf(index))
                : static_cast<RelayMask>(mask_ & static_cast<RelayMask>(~bitOf(index)));
        write(wanted);
        return kOk;
    }

    RelayState state(LimitChannel channel) const override {
        uint8_t index = 0;
        if (!indexOf(channel, index)) {
            return RelayState::Signalled;
        }
        return ((mask_ & bitOf(index)) != 0) ? RelayState::Signalled : RelayState::Clear;
    }

    RelayMask mask() const override { return mask_; }

    Status signalAll() override { return applyMask(kRelayMaskAllSignalled); }

    bool failSafeCoil() const override { return failSafePolarity_; }

    bool feedbackAvailable() const override { return false; }

    const char* channelName(LimitChannel channel) const override {
        uint8_t index = 0;
        if (!indexOf(channel, index)) {
            return "??";
        }
        static constexpr const char* kName[kLimitChannelCount] = {"X1", "X2", "Y1", "Y2"};
        return kName[index];
    }

    // --- estimulo e observacao de teste (nao pertencem a porta) ---

    // Espelha RelayBankGpio::signalAllFromIsr(): estado seguro forcado sem Status e sem despacho
    // virtual, o unico caminho executavel na janela de cache desligada do commit de NVS. Como no
    // alvo, e no-op antes de begin() e nao consulta ready().
    void forceSignalledFromIsr() {
        if (!begun()) {
            return;
        }
        write(kRelayMaskAllSignalled);
    }

    // Faz o proximo begin() (e os seguintes) reprovarem com Err::HwFault, como o alvo faz quando a
    // releitura do latch de saida nao confere.
    void injectBeginFault(bool fault = true) { beginFault_ = fault; }

    bool ready() const { return ready_; }
    bool begun() const { return transitions_ > 0; }

    // Total de transicoes ocorridas desde a construcao, incluindo as que nao couberam no
    // historico. Gesto perdido nao pode ser silencioso: dropped() conta o que foi descartado.
    uint32_t transitionCount() const { return transitions_; }
    uint8_t recorded() const { return recorded_; }
    uint32_t dropped() const { return dropped_; }

    const Transition& transition(uint8_t i) const {
        return history_[(i < recorded_) ? i : (recorded_ > 0 ? recorded_ - 1 : 0)];
    }

    const Transition& lastTransition() const { return transition(recorded_ > 0 ? recorded_ - 1 : 0); }

    uint32_t lastChangeMs() const { return lastTransition().atMs; }

private:
    static constexpr RelayMask bitOf(uint8_t index) {
        return static_cast<RelayMask>(1u << index);
    }

    static bool indexOf(LimitChannel channel, uint8_t& index) {
        const uint8_t raw = static_cast<uint8_t>(channel);
        if (raw >= kLimitChannelCount) {
            return false;
        }
        index = raw;
        return true;
    }

    // Uma escrita = no maximo UMA transicao, e so quando o comando muda de valor. E a promessa de
    // atomicidade da porta vista do lado do observador.
    void write(RelayMask wanted) {
        if (wanted == mask_) {
            return;
        }
        mask_ = wanted;
        record(wanted);
    }

    void record(RelayMask wanted) {
        ++transitions_;
        if (recorded_ < kHistoryCap) {
            history_[recorded_].mask = wanted;
            history_[recorded_].atMs = clock_.nowMs();
            ++recorded_;
        } else {
            ++dropped_;
        }
    }

    const IClock& clock_;
    uint8_t recorded_;
    uint32_t transitions_;
    uint32_t dropped_;
    Transition history_[kHistoryCap];
    RelayMask mask_;
    bool failSafePolarity_;
    bool beginFault_;
    bool ready_;
};

}  // namespace test
