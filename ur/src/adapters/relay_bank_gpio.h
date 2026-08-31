// Adaptador de IRelayBank sobre os quatro GPIOs de limite da DE-PURI-DI261924 REV A, folha 2/2:
// ESP32 -> resistor de base -> BC337 -> bobina do AX1RC-5V (RL5, RL4, RL3, RL2), com os contatos
// nos bornes CN1D/CN1E, CN1F/CN1G, CN1H/CN1I e CN1J/CN1K e os jumpers J10, J9, J8 e J2 (padrao
// NF). Os pinos saem de board::kRelayPins (IO32, IO26, IO25, IO33) e a identificacao de rele,
// borne, jumper e LED de painel sai de board::kRelayMap - inclusive o CRUZAMENTO DE FIACAO do
// CN3, em que a serigrafia do painel NAO corresponde ao net: LIM1 acende o LED serigrafado
// "LED LIM3", LIM2 o "LED LIM1", LIM3 o "LED LIM2" e LIM4 o "LED LIM4". Vale a tabela, nunca a
// serigrafia; e por isso que este arquivo nao repete nenhum numero de pino nem de borne.
//
// ESCRITA ATOMICA (contrato de IRelayBank::applyMask). O regime normal NAO usa digitalWrite():
// os quatro canais vao em REGISTRADOR - GPIO.out_w1ts/out_w1tc para IO25/IO26 e
// GPIO.out1_w1ts/out1_w1tc para IO32/IO33 - porque quatro digitalWrite() do HAL Arduino deixam
// um estado intermediario de 1 a 2 us com dois canais no valor novo e dois no velho, e a porta
// promete que esse estado nao existe. Com registrador o skew cai para a distancia entre dois
// stores (dezenas de ns, dois bancos de GPIO distintos), abaixo de qualquer efeito observavel no
// AX1RC-5V, e o degrau de corrente das bobinas passa a ser unico e deterministico em vez de
// depender do tempo de execucao do HAL (medicoes 7 e 9, kSupplyBudgetW = 5 W).
// O par digitalWrite-antes-de-pinMode do driver de fabrica (src/drivers/relays.cpp), validado em
// bancada, fica SO no begin(), onde o objetivo e o latch de saida e nao a simultaneidade.
//
// CAMINHO SEGURO COM CACHE DESLIGADA (decisao 6: reles vivos em programacao e calibracao).
// signalAllFromIsr() e driveMask() sao IRAM_ATTR e nao tocam em flash: nenhuma chamada ao HAL
// Arduino, nenhuma leitura de board::kRelayPins (que e constexpr e mora em .rodata, ou seja em
// flash mapeada por cache). As mascaras de bit dos quatro pinos sao copiadas para membros
// uint32_t no construtor, e o nivel de bobina de Signalled e resolvido uma unica vez pela mesma
// domain::coilLevel(), tambem no construtor. Assim o estado seguro pode ser forcado de dentro da
// ISR de timer de hardware em IRAM que chuta o WDI, inclusive durante o apagamento de setor da
// NVS (ate 500 ms pelo criterio de M3), janela em que nenhuma linha que execute de flash roda.
// LIMITE DECLARADO: signalAll() (virtual) NAO e chamavel nessa janela mesmo marcada IRAM_ATTR -
// a vtable mora em .rodata, entao o despacho virtual le flash. Quem precisa do caminho de
// cache-off tem de segurar o tipo CONCRETO e chamar signalAllFromIsr(), que nao e virtual.
//
// Decisao A1 (polaridade dos reles): este adaptador NAO decide polaridade e NAO traduz estado em
// nivel por conta propria. A traducao unica do produto e domain::coilLevel(estado, polaridade);
// a polaridade chega por parametro de construcao (urbase::kRelayFailSafePolarity), nunca por
// #ifdef, porque A1 depende das medicoes 7, 8 e 9 e as duas polaridades tem de ser testaveis sem
// recompilar o dominio.
// Base comum, passo 2 da ordem de boot canonica: begin() e o segundo passo do setup(), logo
// depois do watchdog e ANTES de NVS, display, botoes e RS-485 - os quatro GPIOs assumem nivel
// seguro antes de qualquer init lento. Conforme o contrato de IRelayBank::begin(), o nivel
// escrito e o de Signalled, antes e depois do pinMode(OUTPUT).
// DIVERGENCIA DECLARADA, E COMO ELA PARA O BUILD: com kRelayFailSafePolarity = true (recomendada)
// Signalled e nivel BAIXO e coincide com kRelayBootLevel = false do passo 2; com a polaridade do
// manual (false) Signalled e nivel ALTO e begin() energiza as quatro bobinas durante todo o boot,
// que e o que a porta manda e o oposto do que o passo 2 descreve. O comentario nao e a barreira:
// o composition root da etapa 8 tem de escrever, com o header real da base comum,
//   static_assert(RelayBankGpio::signalledCoilLevel(urbase::kRelayFailSafePolarity)
//                     == urbase::kRelayBootLevel,
//                 "passo 2 do boot: nivel de Signalled tem de coincidir com kRelayBootLevel");
// de modo que fechar A1 em false QUEBRE a compilacao e obrigue o bigboss a assinar a divergencia,
// em vez de descobri-la na bancada com as quatro bobinas quentes no inrush da fonte de 5 W.
//
// BLOQUEIO (base comum, tick de 50 ms): nenhum. Nao ha delay(), espera de barramento nem acesso a
// flash em nenhum metodo. Pior caso de begin() (8 chamadas ao HAL mais quatro stores e a releitura
// dos registradores de saida) e de dezenas de microssegundos; applyMask()/set()/signalAll() sao
// quatro stores em registrador, na casa de centenas de nanossegundos.
//
// Decisao 6 e decisao 7 item 6 / decisao 8 item H (estado sinalizado em qualquer falha): nada
// disso e decidido aqui - chega pronto na mascara.
// Decisao 16: nao ha realimentacao de contato nesta placa, feedbackAvailable() e sempre false e
// state()/mask() devolvem o COMANDO, nunca a realidade eletrica. O unico fato que begin() sabe
// provar e que o LATCH de saida do ESP32 aceitou o nivel escrito (releitura de GPIO.out e
// GPIO.out1); bobina, BC337 e contato continuam indetectaveis por software.
#pragma once

