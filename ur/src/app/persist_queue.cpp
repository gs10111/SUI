// src/app/persist_queue.cpp
// Implementacao da fila de slots sujos descrita em app/persist_queue.h. Aritmetica inteira,
// sem alocacao, sem relogio e sem I/O: o unico estado sao tres vetores de dois elementos.
#include "app/persist_queue.h"

namespace app {

PersistQueue::PersistQueue() : dirty_{false, false}, gaveUp_{false, false}, attempts_{0, 0} {}

void PersistQueue::markDirty(Slot slot) {
    if (!slotValid(slot)) {
        return;
    }
    dirty_[idx(slot)] = true;
    // Marcacao nova e um pedido novo: o orcamento de tentativas volta ao inicio. Sem isto, um
    // slot que esgotou as tentativas ha uma hora entregaria a proxima gravacao ja desistindo.
    attempts_[idx(slot)] = 0;
}

bool PersistQueue::dirty(Slot slot) const {
    return slotValid(slot) && dirty_[idx(slot)];
}

bool PersistQueue::anyDirty() const {
    for (uint8_t i = 0; i < kSlotCount; ++i) {
        if (dirty_[i]) {
            return true;
        }
    }
    return false;
}

uint8_t PersistQueue::attempts(Slot slot) const {
    return slotValid(slot) ? attempts_[idx(slot)] : static_cast<uint8_t>(0);
}

bool PersistQueue::nextSlot(Slot& out) const {
    for (uint8_t i = 0; i < kSlotCount; ++i) {
        if (dirty_[i]) {
            out = static_cast<Slot>(i);
            return true;
        }
    }
    return false;
}

void PersistQueue::noteResult(Slot slot, bool ok) {
    if (!slotValid(slot)) {
        return;
    }
    const uint8_t i = idx(slot);
    if (ok) {
        dirty_[i] = false;
        attempts_[i] = 0;
        return;
    }
    if (attempts_[i] < kMaxAttempts) {
        ++attempts_[i];
    }
    if (attempts_[i] >= kMaxAttempts) {
        // Desistencia: o bit sai da fila para o loop() nao ficar reapagando setor a 20 Hz, e a
        // perda vira evento observavel em vez de silencio. O OUTRO slot continua exatamente como
        // estava - e este o ponto do arquivo.
        dirty_[i] = false;
        gaveUp_[i] = true;
    }
}

bool PersistQueue::takeGaveUp(Slot& out) {
    for (uint8_t i = 0; i < kSlotCount; ++i) {
        if (gaveUp_[i]) {
            gaveUp_[i] = false;
            out = static_cast<Slot>(i);
            return true;
        }
    }
    return false;
}

}  // namespace app
