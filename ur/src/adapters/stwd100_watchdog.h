// Adaptador do watchdog externo STWD100YNYWY3F (CI4), folha 1/2 do DE-PURI-DI261924 REV A:
// WDI em IO19 (board::kWdi), RST# do CI4 ligado ao EN pelo jumper J15, EN com pull-down
// interno - o cachorro esta SEMPRE habilitado, nao existe desliga-lo por software.
// Datasheet ST DocID14134 Rev 11: tWD 1,12 s min / 1,6 s tip / 2,24 s max, tPW 210 ms,
// largura minima de WDI 1 us, rejeicao de glitch 100 ns.
//
// Implementa a porta src/ports/i_watchdog.h. REQ: decisao 7 item 12 (laco travado tem de
// virar reset), decisao 12 item 7 e passo 10 da ordem de boot (U8g2/SPI.begin() sequestra o
// IO19, que e o MISO default do VSPI: rearmPin obrigatorio), decisao 13 item 16 (janela de
// cache-off da NVS orcada em 500 ms contra os 800 ms do token), MAN-7-L297..300.
//
// MECANISMO, item 6 da Parte 2 (base comum) de DECISIONS.md e passo 1 da ordem de boot:
// o chute NAO sai de esp_timer. O callback de esp_timer roda em ESP_TIMER_TASK, que executa
// de flash e PARA durante o apagamento de setor da NVS (cache desabilitada) - foi essa a
// falha cedida em DECISIONS.md, "o chute do watchdog nao era independente". Aqui o chute sai
// de ISR de timer de HARDWARE (grupo 0, timer 1) a 1 kHz, alocada com ESP_INTR_FLAG_IRAM e
// escrita inteira em IRAM_ATTR: a cada 250 ticks a ISR levanta o WDI por GPIO.out_w1ts e no
// tick seguinte o baixa por GPIO.out_w1tc. Pulso de 1 ms - 10.000x acima da rejeicao de
// glitch de 100 ns e 210x abaixo do tPW. Sem digitalWrite, sem delayMicroseconds, sem
// busy-wait e sem tocar em nada que more em flash (.rodata inclusive) dentro da ISR.
//
// LED LIG (IO2 / CN4-1) NA MESMA ISR, decisao 12 item 14: enablePowerLed(), chamado no passo 14
// da ordem de boot (depois da janela de strapping do IO2), poe o LED a piscar 900 ms aceso /
// 100 ms apagado a partir do MESMO contador de 1 ms desta ISR e sob o MESMO token de liveness.
// Consequencia pretendida: LED aceso com uma piscada curta por segundo = alimentada E firmware
// vivo; LED apagado = sem alimentacao, firmware travado ou placa em modo download. Um LED de
// "LIG" aceso continuamente por GPIO mente exatamente quando importa - fica aceso no travamento,
// que e o unico instante em que alguem o olharia. Por isso ele nao pode nascer no loop().
//
// TOKEN DE LIVENESS: a ISR so pulsa enquanto o token renovado por heartbeat() tiver menos de
// kLivenessDeadlineMs. Passado o prazo ela PARA de pulsar e o STWD100 reseta a placa. Sem
// esse gate o chute seria incondicional, e um periferico que pulsa com o firmware morto e um
// watchdog desligado - foi por isso que DECISIONS.md refutou gerar o WDI por LEDC ou RMT.
//
// JANELA DE CARENCIA DE BOOT (kBootGraceMs), E NAO UMA JANELA SEM FIM. Antes do primeiro
// heartbeat() nao existe token: a tarefa ctrl, sua unica dona, so e criada no passo 13 da
// ordem de boot, e o setup() canonico bloqueia 231 ms no tipico e 971 ms no pior caso de NVS
// virgem (kBootBlockingWorstMs) - mais que os 800 ms do prazo. Semear o token em begin()
// transformaria o pior caso de boot em boot loop. Por isso a ISR chuta INCONDICIONALMENTE,
// mas so durante kBootGraceMs contados do begin(); depois disso, sem nenhum heartbeat, ela
// para de pulsar e a placa reseta. E o que cobre o modo de falha em que a tarefa ctrl nunca
// nasce (xTaskCreate do passo 13 falhando por heap) ou morre antes do primeiro heartbeat:
// sem essa carencia com prazo, o cachorro ficaria alimentado para sempre com o firmware sem
// dono, reles congelados no ultimo estado e a saida analogica retendo o ultimo angulo.
// kicking() enxerga a mesma regra, e por isso denuncia a carencia vencida.
//
// DIVERGENCIA ABERTA, AGUARDANDO O DONO DA PORTA (nao ha aprovacao registrada no repositorio
// para nenhum dos dois numeros): o cabecalho da porta anota heartbeatTimeoutMs() como "750 ms
// (3 chutes de margem)"; a base comum, Parte 2 item 6 e a constante kCtrlLivenessDeadlineMs
// de DECISIONS.md 2.5, fixam 800 ms - e sao os 800 ms que o item 16 da decisao 13 orca contra
// os 500 ms de cache-off da NVS e que a aritmetica de travamento da decisao 13 item 16 usa.
// Mantidos os 800 ms porque decisao aprovada nao se reabre num adaptador e porque a tarefa
// ctrl e dimensionada contra a base comum; os dois numeros dao 3 chutes de margem e ambos
// cabem sob o tWD minimo (800+250 = 1050 ms contra 1120 ms; com 750 seriam 1000 ms). O
// adaptador NAO arbitra: quem fecha e o dono da porta / bigboss, e enquanto nao fecha o
// numero e publicado uma unica vez, por heartbeatTimeoutMs(), para que a tarefa ctrl nao
// tenha uma segunda copia dele. Ver tambem a nota de kLivenessDeadlineMs abaixo.
//
// LATENCIA DE RESET, A CONTA COMPLETA (este e o unico arquivo que conhece os dois termos):
// os chutes so saem na cadencia de 250 ms, entao o ultimo pulso apos a ctrl parar sai no
// maximo em 750 ms (os ticks de 250, 500 e 750 ms ainda veem idade < 800 ms; o de 1000 ms
// nao). Travamento -> reset = 750 ms + tWD (1120 a 2240 ms) = 1,87 s a 2,99 s; a saida
// analogica so e corrigida no passo 5 do boot: + bootloader (~300 ms) + 6 ms = ate 3,30 s
// exibindo um angulo velho. NAO sao os "~2,6 s" da base comum 2.4 e do manual 6.2, que nao
// somaram o token; DECISIONS.md decisao 13 item 16 ja refez essa aritmetica e declarou os
// 3,30 s - o manual 6.2 e que ainda tem de acompanhar. O static_assert do .cpp trava o numero
// publicado. Travamento ANTES do primeiro heartbeat (setup() preso) vira reset em
// kBootGraceMs + tWD = 4,12 s a 5,24 s, pior por desenho e limitado ao boot.
//
// BLOQUEIO DECLARADO (regra do tick de 50 ms da base comum): heartbeat() e kicking() sao O(1)
// sem espera, com secao critica de spinlock abaixo de 1 us. kickNow() bloqueia
// board::kWdiPulseUs = 5 us. rearmPin() custa um pinMode. begin() e o unico bloqueante
// relevante: ~1,5 ms tipicos e 5,5 ms no pior caso, porque PROVA que a ISR esta correndo
// esperando o primeiro tique com teto de kIsrProbeTimeoutUs; roda uma unica vez, no passo 1
// do boot, cujo orcamento de 0,5 ms passa a 5,5 ms no pior caso (continua desprezivel diante
// dos 971 ms do setup e dos 500 ms de lacuna de WDI declarados para o boot).
//
// SINGLETON POR CONSTRUCAO: ha um unico pino WDI e um unico timer de hardware reservado, e a
// ISR do core 2.x nao recebe argumento. O estado que a ISR toca vive em .bss (DRAM) no .cpp;
// a segunda instancia que chamar begin() recebe Err::Busy. Instancia sem begin() bem-sucedido
// NAO mexe no cachorro nem nos contadores: heartbeat() e kickNow() sao no-op e todos os
// contadores devolvem zero, exatamente como um fake da porta faz (test/fakes/fake_watchdog.h).
#pragma once

