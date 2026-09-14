// O PORTAO DO WATCHDOG, puro: decide se a ISR pode pulsar o WDI neste tique.
//
// Vive fora do driver, sem Arduino e sem driver/timer, por dois motivos que nao sao de estilo:
//
// 1. E ARITMETICA QUE ATRAVESSA O WRAP DE 2^32. A sensora fica energizada meses; aos 49,7 dias
//    o contador de tiques volta a zero. Uma comparacao escrita como "tick > batida + prazo"
//    reabre o portao no wrap e o watchdog vira decoracao por 49,7 dias. A subtracao unsigned
//    atravessa; e ela que esta aqui, e ha teste de host na fronteira.
// 2. ELA RODA DENTRO DE UMA ISR EM IRAM, onde nao ha como olhar. O que se pode provar antes,
//    prova-se antes.
//
// A REGRA, e por que ela tem duas pernas:
//   - COM TOKEN (alguem ja chamou heartbeat()): o portao fica aberto enquanto a ultima batida
//     tiver menos de kLivenessDeadlineTicks. Passado o prazo, a ISR PARA de pulsar e o STWD100
//     reseta a placa. Sem esse portao, migrar o chute para IRAM produziria um defeito PIOR que
//     o de hoje: uma ISR que pulsa incondicionalmente alimenta o cachorro para sempre, e um
//     firmware travado nunca mais reseta.
//   - SEM TOKEN (antes do primeiro heartbeat): vale a CARENCIA DE BOOT, que TEM FIM. O setup()
//     da sensora bloqueia antes de o laco principal existir; semear o token em begin()
//     transformaria um boot lento em boot loop. Mas uma carencia sem fim cobriria para sempre o
//     modo de falha em que o laco nunca comeca - por isso ela vence em kBootGraceTicks.
#pragma once

#include <stdint.h>

namespace wdt {

// 1 kHz: um tique por milissegundo. Todos os prazos abaixo sao, portanto, em ms.
constexpr uint32_t kIsrTickHz = 1000;

// Cadencia do pulso no WDI. Vem de board::kWdtKickPeriodMs (250 ms), repetida aqui como numero
// puro para que o teste de host possa conferir a margem sem incluir board_pins.h.
constexpr uint32_t kKickPeriodTicks = 250;

// Prazo do token de liveness. Mesmo numero da base comum da UR (DECISIONS.md 2.5,
// kCtrlLivenessDeadlineMs): 800 ms, tres chutes de margem.
constexpr uint32_t kLivenessDeadlineTicks = 800;

// Carencia de boot. Mais larga que o pior caso de setup() da sensora e ainda assim finita.
constexpr uint32_t kBootGraceTicks = 3000;

// tWD minimo do STWD100 (ST DocID14134 Rev 11). O prazo mais a cadencia tem de caber aqui.
constexpr uint32_t kWdtMinTimeoutMs = 1120;

static_assert(kLivenessDeadlineTicks + kKickPeriodTicks < kWdtMinTimeoutMs,
              "prazo do token mais a cadencia de chute passam do tWD minimo: o cachorro morderia "
              "antes de o firmware ser declarado morto");
static_assert(kKickPeriodTicks * 3u <= kWdtMinTimeoutMs,
              "menos de tres chutes dentro do tWD minimo: margem insuficiente");
static_assert(kBootGraceTicks > kLivenessDeadlineTicks,
              "carencia de boot menor que o proprio prazo do token");

// tickMs e o contador da ISR; lastBeatTickMs, o valor dele na ultima batida. Os dois sao
// unsigned e a diferenca e tomada unsigned: e assim que o wrap de 2^32 atravessa sem caso
// especial.
constexpr bool gateOpen(uint32_t tickMs, uint32_t lastBeatTickMs, bool livenessArmed) {
    return livenessArmed ? ((tickMs - lastBeatTickMs) < kLivenessDeadlineTicks)
                         : (tickMs < kBootGraceTicks);
}

}  // namespace wdt
