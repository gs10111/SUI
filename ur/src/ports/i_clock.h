// src/ports/i_clock.h
// Relogio monotonico. Unica fonte de tempo do dominio: nenhuma regra chama millis().
// Alvo: EspClock (src/platform/esp_clock.cpp, esp_timer_get_time()).
// Fake: FakeClock (test/native) - o tempo so avanca quando o teste manda.
// REQ:  MAN-5.2-L81 e MAN-5.4-L101 (hold de ~3 s), MAN-5.3-L96 e MAN-5.4-L127
//       (timeouts de ~2 min), decisao 1 (janelas do duplo toque), decisao 4
//       (ciclo de 50 ms), decisao 8 (timeout de 30 ms por transacao).
#pragma once

#include <stdint.h>

class IClock {
public:
    virtual ~IClock() = default;
    IClock(const IClock&) = delete;
    IClock& operator=(const IClock&) = delete;

    // Milissegundos desde o boot. Monotonico, nunca anda para tras, envolve em
    // 2^32 ms (49,7 dias). Toda comparacao de prazo tem de usar elapsedMs()/
    // deadlineReached() abaixo, nunca "a > b".
    virtual uint32_t nowMs() const = 0;

    // Microssegundos desde o boot, mesma base de nowMs(). Envolve em 71,6 min.
    // So para medida de turnaround de fio e de pulso; nao usar em prazo longo.
    virtual uint32_t nowUs() const = 0;

protected:
    IClock() = default;
};

// Aritmetica de prazo imune ao wrap (subtracao unsigned). Valida para intervalos
// menores que 2^31 ms (24,8 dias), o que cobre todo prazo do produto.
constexpr uint32_t elapsedMs(uint32_t sinceMs, uint32_t nowMs) {
    return static_cast<uint32_t>(nowMs - sinceMs);
}

constexpr bool deadlineReached(uint32_t sinceMs, uint32_t nowMs, uint32_t spanMs) {
    return static_cast<uint32_t>(nowMs - sinceMs) >= spanMs;
}
