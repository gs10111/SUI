# Portas da arquitetura hexagonal — SUI / UR-DI151399

Nove headers C++17, verificados com
`g++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Wshadow -I lib_shared/depuri_core/include`
contra um fake que implementa **todas** as nove portas (compila limpo, zero warnings).

Regras honradas em todos: nenhum `Arduino.h` / `SPI.h` / `Preferences.h` / `esp_*.h`; nenhum `throw`, nenhum `dynamic_cast`; nenhuma alocacao (todo buffer e do chamador); nenhum `float` no caminho de decisao (angulo = `int16_t` em decimos de grau, saida analogica = `uint16_t` em codigo de DAC); erro sempre por `Status`/`Err` de `lib_shared/depuri_core/include/status.h`. Toda porta e nao copiavel (construtor protegido, copia deletada) para impedir slicing acidental de um adaptador com estado de hardware.

---

```cpp
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
```

```cpp
// src/ports/i_watchdog.h
// Watchdog externo STWD100YNYWY3F (ST DocID14134 Rev 11), WDI em IO19, RST# -> EN por J15.
// Alvo: ExtWatchdog (src/drivers/ext_wdt.cpp) - pulso de 5 us gerado por esp_timer
//       periodico, NUNCA pelo laco; o esp_timer so pulsa se houver heartbeat recente.
// Fake: FakeWatchdog (test/native) - conta chutes e heartbeats, expoe "teria resetado?".
// REQ:  decisao 7 item 12 (laco travado tem de virar reset), decisao 12 item 7
//       (U8g2/SPI.begin() sequestra IO19: rearmPin obrigatorio depois do display),
//       MAN-7-L297..300 (falha e falta de energia).
#pragma once

#include <stdint.h>

#include "status.h"

class IWatchdog {
public:
    virtual ~IWatchdog() = default;
    IWatchdog(const IWatchdog&) = delete;
    IWatchdog& operator=(const IWatchdog&) = delete;

    // Assume o pino WDI e arma a geracao periodica de pulso fora do laco.
    virtual Status begin() = 0;

    // Prova de vida do laco principal. TEM de ser chamada uma vez por ciclo de
    // controle. O gerador de pulso so continua chutando enquanto houver heartbeat
    // dentro de heartbeatTimeoutMs(); sem isso um laco travado nunca reseta a placa.
    virtual void heartbeat() = 0;

    // Pulso imediato, fora da cadencia. So para trechos longos e conhecidos do
    // setup (ex.: settle de 100 ms do SCL3300). Nunca substitui heartbeat().
    virtual void kickNow() = 0;

    // Retoma a posse eletrica do pino WDI. Obrigatorio depois de qualquer
    // inicializacao que reconfigure o barramento SPI (o display prende IO19).
    virtual Status rearmPin() = 0;

    virtual bool kicking() const = 0;
    virtual uint32_t kickPeriodMs() const = 0;       // 250 ms
    virtual uint32_t heartbeatTimeoutMs() const = 0; // 750 ms (3 chutes de margem)
    virtual uint32_t minTimeoutMs() const = 0;       // 1120 ms (STWD100)
    virtual uint32_t typTimeoutMs() const = 0;       // 1600 ms
    virtual uint32_t kickCount() const = 0;
    virtual uint32_t heartbeatCount() const = 0;

protected:
    IWatchdog() = default;
};
```

```cpp
// src/ports/i_indicator.h
// Sinalizacao luminosa CONTROLAVEL por firmware. Na DE-PURI-DI261924 existe UMA
// unica: CN4-1 (net LED_TEST = IO2), que e o "LED LIG" do manual. Os quatro LEDs
// de limite do CN3 penduram no MESMO net da base do BC337 de cada rele: nao ha
// como acende-los sem acionar o rele, portanto eles NAO pertencem a esta porta -
// pertencem a IRelayBank e sao efeito colateral fisico dela.
// Alvo: GpioIndicator (src/platform/gpio_indicator.cpp) - IO2, ativo alto,
//       cadencia derivada de IClock; IO2 e pino de strapping, escrito so depois do boot.
// Fake: FakeIndicator (test/native) - guarda o padrao corrente e conta transicoes.
// REQ:  MAN-5-L67 ("o LED LIG e acionado, indicando que a UR esta alimentada e em
//       operacao"), decisao 8 item H (piscar 2 Hz enquanto durar a falha de link).
#pragma once

#include <stdint.h>

#include "status.h"

enum class IndicatorId : uint8_t {
    Power = 0,  // CN4-1 "LED LIG"
};

constexpr uint8_t kIndicatorCount = 1;

enum class IndicatorPattern : uint8_t {
    Off = 0,      // apagado
    On,           // aceso continuo: equipamento vivo e link sadio
    Blink1Hz,     // 500 ms aceso / 500 ms apagado
    Blink2Hz,     // 250 ms / 250 ms: falha de comunicacao com a sensora
};

class IIndicator {
public:
    virtual ~IIndicator() = default;
    IIndicator(const IIndicator&) = delete;
    IIndicator& operator=(const IIndicator&) = delete;

    // Deixa o pino em nivel definido (apagado) antes de virar saida.
    virtual Status begin() = 0;

    virtual uint8_t count() const = 0;
    virtual Status set(IndicatorId id, IndicatorPattern pattern) = 0;
    virtual IndicatorPattern pattern(IndicatorId id) const = 0;

    // Avanca a cadencia de piscar. Chamada uma vez por ciclo de controle.
    // Nao bloqueia e nao decide nada: quem escolhe o padrao e o dominio.
    virtual void service() = 0;

    // Nivel eletrico corrente do pino, para o roteiro de fabrica. NAO e leitura
    // de volta do LED: nao ha realimentacao optica nesta placa.
    virtual bool driven(IndicatorId id) const = 0;

protected:
    IIndicator() = default;
};
```