#include <stdint.h>

#include "board_pins.h"
#include "ports/i_watchdog.h"
#include "status.h"

class Stwd100Watchdog final : public IWatchdog {
public:
    // Numeros do CI (datasheet ST DocID14134 Rev 11) e do mecanismo desta ISR.
    static constexpr uint32_t kMaxTimeoutMs = 2240;
    static constexpr uint32_t kResetPulseMs = 210;
    static constexpr uint32_t kMinPulseUs = 1;
    static constexpr uint32_t kGlitchRejectNs = 100;
    static constexpr uint32_t kIsrTickHz = 1000;
    static constexpr uint32_t kPulseMs = 1;

    // POLITICA DA BASE COMUM, NAO PROPRIEDADE DO CI: espelha kCtrlLivenessDeadlineMs de
    // DECISIONS.md 2.5 (Parte 2, base comum), que e o orcamento de atraso tolerado da tarefa
    // ctrl. Nao ha, hoje, cabecalho de constantes da base comum no repositorio (a Parte 2 de
    // DECISIONS.md e a fonte); quando ele existir, esta constante passa a referenciar
    // urbase::kCtrlLivenessDeadlineMs em vez de repetir o numero. Enquanto isso o valor e
    // publicado por heartbeatTimeoutMs() e a tarefa ctrl le dali - nunca uma segunda copia.
    static constexpr uint32_t kLivenessDeadlineMs = 800;
    static constexpr uint32_t kLedOnMs = 900;
    static constexpr uint32_t kLedOffMs = 100;

