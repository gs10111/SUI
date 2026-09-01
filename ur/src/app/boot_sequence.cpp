// src/app/boot_sequence.cpp
// Implementacao da maquina de estados descrita em boot_sequence.h. Cada tick() faz no maximo um
// quadro de display e volta; nao ha laco de espera, nao ha delay e nao ha alocacao. As telas sao
// escritas sem acentuacao, na convencao das mensagens literais do manual ("RESET DE FABRICA"
// L246, "Alteracao bem sucedida!" L183). O padrao de autoteste vem do proprio adaptador de
// display por showPattern(), que e quem conhece a geometria de 256x64 do SSD1322.
#include "app/boot_sequence.h"

#include <string.h>

namespace app {

namespace {

// Coordenadas corrigidas em 2026-09-01, quando o invariante geometrico entrou em test_boot e
// mostrou duas violacoes reais que nenhuma afirmacao de conteudo enxergava:
//   kBrandY = 30 punha a caixa da marca em 30..58 sobre a do modelo em 52..64 (6 px de
//   sobreposicao), e kMessageY = 40 punha a mensagem em 40..68, quatro px FORA de um painel de
//   64. Nao aparecia porque a fonte grande tem 28 px de caixa e so cerca de 19 de tinta acima
//   da linha de base: a folga estava vindo de acaso de desenho da fonte, e nao do layout.
// Agora: marca 24..52, modelo 52..64, mensagem 34..62 - tudo dentro e sem cruzamento.
constexpr int16_t kBrandY = 24;
constexpr int16_t kModelY = 52;
constexpr int16_t kMessageY = 34;
constexpr int16_t kSelfTestTextY = 12;

}  // namespace

BootSequence::BootSequence(IDisplay& displayRef, IKeypad& keypadRef, const IClock& clockRef,
                           const char* firmwareVersion)
    : display_(displayRef),
      keypad_(keypadRef),
      clock_(clockRef),
      version_((firmwareVersion != nullptr) ? firmwareVersion : ""),
      stage_(Stage::Idle),
      lastStatus_(kOk),
      bootMs_(clockRef.nowMs()),
      stageSinceMs_(clockRef.nowMs()),
      pattern_(0xFF),
      onDemand_(false),
      exitArmed_(false),
      resetCandidate_(false),
      resetPending_(false) {}

void BootSequence::keep(Status status) {
    if (status.failed() && lastStatus_.ok()) {
        lastStatus_ = status;
    }
}

bool BootSequence::upHeld() const {
    return keypad_.pressedForMs(Key::Up) != 0u;
}

bool BootSequence::takeFactoryReset() {
    const bool pending = resetPending_;
    resetPending_ = false;
    return pending;
}

void BootSequence::begin(uint32_t bootAtMs, uint8_t bootKeyMask) {
    bootMs_ = bootAtMs;
    onDemand_ = false;
    exitArmed_ = false;
    resetPending_ = false;
    resetCandidate_ = ((bootKeyMask & kMaskUp) != 0u) &&
                      ((bootKeyMask & static_cast<uint8_t>(kMaskMenu | kMaskDown)) == 0u);
    pattern_ = 0xFF;
    enter(Stage::SelfTest, clock_.nowMs());
}

void BootSequence::beginOnDemand() {
    bootMs_ = clock_.nowMs();
    onDemand_ = true;
    exitArmed_ = false;
    resetPending_ = false;
    resetCandidate_ = false;
    pattern_ = 0xFF;
    enter(Stage::SelfTest, clock_.nowMs());
}

void BootSequence::enter(Stage next, uint32_t atMs) {
    stage_ = next;
    stageSinceMs_ = atMs;
    switch (next) {
        case Stage::SelfTest: drawPattern(0); break;
        case Stage::Logo: drawLogo(); break;
        case Stage::ResetMessage: drawMessage(kTextFactoryReset); break;
        case Stage::StuckKey: drawMessage(kTextStuckKey); break;
        case Stage::Idle:
        case Stage::Done: break;
    }
}

void BootSequence::centered(int16_t y, const char* text, TextFont font) {
    const uint16_t width = display_.textWidthPx(font, text);
    const uint16_t screen = display_.widthPx();
    const int16_t x = (width < screen) ? static_cast<int16_t>((screen - width) / 2u) : 0;
    keep(display_.drawText(x, y, text, font, TextInk::Normal));
}

void BootSequence::drawPattern(uint8_t index) {
    const uint8_t count = display_.patternCount();
    const uint8_t slot = (count == 0u) ? 0u : static_cast<uint8_t>(index % count);
    pattern_ = slot;
    keep(display_.showPattern(slot));
    char line[kLineCap];
    const size_t prefix = strlen(kTextSelfTest);
    size_t used = (prefix < kLineCap - 1u) ? prefix : static_cast<size_t>(kLineCap - 1u);
    memcpy(line, kTextSelfTest, used);
    const size_t room = static_cast<size_t>(kLineCap - 1u) - used;
    const size_t tail = strlen(version_);
    const size_t copy = (tail < room) ? tail : room;
    memcpy(line + used, version_, copy);
    used += copy;
    line[used] = '\0';
    keep(display_.drawText(2, kSelfTestTextY, line, TextFont::Small, TextInk::Normal));
    keep(display_.present());
}

void BootSequence::drawLogo() {
    keep(display_.clear());
    centered(kBrandY, kTextBrand, TextFont::Large);
    centered(kModelY, kTextModel, TextFont::Small);
    keep(display_.present());
}

void BootSequence::drawMessage(const char* text) {
    keep(display_.clear());
    centered(kMessageY, text, TextFont::Large);
    keep(display_.present());
}

void BootSequence::watchReset(uint32_t nowMs) {
    if (!resetCandidate_) {
        return;
    }
    if (!upHeld()) {
        resetCandidate_ = false;
        return;
    }
    if (deadlineReached(bootMs_, nowMs, kResetHoldMs)) {
        enter(Stage::ResetMessage, nowMs);
    }
}

void BootSequence::tick() {
    if (stage_ == Stage::Idle || stage_ == Stage::Done) {
        return;
    }
    const uint32_t nowMs = clock_.nowMs();
    switch (stage_) {
        case Stage::SelfTest: {
            const uint32_t held = elapsedMs(stageSinceMs_, nowMs);
            if (onDemand_) {
                if (keypad_.pressedMask() == 0u) {
                    exitArmed_ = true;
                }
                if ((exitArmed_ && keypad_.pressedMask() != 0u) ||
                    held >= kOnDemandCeilingMs) {
                    enter(Stage::Done, nowMs);
                    return;
                }
            } else if (held >= kSelfTestMs) {
                enter(Stage::Logo, nowMs);
                watchReset(nowMs);
                return;
            }
            const uint8_t slot = static_cast<uint8_t>(held / kPatternMs);
            if (slot != pattern_) {
                drawPattern(slot);
            }
            watchReset(nowMs);
            return;
        }
        case Stage::Logo:
            watchReset(nowMs);
            // A LOGOMARCA SEGURA A MAQUINA ENQUANTO O GESTO ESTIVER ARMADO. Sem esta guarda o
            // Reset de Fabrica era INALCANCAVEL: o splash soma kSelfTestMs + kLogoMs = 1200 ms
            // e, com os ~231 ms tipicos de setup(), a maquina chegava a Done por volta de
            // t = 1430 ms - bem antes dos kResetHoldMs = 3000 ms contados da entrada do
            // setup() (decisao 1 item 23). Em Done o tick() retorna na primeira linha, entao
            // watchReset() nunca mais rodava e o candidato morria com a tecla ainda prensada.
            // Enquanto resetCandidate_ vive, o operador esta com ▲ prensada: qualquer tick que
            // o veja soltar limpa o candidato em watchReset() e o proximo tick fecha a
            // logomarca normalmente. O custo maximo desta espera e o proprio prazo do gesto, e
            // ele corre no loop(), com a tarefa ctrl polando a sensora e comandando os quatro
            // reles o tempo todo.
            if (stage_ == Stage::Logo && !resetCandidate_ &&
                elapsedMs(stageSinceMs_, nowMs) >= kLogoMs) {
                enter(Stage::Done, nowMs);
            }
            return;
        case Stage::ResetMessage: {
            const uint32_t shown = elapsedMs(stageSinceMs_, nowMs);
            if (upHeld()) {
                if (deadlineReached(bootMs_, nowMs, kStuckKeyDeadlineMs)) {
                    resetCandidate_ = false;
                    enter(Stage::StuckKey, nowMs);
                }
                return;
            }
            if (shown >= kResetMessageMs) {
                resetPending_ = resetCandidate_;
                resetCandidate_ = false;
                enter(Stage::Done, nowMs);
            }
            return;
        }
        case Stage::StuckKey:
            if (elapsedMs(stageSinceMs_, nowMs) >= kStuckKeyMessageMs) {
                enter(Stage::Done, nowMs);
            }
            return;
        case Stage::Idle:
        case Stage::Done: return;
    }
}

}  // namespace app
