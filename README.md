# Jig de teste de fabrica — DE-PURI-DI261924 REV A

Firmware de bring-up / teste de producao da placa supervisora de inclinacao (DiEletrons).
Nao e o firmware de aplicacao: e o jig operado na bancada, com operador presente, multimetro,
fonte de bancada e carga de 250 ohm.

- MCU: ESP32-WROOM-32D (sem PSRAM — IO16/IO17 livres para a UART2)
- Ambiente: PlatformIO, framework Arduino (`espressif32` / `esp32dev`), C++17
- Console: UART0, 115200 8N1

```
pio run  -e esp32dev             # firmware, IHM desabilitada (padrao)
pio run  -e esp32dev-ihm         # firmware com display e botoes habilitados
pio run  -e esp32dev -t upload
pio test -e native               # 46 testes unitarios de host (sem ESP32)
pio run  -e sim      && .pio/build/sim/program          # simulador de bancada no PC
pio run  -e sim-ihm  && .pio/build/sim-ihm/program      # idem, com display e botoes
```

## Simulador de bancada (env `sim`)

Da para rodar a suite inteira no PC, sem placa, e ver exatamente os prints que o operador vera
no terminal. Isso e possivel porque `console.cpp`, `test_runner.cpp`, `registry.cpp` e os sete
`src/tests/*.cpp` **nao dependem de Arduino**: todo acesso a hardware passa pelas interfaces do
`Ctx`, e todo acesso a tempo passa por `ctx.io.nowMs()`.

```
.pio/build/sim/program "<roteiro>" <escala_de_tempo> "<respostas>"
```

- `<roteiro>`: comandos separados por `\n` (padrao: `status`, `serial`, `selftest`, `report csv`)
- `<escala_de_tempo>`: multiplicador do relogio (padrao 200) — a sessao de 30 s do RS-485 roda em
  fracao de segundo
- `<respostas>`: sequencia de vereditos do operador (`p`/`f`/`s`/`a`); o que faltar vira `p`

Exemplos:

```
.pio/build/sim/program "calfake\ntest t1\n" 100          # so a saida analogica
.pio/build/sim-ihm/program "test t4\ntest t5\n" 100      # so display e botoes
.pio/build/sim/program "selftest\n" 100 "ppfp"           # terceiro ponto reprovado
```

O comando `calfake` existe **so no simulador**: injeta uma calibracao plausivel (0-10 V e 4-20 mA
sobre 0x0000..0xFFFF) para o item t1 rodar inteiro sem multimetro.

O que o simulador cobre: t0, t1, t2, t3, t4 e t5 ponta a ponta, com quadro RS-485 real (uma
sensora de mentira responde com `frame::encode`) e persistencia em NVS simulada.
**O t6 sempre FALHA no simulador, por construcao**: so o STWD100 real puxa `EN`. Esse item exige
bancada.

## Estado atual

| Bloco | Estado |
|---|---|
| Teste 0 — boot, alimentacao, identidade | implementado |
| Teste 1 — saida analogica (DAC8562 + XTR300) | implementado |
| Teste 2 — reles de limite | implementado |
| Teste 3 — RS-485 | implementado |
| Teste 4 — display | implementado — SSD1322 256x64 via U8g2, base do repo `CDM4L-DI221651`. Compila no env `esp32dev-ihm`. |
| Teste 5 — botoes | implementado — ativos em nivel baixo, com deteccao explicita de pull-up ausente em IO34/IO35. |
| Teste 6 — watchdog externo | implementado |

O display usa o mesmo controlador do projeto `CDM4L-DI221651`: **SSD1322 NHD 256x64 por SPI de
4 fios, via U8g2** (`U8g2Display`). Os botoes seguem a mesma convencao da familia: **ativos em nivel
baixo**, com `INPUT_PULLUP` em IO15 e `INPUT` puro em IO34/IO35. O env `esp32dev` (padrao) sobe com
`NullDisplay` e `IHM_ENABLED=0`, e os itens t4/t5 devolvem `SKIP` por nao haver IHM na build; o env
`esp32dev-ihm` compila os dois.

**A supervisora nao tem pull-up nas linhas de botao**: elas vao direto do ESP32 ao CN3 (confirmado
no esquematico). Como IO34/IO35 sao input-only e `pinMode(INPUT_PULLUP)` e silenciosamente ignorado
neles, o pull-up **tem** de estar na placa de IHM. O teste 5 checa isso antes de pedir qualquer
pressionamento: nivel de repouso diferente de alto reprova com a causa provavel escrita por extenso
(falta de pull-up, botao preso fechado, ou curto para o 0V do CN3-4).