```cpp
// src/ports/i_display.h
// Painel OLED do CN4 (SSD1322, 256x64, SPI 4 fios, sem MISO). Porta de DESENHO:
// nao conhece teclado, nao conhece menu, nao conhece angulo. So pixels e texto.
// ATENCAO DE SEGURANCA: o CN4 nao tem via de leitura de volta. begin() bem
// sucedido prova apenas que o barramento foi configurado, NUNCA que o painel
// respondeu (verifiable() == false). Nenhuma decisao de rele ou de saida
// analogica pode depender desta porta, e a ausencia de imagem jamais inibe a
// atuacao dos reles.
// Alvo: U8g2Display (src/drivers/display_u8g2.cpp), SSD1322 NHD 256x64 4W HW SPI.
// Fake: FakeDisplay (test/native) - framebuffer 256x64 em array estatico, com
//       captura das strings desenhadas para assercao literal das telas do manual.
// REQ:  MAN-2.1-L26 (OLED 3,2", 256x64), MAN-4-L60 (indicacao da medicao),
//       MAN-5-L66..68 (autoteste do display e logomarca), MAN-5.3..5.11 (telas
//       literais), MAN-7-L297 (mensagem de falha de comunicacao),
//       decisao 12 (padrao de verificacao, batimento de 1 Hz, deslocamento anti-burn-in).
#pragma once

#include <stdint.h>

#include "status.h"

enum class TextFont : uint8_t {
    Small = 0,  // legendas, rodape, itens de menu
    Large,      // area de medicao (X: e Y:) e mensagens de falha
};

enum class TextInk : uint8_t {
    Normal = 0,  // pixel aceso sobre fundo apagado
    Inverse,     // fundo aceso, glifo apagado: marca o digito em edicao
};

class IDisplay {
public:
    virtual ~IDisplay() = default;
    IDisplay(const IDisplay&) = delete;
    IDisplay& operator=(const IDisplay&) = delete;

    virtual Status begin() = 0;
    virtual Status hardReset() = 0;

    // Geometria fisica, em pixels. Fonte unica para o layout do dominio.
    virtual uint16_t widthPx() const = 0;   // 256
    virtual uint16_t heightPx() const = 0;  // 64

    // Metrica de fonte, para o dominio centralizar e alinhar sem adivinhar.
    virtual uint8_t lineHeightPx(TextFont font) const = 0;
    virtual uint16_t textWidthPx(TextFont font, const char* text) const = 0;

    // --- protocolo de quadro: clear -> desenha -> present ---
    // Nada aparece antes de present(). present() e a UNICA chamada bloqueante
    // (8192 bytes a 4 MHz = 16,4 ms) e por isso e sempre a ULTIMA coisa do ciclo,
    // depois de avaliar limites e comandar reles.
    virtual Status clear() = 0;
    virtual Status drawText(int16_t x, int16_t y, const char* text, TextFont font,
                            TextInk ink) = 0;
    virtual Status fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h, bool on) = 0;
    virtual Status drawFrame(int16_t x, int16_t y, uint16_t w, uint16_t h) = 0;
    virtual Status present() = 0;

    // Deslocamento global do conteudo, em pixels, contra desgaste do OLED com
    // layout estatico 24/7. Aplicado por present(); dx e dy em [-2, +2].
    virtual Status setOrigin(int8_t dx, int8_t dy) = 0;

    virtual Status setContrast(uint8_t value) = 0;
    virtual Status off() = 0;

    // Autoteste do item 5 do manual: padrao deterministico de aceitacao visual
    // (moldura fechada + regua + marcas em x=0/64/128/192/255). Discrimina painel
    // de 128 colunas e offset de coluna errado.
    virtual uint8_t patternCount() const = 0;
    virtual const char* patternDescription(uint8_t index) const = 0;
    virtual Status showPattern(uint8_t index) = 0;

    // Sempre false nesta placa: sem MISO nao ha como provar que o painel existe.
    virtual bool verifiable() const = 0;
    virtual const char* driverName() const = 0;

protected:
    IDisplay() = default;
};
```

