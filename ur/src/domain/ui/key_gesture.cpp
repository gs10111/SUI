// Implementacao do reconhecedor de gesto. Contrato, citacoes de manual e justificativa das
// tres escolhas nao obvias (hold na borda dos 3000 ms, duplo toque entregue so no fim da janela
// do terceiro toque, gestos independentes) estao em domain/ui/key_gesture.h.
//
// Decisao 1, item 7 (APROVADA): 30 ms <= toque <= 600 ms, intervalo <= 400 ms, gesto <= 1600 ms,
// borda de MENU ou DOWN anula, terceiro toque anula.
#include "domain/ui/key_gesture.h"

namespace domain {

KeyGesture::KeyGesture(IKeypad& keypad, const IClock& clock)
    : keypad_(keypad),
      clock_(clock),
      queue_{},
      head_(0),
      count_(0),
      dropped_(0),
      down_{},
      holdFired_{},
      downSinceMs_{},
      doubleState_(DoubleState::Idle),
      doubleFirstPressMs_(0),
      doubleFirstReleaseMs_(0),
      doublePendingSinceMs_(0),
      doublePendingSpanMs_(0) {}

uint8_t KeyGesture::index(Key key) { return static_cast<uint8_t>(key); }

bool KeyGesture::tapDurationOk(uint16_t heldMs) {
    return heldMs >= kShortTapMinMs && heldMs <= kShortTapMaxMs;
}

void KeyGesture::update() {
    KeyEvent event{};
    while (keypad_.takeEvent(event)) {
        advanceTo(event.atMs);
        handle(event);
    }
    advanceTo(clock_.nowMs());
}

void KeyGesture::advanceTo(uint32_t timeMs) {
    for (uint8_t posicao = 0; posicao < kKeyCount; ++posicao) {
        if (!down_[posicao] || holdFired_[posicao]) {
            continue;
        }
        if (deadlineReached(downSinceMs_[posicao], timeMs, kHoldMs)) {
            holdFired_[posicao] = true;
            emit(GestureKind::Hold, static_cast<Key>(posicao), downSinceMs_[posicao] + kHoldMs);
        }
    }

    if (doubleState_ == DoubleState::Pending &&
        deadlineReached(doublePendingSinceMs_, timeMs, doublePendingSpanMs_)) {
        const uint32_t carimbo = doublePendingSinceMs_;
        resetDouble();
        emit(GestureKind::DoubleTap, kDoubleTapKey, carimbo);
    }
}

void KeyGesture::handle(const KeyEvent& event) {
    const uint8_t posicao = index(event.key);

    if (event.edge == KeyEdge::Press) {
        down_[posicao] = true;
        holdFired_[posicao] = false;
        downSinceMs_[posicao] = event.atMs;
        applyToDouble(event);
        return;
    }

    const bool prensagemConhecida = down_[posicao];
    const bool holdJaDisparado = holdFired_[posicao];
    down_[posicao] = false;
    holdFired_[posicao] = false;

    if (prensagemConhecida && !holdJaDisparado && tapDurationOk(event.heldMs)) {
        emit(GestureKind::ShortTap, event.key, event.atMs);
    }

    if (prensagemConhecida || event.key != kDoubleTapKey) {
        applyToDouble(event);
    }
}

void KeyGesture::applyToDouble(const KeyEvent& event) {
    if (event.key != kDoubleTapKey) {
        if (doubleState_ != DoubleState::Idle) {
            resetDouble();
        }
        return;
    }

    const bool prensagem = event.edge == KeyEdge::Press;

    switch (doubleState_) {
        case DoubleState::Idle:
            if (prensagem) {
                doubleState_ = DoubleState::FirstDown;
                doubleFirstPressMs_ = event.atMs;
            }
            return;

        case DoubleState::FirstDown:
            if (!prensagem && tapDurationOk(event.heldMs) &&
                elapsedMs(doubleFirstPressMs_, event.atMs) <= kDoubleSpanMaxMs) {
                doubleState_ = DoubleState::FirstUp;
                doubleFirstReleaseMs_ = event.atMs;
            } else {
                resetDouble();
            }
            return;

        case DoubleState::FirstUp:
            if (!prensagem) {
                resetDouble();
                return;
            }
            if (elapsedMs(doubleFirstReleaseMs_, event.atMs) <= kDoubleGapMaxMs &&
                elapsedMs(doubleFirstPressMs_, event.atMs) <= kDoubleSpanMaxMs) {
                doubleState_ = DoubleState::SecondDown;
            } else {
                doubleState_ = DoubleState::FirstDown;
                doubleFirstPressMs_ = event.atMs;
            }
            return;

        case DoubleState::SecondDown: {
            if (prensagem || !tapDurationOk(event.heldMs)) {
                resetDouble();
                return;
            }
            const uint32_t gastoMs = elapsedMs(doubleFirstPressMs_, event.atMs);
            if (gastoMs > kDoubleSpanMaxMs) {
                resetDouble();
                return;
            }
            uint32_t esperaMs = kDoubleGapMaxMs;
            const uint32_t restanteMs = kDoubleSpanMaxMs - gastoMs;
            if (restanteMs < esperaMs) {
                esperaMs = restanteMs;
            }
            doubleState_ = DoubleState::Pending;
            doublePendingSinceMs_ = event.atMs;
            doublePendingSpanMs_ = esperaMs;
            return;
        }

        case DoubleState::Pending:
            resetDouble();
            return;
    }
}

void KeyGesture::resetDouble() {
    doubleState_ = DoubleState::Idle;
    doubleFirstPressMs_ = 0;
    doubleFirstReleaseMs_ = 0;
    doublePendingSinceMs_ = 0;
    doublePendingSpanMs_ = 0;
}

void KeyGesture::emit(GestureKind kind, Key key, uint32_t atMs) {
    if (count_ >= kQueueCap) {
        ++dropped_;
        return;
    }
    queue_[(head_ + count_) % kQueueCap] = Gesture{kind, key, atMs};
    ++count_;
}

bool KeyGesture::takeGesture(Gesture& out) {
    if (count_ == 0) {
        return false;
    }
    out = queue_[head_];
    head_ = static_cast<uint8_t>((head_ + 1) % kQueueCap);
    --count_;
    return true;
}

void KeyGesture::flush() {
    keypad_.flush();
    head_ = 0;
    count_ = 0;
    resetDouble();
    for (uint8_t posicao = 0; posicao < kKeyCount; ++posicao) {
        down_[posicao] = false;
        holdFired_[posicao] = false;
        downSinceMs_[posicao] = 0;
    }
}

bool KeyGesture::doubleTapPending() const { return doubleState_ == DoubleState::Pending; }

uint32_t KeyGesture::droppedGestures() const { return dropped_; }

void KeyGesture::resetCounters() { dropped_ = 0; }

}  // namespace domain