**Armadilha herdada do U8g2**: ele chama `SPI.begin()` sem argumentos, e o core do ESP32 entao
prende o MISO default do VSPI, que nesta placa e o **IO19 = `WDI` do watchdog**, virando o pino em
entrada. Por isso `ExtWatchdog::rearmPin()` existe e e chamado no `setup()` depois de toda
inicializacao de SPI. Sem isso a placa entra em reset-loop de 1,6 s.

## Riscos de projeto que o jig expoe (nao corrige)

### 1. Niveis logicos do DAC8562 — nao conformidade provavel

O DAC8562 (TI SLAS719E) especifica `VIH` minimo = **0,7 x AVDD**. Com **AVDD = 5 V isso da 3,5 V**,
acima dos 3,3 V que o ESP32 entrega. `VIL` maximo e 0,8 V (o nivel baixo esta folgado).

Sintoma esperado: escrita erratica/intermitente com bordas limpas no analisador logico.
Diagnostico: `dac spi 1000000` contra `dac spi 10000000` — se o comportamento melhora ao baixar o
clock, e margem de `VIH`, nao software. Por isso o clock **padrao e 1 MHz**, nao 10 MHz.

Correcoes possiveis no hardware:
- alimentar AVDD com 3V3 (perde-se o ganho x2; fundo de escala do DAC cai para 2,5 V); ou
- inserir buffer/level-shifter (74AHCT125 ou 74LVC1T45) em `SYNC`, `SCLK` e `DIN`.

### 2. Corrente de base dos reles — sem margem

BC337 com base em 2 K + 1N4007 em serie e 1 K de pull-down. Com 3,3 V:
`(3,3 - 0,7 Vbe - 0,7 Vd) / 2 K ~= 0,95 mA`, menos `0,7 V / 1 K = 0,7 mA` desviados pelo pull-down
→ **~0,25 mA de base**. Para a bobina AX1RC-5V de ~36 mA isso exige **hFE >= 145**: dentro da faixa
do BC337, mas sem margem no pior caso de ganho e temperatura.

`relay all on` (pior caso de carga do +5 V) e `relay margin <n>` (20 ciclos) tornam isso mensuravel.
Um unico fecho intermitente reprova o item.

### 3. RS-485 — criterio de PASS depende da terminacao

O `SN65HVD75D` tem failsafe interno: com o barramento aberto, em curto ou sem trafego, a saida do
receptor vai para nivel alto **sem resistores de bias externos**. Logo, em `rs485 idle` o esperado
com multimetro e **A-B ~ 0 V** (barramento passivo), e nao um nivel de bias.

Em `rs485 drive`, medindo A-B (**correcao ao roteiro original**: o `SN65HVD75D` e um transceptor
de **3,3 V**, entao um diferencial de 3,5 a 5 V e fisicamente inalcancavel e reprovaria placa boa):
- apenas o terminador local de 120 ohm (J7): **2,0 a 3,3 V**
- dois terminadores (60 ohm efetivos): **1,5 a 3,0 V** (`VOD` minimo do datasheet com 54 ohm = 1,5 V)

Diferencial muito baixo com carga leve → incluir **TVS CDSOT23-SM712 em curto** nas causas provaveis.

### 4. Sinalizacao de falha do XTR300 nao chega ao MCU

`EFOT`, `EFLD` e `EFCM` so acendem LEDs locais (LD1..LD3 no eixo X, LD4..LD6 no eixo Y). O roteiro
pede inspecao visual em cada ponto medido e o relatorio tem campo por ponto.

### 5. Saidas analogicas nao sao isoladas

O esquematico (folha 2/2) mostra o pino `0V` de saida do A0515S-2WR3 ligado ao mesmo `0V` do
sistema: o conversor gera os +/-15 V mas **nao isola**. Qualquer material que prometa isolacao
galvanica das saidas precisa ser corrigido, e o roteiro de medicao considera laco de terra comum
com a carga de 250 ohm.

### 6. LDAC do DAC8562 esta em nivel alto, nao em 0 V

`R15` de 10K puxa `LDAC` para **+5 V** (folha 2/2). O `begin()` do driver programa o registro LDAC
(comando 110, `0x30 0x00 0x03`) para que os dois canais atualizem independentemente do pino.

### 7. Watchdog externo STWD100YNYWY3F

Decodificacao (ST DocID14134 Rev 11): **Y** automotivo, **N** saida `WDO` **open-drain**,
**Y** `tWD` = **1,6 s tipico (min 1,12 s / max 2,24 s)**, `tPW` 210 ms, SOT23-5.

