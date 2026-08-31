// Reconhecedor de gesto de tecla: transforma as bordas debounced da porta IKeypad em toque
// curto, toque longo (hold) e duplo toque. E politica de produto, nao eletronica - por isso
// vive no dominio, compila em env:native e e testavel sem placa. A porta cuida so do que e
// eletrico (amostragem, anti-repique de 20 ms, diagnostico de linha) e diz isso no proprio
// cabecalho de src/ports/i_keypad.h.
//
// Manual SUI-DI141388XY (docs/ihm-estados.md secao 1.2 e docs/manual-cliente-sui-2026.txt):
//   Toque longo  - L81 e L101 do manual citado por docs/ihm-estados.md (arquivo bruto L90:
//                  "Mantida pressionada por aproximadamente 3 segundos, permite o acesso ao
//                  Modo Programacao" e L110: "mantenha a tecla MENU pressionada por
//                  aproximadamente 3 segundos para confirmar e gravar o novo valor").
//   Duplo toque  - L152 (arquivo bruto L161: "execute o comando de preset por meio de um duplo
//                  acionamento da tecla UP (PSET). O display piscara").
//
// Decisao 1, item 7, APROVADA: o gesto e medido sobre os carimbos ja depois do debounce de
// 20 ms - duas prensagens de 30 ms a 600 ms cada, intervalo entre a soltura da primeira e a
// prensagem da segunda de ate 400 ms, gesto inteiro em ate 1600 ms; qualquer borda de MENU ou
// DOWN dentro da janela anula, e um terceiro toque dentro da janela tambem anula, para que
// repique mecanico nao vire PSET.
//
// TRES ESCOLHAS QUE PRECISAM ESTAR ESCRITAS, porque nenhuma e obvia:
//
// 1. O HOLD DISPARA NA BORDA DOS 3000 ms, com a tecla ainda prensada, nao na soltura. E o que
//    o operador espera de "mantenha pressionada por aproximadamente 3 segundos": ele solta
//    quando ve a tela mudar. Consequencia deliberada: uma prensagem longa NUNCA vira toque
//    curto na soltura, e o hold nao redispara enquanto a tecla continuar prensada.
//
// 2. O DUPLO TOQUE SO E ENTREGUE QUANDO A JANELA DE UM TERCEIRO TOQUE FECHA. Entregar no ato
//    da segunda soltura tornaria impossivel cumprir o "terceiro toque anula" da decisao 1: o
//    PSET ja teria deslocado os quatro pontos de atuacao antes de o repique chegar. A espera e
//    a menor das duas que a propria decisao 1 define - 400 ms apos a segunda soltura, ou o que
//    restar dos 1600 ms de gesto, o que vencer primeiro. O carimbo entregue continua sendo o
//    da segunda soltura, que e quando o operador terminou o gesto.
//
// 3. GESTOS SAO INDEPENDENTES: um duplo toque valido tambem entrega os dois toques curtos que o
//    compoem. Quem decide o que ignorar e a maquina de estados - no Modo Normal o toque simples
//    de UP nao tem funcao (decisao 1, item 2), e num menu ele pagina, que e o certo nos dois
//    casos. Filtrar aqui esconderia do estado uma borda que ele precisa ver.
//
// TEMPO: todo prazo passa por elapsedMs()/deadlineReached() de ports/i_clock.h, nunca por
// "a > b", e a base e SEMPRE o carimbo da borda, nunca o instante em que o gesto foi drenado.
// Por isso update() reconstroi os prazos evento a evento: um ciclo atrasado por gravacao de NVS
// nao desloca o instante do hold nem alarga a janela do duplo toque.
//
// ESTADO SEGURO: flush() e a troca de tela ou de modo. Ele drena a fila da porta, joga fora o
// gesto pela metade e MARCA como consumida toda tecla que esteja prensada agora - a soltura
// dessa tecla nao vira toque curto na tela seguinte. Uma soltura sem prensagem conhecida (boot
// com tecla presa, cabo de IHM solto) nao gera gesto nenhum.
#pragma once

#include <stdint.h>

#include "ports/i_clock.h"
#include "ports/i_keypad.h"

namespace domain {

enum class GestureKind : uint8_t {
    ShortTap = 0,
    Hold,
    DoubleTap,
};

struct Gesture {
    GestureKind kind;
    Key key;
    uint32_t atMs;
};

class KeyGesture {
public:
    static constexpr uint16_t kShortTapMinMs = 30;
    static constexpr uint16_t kShortTapMaxMs = 600;
    static constexpr uint16_t kHoldMs = 3000;
    static constexpr uint16_t kDoubleGapMaxMs = 400;
    static constexpr uint16_t kDoubleSpanMaxMs = 1600;

    // O duplo toque e de tecla UNICA (decisao 1, item 4): UP e o unico botao com pull-up
    // interno (IO15), entao e o unico cujo repouso nao depende do cabo do painel.
    static constexpr Key kDoubleTapKey = Key::Up;

    static constexpr uint8_t kQueueCap = 8;

    KeyGesture(IKeypad& keypad, const IClock& clock);

    KeyGesture(const KeyGesture&) = delete;
    KeyGesture& operator=(const KeyGesture&) = delete;

    // Drena a fila da porta e avalia os prazos. Chamada uma vez por ciclo de IHM.
    void update();

    // Consome o gesto mais antigo. false quando nao ha gesto.
    bool takeGesture(Gesture& out);

    // Troca de tela ou de modo: nada do gesto corrente atravessa.
    void flush();

    // Ha um duplo toque completo esperando so o fim da janela do terceiro toque.
    bool doubleTapPending() const;

    // Gesto perdido por fila cheia. Diferente de zero e defeito de escalonamento e tem de ser
    // visivel: em equipamento de seguranca gesto perdido nao pode ser silencioso.
    uint32_t droppedGestures() const;
    void resetCounters();

private:
    enum class DoubleState : uint8_t {
        Idle = 0,
        FirstDown,
        FirstUp,
        SecondDown,
        Pending,
    };

    static uint8_t index(Key key);
    static bool tapDurationOk(uint16_t heldMs);

    void advanceTo(uint32_t timeMs);
    void handle(const KeyEvent& event);
    void applyToDouble(const KeyEvent& event);
    void resetDouble();
    void emit(GestureKind kind, Key key, uint32_t atMs);

    IKeypad& keypad_;
    const IClock& clock_;

    Gesture queue_[kQueueCap];
    uint8_t head_;
    uint8_t count_;
    uint32_t dropped_;

    bool down_[kKeyCount];
    bool holdFired_[kKeyCount];
    uint32_t downSinceMs_[kKeyCount];

    DoubleState doubleState_;
    uint32_t doubleFirstPressMs_;
    uint32_t doubleFirstReleaseMs_;
    uint32_t doublePendingSinceMs_;
    uint32_t doublePendingSpanMs_;
};

}  // namespace domain
