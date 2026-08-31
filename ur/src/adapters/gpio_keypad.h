// src/adapters/gpio_keypad.h
// Adaptador de IKeypad para as tres teclas do CN3 (DE-PURI-DI261924 REV A, folha 1/2):
// MENU = IO35, UP = IO15, DOWN = IO34, ativas em nivel BAIXO. Envelopa o ButtonMonitor
// validado em bancada (src/drivers/buttons.cpp) atras da porta; nao ha regra de produto
// aqui - gesto, hold de 3 s, duplo toque e tecla presa vivem em src/domain/ui/key_gesture.
//
// ARMADILHA DE HARDWARE (folha 1/2, CN3; board_pins.h:27..29): IO34 (DOWN) e IO35 (MENU)
// sao INPUT-ONLY e ignoram INPUT_PULLUP EM SILENCIO - este adaptador chama pinMode(INPUT)
// nesses dois pinos de proposito, e o pull-up deles tem de vir da PLACA DE IHM, que nao tem
// esquematico neste repositorio. So UP = IO15 tem pull-up interno, e por isso
// hasInternalPullup() devolve true SO para Key::Up (decisao 1, item 4: o Reset Geral e de
// tecla unica justamente porque o cabo de IHM solto tem de ler SOLTA, nao presa).
//
// TEMPO: nenhuma chamada a millis(). Todo instante vem do IClock injetado, a mesma base do
// FakeClock que alimenta o FakeKeypad - e o que torna este adaptador substituivel pelo fake
// sem o dominio perceber (LSP).
//
// BLOQUEIO DECLARADO: NENHUM. begin() nao chama delay() e nao espera linha nenhuma - faz
// pinMode nas tres teclas e UMA amostragem, que e exatamente o passo 11 da ordem de boot da
// base comum ("pinMode em IO15/34/35 e primeira amostragem. ORCAMENTO 0,5 ms"). poll()
// tambem nao bloqueia e nao usa interrupcao: pior caso sao tres digitalRead, um nowMs() e
// ate tres secoes criticas de dezenas de instrucoes. A acomodacao eletrica da linha e feita
// pela maquina de debounce de 20 ms de poll(), que roda sem custo de boot.
//
// BORDAS DE BOOT: begin() NAO enfileira evento nenhum - semeia so o NIVEL, igual ao
// FakeKeypad::begin(), que sai com a fila vazia. Uma tecla encontrada prensada na
// energizacao aparece em pressedMask()/pressed()/pressedForMs() (que e o que a decisao 1
// itens 22/23 e a guarda de tecla presa de A13 item 13 consomem) e a PRIMEIRA borda
// entregue e a soltura real, ja debounced. Sintetizar um Press de boot tornaria "conhecida"
// uma prensagem que ninguem fez e faria a soltura virar gesto - com o cabo de IHM solto,
// IO34/IO35 flutuando entregariam um ShortTap ou um Hold de MENU fantasma, que e a tecla de
// entrada no Modo Programacao. O dominio (key_gesture.h) foi escrito para o contrario:
// "uma soltura sem prensagem conhecida ... nao gera gesto nenhum".
//
// CADENCIA E CONCORRENCIA: pela decisao 1, item 5 (emenda declarada a base comum), a
// amostragem vive na tarefa FreeRTOS "btn" a 5 ms, e a maquina de estados da IHM consome as
// bordas no loop(). Sao DUAS tarefas sobre a MESMA fila, entao a fila, os contadores e o
// nivel debounced sao serializados por um portMUX (secao critica de TAREFA, nao de ISR - ver
// gpio_keypad.cpp). Sem isso o read-modify-write de count_ nao e atomico no Xtensa e uma
// preempcao perderia bordas EM SILENCIO, o oposto do que a porta exige de droppedEvents().
// Cada borda e publicada JUNTO com o nivel que ela representa, dentro da mesma secao
// critica, para que o consumidor nunca veja evento sem nivel nem nivel sem evento.
//
// LIMITE QUE O FIRMWARE NAO FECHA: se a tarefa "btn" ficar sem rodar por mais tempo que a
// prensagem (janela de cache-off da NVS, prioridade maior segurando a CPU), a prensagem se
// perde sem contabilizacao possivel - nao ha borda para contar. E o que a medicao 15 da
// decisao 1 ensaia (zero prensagens de 30 ms perdidas); a saida dela e cadencia, nao codigo.
//
// DECISOES: 1 (itens 4, 5 e 7 - tecla unica, tarefa btn, gesto medido sobre estes carimbos),
//           A13 item 13 (guarda de tecla presa, servida por pressedForMs()).
// REQ:      MAN-2.1-L23, MAN-5.2-L79..85, MAN-5.4-L99..101, MAN-5.6-L143..152,
//           MAN-5.11-L232..239. Porta: src/ports/i_keypad.h. Fake: test/fakes/fake_keypad.h.
// REAPROVEITADO de src/drivers/buttons.cpp: maquina de debounce nao bloqueante de 20 ms,
//           contagem de repique e fila de 16 bordas. CORRIGIDO para o contrato da porta:
//           fila descarta a borda NOVA e conta droppedEvents() (o driver descartava a mais
//           antiga), resetCounters() nao esvazia a fila, e a borda carrega heldMs e carimbo.
#pragma once

#include <stdint.h>

#include "board_pins.h"
#include "ports/i_clock.h"
#include "ports/i_keypad.h"
#include "status.h"

class GpioKeypad final : public IKeypad {
public:
    static constexpr uint8_t kQueueCap = 16;
    static constexpr uint16_t kDebounceMs = 20;

    explicit GpioKeypad(const IClock& clock);

    Status begin() override;
    void poll() override;
    bool takeEvent(KeyEvent& out) override;
    bool pressed(Key key) const override;
    uint8_t pressedMask() const override;
    uint32_t pressedForMs(Key key) const override;
    void flush() override;
    uint16_t debounceMs() const override;
    bool hasInternalPullup(Key key) const override;
    uint32_t bounceCount(Key key) const override;
    uint32_t droppedEvents() const override;
    void resetCounters() override;
    const char* keyName(Key key) const override;

    board::Pin pin(Key key) const;
    bool ready() const { return ready_; }
    uint8_t queued() const { return count_; }

private:
    static uint8_t index(Key key);
    static bool indexOk(uint8_t i);
    // PRE-CONDICAO: o portMUX da fila ja tem de estar tomado pelo chamador.
    void pushLocked(const KeyEvent& event);

    const IClock& clock_;
    KeyEvent queue_[kQueueCap];
    uint8_t head_;
    volatile uint8_t count_;
    volatile uint32_t dropped_;
    volatile bool down_[kKeyCount];
    bool raw_[kKeyCount];
    uint32_t changeMs_[kKeyCount];
    volatile uint32_t sinceMs_[kKeyCount];
    volatile uint32_t bounces_[kKeyCount];
    bool ready_;
};
