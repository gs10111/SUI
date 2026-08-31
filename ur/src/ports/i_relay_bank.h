// src/ports/i_relay_bank.h
// Os quatro reles de limite (RL5..RL2, bornes CN1D..CN1K). A porta fala em
// SEMANTICA DE APLICACAO - canal de limite e condicao sinalizada - e nunca em
// indice de GPIO, nivel eletrico ou polaridade de bobina. O mapeamento
// "sinalizado -> bobina desenergizada -> contato NF fechado" (fail-safe, padrao
// de fabrica NF do manual) e responsabilidade EXCLUSIVA do adaptador; o dominio
// jamais escreve "ligar" ou "desligar", escreve "sinalizado" ou "livre".
// Esta porta NAO expoe saida analogica, nem LED, nem display: cada LED de limite
// do painel pendura no mesmo net da base do BC337 do rele, portanto acender LED
// e efeito colateral fisico e inseparavel de sinalizar - e por isso que nao ha
// uma porta de LED de limite em lugar nenhum.
// LIMITACAO DECLARADA: nao existe realimentacao de contato nesta placa. state()
// devolve o COMANDO, nao a realidade; bobina aberta, BC337 aberto ou contato
// colado sao indetectaveis por software (feedbackAvailable() == false).
// Alvo: RelayBank (src/drivers/relays.cpp) sobre board::kRelayPins.
// Fake: FakeRelayBank (test/native) - guarda a mascara e o historico de
//       transicoes com carimbo de tempo, para assercao de latencia e de permanencia.
// REQ:  MAN-5.9-L192..199 e L214 (operacao dos limites sobre a leitura exibida),
//       MAN-Tabela-4 L317..336 (quatro reles, jumper NA/NF, padrao NF),
//       decisao 5 (histerese e permanencia), decisao 6 (reles vivos em programacao),
//       decisao 7 item 6 e decisao 8 item H (estado sinalizado em qualquer falha).
#pragma once

#include <stdint.h>

#include "status.h"

enum class LimitChannel : uint8_t {
    Limit1 = 0,  // eixo X, rotulo X1 no manual
    Limit2,      // eixo X, rotulo X2
    Limit3,      // eixo Y, rotulo Y1
    Limit4,      // eixo Y, rotulo Y2
};

constexpr uint8_t kLimitChannelCount = 4;

enum class RelayState : uint8_t {
    Clear = 0,   // condicao normal, limite nao atingido
    Signalled,   // limite atingido, ou falha (link, faixa, boot, watchdog)
};

// Mascara com bit n = 1 quando o canal n esta em Signalled.
using RelayMask = uint8_t;

constexpr RelayMask kRelayMaskAllClear = 0x00;
constexpr RelayMask kRelayMaskAllSignalled = 0x0F;

class IRelayBank {
public:
    virtual ~IRelayBank() = default;
    IRelayBank(const IRelayBank&) = delete;
    IRelayBank& operator=(const IRelayBank&) = delete;

    // Leva os quatro canais a Signalled ANTES de configurar os pinos como saida,
    // e de novo depois: a janela de energizacao nao pode apresentar "sem alarme".
    virtual Status begin() = 0;

    virtual uint8_t count() const = 0;

    // Escrita ATOMICA dos quatro canais. E a via normal do ciclo de controle:
    // um unico ponto de decisao por ciclo, sem estado intermediario em que dois
    // canais ja mudaram e dois ainda nao.
    virtual Status applyMask(RelayMask mask) = 0;

    // Escrita de um canal so. Reservada ao roteiro de fabrica e ao diagnostico;
    // a aplicacao usa applyMask().
    virtual Status set(LimitChannel channel, RelayState state) = 0;

    // Estado COMANDADO (cache de escrita), nao medido.
    virtual RelayState state(LimitChannel channel) const = 0;
    virtual RelayMask mask() const = 0;

    // Atalho para o estado seguro: equivale a applyMask(kRelayMaskAllSignalled).
    // Nunca falha silenciosamente; se falhar, o chamador deve derrubar a placa.
    virtual Status signalAll() = 0;

    // true quando o adaptador foi construido com a polaridade de corrente de
    // repouso (Signalled = bobina desenergizada). Item de inspecao de recebimento:
    // com false, uma UR morta apresenta a mesma indicacao de uma UR sadia sem alarme.
    virtual bool failSafeCoil() const = 0;

    // Sempre false nesta placa: sem contato auxiliar de realimentacao.
    virtual bool feedbackAvailable() const = 0;

    virtual const char* channelName(LimitChannel channel) const = 0;

protected:
    IRelayBank() = default;
};