```cpp
// src/ports/i_keypad.h
// Teclado do CN3: TRES teclas (MENU, UP, DOWN), ativas em nivel BAIXO.
// A porta entrega BORDAS JA DEBOUNCED com carimbo de tempo e nivel corrente.
// Ela NAO reconhece gesto: toque curto, hold de 3 s e duplo toque sao politica de
// produto e vivem no dominio (KeyGesture, compilavel em env:native), onde podem
// ser testados sem hardware. A porta cuida so do que e eletrico: amostragem,
// anti-repique de 20 ms e diagnostico de linha.
// ARMADILHA DE HARDWARE que esta porta expoe de proposito: UP = IO15 tem pull-up
// interno; DOWN = IO34 e MENU = IO35 sao INPUT-ONLY e ignoram INPUT_PULLUP em
// silencio - o pull-up tem de vir da placa de IHM, que nao tem esquematico no
// repositorio. Cabo de IHM solto pode ser lido como tecla PRESA. Por isso
// hasInternalPullup() existe: o dominio recusa gestos destrutivos (Reset de
// Fabrica) em teclas sem pull-up garantido e trata "as tres prensadas no boot"
// como assinatura de cabo em curto, nao como comando.
// Alvo: ButtonMonitor (src/drivers/buttons.cpp), poll sem bloqueio, fila de 16 bordas.
// Fake: FakeKeypad (test/native) - roteiro de (tecla, borda, instante) alimentado
//       pelo FakeClock; permite reproduzir duplo toque, hold e repique.
// REQ:  MAN-2.1-L23 (tres teclas), MAN-5.2-L79..85, MAN-5.3-L88..96 (login),
//       MAN-5.4-L99..101 (hold de ~3 s), MAN-5.6-L143..152 (duplo toque de PSET),
//       MAN-5.11-L232..239 (UP mantida na energizacao), decisoes 1 e 2.
#pragma once

#include <stdint.h>

#include "status.h"

enum class Key : uint8_t {
    Menu = 0,  // CN3-3, IO35, input-only
    Up,        // CN3-1, IO15, unico com pull-up interno
    Down,      // CN3-2, IO34, input-only
};

constexpr uint8_t kKeyCount = 3;

enum class KeyEdge : uint8_t {
    Press = 0,
    Release,
};

struct KeyEvent {
    Key key;
    KeyEdge edge;
    uint32_t atMs;    // instante da borda que sobreviveu ao debounce (base IClock)
    uint16_t heldMs;  // duracao da prensagem; valido so em Release, satura em 65535
};

class IKeypad {
public:
    virtual ~IKeypad() = default;
    IKeypad(const IKeypad&) = delete;
    IKeypad& operator=(const IKeypad&) = delete;

    virtual Status begin() = 0;

    // Amostra as tres linhas e aplica o debounce. Nao bloqueia, nao usa interrupcao.
    // Chamada uma vez por ciclo de controle; o periodo do ciclo tem de ser menor
    // que debounceMs() para que nenhuma prensagem curta seja perdida.
    virtual void poll() = 0;

    // Consome a borda mais antiga da fila. false quando a fila esta vazia.
    // Esta e a UNICA via de gesto: nivel amostrado por conta propria perde toques.
    virtual bool takeEvent(KeyEvent& out) = 0;

    // Nivel logico debounced corrente (true = prensada).
    virtual bool pressed(Key key) const = 0;

    // Mascara com bit por tecla, na ordem do enum. Serve ao instantaneo do boot
    // (Reset de Fabrica e assinatura de cabo em curto) sem drenar a fila.
    virtual uint8_t pressedMask() const = 0;

    // Ha quanto tempo a tecla esta prensada, 0 se solta. Permite disparar a acao
    // NO INSTANTE em que o hold completa 3000 ms, com a tecla ainda prensada,
    // em vez de so na soltura - que e o que o operador espera do manual.
    virtual uint32_t pressedForMs(Key key) const = 0;

    // Descarta bordas pendentes. Chamado em toda troca de tela ou de modo, para
    // que um toque de confirmacao nao vaze para a tela seguinte.
    virtual void flush() = 0;

    virtual uint16_t debounceMs() const = 0;  // 20 ms

    // Diagnostico eletrico. hasInternalPullup(Down/Menu) == false nesta placa.
    virtual bool hasInternalPullup(Key key) const = 0;

    // Repiques rejeitados pelo debounce; cresce com cabo ruim ou sem pull-up.
    virtual uint32_t bounceCount(Key key) const = 0;

    // Bordas perdidas por fila cheia. Diferente de zero e defeito de escalonamento
    // e TEM de ser visivel: gesto perdido em equipamento de seguranca nao pode ser
    // silencioso.
    virtual uint32_t droppedEvents() const = 0;
    virtual void resetCounters() = 0;

    virtual const char* keyName(Key key) const = 0;

protected:
    IKeypad() = default;
};
```

```cpp
// src/ports/i_relay_bank.h
// Os quatro reles de limite (RL5..RL2, bornes CN1D..CN1K). A porta fala em
// SEMANTICA DE APLICACAO - canal de limite e condicao sinalizada - e nunca em
// indice de GPIO, nivel eletrico ou polaridade de bobina. O mapeamento
// "sinalizado -> bobina desenergizada -> contato NF fechado" (fail-safe, padrao
// de fabrica NF do manual) e responsabilidade EXCLUSIVA do adaptador; o dominio
// jamais escreve "ligar" ou "desligar", escreve "sinalizado" ou "livre".
// Esta porta NAO expoe saida analogica, nem LED, nem display: cada LED de limite
// do painel pendura no mesmo net da base do BC337 do rele, portanto acender LED
// e efeito colateral fisico e inseparavel de sinalizar - e por isso que nao ha
// uma porta de LED de limite em lugar nenhum.
// LIMITACAO DECLARADA: nao existe realimentacao de contato nesta placa. state()
// devolve o COMANDO, nao a realidade; bobina aberta, BC337 aberto ou contato
// colado sao indetectaveis por software (feedbackAvailable() == false).
// Alvo: RelayBank (src/drivers/relays.cpp) sobre board::kRelayPins.
// Fake: FakeRelayBank (test/native) - guarda a mascara e o historico de
//       transicoes com carimbo de tempo, para assercao de latencia e de permanencia.
// REQ:  MAN-5.9-L192..199 e L214 (operacao dos limites sobre a leitura exibida),
//       MAN-Tabela-4 L317..336 (quatro reles, jumper NA/NF, padrao NF),
//       decisao 5 (histerese e permanencia), decisao 6 (reles vivos em programacao),
//       decisao 7 item 6 e decisao 8 item H (estado sinalizado em qualquer falha).
#pragma once

#include <stdint.h>

#include "status.h"

enum class LimitChannel : uint8_t {
    Limit1 = 0,  // eixo X, rotulo X1 no manual
    Limit2,      // eixo X, rotulo X2
    Limit3,      // eixo Y, rotulo Y1
    Limit4,      // eixo Y, rotulo Y2
};

constexpr uint8_t kLimitChannelCount = 4;

enum class RelayState : uint8_t {
    Clear = 0,   // condicao normal, limite nao atingido
    Signalled,   // limite atingido, ou falha (link, faixa, boot, watchdog)
};

// Mascara com bit n = 1 quando o canal n esta em Signalled.
using RelayMask = uint8_t;

constexpr RelayMask kRelayMaskAllClear = 0x00;
constexpr RelayMask kRelayMaskAllSignalled = 0x0F;

class IRelayBank {
public:
    virtual ~IRelayBank() = default;
    IRelayBank(const IRelayBank&) = delete;
    IRelayBank& operator=(const IRelayBank&) = delete;

    // Leva os quatro canais a Signalled ANTES de configurar os pinos como saida,
    // e de novo depois: a janela de energizacao nao pode apresentar "sem alarme".
    virtual Status begin() = 0;

    virtual uint8_t count() const = 0;

    // Escrita ATOMICA dos quatro canais. E a via normal do ciclo de controle:
    // um unico ponto de decisao por ciclo, sem estado intermediario em que dois
    // canais ja mudaram e dois ainda nao.
    virtual Status applyMask(RelayMask mask) = 0;

    // Escrita de um canal so. Reservada ao roteiro de fabrica e ao diagnostico;
    // a aplicacao usa applyMask().
    virtual Status set(LimitChannel channel, RelayState state) = 0;

    // Estado COMANDADO (cache de escrita), nao medido.
    virtual RelayState state(LimitChannel channel) const = 0;
    virtual RelayMask mask() const = 0;

    // Atalho para o estado seguro: equivale a applyMask(kRelayMaskAllSignalled).
    // Nunca falha silenciosamente; se falhar, o chamador deve derrubar a placa.
    virtual Status signalAll() = 0;

    // true quando o adaptador foi construido com a polaridade de corrente de
    // repouso (Signalled = bobina desenergizada). Item de inspecao de recebimento:
    // com false, uma UR morta apresenta a mesma indicacao de uma UR sadia sem alarme.
    virtual bool failSafeCoil() const = 0;

    // Sempre false nesta placa: sem contato auxiliar de realimentacao.
    virtual bool feedbackAvailable() const = 0;

    virtual const char* channelName(LimitChannel channel) const = 0;

protected:
    IRelayBank() = default;
};
```

