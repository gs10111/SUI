// src/app/boot_sequence.h
// Sequencia de energizacao da IHM: autoteste do display, logomarca e o gesto de Reset de Fabrica
// (REQ-RST-01, manual 5.11 L243..L248). Camada de aplicacao pura: fala com IDisplay, IKeypad e
// IClock, nao inclui Arduino.h, nao usa ponto flutuante e NAO BLOQUEIA.
//
// Por que maquina de estados e nao delay(): o STWD100 tem tWD minimo de 1,12 s e o splash da
// decisao 12 mais a janela do gesto de reset somam ate 13 s. Um delay() de 600 ms ja seria
// metade do orcamento do cachorro; os 3000 ms de espera pela tecla ▲ seriam tres vezes o tWD.
// A base comum (DECISIONS.md 2.2, passo 15) manda o splash rodar DEPOIS do setup(), no loop(),
// com a tarefa ctrl ja polando a sensora, avaliando os quatro limites e comandando os quatro
// reles durante todo o tempo em que estas telas estao no ar.
//
// AUTOTESTE (decisao 12 itens 6 e 7): 600 ms percorrendo os quatro padroes do adaptador de
// display - area cheia, moldura de 1 px, regua de 16 px e as colunas isoladas - a 150 ms cada.
// O criterio de aprovacao e do operador: quatro bordas fechadas, marcas altas visiveis e texto
// legivel. Em seguida 600 ms de logomarca. Os 2000 ms + 1500 ms do rascunho da decisao 12 estao
// mortos: sozinhos violavam o tWD.
//
// AUTOTESTE SOB DEMANDA (decisao 12 item 8): o mesmo padrao roda em Modo Normal por
// beginOnDemand(), disparado pela tecla BAIXO mantida 3000 ms, sem senha. Sai ao primeiro toque
// em qualquer tecla - depois que a tecla do proprio gesto for solta - ou por teto de 30000 ms.
//
// RESET DE FABRICA (decisao 1, itens 21 a 26):
//   - o gesto NAO bloqueia o boot: main.cpp amostra os tres pinos de tecla no passo 4 da ordem
//     de boot e entrega a mascara a begin(); o setup() segue inteiro;
//   - assinatura de cabo (item 22): se BAIXO ou MENU tambem lerem prensadas na amostragem, o
//     gesto e abortado - tres teclas prensadas na energizacao e curto, nao operador;
//   - confirmacao (item 23): ▲ prensada continuamente ate t = 3000 ms contados da entrada do
//     setup(). A continuidade e verificada por pressedForMs() da porta, sem passar por bordas de
//     fila, a cada tick; qualquer tick que leia a tecla solta anula o gesto. LIMITACAO
//     DECLARADA: entre a amostragem do passo 4 e o begin() do teclado (passo 11, ~220 ms) nao ha
//     observador, entao uma soltura contida inteiramente nessa janela nao e vista;
//   - A LOGOMARCA ESPERA O GESTO. O splash inteiro dura kSelfTestMs + kLogoMs = 1200 ms e o
//     prazo do gesto e 3000 ms contados da ENTRADA do setup(): se a maquina fosse a Done ao
//     fim da logomarca, tick() passaria a retornar na primeira linha, watchReset() nunca mais
//     rodaria e o Reset de Fabrica seria INALCANCAVEL - o candidato morreria por volta de
//     t = 1430 ms com a tecla ainda prensada. Por isso a logomarca so termina quando o
//     candidato tiver sido resolvido: soltar a tecla limpa o candidato e o proximo tick fecha
//     a logomarca; segurar ate 3000 ms abre a tela do item 24;
//   - tela (item 24): `RESET DE FABRICA`, byte a byte da L246, por no minimo 2000 ms;
//   - execucao (item 25, L247): so na SOLTURA da tecla. Quem apaga e regrava a Tabela 2 e o
//     composition root, ao ver takeFactoryReset() devolver true - esta classe nao toca em NVS;
//   - tecla presa (item 26): ▲ ainda prensada 10000 ms depois da mensagem (t = 13000 ms) aborta
//     o reset, exibe `TECLA PRESA` por 3000 ms e o boot segue SEM apagar nada.
#pragma once

