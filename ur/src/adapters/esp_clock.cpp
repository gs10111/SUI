// EspClock: leitura do contador de 64 bits em us do U1 ESP32-WROOM-32D (folha 1/2), base de
// tempo unica da DECISIONS.md secao 2.1. Sem estado, sem init, sem lock, sem bloqueio.
//
// Seguro de qualquer TAREFA. PROIBIDO de ISR em IRAM: nowMs()/nowUs(), os pools de literais
// deste arquivo, a vtable de IClock e o __udivdi3 da divisao por 1000 executam de FLASH
// (conferido por objdump do .o); durante o apagamento de setor da NVS a cache esta desligada e
// a chamada trava. Quem precisa de tempo dentro da ISR do WDI conta os proprios ticks de 1 kHz
// (800 ticks = 800 ms do token de liveness), nao chama o relogio. Ver o cabecalho de
// esp_clock.h para o contrato completo, inclusive a regra de nao misturar ms com us.
#include "adapters/esp_clock.h"

#include <esp_timer.h>

namespace adapters {
namespace {

constexpr uint64_t kUsPerMs = 1000u;

inline uint64_t sinceBootUs() { return static_cast<uint64_t>(esp_timer_get_time()); }

}  // namespace

uint32_t EspClock::nowMs() const { return static_cast<uint32_t>(sinceBootUs() / kUsPerMs); }

uint32_t EspClock::nowUs() const { return static_cast<uint32_t>(sinceBootUs()); }

}  // namespace adapters