```cpp
// src/ports/i_analog_output.h
// Duas saidas analogicas: DAC8562 (16 bits, ref interna 2,5 V, ganho 2) seguido de
// XTR300 com offset por VREF (R_OS = 1K no pino SET, R_GAIN = 10K, sem R_SET).
// A cadeia e BIPOLAR: V_OUT = 25*D/65536 - 12,5 V. O zero eletrico e o codigo
// 0x8000, NAO 0x0000 - 0x0000 vale -12,5 V, isto e, fundo de escala negativo.
// A porta so fala em CODIGO DE DAC (uint16). A conversao angulo -> codigo, a
// calibracao de dois pontos e a saturacao em +/-10,00 V sao dominio puro
// (AnalogScaler, aritmetica inteira), nao adaptador: e por isso que aqui nao ha
// nenhum setEngineering(float) e nenhum parametro em graus.
// Alvo: Xtr300AnalogOutput (src/drivers/xtr300.cpp) sobre Dac8562 + SpiBus.
// Fake: FakeAnalogOutput (test/native) - registra os codigos escritos por eixo e
//       reprova qualquer escrita fora de [codeMin(), codeMax()].
// REQ:  MAN-2.1-L33/L35 (bipolar -10..+10 V, D/A de 16 bits, duas saidas),
//       MAN-5.2-L78 (saidas atualizadas no Modo Normal),
//       MAN-5.7-L155..180 (Auto Calibracao em dois pontos e saturacao),
//       decisao 6 (override so no eixo em calibracao), decisao 9, decisao 10,
//       decisao 7 item 7 / decisao 8 item H (nivel eletrico de falha).
#pragma once

#include <stdint.h>

#include "status.h"

enum class AnalogAxis : uint8_t {
    X = 0,  // bornes CN1L(+)/CN1M(-)
    Y,      // bornes CN1N(+)/CN1O(-)
};

constexpr uint8_t kAnalogAxisCount = 2;

enum class AoMode : uint8_t {
    Voltage = 0,  // M2 = L
    Current,      // M2 = H
};

class IAnalogOutput {
public:
    virtual ~IAnalogOutput() = default;
    IAnalogOutput(const IAnalogOutput&) = delete;
    IAnalogOutput& operator=(const IAnalogOutput&) = delete;

    // Configura o CI e escreve um codigo definido nos dois canais ANTES de
    // qualquer outro periferico. O DAC8562 faz POR em zero-scale com a referencia
    // interna desligada: ate begin() rodar, as saidas estao encostadas no trilho
    // negativo. begin() e o primeiro passo do setup, nao o ultimo.
    virtual Status begin() = 0;
    virtual bool ready() const = 0;

    virtual uint8_t axisCount() const = 0;

    // --- escrita, so em codigo de DAC ---
    virtual Status write(AnalogAxis axis, uint16_t code) = 0;

    // Escreve os dois eixos na mesma passagem. Via normal do ciclo de controle:
    // evita apresentar ao CLP um eixo do ciclo novo e outro do ciclo anterior.
    virtual Status writeBoth(uint16_t codeX, uint16_t codeY) = 0;

    // Leva AMBOS os eixos ao nivel eletrico de falha (fora da faixa de medicao,
    // dentro do swing do XTR300), para que o CLP a jusante distinga falha de
    // inclinacao maxima. Usado na perda de link, na faixa mecanica excedida e na
    // janela de energizacao.
    virtual Status writeFaultLevel() = 0;

    // --- constantes eletricas do canal, para o dominio calcular sem adivinhar ---
    virtual uint16_t fullScaleCode() const = 0;  // 0xFFFF
    virtual uint16_t midScaleCode() const = 0;   // 0x8000 = 0,000 V nominal
    virtual uint16_t faultCode() const = 0;      // codigo do nivel de falha
    virtual uint16_t codeMin() const = 0;        // grampo duro inferior
    virtual uint16_t codeMax() const = 0;        // grampo duro superior
    // Todo codigo e grampeado em [codeMin(), codeMax()] pelo adaptador. Escrita
    // fora da faixa devolve Err::Range E grava o valor grampeado: em equipamento
    // de seguranca a saida nunca fica no valor errado por causa de um erro de
    // faixa a montante.

    // Estado COMANDADO. Nao ha leitura de volta: o DAC8562 nao tem readback e
    // nao existe ADC de conferencia (readbackAvailable() == false).
    virtual uint16_t lastCode(AnalogAxis axis) const = 0;
    virtual bool readbackAvailable() const = 0;

    // --- modo tensao/corrente ---
    // Existe UM unico net OP_MODE para os DOIS XTR300: o modo e global, nunca por
    // eixo, e a assinatura reflete isso. Alem disso o net so chega ao M2 se os
    // jumpers estiverem na posicao "uC"; na posicao fixa setMode() nao tem efeito
    // e nao ha como o firmware perceber - por isso modeSelectable() e uma
    // declaracao de configuracao, nao uma medida.
    virtual Status setMode(AoMode mode) = 0;
    virtual AoMode mode() const = 0;
    virtual bool modeSelectable() const = 0;

protected:
    IAnalogOutput() = default;
};
```