#include <stdint.h>

#include "ports/i_clock.h"
#include "ports/i_display.h"
#include "ports/i_keypad.h"
#include "status.h"

namespace app {

class BootSequence {
public:
    enum class Stage : uint8_t {
        Idle = 0,
        SelfTest,
        Logo,
        ResetMessage,
        StuckKey,
        Done,
    };

    static constexpr uint32_t kSelfTestMs = 600;
    static constexpr uint32_t kLogoMs = 600;
    static constexpr uint32_t kPatternMs = 150;
    // Decisao 1 item 23: "▲ prensada CONTINUAMENTE ... desde o passo 4 ate t = 3000 ms contados
    // da entrada no setup()". O valor 2000 que estava aqui divergia da decisao E do comentario
    // do proprio .cpp (linha "bem antes dos kResetHoldMs = 3000 ms"); nenhum teste reprovava
    // porque a suite de boot referenciava o SIMBOLO em vez do numero do manual. Agora ha
    // static_assert e assercao literal em test/native/test_boot.
    static constexpr uint32_t kResetHoldMs = 3000;
    static constexpr uint32_t kResetMessageMs = 2000;
    static constexpr uint32_t kStuckKeyDeadlineMs = 13000;
    static constexpr uint32_t kStuckKeyMessageMs = 3000;
    static constexpr uint32_t kOnDemandCeilingMs = 30000;
    static constexpr const char* kTextFactoryReset = "RESET DE FABRICA";
    static constexpr const char* kTextStuckKey = "TECLA PRESA";
    static constexpr const char* kTextBrand = "DI-ELETRONS";
    static constexpr const char* kTextModel = "SUI-DI141388XY  UR DE-PURI-DI261924";
    static constexpr const char* kTextSelfTest = "AUTOTESTE  FW ";
    static constexpr uint8_t kLineCap = 48;
    static constexpr uint8_t kMaskMenu = 1u << static_cast<uint8_t>(Key::Menu);
    static constexpr uint8_t kMaskUp = 1u << static_cast<uint8_t>(Key::Up);
    static constexpr uint8_t kMaskDown = 1u << static_cast<uint8_t>(Key::Down);

    BootSequence(IDisplay& displayRef, IKeypad& keypadRef, const IClock& clockRef,
                 const char* firmwareVersion);
    BootSequence(const BootSequence&) = delete;
    BootSequence& operator=(const BootSequence&) = delete;

    void begin(uint32_t bootAtMs, uint8_t bootKeyMask);
    void beginOnDemand();
    void tick();

    Stage stage() const { return stage_; }
    bool ownsDisplay() const { return stage_ != Stage::Idle && stage_ != Stage::Done; }
    bool finished() const { return stage_ == Stage::Done; }
    bool resetArmed() const { return resetCandidate_; }
    bool takeFactoryReset();
    Status lastStatus() const { return lastStatus_; }

private:
    void enter(Stage next, uint32_t atMs);
    void drawPattern(uint8_t index);
    void drawLogo();
    void drawMessage(const char* text);
    void centered(int16_t y, const char* text, TextFont font);
    void keep(Status status);
    bool upHeld() const;
    void watchReset(uint32_t nowMs);

    IDisplay& display_;
    IKeypad& keypad_;
    const IClock& clock_;
    const char* version_;
    Stage stage_;
    Status lastStatus_;
    uint32_t bootMs_;
    uint32_t stageSinceMs_;
    uint8_t pattern_;
    bool onDemand_;
    bool exitArmed_;
    bool resetCandidate_;
    bool resetPending_;
};

}  // namespace app