    // Carencia do boot: teto absoluto do chute incondicional antes do primeiro heartbeat().
    // Cobre os 971 ms de setup no pior caso mais os 1200 ms de splash nao bloqueante, com
    // folga. Vencida sem token, a ISR para de pulsar e a placa reseta.
    static constexpr uint32_t kBootGraceMs = 3000;

    // timer_dev[2] do core 2.x = grupo 0, timer 1. O .cpp trava esse par com static_assert.
    static constexpr uint8_t kTimerIndex = 2;
    static constexpr uint16_t kTimerDivider = 80;
    static constexpr uint32_t kTimerTicksPerIsr = 1000;

    // Teto da prova de tique feita em begin(). Em 1 kHz o primeiro tique vem em ~1 ms.
    static constexpr uint32_t kIsrProbeTimeoutUs = 5000;

    Stwd100Watchdog();

    // Assume o WDI, pulsa uma vez e arma a ISR. Devolve Err::Busy se outra instancia ja
    // armou, Err::Param se o pino nao existe e Err::HwFault se o timer nao abriu, se a ISR
    // nao pode ser instalada ou se ela NAO tiquetou dentro de kIsrProbeTimeoutUs - nesse
    // ultimo caso o timer e desfeito e nada fica meio armado. Bloqueia ate ~5,5 ms.
    Status begin() override;

    void heartbeat() override;
    void kickNow() override;
    Status rearmPin() override;
    Status enablePowerLed();
    bool powerLedArmed() const;

    // GANCHO DE ESTADO SEGURO, chamado UMA UNICA VEZ no tique em que o portao fecha - isto e, no
    // instante em que esta ISR declara o firmware morto e para de alimentar o STWD100. Dali ate
    // o reset passam de 1,12 a 2,24 s de tWD, e sem este gancho os quatro reles ficam congelados
    // no ultimo nivel permissivo durante todo esse tempo, apresentando "sem alarme" com o
    // firmware ja declarado morto pelo proprio firmware. Existia caminho pronto para isso
    // (RelayBankGpio::signalAllFromIsr, IRAM_ATTR e nao virtual) e ele nao era chamado de lugar
    // nenhum ate a etapa 8: mecanismo de seguranca construido e nao ligado e pior que ausente,
    // porque parece cobertura.
    // CONTRATO DO ALVO, e nao ha como o compilador cobra-lo: funcao livre marcada IRAM_ATTR, sem
    // despacho virtual, sem acesso a .rodata e sem chamada de biblioteca - as mesmas regras da
    // propria ISR, porque a cache de flash pode estar desligada. Depois de disparado, a placa
    // esta em falha declarada e ninguem retoma escrita normal: o que vem a seguir e o reset.
    // Registrar exige begin() bem-sucedido (Err::NotInit em caso contrario); nullptr desliga.
    Status setSafeStateHook(void (*hook)());
    uint32_t safeStateCalls() const;

    bool kicking() const override;
    uint32_t kickPeriodMs() const override;
    uint32_t heartbeatTimeoutMs() const override;
    uint32_t minTimeoutMs() const override;
    uint32_t typTimeoutMs() const override;
    uint32_t kickCount() const override;
    uint32_t heartbeatCount() const override;

    uint32_t maxTimeoutMs() const { return kMaxTimeoutMs; }
    uint32_t resetPulseMs() const { return kResetPulseMs; }
    uint32_t pulseMs() const { return kPulseMs; }
    uint32_t manualPulseUs() const { return board::kWdiPulseUs; }
    uint32_t bootGraceMs() const { return kBootGraceMs; }
    board::Pin pin() const { return board::kWdi; }
    bool ready() const { return ready_; }

    bool livenessArmed() const;
    uint32_t livenessAgeMs() const;
    uint32_t isrTickMs() const;

private:
    bool ready_;
    volatile uint32_t manualKicks_;
    volatile uint32_t heartbeats_;
};