```cpp
// src/ports/i_sensor_link.h
// Link com a placa sensora PUSI-DI261930 (SI-DI141389XY): RS-485 half duplex,
// 19200 8N1, Modbus RTU, escravo 1, FC 0x03, start 0, quantidade 8.
// COMO O DOMINIO SABE QUE O LINK CAIU: nao e por ausencia de chamada e nao e por
// dado velho. Cada ciclo tem um RESULTADO EXPLICITO (LinkPoll). A porta so devolve
// Fresh quando uma resposta integra chegou DENTRO do timeout, e nunca reapresenta
// a amostra anterior - nao existe lastSample() nesta interface, de proposito. O
// dominio (LinkSupervisor, puro) aplica a regra de aceitacao sobre a amostra e
// conta ciclos consecutivos: 3 reprovados declaram falha, 10 aprovados restabelecem.
// A porta valida TRANSPORTE (prazo, endereco, funcao, byte count, CRC). A porta
// NAO julga o conteudo: quem exige status == 0x0001 exato, heartbeat avancado e
// |angulo| <= 900 e o dominio, porque isso e regra de seguranca do produto e tem
// de ser testavel em env:native sem nenhum fio.
// Alvo: ModbusRtuLink (src/proto/modbus_rtu.cpp) sobre Rs485Transport.
// Fake: FakeSensorLink (test/native) - roteiro de respostas, inclusive os casos
//       traicoeiros reais: angulo congelado com status 0x0011, selftest latchado
//       0x0009, heartbeat parado com CRC bom, e silencio total.
// REQ:  MAN-2.1-L27/L34 (sensor remoto ate 500 m, RS485), MAN-4-L58, MAN-5.5-L130,
//       MAN-7-L296..299 (falha de comunicacao), decisao 7, decisao 8, decisao 11.
#pragma once

#include <stdint.h>

#include "status.h"

// Bits do registrador 3 publicado pela sensora (contrato de fio congelado).
constexpr uint16_t kStsDataValid = 0x0001;
constexpr uint16_t kStsSclCrcError = 0x0002;
constexpr uint16_t kStsSclStartup = 0x0004;
constexpr uint16_t kStsSclSelfTestFail = 0x0008;
constexpr uint16_t kStsSclNotResponding = 0x0010;
constexpr uint16_t kStsSaturated = 0x0020;
constexpr uint16_t kStsWdtReset = 0x0040;

// Unico valor de status aceitavel. Mascarar kStsDataValid NAO serve: 0x0011
// (angulo congelado) e 0x0009 (selftest reprovado latchado) tambem contem o bit.
constexpr uint16_t kStsAcceptedExact = kStsDataValid;

// Faixa mecanica valida da leitura crua, em decimos de grau.
constexpr int16_t kAngleDeciMin = -900;
constexpr int16_t kAngleDeciMax = 900;

struct SensorSample {
    int16_t xDeci;      // reg 0, decimos de grau, com sinal
    int16_t yDeci;      // reg 1
    int16_t zDeci;      // reg 2, diagnostico; nao decide rele
    uint16_t status;    // reg 3, bitfield cru, sem interpretacao
    int16_t tempDeciC;  // reg 4, decimos de grau Celsius
    uint16_t whoAmI;    // reg 5, 0x00C1 quando o SCL3300 respondeu
    uint16_t fwVersion; // reg 6, (major << 8) | minor
    uint16_t heartbeat; // reg 7, avanca a cada ciclo da sensora; envolve em 2^16
    uint32_t atMs;      // instante do ultimo byte da resposta (base IClock)
};

enum class LinkPoll : uint8_t {
    Idle = 0,   // nenhuma transacao em curso; request() ainda nao foi chamado
    Busy,       // transacao em curso, resposta incompleta: nao e falha nem sucesso
    Fresh,      // resposta integra e dentro do prazo; 'out' preenchido
    Timeout,    // nenhuma resposta completa dentro de timeoutMs()
    BadFrame,   // CRC, endereco, funcao, byte count, comprimento ou excecao Modbus
};

struct LinkStats {
    uint32_t requests;
    uint32_t fresh;
    uint32_t timeouts;
    uint32_t crcErrors;
    uint32_t framingErrors;   // endereco/funcao/byte count/comprimento
    uint32_t exceptions;      // resposta de excecao Modbus (func | 0x80)
    uint32_t bytesRx;
    uint32_t bytesTx;
    uint32_t lastTurnaroundUs;
};

class ISensorLink {
public:
    virtual ~ISensorLink() = default;
    ISensorLink(const ISensorLink&) = delete;
    ISensorLink& operator=(const ISensorLink&) = delete;

    virtual Status begin() = 0;

    // Emite UMA transacao. Err::Busy se a anterior ainda nao terminou - a porta
    // nunca enfileira pedidos e nunca sobrepoe transacoes no barramento.
    virtual Status request() = 0;

    // Avanca a maquina de estados sem bloquear e devolve o resultado do ciclo.
    // 'out' so e tocado quando o retorno e Fresh. Cada request() produz no maximo
    // um Fresh; depois disso o retorno volta a Idle ate o proximo request().
    virtual LinkPoll poll(SensorSample& out) = 0;

    // Cancela a transacao pendente e limpa o buffer de recepcao (troca de modo,
    // reconfiguracao). Depois disto o proximo poll() devolve Idle.
    virtual void abort() = 0;

    virtual bool busy() const = 0;
    virtual uint32_t timeoutMs() const = 0;   // 30 ms
    virtual uint32_t baud() const = 0;        // 19200
    virtual uint8_t slaveAddress() const = 0; // 1
    virtual const char* protocolName() const = 0;

    virtual const LinkStats& stats() const = 0;
    virtual void resetStats() = 0;

protected:
    ISensorLink() = default;
};
```

