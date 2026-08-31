// EspClock: relogio monotonico do U1 ESP32-WROOM-32D (folha 1/2), unica fonte de tempo do
// firmware da UR. Implementa ports/i_clock.h e tem de ser substituivel pelo FakeClock de
// test/fakes/fake_clock.h: mesma semantica, mesmos limites de wrap, sem pre-condicao de init.
//
// Implementa a base de tempo unica da DECISIONS.md secao 2.1 (poll de 50 ms, timeout de
// 35 ms por transacao, avaliacao de limite no mesmo tick de 50 ms) e, por consequencia, todo
// prazo citado no cabecalho da porta: hold de 3 s (MAN-5.2-L81, MAN-5.4-L101), timeout de
// 2 min (MAN-5.3-L96, MAN-5.4-L127), janelas do duplo toque (decisao 1) e os 30 ms de
// turnaround da decisao 8.
//
// Fonte unica: esp_timer_get_time(), o contador de 64 bits em microssegundos do SoC. NAO se
// usa millis() com micros(): sao dois caminhos de leitura distintos e comparar um prazo em ms
// com uma medida em us tirada do outro deixa os dois derivarem.
//
// UNIDADES - NAO MISTURAR. Cada chamada faz a SUA propria leitura do contador. A relacao
// nowUs = nowMs*1000 + r, com r em [0, 1000) us, vale so DENTRO DE UM INSTANTE, nunca entre
// duas chamadas: entre um nowMs() e um nowUs() cabe uma preempcao da tarefa ctrl (prio 5) ou
// uma janela de cache-off da NVS de ate ~100 ms (DECISIONS.md secao 2.1, ressalva da decisao 5),
// e r vira dezenas de milhares de us. Se a ordem for nowUs() ANTES de nowMs(), a diferenca e
// negativa e a subtracao unsigned devolve algo perto de 2^32. REGRA: cada prazo mede em UMA
// unidade so - prazo longo por nowMs() com elapsedMs()/deadlineReached() de ports/i_clock.h,
// turnaround de fio por um par nowUs()/nowUs(). Nunca as duas unidades no mesmo calculo.
//
// O FakeClock e MAIS FROUXO neste ponto, e a divergencia e nesta direcao: advanceUs() faz
// nowMs_ += deltaUs / 1000u, truncando a CADA chamada, entao no host nowUs e nowMs derivam sem
// limite (mil advanceUs(999) somam 999000 us e 0 ms). O adaptador nunca deriva - as duas
// funcoes saem do mesmo contador de 64 bits. Consequencia: NENHUMA regra do dominio pode
// depender da relacao entre as duas unidades; ela nao e verificavel no host, e um teste que se
// apoiasse nela passaria por acidente sem provar nada sobre a placa.
//
// Wrap: nowMs() e o contador de 64 bits dividido por 1000 e truncado em 32 bits, ou seja
// envolve em 2^32 ms (49,7 dias), como a porta promete - e nao em 71,6 min, que e o que sairia
// de truncar primeiro e dividir depois. nowUs() e o mesmo contador truncado em 32 bits,
// envolvendo em 71,6 min. Nenhum acumulador proprio, nenhum estado: sem drift e sem salto no
// wrap; toda comparacao de prazo continua sendo de elapsedMs()/deadlineReached().
//
// BLOQUEIO: nenhum. As duas funcoes leem o contador de hardware sem lock, sem espera e sem
// dormir; pior caso O(1) (nowMs() paga um __udivdi3 de software, dezenas de ciclos, porque o
// ESP32 nao tem divisor de 64 bits). Cabe folgado no tick de 50 ms da tarefa ctrl.
//
// CONTEXTO DE CHAMADA: seguro de QUALQUER TAREFA. PROIBIDO de ISR em IRAM. nowMs()/nowUs(),
// os pools de literais das duas, a vtable de IClock (.rodata) e o __udivdi3 da divisao por
// 1000 executam de FLASH - conferido por objdump do .o: .text._ZNK8adapters8EspClock5nowMsEv
// e ...nowUsEv, com .literal.* proprios e callx8 para esp_timer_get_time e para __udivdi3
// (libgcc _udivdi3.o, que o sections.ld NAO coloca em IRAM: a regra de IRAM cobre so
// libgcc.a:lib2funcs.*). Durante o apagamento de setor da NVS a cache esta desligada e a
// chamada trava - exatamente a janela que a ISR de 1 kHz do WDI existe para cobrir
// (DECISIONS.md secao 2.2, passo 1). Quem precisa de tempo dentro dessa ISR conta os PROPRIOS
// ticks de 1 kHz (800 ticks = os 800 ms de kCtrlLivenessDeadlineMs do token de liveness), nao
// chama este relogio. Nao ha, de proposito, um atalho "IRAM-safe" aqui: o unico pedaco em IRAM
// e o esp_timer_get_time() da IDF (.iram1.27 em esp_timer_impl_lac.c.obj), que e detalhe de
// implementacao da IDF e nao contrato - promete-lo neste cabecalho seria uma promessa que o
// proximo upgrade de core pode quebrar sem aviso.
//
// t=0: esp_timer_get_time() zera na inicializacao do esp_timer, que ocorre antes de setup(),
// nao no reset do STWD100 nem no power-on. O orcamento de boot dos 13 passos e medido a partir
// do reset; o bootloader mais o init da IDF (~300 ms, kBootloaderDeadTimeMs) NAO aparecem neste
// relogio e tem de sair do osciloscopio (medicao 3), nunca de nowMs().
//
// Monotonicidade: garantida sem sleep. Deep/light sleep nao sao usados neste produto; se algum
// dia forem ligados, a promessa de "nunca anda para tras" da porta tem de ser reverificada.
#pragma once

#include <stdint.h>

#include "ports/i_clock.h"

namespace adapters {

class EspClock final : public IClock {
public:
    EspClock() = default;

    uint32_t nowMs() const override;
    uint32_t nowUs() const override;
};

}  // namespace adapters