- kick fixo em **250 ms** (< 1/3 de 1,12 s), gerado por `esp_timer` periodico — nunca pelo `loop()`
- `WDI` recebe pulso de 5 us (o minimo do datasheet e 1 us; glitch < 100 ns e ignorado)
- o timer de kick sobe na **primeira linha util do `setup()`**, antes de SPI, NVS, display e console
- **nao existe `wdt off` por software**: o pino `EN` tem pull-down interno de 32-100 k e habilita o
  watchdog quando flutuante ou baixo. O comando se chama `wdt kick off` e avisa que a placa reseta
  em ~1,6 s se J15 estiver fechado
- `wdt test` grava `wdt_expect=1` na **NVS** (nao em `RTC_NOINIT_ATTR`: o reset pelo pino `EN`
  aparece como `ESP_RST_POWERON` e apaga a RTC memory), para de chutar e conta 3 s

## Armadilha do core Arduino: MISO nao pode ser -1

`SPIClass::begin(sck, miso, mosi, ss)` do core ESP32 **nao desliga o MISO quando recebe -1**:
`spiAttachMISO()` substitui pelo pino default do barramento e faz `pinMode(pino, INPUT)`.
No ESP32 isso e **IO12 no HSPI** (o `SYNC` do DAC8562) e **IO19 no VSPI** (o `WDI` do watchdog).
Passar -1 transformaria o pino de chute do watchdog em entrada e a placa entraria em reset-loop de
~1,6 s, com sintoma indistinguivel de defeito de hardware.

Por isso `board::kDacMiso = 36` e `board::kDispMiso = 39`: pinos **input-only** do ESP32, sem uso
neste projeto, que absorvem o MISO sem tocar em nenhum sinal real. Confirmar na montagem que IO36
e IO39 estao mesmo sem conexao.

## Pinos de strapping

`IO12` (MTDI), `IO15` (MTDO), `IO5` (CS do display), `IO2` (LED_TEST) e `IO0` sao strapping.
O firmware **le e imprime o nivel dos cinco no boot, antes de reconfigurar qualquer pino**
(comando `boot`), junto com `esp_reset_reason()`. Isso e o teste 0 e detecta erro de montagem que
hoje so aparece como "placa nao liga". `SYNC` (IO12) so vira saida depois do boot e nunca recebe
pull-up.

## Cruzamento LED x rele no CN3 (esta assim no esquematico)

| Net | GPIO | Rele / saida | Pino do CN3 (serigrafia da IHM) |
|---|---|---|---|
| LIM1 | IO32 | RL5 → CN1D/E | CN3-6 ("LED LIM3") |
| LIM2 | IO26 | RL4 → CN1F/G | CN3-8 ("LED LIM1") |
| LIM3 | IO25 | RL3 → CN1H/I | CN3-7 ("LED LIM2") |
| LIM4 | IO33 | RL2 → CN1J/K | CN3-5 ("LED LIM4") |

O mesmo GPIO aciona o transistor do rele **e** o LED do painel: nao existe teste de LED
independente do rele. O teste 2 imprime o LED esperado para o operador detectar troca de fiacao.

## Escala da saida analogica: calibracao obrigatoria

A escala do DAC (0-5 V com referencia interna e ganho x2) **nao e** a escala da saida
(0-10 V / 4-20 mA): o ganho esta na malha do XTR300 (R18, R14/R19 2K2, R17 10K, RSET), cujos valores
estao em aberto (pergunta 6). O firmware **nao presume fator nenhum**:

```
ao raw x 0x1999      # 10% do fundo de escala
cal x v              # pede os dois valores medidos e resolve valor = a*code + b
cal show             # coeficientes por eixo e por modo
ao x v 5.0           # so aceito depois da calibracao daquele par eixo/modo
```

Persistencia em NVS (`Preferences`, namespace `depuri1`), registro versionado com CRC16-MODBUS,
sobrevive a power cycle. `cal erase` limpa.

## Linha de resultado para a planilha de producao

```
#RESULT_BEGIN
SN-0001,0.1.0,A,2026-08-28,PASS,t0=P,t1=P,t2=P,t3=P,t4=S,t5=S,t6=P
#RESULT_END
```

Captura no PC com uma linha de Python:

```python
python3 -c "import re,sys;print(re.search(r'#RESULT_BEGIN\r?\n(.*?)\r?\n#RESULT_END',sys.stdin.read(),re.S).group(1))" < log.txt
```