#include <esp_attr.h>
#include <stdint.h>

#include "board_pins.h"
#include "domain/limit_rule.h"
#include "ports/i_relay_bank.h"
#include "status.h"

class RelayBankGpio final : public IRelayBank {
public:
    explicit RelayBankGpio(bool failSafePolarity);

    // Nivel de GPIO que este adaptador escreve para Signalled sob uma dada polaridade. Existe
    // para o composition root amarrar A1 ao kRelayBootLevel da base comum em static_assert (ver
    // o cabecalho acima); nao e ponto de decisao, e a mesma domain::coilLevel() exposta.
    static constexpr bool signalledCoilLevel(bool failSafePolarity) {
        return domain::coilLevel(RelayState::Signalled, failSafePolarity);
    }

    // Passo 2 do boot. Escreve Signalled, configura os quatro pinos como saida, reescreve
    // Signalled e RELE o que o ESP32 sabe dizer de volta: GPIO_ENABLE (o pinMode(OUTPUT) valeu
    // nos quatro pinos) e GPIO_OUT (o latch guardou o nivel). Devolve Err::HwFault quando essa
    // releitura nao confere - e o unico caso em que o adaptador nao consegue provar que comanda
    // os pinos; nesse caso ready()
    // fica false e applyMask()/set() passam a devolver Err::NotInit, com o hardware ja deixado no
    // nivel de Signalled. Nunca devolve kOk sem ter lido de volta o que escreveu.
    Status begin() override;
    uint8_t count() const override;
    Status applyMask(RelayMask wanted) override;
    Status set(LimitChannel channel, RelayState relayState) override;
    RelayState state(LimitChannel channel) const override;
    RelayMask mask() const override;
    Status signalAll() override;
    bool failSafeCoil() const override;
    bool feedbackAvailable() const override;
    const char* channelName(LimitChannel channel) const override;

    // Estado seguro forcado sem despacho virtual e sem tocar em flash: e o unico metodo chamavel
    // de ISR em IRAM e durante a janela de cache-off do commit de NVS. Nao devolve Status porque
    // nao ha nada a decidir em ISR - escreve os quatro canais em Signalled e atualiza o cache.
    // Pre-condicao: os pinos ja configurados (begin() executado, mesmo que tenha reprovado o
    // latch); antes disso e no-op. Depois de uma chamada destas a placa esta em estado de falha
    // declarado: o chamador nao retoma escrita normal, derruba a placa.
    void IRAM_ATTR signalAllFromIsr();

    bool ready() const { return ready_; }
    board::Pin pin(LimitChannel channel) const;
    const board::RelayMap& info(LimitChannel channel) const;

private:
    static bool indexOf(LimitChannel channel, uint8_t& index);
    void IRAM_ATTR driveMask(RelayMask wanted) const;
    bool latchMatches(RelayMask wanted) const;

    // Mascaras de bit dos quatro pinos, separadas por banco de GPIO (IO0..IO31 e IO32..IO39) e
    // resolvidas no construtor: e o que permite driveMask() rodar de IRAM sem ler .rodata.
    uint32_t bank0Bit_[kLimitChannelCount];
    uint32_t bank1Bit_[kLimitChannelCount];
    // mask_ e escrita tambem por signalAllFromIsr(); volatile para o cache lido pela tarefa ctrl
    // nao ficar preso em registrador.
    volatile RelayMask mask_;
    bool failSafePolarity_;
    bool signalledLevel_;  // domain::coilLevel(Signalled, failSafePolarity_), resolvido 1 vez
    bool configured_;      // pinMode(OUTPUT) ja aplicado - habilita o caminho de ISR
    bool ready_;           // latch de saida conferido - habilita applyMask()/set()
};
