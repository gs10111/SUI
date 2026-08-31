// src/ports/i_keypad.h
// Teclado do CN3: TRES teclas (MENU, UP, DOWN), ativas em nivel BAIXO.
// A porta entrega BORDAS JA DEBOUNCED com carimbo de tempo e nivel corrente.
// Ela NAO reconhece gesto: toque curto, hold de 3 s e duplo toque sao politica de
// produto e vivem no dominio (KeyGesture, compilavel em env:native), onde podem
// ser testados sem hardware. A porta cuida so do que e eletrico: amostragem,
// anti-repique de 20 ms e diagnostico de linha.
// ARMADILHA DE HARDWARE que esta porta expoe de proposito: UP = IO15 tem pull-up
// interno; DOWN = IO34 e MENU = IO35 sao INPUT-ONLY e ignoram INPUT_PULLUP em
// silencio - o pull-up tem de vir da placa de IHM, que nao tem esquematico no
// repositorio. Cabo de IHM solto pode ser lido como tecla PRESA. Por isso
// hasInternalPullup() existe: o dominio recusa gestos destrutivos (Reset de
// Fabrica) em teclas sem pull-up garantido e trata "as tres prensadas no boot"
// como assinatura de cabo em curto, nao como comando.
// Alvo: ButtonMonitor (src/drivers/buttons.cpp), poll sem bloqueio, fila de 16 bordas.
// Fake: FakeKeypad (test/native) - roteiro de (tecla, borda, instante) alimentado
//       pelo FakeClock; permite reproduzir duplo toque, hold e repique.
// REQ:  MAN-2.1-L23 (tres teclas), MAN-5.2-L79..85, MAN-5.3-L88..96 (login),
//       MAN-5.4-L99..101 (hold de ~3 s), MAN-5.6-L143..152 (duplo toque de PSET),
//       MAN-5.11-L232..239 (UP mantida na energizacao), decisoes 1 e 2.
#pragma once

#include <stdint.h>

#include "status.h"

enum class Key : uint8_t {
    Menu = 0,  // CN3-3, IO35, input-only
    Up,        // CN3-1, IO15, unico com pull-up interno
    Down,      // CN3-2, IO34, input-only
};

constexpr uint8_t kKeyCount = 3;

enum class KeyEdge : uint8_t {
    Press = 0,
    Release,
};

struct KeyEvent {
    Key key;
    KeyEdge edge;
    uint32_t atMs;    // instante da borda que sobreviveu ao debounce (base IClock)
    uint16_t heldMs;  // duracao da prensagem; valido so em Release, satura em 65535
};

class IKeypad {
public:
    virtual ~IKeypad() = default;
    IKeypad(const IKeypad&) = delete;
    IKeypad& operator=(const IKeypad&) = delete;

    virtual Status begin() = 0;

    // Amostra as tres linhas e aplica o debounce. Nao bloqueia, nao usa interrupcao.
    // Chamada uma vez por ciclo de controle; o periodo do ciclo tem de ser menor
    // que debounceMs() para que nenhuma prensagem curta seja perdida.
    virtual void poll() = 0;

    // Consome a borda mais antiga da fila. false quando a fila esta vazia.
    // Esta e a UNICA via de gesto: nivel amostrado por conta propria perde toques.
    virtual bool takeEvent(KeyEvent& out) = 0;

    // Nivel logico debounced corrente (true = prensada).
    virtual bool pressed(Key key) const = 0;

    // Mascara com bit por tecla, na ordem do enum. Serve ao instantaneo do boot
    // (Reset de Fabrica e assinatura de cabo em curto) sem drenar a fila.
    virtual uint8_t pressedMask() const = 0;

    // Ha quanto tempo a tecla esta prensada, 0 se solta. Permite disparar a acao
    // NO INSTANTE em que o hold completa 3000 ms, com a tecla ainda prensada,
    // em vez de so na soltura - que e o que o operador espera do manual.
    virtual uint32_t pressedForMs(Key key) const = 0;

    // Descarta bordas pendentes. Chamado em toda troca de tela ou de modo, para
    // que um toque de confirmacao nao vaze para a tela seguinte.
    virtual void flush() = 0;

    virtual uint16_t debounceMs() const = 0;  // 20 ms

    // Diagnostico eletrico. hasInternalPullup(Down/Menu) == false nesta placa.
    virtual bool hasInternalPullup(Key key) const = 0;

    // Repiques rejeitados pelo debounce; cresce com cabo ruim ou sem pull-up.
    virtual uint32_t bounceCount(Key key) const = 0;

    // Bordas perdidas por fila cheia. Diferente de zero e defeito de escalonamento
    // e TEM de ser visivel: gesto perdido em equipamento de seguranca nao pode ser
    // silencioso.
    virtual uint32_t droppedEvents() const = 0;
    virtual void resetCounters() = 0;

    virtual const char* keyName(Key key) const = 0;

protected:
    IKeypad() = default;
};