```cpp
// src/ports/i_parameter_store.h
// Persistencia dos parametros. A porta e um armazem de BLOB POR SLOT, com buffer
// do chamador: ela nao conhece o layout do registro, nao calcula CRC, nao decide
// qual banco e o mais novo e nao interpreta numero de sequencia. Tudo isso e
// dominio puro (ParamRecord + ParamStoreLogic), testavel em env:native com um
// slot em RAM - que e exatamente o que torna auditavel a atomicidade de dois
// bancos exigida por equipamento de seguranca.
// O manual fala em "EEPROM interna"; a placa e ESP32-WROOM-32D e a retencao e NVS
// em flash, com apagamento por setor. Dai as duas exigencias que a porta declara:
// write() so devolve kOk depois de RELER e conferir o que gravou, e existe um
// orcamento de tempo publicado (writeBudgetMs) para o chamador planejar o ciclo.
// A porta nunca chuta o watchdog e nunca chama o relogio: quem controla o ciclo
// e o dominio.
// Alvo: NvsParameterStore (src/platform/nvs_parameter_store.cpp) sobre NvsStore,
//       namespace "depuri1", chaves "par_a", "par_b", "cal_fab".
// Fake: FakeParameterStore (test/native) - tres arrays estaticos com injecao de
//       falha: escrita truncada, corrupcao de byte e Err::Storage sob demanda,
//       para reproduzir queda de energia no meio da gravacao.
// REQ:  MAN-2.1-L32 (retencao sem bateria), MAN-3-L47 (dita EEPROM interna),
//       MAN-5.4-L101/L102/L127/L128 e MAN-7-L299/L300 (momento da gravacao),
//       MAN-5.11-L233 e Tabela 2 (reset de fabrica restaura tambem a calibracao),
//       decisao 2 (commit-on-confirm com dois bancos), decisao 9 item 11.
#pragma once

#include <stdint.h>

#include "status.h"

enum class ParamSlot : uint8_t {
    BankA = 0,    // banco de parametros do usuario, copia 1
    BankB,        // banco de parametros do usuario, copia 2
    FactoryCal,   // calibracao gravada pelo jig; a IHM nunca escreve aqui
};

constexpr uint8_t kParamSlotCount = 3;

class IParameterStore {
public:
    virtual ~IParameterStore() = default;
    IParameterStore(const IParameterStore&) = delete;
    IParameterStore& operator=(const IParameterStore&) = delete;

    virtual Status begin() = 0;
    virtual bool ready() const = 0;

    // Maior blob aceito por slot. O registro do produto tem 48 bytes.
    virtual uint16_t capacityBytes() const = 0;

    // true quando o slot ja recebeu alguma escrita. NAO diz nada sobre validade
    // do conteudo: integridade e assunto do CRC do dominio.
    virtual bool exists(ParamSlot slot) const = 0;

    // Le o slot inteiro para o buffer do chamador. Err::Param se cap < tamanho
    // gravado; Err::Storage se o slot nunca foi escrito ou a midia falhou.
    virtual Status read(ParamSlot slot, void* dst, uint16_t cap, uint16_t& outLen) = 0;

    // Grava o blob inteiro (nunca campo a campo) e RELE para conferir byte a byte
    // antes de devolver kOk. Bloqueia por ate writeBudgetMs(); pode envolver
    // apagamento de setor. Err::Storage se a releitura divergir - nesse caso o
    // chamador tem de restaurar o valor anterior e avisar o operador.
    virtual Status write(ParamSlot slot, const void* src, uint16_t len) = 0;

    virtual Status erase(ParamSlot slot) = 0;

    // Orcamento de bloqueio de write(), em ms (250). O chamador usa isto para
    // garantir que a gravacao cabe entre duas avaliacoes de rele e dentro da
    // margem do watchdog.
    virtual uint32_t writeBudgetMs() const = 0;

    // Escritas concluidas desde o boot, por slot. Telemetria de desgaste da flash.
    virtual uint32_t writeCount(ParamSlot slot) const = 0;

    virtual const char* slotName(ParamSlot slot) const = 0;

protected:
    IParameterStore() = default;
};
```

