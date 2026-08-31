// Implementacao da senha de acesso ao Modo Programacao.
// Manual SUI-DI141388XY 5.3 L99 a L106, Tabela 1 L131, 5.4 L136 e 5.10 L224 a L237.
// Decisao A13: efetivacao unica no SAIR e bloqueio de 60 s temporario e volatil.
//
// submit() carimba atividade antes de qualquer decisao, porque digitar a senha e um gesto de
// tecla e L105 conta inatividade, nao ausencia de acerto: sem esse carimbo, o tecnico que erra
// a senha aos 119 s e reenvia cai no Modo Normal no meio da tentativa.
#include "domain/password.h"

namespace domain {

Password::Password(const IClock& clock)
    : clock_(clock),
      lockSinceMs_(0),
      lastActivityMs_(clock.nowMs()),
      stored_(kFactory),
      staged_(kFactory),
      attempts_(0),
      hasStaged_(false),
      lockActive_(false) {}

bool Password::load(uint16_t stored) {
    if (!inRange(stored)) {
        return false;
    }
    stored_ = stored;
    return true;
}

uint16_t Password::effective() const { return stored_; }

AccessResult Password::submit(uint16_t typed) {
    noteActivity();
    if (locked()) {
        return AccessResult::Locked;
    }
    if (lockActive_) {
        lockActive_ = false;
        attempts_ = 0;
    }
    if (typed == stored_) {
        attempts_ = 0;
        return AccessResult::Granted;
    }
    ++attempts_;
    if (attempts_ >= kMaxAttempts) {
        lockActive_ = true;
        lockSinceMs_ = clock_.nowMs();
    }
    return AccessResult::Wrong;
}

bool Password::locked() const {
    return lockActive_ && !deadlineReached(lockSinceMs_, clock_.nowMs(), kLockoutMs);
}

uint8_t Password::attemptsLeft() const {
    if (locked()) {
        return 0;
    }
    if (lockActive_) {
        return kMaxAttempts;
    }
    return static_cast<uint8_t>(kMaxAttempts - attempts_);
}

bool Password::stage(uint16_t candidate) {
    if (!inRange(candidate)) {
        return false;
    }
    staged_ = candidate;
    hasStaged_ = true;
    return true;
}

bool Password::staged() const { return hasStaged_; }

void Password::commitOnExit() {
    if (hasStaged_) {
        stored_ = staged_;
        hasStaged_ = false;
    }
}

void Password::noteActivity() { lastActivityMs_ = clock_.nowMs(); }

bool Password::timedOut() const {
    return deadlineReached(lastActivityMs_, clock_.nowMs(), kInactivityMs);
}

}  // namespace domain