Vereditos: `P` pass, `F` fail, `S` skip, `A` abort, `-` nao executado.
O relatorio tambem fica na NVS: `report last`.

## Arquitetura

```
src/main.cpp                    composition root: unico lugar que constroi objetos concretos
include/board_pins.h            TODOS os pinos como constexpr, fonte unica da verdade
include/status.h                enum class Err + struct Status, sem excecoes
include/iface/*.h               IAnalogOutput, IDigitalOutputBank, ISerialTransport, IDisplay,
                                IButtons, IWatchdog, IOperator, ISafeState
src/core/console.{h,cpp}        maquina de estados + parser, depende so de IConsoleIO
src/core/cmd_parser.{h,cpp}     tokenizador puro (testado no host)
src/core/test_runner.{h,cpp}    registry de ITest, execucao, abort, relatorio
src/core/report.{h,cpp}         formato legivel + linha CSV + persistencia via IKeyValueStore
src/drivers/spi_bus.{h,cpp}     dono do SPIClass; injetado nos perifericos
src/drivers/dac8562.{h,cpp}     SLAS719E
src/drivers/xtr300.{h,cpp}      OP_MODE + conversao valor<->codigo
src/drivers/calibration.{h,cpp} matematica pura + persistencia (testavel no host)
src/drivers/relays.{h,cpp}      src/drivers/buttons.{h,cpp}   src/drivers/display.{h,cpp}
src/drivers/rs485.{h,cpp}       UART_MODE_RS485_HALF_DUPLEX
src/drivers/ext_wdt.{h,cpp}     esp_timer de 250 ms
src/proto/irs485_protocol.h     + echo_protocol.cpp, modbus_rtu.cpp (stub)
src/cmds/*.cpp                  um ICommand por comando, auto-registrado
src/tests/test_*.cpp            um ITest por teste, auto-registrado
test/native/                    unidade de host: calibracao, CRC16, quadro, parser, relatorio
```

Como cada principio SOLID aparece aqui:

- **SRP** — driver so fala com hardware; teste so executa procedimento e emite veredito; console so
  faz parsing e I/O; `Report` so formata e persiste. Nenhum driver contem `Serial` ou `printf`.
- **OCP** — `ITest` + `REGISTER_TEST` e `ICommand` + `REGISTER_COMMAND`. Acrescentar um teste ou um
  comando e acrescentar **um arquivo**; `console.cpp` e `test_runner.cpp` nao mudam. Nao ha `switch`
  sobre nome de teste.
- **LSP** — `NullDisplay`, `MockKvStore` e `EchoProtocol` tem a mesma semantica de retorno dos reais.
  `NullDisplay` devolve `Ok` em tudo, para o `selftest` rodar em bancada sem IHM.
- **ISP** — nada de `IBoard` gorda: `IAnalogOutput`, `IDigitalOutputBank`, `ISerialTransport`,
  `IDisplay`, `IButtons`, `IWatchdog`, `IOperator`, `ISafeState` sao separadas.
- **DIP** — nenhum driver chama `SPI.begin()`, `Serial.begin()` ou `Preferences.begin()` sozinho.
  A prova objetiva e o env `native`: `calibration`, `crc16`, `frame`, `cmd_parser` e `report`
  compilam e sao testados **sem ESP32**, com `MockKvStore`.

## Maquina de estados do console

```
BOOT ──► IDLE ──► PARSING ──┬──► TEST_RUNNING ──► AWAIT_VERDICT ──┬──► (PASS|FAIL|SKIP) ──► IDLE
                            │                          │           │
                            └──► (comando simples) ─────┴──► ABORT ─┘
SUITE_RUNNING: estado paralelo que encadeia t0..t6, com abort individual ('a') e abort geral
```

Todo estado tem timeout com mensagem: `AWAIT_VERDICT` espera 180 s (lembrete a cada 30 s, timeout
vira `SKIP`), `IDLE` imprime uma dica a cada 300 s de ociosidade. O jig nunca fica mudo.

## Estado seguro

`enterSafeState()` — reles desenergizados, DAC em zero, `OP_MODE` em tensao, display apagado.
Chamada no `setup()`, ao fim de **todo** teste (pass, fail, skip ou abort), em qualquer abort e
antes de `wdt test`. Nunca se comuta `OP_MODE` com a saida em fundo de escala.

## Radio desligado

`WiFi.mode(WIFI_OFF)` e `btStop()` no `setup()`: nao ha uso de radio, e a comutacao de RF injeta
ruido na faixa que compromete a tolerancia de +/-0,5 % FE das medidas analogicas, alem de aumentar
o pico de corrente.