---

## Justificativa das decisoes de interface que nao sao obvias

### i_keypad — por que a porta entrega bordas e nao gestos

O corte esta na fronteira **eletrico / politica**. Debounce de 20 ms e propriedade do contato mecanico e do cabo: nao ha como testa-lo no host e nao ha decisao de produto nele — fica no adaptador, com o numero publicado por `debounceMs()` para que o dominio saiba dimensionar o periodo do ciclo (o ciclo de 50 ms e maior que o debounce, entao **o nivel amostrado por conta propria perderia toques curtos** — dai `takeEvent()` ser a unica via legitima de gesto). Ja "duplo toque de ▲ com 30..600 ms por toque, ≤400 ms de intervalo, anulado por qualquer borda de MENU/▼" e "hold de exatamente 3000 ms" sao politica de produto, escrita no manual, sujeita a revisao do bigboss e responsavel por acoes destrutivas (PSET desloca os quatro pontos de trip; Reset de Fabrica apaga a calibracao). Isso **tem** de ser dominio puro compilavel em `env:native`, testado com relogio falso, e nao pode morar num `.cpp` que so roda na placa.

Consequencias diretas na assinatura:

- **`KeyEvent` carrega `atMs`, nao um "faz quanto tempo"**. O reconhecedor de gesto precisa medir intervalos entre bordas com precisao maior que o periodo do ciclo; um carimbo absoluto na base do `IClock` permite isso mesmo que o dominio drene a fila alguns milissegundos depois. `heldMs` no `Release` evita que o dominio precise casar cada `Press` com seu `Release` so para saber a duracao de um toque.
- **`pressedForMs()` existe alem da fila** porque o manual manda gravar quando o hold *completa* 3 s (5.4, linha 101), com a tecla ainda prensada — nao na soltura. Sem esse acessor, o dominio so descobriria a duracao no `Release`, e o operador teria de soltar a tecla para o equipamento reagir, o que muda o gesto que o cliente ja conhece.
- **`pressedMask()` sem drenar a fila** porque o Reset de Fabrica (5.11) e uma leitura *instantanea* em t = 50 ms apos o reset, e porque a assinatura de cabo em curto ("as tres prensadas ao mesmo tempo") e uma condicao de nivel, nao de borda.
- **`hasInternalPullup()` e uma nao conformidade eletrica exposta na API, de proposito.** IO34/IO35 sao input-only e ignoram `INPUT_PULLUP` em silencio; o pull-up tem de vir de uma placa de IHM cujo esquematico nao existe no repositorio. Com o cabo solto, MENU e ▼ tem nivel indefinido e podem ser lidos como prensados. Esconder isso atras de uma interface "limpa" seria transferir para o firmware um risco que ele nao pode avaliar. Com o acessor, o dominio pode escrever a regra correta: gesto destrutivo so em tecla com pull-up garantido (▲/IO15), e mascara "111" no boot vira aborto, nao comando.
- **`droppedEvents()`** existe porque a fila e finita (16 bordas). Em equipamento de seguranca, um gesto perdido por overflow tem de ser contavel e reportavel; a alternativa (perder em silencio) produz o pior sintoma possivel: "as vezes o PSET nao pega".
- **`flush()`** e o antidoto do vazamento de toque entre telas: o MENU que confirmou a senha nao pode reaparecer como o MENU que abre a edicao da tela seguinte.

Nao ha `Key` como `uint8_t` cru nem indice numerico na API: `enum class Key` impede trocar UP por MENU numa chamada, que e exatamente o tipo de erro que a serigrafia cruzada desta placa ja produz em outro lugar.

### i_sensor_link — como o dominio sabe que o link caiu

A pergunta so tem resposta segura se **o silencio nunca for ambiguo**. Por isso a porta nao tem `bool poll(Angle&)` (o desenho atual do repo), que confunde tres coisas diferentes num unico `false`: "ainda nao chegou", "estourou o prazo" e "chegou lixo". `LinkPoll` separa `Busy` (nao e falha), `Timeout` e `BadFrame` (sao falha, e de tipos distintos, com estatistica separada para diagnostico de campo — cabo rompido da timeout, A/B invertidos dao lixo).

Tres escolhas sustentam a seguranca:

1. **Nao existe `lastSample()` / `lastAngle()`.** Essa e a ausencia mais importante da interface. Uma porta que reapresenta a ultima amostra boa reproduz, do lado da UR, exatamente o defeito que a sensora ja tem: em falha do SCL3300 ela **congela** os angulos e faz `|=` no status, publicando `0x0011` = DATA_VALID **e** SCL_NOT_RESPONDING sobre um angulo velho. Se a porta tambem guardasse o ultimo valor, o dominio teria duas fontes de dado velho e nenhuma pista. Aqui, `Fresh` e um evento: acontece no maximo uma vez por `request()`, preenche `out` e some. Quem quiser guardar o valor, guarda — e assume a responsabilidade explicitamente.
2. **A porta valida transporte; o dominio valida conteudo.** Prazo, endereco, funcao, byte count e CRC sao propriedades do fio e ficam no adaptador. Mas `status == 0x0001` **exato** (nao mascarado), `heartbeat` diferente do ciclo anterior e `|angulo| <= 900` sao regras de seguranca do produto, derivadas do comportamento real do escravo, e precisam rodar no host contra um roteiro de respostas maliciosas: `0x0011` congelado, `0x0009` de selftest latchado, heartbeat parado com CRC perfeito. Por isso `SensorSample` entrega o `status` **cru**, sem interpretacao, e as constantes do contrato (`kSts*`, `kStsAcceptedExact`, `kAngleDeci*`) moram no header da porta — sao contrato de fio, nao detalhe do adaptador, e assim o predicado do dominio nao precisa incluir nada do firmware da sensora.
3. **A declaracao de falha e contagem, nao instante.** A porta nao tem `LinkState`, nao tem `isDown()`: quem conta 3 ciclos reprovados para declarar falha e 10 aprovados para restabelecer (assimetria deliberada: rapido para falhar, lento para confiar) e o `LinkSupervisor`, dominio puro. Colocar essa histerese no adaptador tornaria intestavel justamente a regra que decide se quatro reles de seguranca vao ao estado sinalizado.

