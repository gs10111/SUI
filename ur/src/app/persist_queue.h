// src/app/persist_queue.h
// Fila de slots sujos da memoria nao volatil: qual dos dois registros de A8 (parametros que
// comandam rele, calibracao analogica) ainda precisa ser gravado, quantas vezes ja se tentou e
// quando desistir. Camada de aplicacao pura - nao inclui Arduino.h, nao conhece NVS, nao mede
// tempo e nao serializa nada. Quem grava e o composition root; esta classe so diz o que tentar
// nesta passagem e o que fazer com o resultado.
//
// POR QUE ISTO SAIU DO main.cpp. A versao anterior vivia em servicePersist() como uma mascara
// de bits solta e tinha um defeito sem rede de teste: limpava o bit ANTES de gravar e, quando a
// gravacao reprovava, zerava a MASCARA INTEIRA. O caso concreto e o Reset Geral, o unico
// caminho que suja os dois slots de proposito (decisao 1 item 28) e que e fatiado em duas
// passagens do loop() para nao encostar tres commits de 250 ms nos 800 ms do token de liveness:
// falhando a gravacao do BankA, o BankB nunca chegava a ser tentado e a calibracao restaurada
// sumia da flash sem indicio nenhum - na energizacao seguinte o bloco ausente ou velho voltava
// pela porta da NVS. Aqui a falha de um slot NAO toca no outro.
//
// REGRAS, e cada uma existe por um modo de falha:
//  - UM SLOT POR PASSAGEM (decisao 2 item 16, e a razao de o Reset Geral ser fatiado): nextSlot()
//    devolve um so, com os parametros na frente da calibracao, porque sao eles que comandam os
//    quatro reles;
//  - O BIT SO E LIMPO DEPOIS DO SUCESSO. Enquanto a gravacao nao valer, o slot continua sujo e
//    volta a ser tentado na passagem seguinte;
//  - TETO DE TENTATIVAS. Uma NVS que reprova costuma reprovar sempre; sem teto o loop() ficaria
//    gravando para sempre a 20 Hz, com apagamento de setor e cache-off a cada passagem, o que
//    e pior do que a perda que ele tenta evitar. Esgotado kMaxAttempts o slot e LIBERADO e a
//    desistencia fica registrada, uma unica vez, para quem tem tela;
//  - A DESISTENCIA E POR SLOT E NOMEADA. "Falha de gravacao!" sem dizer qual banco nao distingue
//    "seus setpoints nao foram para a flash" de "sua calibracao nao foi"; takeGaveUp() entrega
//    QUAL slot desistiu, e o chamador decide o que mostrar e o que registrar no console.
#pragma once

#include <stdint.h>

namespace app {

class PersistQueue {
public:
    // Os dois registros separados de A8. A ordem do enum e a ordem de prioridade.
    enum class Slot : uint8_t {
        Params = 0,  // BankA: preset, sentido, os quatro limites, senha - comanda rele
        Cal = 1,     // BankB: os dois pares de calibracao analogica
    };
    static constexpr uint8_t kSlotCount = 2;

    // Tres tentativas a 50 ms de distancia (uma por passagem do loop()) = 150 ms ate desistir,
    // muito abaixo dos 2000 ms que a mensagem de falha fica no ar.
    static constexpr uint8_t kMaxAttempts = 3;

    PersistQueue();

    void markDirty(Slot slot);
    bool dirty(Slot slot) const;
    bool anyDirty() const;
    uint8_t attempts(Slot slot) const;

    // O slot a tentar nesta passagem, se houver. Nao muda estado: quem muda e noteResult().
    bool nextSlot(Slot& out) const;

    // Resultado da tentativa daquele slot. Sucesso limpa o bit e zera as tentativas. Falha
    // MANTEM o bit sujo e conta a tentativa; na kMaxAttempts-esima o bit e liberado e a
    // desistencia fica pendente para takeGaveUp(). O outro slot nunca e tocado.
    void noteResult(Slot slot, bool ok);

    // Consumo unico da desistencia mais antiga pendente: devolve true uma vez por slot que
    // esgotou as tentativas, para que a IHM nao repita a mensagem a cada passagem.
    bool takeGaveUp(Slot& out);

    static bool slotValid(Slot slot) { return static_cast<uint8_t>(slot) < kSlotCount; }

private:
    static uint8_t idx(Slot slot) { return static_cast<uint8_t>(slot); }

    bool dirty_[kSlotCount];
    bool gaveUp_[kSlotCount];
    uint8_t attempts_[kSlotCount];
};

}  // namespace app
