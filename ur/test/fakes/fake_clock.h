// Relogio de teste: o tempo so anda quando o teste manda.
//
// E o que torna verificavel todo prazo do produto sem esperar por ele - o hold de 3 s do MENU,
// o timeout de 2 minutos do Modo Programacao, a liberacao de 3000 ms da histerese de A3, o teto
// anti-chatter de 600 s e o bloqueio de senha de 60 s de A13. Um teste que dorme e um teste que
// vai ser desligado no primeiro dia em que a suite ficar lenta.
//
// Comeca em um valor alto de proposito: prazo calculado com "a > b" em vez da subtracao unsigned
// de i_clock.h quebra perto do wrap de 2^32 ms, e um relogio que comeca em zero esconde isso.
#pragma once

#include <stdint.h>

#include "ports/i_clock.h"

namespace test {

class FakeClock : public IClock {
public:
    static constexpr uint32_t kDefaultStartMs = 0xFFFF0000u;

    explicit FakeClock(uint32_t startMs = kDefaultStartMs)
        : nowMs_(startMs), nowUs_(startMs * 1000u) {}

    uint32_t nowMs() const override { return nowMs_; }
    uint32_t nowUs() const override { return nowUs_; }

    void advanceMs(uint32_t deltaMs) {
        nowMs_ += deltaMs;
        nowUs_ += deltaMs * 1000u;
    }

    void advanceUs(uint32_t deltaUs) {
        nowUs_ += deltaUs;
        nowMs_ += deltaUs / 1000u;
    }

    void setMs(uint32_t valueMs) {
        nowMs_ = valueMs;
        nowUs_ = valueMs * 1000u;
    }

private:
    uint32_t nowMs_;
    uint32_t nowUs_;
};

}  // namespace test