`request()` separado de `poll()` mantem o ritmo do barramento sob controle do dominio (um pedido por ciclo de 50 ms, sem sobreposicao — `Err::Busy` em vez de dois pedidos no fio), e `atMs` no `SensorSample` permite ao dominio medir frescor com a mesma base de tempo de todo o resto.

### As demais portas

- **`IRelayBank` fala "Signalled/Clear", nunca "on/off".** A polaridade fail-safe (sinalizado = bobina desenergizada = contato NF fechado) e a unica escolha em que falta de energia, cabo rompido, firmware travado e limite atingido produzem a **mesma** indicacao no laco do cliente. Se o dominio escrevesse `set(i, true)`, a inversao viraria uma linha de codigo esquecivel num arquivo qualquer; escrevendo `Signalled`, a inversao vive num unico lugar e `failSafeCoil()` a declara para o roteiro de recebimento. `applyMask()` existe porque a decisao de rele e por ciclo e tem de ser atomica: nao pode haver instante em que dois canais ja refletem a leitura nova e dois ainda a velha. E `feedbackAvailable() == false` esta na API porque a mentira mais perigosa desta placa seria um `get()` que parece medicao e e cache de escrita — rele mudo e indetectavel aqui, e a interface diz isso em vez de disfarcar.
- **`IAnalogOutput` so aceita codigo de DAC.** Toda a matematica (calibracao de dois pontos, espelhamento bipolar exato, saturacao em ±10,00 V) e inteira e vive no dominio; se o adaptador aceitasse graus, a escala do produto so seria testavel com um multimetro. Os acessores `midScaleCode()`, `faultCode()`, `codeMin()/codeMax()` existem porque o zero eletrico desta cadeia e `0x8000` e nao `0x0000` — a constante errada aqui e, hoje, uma falha de seguranca real (boot e estado seguro anunciando ~-12 V ao CLP). Escrita fora da faixa devolve `Err::Range` **e** grava o valor grampeado: em equipamento de seguranca, recusar a escrita deixaria a saida no valor anterior, possivelmente pior. `setMode()` sem parametro de eixo reflete o net OP_MODE unico dos dois XTR300, e `modeSelectable()` avisa que jumper na posicao fixa torna o comando inoperante sem erro observavel.
- **`IDisplay` e desenho, nao IHM.** ISP levado a serio: nao ha uma tecla nesta porta, e nao ha uma unica funcao de dominio (`showAngle`, `showMenu`) — o driver de fabrica hoje monta identidade e layout dentro do adaptador, e e isso que impede reusa-lo no produto. Coordenadas em pixel com metrica de fonte consultavel (`lineHeightPx`, `textWidthPx`) permitem ao dominio montar as telas literais do manual sem adivinhar; `fillRect` cobre tanto o batimento de 1 Hz quanto os campos invertidos; `setOrigin` resolve burn-in sem que a camada de tela saiba disso. `verifiable() == false` e a admissao formal de que o CN4 nao tem MISO: nenhum rele pode depender desta porta.
- **`IParameterStore` e slot de blob, nao chave/valor.** Dois bancos, CRC e numero de sequencia sao a **logica** de atomicidade — o coracao da promessa de "retencao sem bateria" numa flash que apaga por setor — e precisam ser testados com injecao de falha no host. Se a porta oferecesse `putU8`/`putString`, a tentacao de gravar campo a campo produziria o pior estado possivel: Limite 1 novo com Operacao Limite 1 velha, combinacao que ninguem validou. `write()` so retorna `kOk` depois de reler e conferir, e `writeBudgetMs()` publica o bloqueio de 250 ms para que o dominio decida onde ele cabe no ciclo.
- **`IClock` separado de tudo.** Hoje o tempo entra no dominio por `IConsoleIO::nowMs()`, o que amarra regra de seguranca a console. Com a porta propria, cada regra temporal (hold de 3 s, timeout de 2 min, ciclo de 50 ms, permanencia minima de rele) e reproduzivel em teste com o tempo sob controle do teste. As duas funcoes `constexpr` no fim do header nao sao conveniencia: elas impedem o bug classico de comparar `now > deadline` atraves do wrap de 49,7 dias, que num equipamento que roda 24/7 aparece uma vez por ano e some.
- **`IWatchdog::heartbeat()`** e a diferenca entre watchdog e enfeite. Hoje o chute sai de um `esp_timer` independente do laco: um laco travado continua sendo chutado e a placa nunca reseta. Com o heartbeat na interface, o gerador de pulso passa a exigir prova de vida do laco, e travamento vira reset em 1,12 a 2,24 s — com os reles caindo no estado sinalizado. `rearmPin()` esta na porta, e nao escondido no adaptador, porque a ordem de inicializacao (display **antes**, rearm **depois**) e um requisito do composition root: quem reordenar o setup sem ver essa chamada desliga o watchdog sem nenhum sintoma.
- **`IIndicator` tem exatamente um canal** e nao inclui os LEDs de limite — nao por minimalismo, mas porque eles fisicamente nao existem como saida independente: compartilham o net da base do BC337. Uma porta que os expusesse prometeria o que o hardware nao entrega ("pisque o LED 3 sem acionar o rele 3").
