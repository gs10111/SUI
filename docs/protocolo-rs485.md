# Protocolo RS-485 UR <-> Sensora (contrato de fio)

Contrato do enlace serial entre a **Unidade Remota UR-DI151399** (placa `DE-PURI-DI261924`, papel de
**mestre**) e a **placa sensora SI-DI141389XY / PUSI-DI261930** (papel de **escravo**).

Este documento e **normativo**: qualquer alteracao de baud, formato, endereco, funcao, ordem ou
significado de registrador quebra as duas placas ao mesmo tempo e exige revisao conjunta de firmware,
manual e roteiro de fabrica.

**Regra de autoria deste arquivo:** tudo que esta descrito abaixo como "implementado" foi lido no
codigo-fonte, nao presumido. Onde o comportamento real diverge do desejavel, ha um item explicito na
secao [12. Divergencias conhecidas e pendencias](#12-divergencias-conhecidas-e-pendencias), marcado
como pendencia — **nunca silenciado nem "corrigido" na descricao**.

## Fontes

Secoes do Manual do Cliente SUI-DI141388XY rev.2026 (`docs/manual-cliente-sui-2026.txt`):

| Secao | Linha | O que fixa |
|---|---|---|
| 2.1 | 27 | Sensor em modulo independente, instalacao a ate **500 metros** |
| 2.1 | 34 | "Interface de comunicacao serial RS485 para comunicacao entre o Sensor de Inclinacao e a Unidade Remota" |
| 4 | 58 | O sensor mede a inclinacao instantanea e **transmite** os valores a UR pelo RS-485 |
| 5.5 | 130 | O angulo e calculado **no sensor** e transmitido ja convertido em graus; faixa +/-90,0 graus, resolucao 0,1 grau |
| 7 | 297-298 | Falha de comunicacao: mensagem no display; causas (cabo rompido, A/B invertidos, ausencia de +5 Vcc, blindagem nao aterrada) |
| 8 / Tabela 3 | 304-311 | Terminais CN1 4 a 8: +5 Vcc, 0 V, A, B, blindagem |

Arquivos-fonte que definem o comportamento real (caminhos absolutos):

| Arquivo | Papel |
|---|---|
| `/home/ubuntu/repos/SUI/sensor/include/sensor_map.h` | Mapa de registradores publicado (indices e contagem) |
| `/home/ubuntu/repos/SUI/sensor/include/tilt.h` | Palavra de status, bit a bit, e a estrutura publicada |
| `/home/ubuntu/repos/SUI/sensor/src/proto/modbus_slave.cpp` | Escravo Modbus RTU: funcoes, excecoes, silencios, ordem de bytes |
| `/home/ubuntu/repos/SUI/sensor/src/proto/modbus_slave.h` | Constantes normativas do escravo (`kMaxReadCount`, `kReadRequestLen`, ...) |
| `/home/ubuntu/repos/SUI/sensor/src/main.cpp` | Enquadramento t3.5, periodo de publicacao, o que entra em cada registrador |
| `/home/ubuntu/repos/SUI/sensor/src/drivers/rs485.cpp` | `charTimeUs()`, UART em half-duplex por hardware |
| `/home/ubuntu/repos/SUI/sensor/src/drivers/scl3300.cpp` | Montagem da palavra de status a partir do SCL3300 |
| `/home/ubuntu/repos/SUI/sensor/src/drivers/scl3300_math.cpp` | Conversao de angulo e de temperatura para decimos |
| `/home/ubuntu/repos/SUI/sensor/include/board_pins.h` | Pinos, baud padrao, `kModbusSlaveId`, terminacao, bias |
| `/home/ubuntu/repos/SUI/include/board_pins.h` | Pinos do lado da UR |
| `/home/ubuntu/repos/SUI/lib_shared/depuri_wire/src/crc16.cpp` | CRC16-MODBUS compartilhado pelas duas placas |
| `/home/ubuntu/repos/SUI/src/proto/modbus_rtu.h` | Mestre da UR (hoje incompleto — ver secao 12) |
| `/home/ubuntu/repos/SUI/sensor/test/native/test_modbus/test_modbus.cpp` | Testes de host que travam o comportamento do escravo |

---

## 1. Escopo e papeis

- **Mestre unico:** a UR. Ela e a unica que inicia transacao no barramento.
- **Escravo:** a sensora. Nunca transmite espontaneamente; so responde a um pedido enderecado a ela.
- **Multiponto:** o barramento e half-duplex de 2 fios; o protocolo suporta outros escravos, mas o
  produto tem **um unico escravo** (endereco 1).
- **Sem escrita:** a sensora **nao aceita nenhuma funcao de escrita**. Todo parametro de operacao
  mora na UR. O enlace e, na pratica, somente-leitura.

---

## 2. Camada fisica

### 2.1 Meio

| Item | Especificacao |
|---|---|
| Padrao | TIA/EIA-485-A, half-duplex, 2 fios (A/B) + blindagem |
| Transceptor (ambas as placas) | `SN65HVD75DR`, alimentado em **3V3** |
| Cabo do par de dados | Par trancado blindado, impedancia caracteristica 100 a 120 ohm |
| Cabo de alimentacao do sensor | Par separado (+5 Vcc / 0 V), dentro da mesma capa |
| Distancia maxima de **sinalizacao** | 500 m a 19200 bps (manual 2.1, linha 27) |
| Distancia maxima de **alimentacao remota** | **50 m em 20 AWG** — ver 2.5 |
| Terminacao | 120 ohm em **cada extremidade** do tronco. Na sensora ja e de placa (`kRs485TerminatorOnBoard = true`, `sensor/include/board_pins.h`); na UR o resistor `R2` esta atras do jumper `J7` |
| Polarizacao (bias) externa | **Nao existe** nas duas placas (`kRs485ExternalBias = false`). O projeto depende do failsafe interno do SN65HVD75, que leva o receptor a nivel alto (idle) com a linha aberta |
| Blindagem | Aterrada em **uma unica extremidade**, na UR (terminal 8 do CN1), para nao fechar laco de terra |

Margem de velocidade x distancia: 19200 bps x 500 m = 9,6e6 baud.m, uma ordem de grandeza abaixo do
limite pratico de ~1e8 baud.m para par trancado terminado. **Nao ha motivo tecnico para subir o baud**
e ha motivo para nao subir: margem de ruido em patio portuario com inversores de frequencia.

### 2.2 Direcao do barramento (DE / RE)

Nas duas placas o `DE` e o `/RE` do SN65HVD75 estao **unidos** e sao chaveados pelo **periferico UART**
do ESP32 (`uart_set_mode(..., UART_MODE_RS485_HALF_DUPLEX)`, pino RTS), **nunca por software**.
Isso e requisito: a 19200 8N1 o turnaround por GPIO seria imprevisivel e produziria colisao com o
primeiro byte da resposta. Ver `sensor/src/drivers/rs485.cpp` e `src/drivers/rs485.cpp`.

### 2.3 Pinos

| Sinal | UR (`DE-PURI-DI261924`) | Sensora (`PUSI-DI261930`) |
|---|---|---|
| UART | UART2 | UART2 |
| RX | IO16 (`kRs485Rx`) | IO16 (`kRs485Rx`) |
| TX | IO17 (`kRs485Tx`) | IO17 (`kRs485Tx`) |
| DE + /RE | **IO14** (`kRs485De`) | **IO13** (`kRs485De`) |

Atencao: o pino de DE e **diferente** nas duas placas. Comentarios de cabecalho em
`sensor/src/drivers/rs485.cpp` citam "DE/RE 14", o que esta **errado** para a sensora (14 e o `WDI`
do STWD100). O codigo usa sempre `board::kRs485De`, entao o binario esta correto; o comentario e que
esta defasado (ver secao 12, pendencia P7).

### 2.4 Cabeamento do sensor — a numeracao diverge entre manual e esquematico

**Manual, Tabela 3 (CN1 da UR), linhas 304-311:**

| Terminal | Identificacao | Funcao |
|---|---|---|
| 4 | Vermelho — +5 Vcc | Alimentacao do sensor, +5 Vcc / 200 mA max |
| 5 | Amarelo — 0 V | Referencia de 0 V da alimentacao do sensor |
| 6 | Laranja — A | RS-485, sinal A |
| 7 | Marrom — B | RS-485, sinal B |
| 8 | Blindagem — BL | Blindagem do cabo de comunicacao |

**Esquematico `DE-PURI-DI261924` folha 1/2 — o conector do sensor e o CN2:**

| Borne | Net | Funcao |
|---|---|---|
| CN2A | +5V | Alimentacao do sensor |
| CN2B | 0V | Referencia |
| CN2C | RS485 A | Sinal A |
| CN2D | RS485 B | Sinal B |

**Divergencia declarada:** o manual chama de **CN2** o conector dos reles (terminais 13 a 25) e
distribui a ligacao do sensor no **CN1**; o esquematico usa **CN2 para o sensor** e CN1D..CN1O para
reles e saidas analogicas. **Nao existe no repositorio nenhum mapa que amarre CN1-4..8 (manual) a
CN2A..D (placa).** Isso tem de ser fechado **antes de imprimir qualquer etiqueta ou borneira**, sob
pena de o instalador cablar A e B na borneira errada — que e exatamente a primeira causa de falha
listada no item 7 do manual (linha 297). Ver pendencia P8.

### 2.5 Os 500 m valem para o sinal, nao para a alimentacao

O manual (linha 27) promete 500 m e a Tabela 3 promete +5 Vcc / 200 mA no mesmo cabo. As duas coisas
nao coexistem: com 20 AWG (0,0333 ohm/m), 500 m de cabo sao 1000 m de condutor de ida e volta =
33 ohm; a 100 mA isso da **3,3 V de queda** e o sensor nao liga.

Especificacao correta a ser adotada e escrita no manual:

| Grandeza | Limite |
|---|---|
| Sinalizacao RS-485 (A/B) | 500 m a 19200 bps |
| Alimentacao remota +5 Vcc pelo mesmo cabo, 20 AWG, 100 mA | **50 m** (queda 0,33 V) |
| Acima de 50 m | Alimentar o sensor localmente, mantendo o **0 V comum** com a UR |

---

## 3. Camada de enlace

| Parametro | Valor | Onde esta fixado |
|---|---|---|
| Velocidade | **19200 bps** | `board::kRs485DefaultBaud` nas duas placas |
| Bits de dados | **8** | `g_link.begin(kRs485DefaultBaud, 8, 'N', 1)`, `sensor/src/main.cpp` |
| Paridade | **nenhuma (N)** | idem |
| Stop bits | **1** | idem |
| Formato resumido | **19200 8N1** | |
| Bits por caractere no fio | 10 (1 start + 8 dados + 1 stop) | `Rs485Transport::charTimeUs()` |
| Tempo de caractere | **520 us** (aritmetica inteira: 10 x 1e6 / 19200 = 520) | `sensor/src/drivers/rs485.cpp` |
| Protocolo | **Modbus RTU** | `ModbusRtuSlave` |
| CRC | **CRC16-MODBUS**: polinomio 0xA001 refletido, semente 0xFFFF, transmitido **byte baixo antes do alto** | `lib_shared/depuri_wire/src/crc16.cpp` |
| Ordem dos registradores no quadro | **big-endian** (byte alto primeiro) | `ModbusRtuSlave::readRegisters` |

O baud **nao e configuravel** em nenhuma das placas: nao ha comando de console nem parametro em NVS
que o altere. O driver aceita 300..921600, mas nada no firmware muda o valor. Trocar o baud exige
recompilar **os dois** firmwares.

O CRC cobre **todos os bytes do quadro menos os dois do proprio CRC** — endereco e funcao inclusive.

---

## 4. Enderecamento

| Item | Valor |
|---|---|
| Endereco do escravo | **1** (`board::kModbusSlaveId`, `sensor/include/board_pins.h`) |
| Configuravel em tempo de execucao | Nao no produto. Existe `ModbusRtuSlave::setSlaveId()`, usado apenas pelos testes de host |
| Endereco de broadcast | 0 — **reconhecido e deliberadamente ignorado** (ver 5.3) |
| Endereco do mestre | Nao existe endereco de mestre em Modbus RTU |

---

## 5. Funcoes suportadas, excecoes e silencios

### 5.1 Funcoes aceitas

| Codigo | Nome | Comportamento na sensora |
|---|---|---|
| `0x03` | Read Holding Registers | Le o banco de 8 registradores |
| `0x04` | Read Input Registers | **Alias exato de 0x03** — le o mesmo banco |

Nao ha dois bancos. `0x03` e `0x04` sao intercambiaveis e devolvem os mesmos valores. **A UR usa
`0x03`.**

Qualquer outra funcao (incluindo todas as de escrita: 0x05, 0x06, 0x0F, 0x10) e recusada com excecao
`0x01`.

### 5.2 Formato dos quadros

**Pedido de leitura (sempre 8 bytes, `kReadRequestLen`):**

```
[addr][func][startHi][startLo][countHi][countLo][crcLo][crcHi]
```

**Resposta normal (`3 + 2*count + 2` bytes):**

```
[addr][func][byteCount = 2*count][reg0Hi][reg0Lo]...[crcLo][crcHi]
```

**Resposta de excecao (sempre 5 bytes, `kExceptionLen`):**

```
[addr][func | 0x80][codigoDeExcecao][crcLo][crcHi]
```

### 5.3 Tabela de decisao completa do escravo

Transcrita de `ModbusRtuSlave::handle()` e `ModbusRtuSlave::readRegisters()`, **na ordem em que os
testes sao aplicados** (a ordem importa: ela decide qual excecao sai quando dois erros coexistem).

| # | Condicao | Resposta | Contador afetado |
|---|---|---|---|
| 1 | Ponteiro de pedido ou de resposta nulo | Silencio | — |
| 2 | `len < 4` (`kMinRequestLen`) | **Silencio** | — |
| 3 | CRC16 nao confere | **Silencio** | `badFrames++` |
| 4 | Endereco != 1 e != 0 | **Silencio** | — |
| 5 | (a partir daqui) qualquer quadro integro enderecado a nos | — | `requests++` |
| 6 | Funcao != 0x03 e != 0x04, **e** endereco = 0 (broadcast) | Silencio | — |
| 7 | Funcao != 0x03 e != 0x04, enderecada | **Excecao 0x01** ILLEGAL_FUNCTION | `exceptions++`, `responses++` |
| 8 | Funcao valida mas `len != 8` | **Silencio** | — |
| 9 | Funcao valida, `len == 8`, endereco = 0 (broadcast) | **Silencio** | — |
| 10 | `count == 0` | **Excecao 0x02** ILLEGAL_DATA_ADDRESS | `exceptions++`, `responses++` |
| 11 | `count > 125` (`kMaxReadCount`) | **Excecao 0x03** ILLEGAL_DATA_VALUE | `exceptions++`, `responses++` |
| 12 | `start + count > 8` | **Excecao 0x02** ILLEGAL_DATA_ADDRESS | `exceptions++`, `responses++` |
| 13 | Buffer de resposta menor que o necessario | Silencio | — |
| 14 | Tudo valido | Resposta normal | `responses++` |

**Duas divergencias do Modbus canonico, deliberadas e testadas — a UR nao pode se surpreender com elas:**

1. `count == 0` devolve **0x02** (o padrao manda 0x03 ILLEGAL_DATA_VALUE).
2. Um pedido com funcao **valida** mas comprimento diferente de 8 e **descartado em silencio**, sem
   excecao. Ja um pedido com funcao **invalida** e comprimento errado ainda recebe a excecao 0x01,
   porque o teste de funcao (linha 7 da tabela) vem **antes** do teste de comprimento (linha 8).

**Limite duro:** `start + count <= 8`. A leitura maxima util e `start = 0, count = 8`, resposta de
**21 bytes**.

### 5.4 Exemplos byte a byte (CRC real, conferido)

Leitura completa do banco — **e a transacao que a UR usa em regime**:

```
Pedido  (8 bytes) : 01 03 00 00 00 08 44 0C
Resposta (21 bytes): 01 03 10 FF 85 01 41 FF F9 00 01 01 0A 00 C1 00 01 0E 10 29 61
                     |  |  |  X---- Y---- Z---- ST--- T---- WHO-- FW--- UP--- CRC
                     |  |  byteCount = 16
                     |  funcao
                     endereco
```

Decodificando a resposta acima: X = 0xFF85 = -123 = **-12,3 graus**; Y = 0x0141 = +321 = **+32,1
graus**; Z = 0xFFF9 = -7 = **-0,7 grau**; STATUS = **0x0001** (dado valido); T = 0x010A = 266 =
**26,6 graus C**; WHO_AM_I = **0x00C1**; FW = 0x0001 = **v0.1**; UPTIME = 0x0E10 = **3600 s**.

Outros quadros de referencia:

| Situacao | Pedido | Resposta |
|---|---|---|
| Leitura parcial (regs 0..3) | `01 03 00 00 00 04 44 09` | resposta normal de 13 bytes |
| `count == 0` | `01 03 00 00 00 00 45 CA` | `01 83 02 C0 F1` |
| `start + count > 8` | `01 03 00 00 00 09 85 CC` | `01 83 02 C0 F1` |
| `count > 125` | `01 03 00 00 00 C8 44 5C` | `01 83 03 01 31` |
| Funcao de escrita 0x06 | `01 06 00 00 00 01 48 0A` | `01 86 01 83 A0` |

---

## 6. Mapa de registradores

Transcrito de `sensor/include/sensor_map.h` e do que `publishTilt()` (`sensor/src/main.cpp`)
realmente escreve. **Nao ha registrador de escrita.**

| Addr | Nome | Tipo no fio | Interpretacao | Unidade | Faixa util | Significado |
|---:|---|---|---|---|---|---|
| 0 | `ANG_X` | uint16 big-endian | **reinterpretar como int16 com sinal** | decimo de grau | -900 a +900 | Inclinacao do eixo X. O formato representa ate +/-1800 (+/-180,0 graus) |
| 1 | `ANG_Y` | uint16 BE | int16 com sinal | decimo de grau | -900 a +900 | Inclinacao do eixo Y |
| 2 | `ANG_Z` | uint16 BE | int16 com sinal | decimo de grau | -900 a +900 | Inclinacao do eixo Z. **Diagnostico apenas** — a UR le, registra, e nao usa para rele nem para saida analogica |
| 3 | `STATUS` | uint16 BE | bitfield | — | ver secao 7 | Palavra de saude do sensor. **E o unico criterio de validade do dado** |
| 4 | `TEMP` | uint16 BE | int16 com sinal | decimo de grau Celsius | tipico -400 a +900 | Temperatura interna do SCL3300 |
| 5 | `WHO_AM_I` | uint16 BE | constante | — | `0x00C1` ou `0x0000` | `0x00C1` quando o SCL3300 respondeu ao menos uma vez; `0x0000` desde o boot ate a primeira leitura boa |
| 6 | `FW_VERSION` | uint16 BE | empacotado | — | `(major << 8) \| minor` | Versao do firmware da sensora. **Hoje fixo em `0x0001`** — ver pendencia P5 |
| 7 | `UPTIME` | uint16 BE | uint16 | segundo | 0 a 65535 | Uptime da sensora truncado a 16 bits. **Envolve a cada 65535 s = 18 h 12 min 15 s.** So avanca em leitura **boa** do SCL3300 — ver 7.3 e pendencia P4 |
| — | `kRegCount` | — | — | — | **8** | Contagem total. `start + count` nunca pode ultrapassar este valor |

### 6.1 Conversao de angulo (feita na sensora, nao na UR)

`scl::angleDeciDegrees()` em `sensor/src/drivers/scl3300_math.cpp`:

```
deci = arredondamento_simetrico( (int16)raw_do_SCL3300 * 900 / 16384 )
```

Aritmetica inteira, arredondamento simetrico em torno do zero (nao truncamento). Consequencias
verificadas sobre os 65536 codigos possiveis:

| Codigo bruto do SCL3300 | Valor publicado | Grau |
|---|---|---|
| `0x4000` | +900 | +90,0 exatos |
| `0x2000` | +450 | +45,0 |
| `0x0000` | 0 | 0,0 |
| `0xC000` | -900 | -90,0 exatos |
| `0x7FFF` / `0x8000` | +1800 / -1800 | +/-180,0 (sem overflow em int16) |

LSB do sensor = 90 / 16384 = 0,0054932 grau, ou seja **18,2 LSB por digito de 0,1 grau** — folga de
18x sobre a resolucao exigida pelo manual (linha 130). O erro de quantizacao para decimo de grau e de
**+/-0,05 grau**, e consome mais da metade dos +/-0,09 grau (+/-0,1 % de fundo de escala) prometidos
no manual.

**Regra derivada e obrigatoria para a UR:** Preset e Sentido do Sensor sao aplicados como aritmetica
**inteira exata** sobre os decimos — subtracao e troca de sinal. **Nunca** como ganho fracionario,
porque qualquer multiplicacao amplificaria esses 0,05 grau.

**Ramo com sinal, nao sem sinal.** O valor tem de ser lido como `int16`. Lido como `uint16`, -45,0
graus apareceria como +315,0, incompativel com o formato `+/-XXX,X` e com limites de -90,0 a +90,0.

### 6.2 Conversao de temperatura

`scl::temperatureDeciC()`:

```
deci_C = arredondamento( raw * 100 / 189 ) - 2730
```

Resultado em decimos de grau Celsius, com sinal. E dado de diagnostico: nao entra em nenhuma decisao
de rele nem de saida analogica.

---

## 7. Palavra de status (registrador 3), bit a bit

Definida em `/home/ubuntu/repos/SUI/sensor/include/tilt.h` e montada em `Scl3300::read()`
(`sensor/src/drivers/scl3300.cpp`).

| Bit | Mascara | Nome | Significado | Sentido |
|---:|---|---|---|---|
| 0 | `0x0001` | `DATA_VALID` | A leitura desta publicacao passou em **todos** os criterios de sanidade | **1 = bom** |
| 1 | `0x0002` | `SCL_CRC_ERROR` | Ao menos um quadro SPI do SCL3300 reprovou no CRC de 8 bits | 1 = falha |
| 2 | `0x0004` | `SCL_STARTUP` | Bit de startup do SCL3300 visto, ou RS de erro sem falta grave — sensor ainda estabilizando | 1 = falha |
| 3 | `0x0008` | `SCL_SELFTEST_FAIL` | Falha grave do STATUS do SCL3300, **ou** autoteste reprovado (condicao latchada ate `reinit`) | 1 = falha |
| 4 | `0x0010` | `SCL_NOT_RESPONDING` | Link SPI com o SCL3300 mudo, ou STATUS nao pode ser lido | 1 = falha |
| 5 | `0x0020` | `SATURATED` | Bit SAT do SCL3300: aceleracao acima do fundo de escala do modo — angulo **nao confiavel** | 1 = falha |
| 6 | `0x0040` | `WDT_RESET` | Reservado para sinalizar reset por watchdog | **Nunca escrito hoje** — pendencia P6 |
| 7 | `0x0080` | `STO_OUT_OF_RANGE` | Self-Test Output do SCL3300 fora do limiar da Tabela 23 do datasheet por **20 leituras consecutivas** (200 ms). Datasheet 6.2 manda ler o STO continuamente apos cada leitura XYZ e contar eventos subsequentes; uma amostra isolada **nao** e falha. Latchado: so `reinit` limpa | 1 = falha |
| 7..15 | `0xFF80` | — | Reservados. Devem ser lidos como 0 | — |

`DATA_VALID` so e setado quando **todas** estas condicoes valem simultaneamente: todos os 6 quadros
SPI da rajada chegaram, sem CRC ruim, sem link mudo, sem bit de startup, sem RS de erro, **sem
saturacao**, sem falta grave e com `RS == Ok`.

### 7.1 Valores realmente observaveis no fio

De todas as combinacoes possiveis, o caminho de publicacao so deixa chegar estas:

| Valor | Como acontece | O que a UR deve fazer |
|---|---|---|
| `0x0000` | Registradores zerados no boot e **nenhuma leitura boa desde entao** | **Rejeitar** |
| `0x0001` | Operacao normal | **Unico valor aceito** |
| `0x0011` | `DATA_VALID` de uma leitura boa anterior **sobrevivendo** ao `\|=` de `SCL_NOT_RESPONDING` sobre angulos **congelados** | **Rejeitar** |
| `0x0009` | `DATA_VALID` com autoteste reprovado latchado (`selfTestFailed_` nao entra na condicao de `valid`) | **Rejeitar** |

`SCL_CRC_ERROR`, `SCL_STARTUP` e `SATURATED` sao calculados corretamente por `read()`, mas em todas
essas situacoes `read()` devolve `Status` falho e `publishTilt()` **nao chega a ser chamado** — logo
esses bits **nao alcancam o fio** hoje. Sao visiveis apenas no console local da sensora. Ver
pendencia P3.

### 7.2 A regra de aceitacao do mestre

Esta e a regra de seguranca do enlace. **Nenhuma amostra entra no filtro, na saida analogica ou na
decisao de rele sem passar por todos os itens:**

1. Resposta completa recebida dentro do timeout (secao 8.3);
2. CRC16 correto;
3. `addr == 1`, `func == 0x03`, `byteCount == 16`, comprimento total `== 21`;
4. **`registrador 3 == 0x0001` EXATAMENTE**;
5. `|ANG_X| <= 900` e `|ANG_Y| <= 900` decimos.

**O item 4 nao pode ser implementado como mascara.** `if (status & 0x0001)` aceita `0x0011`
(angulo congelado de sensor morto) e `0x0009` (autoteste reprovado) — e nesses casos o rele **nao
atuaria**, que e exatamente a falha de seguranca que este equipamento existe para evitar.

### 7.3 Frescor: por que o status sozinho nao basta

Se o laco principal da sensora travar mas o `esp_timer` continuar chutando o STWD100 (o chute **nao**
vem do laco — `sensor/src/drivers/ext_wdt.cpp`), a placa fica **viva no barramento**, respondendo
Modbus com CRC correto, valores congelados e `STATUS = 0x0001`. Os itens 1 a 5 acima passam todos.

O unico detector disponivel **no contrato atual** e o registrador 7: ele so avanca dentro de
`publishTilt()`, ou seja, e um carimbo do instante da ultima publicacao valida. Limitacoes que a UR
tem de respeitar:

- **Granularidade de 1 segundo.** Sao precisos ~3 s de estagnacao para concluir congelamento com
  seguranca — tempo longo demais para um rele de seguranca.
- **Envolve em 65535 s.** A comparacao tem de ser **"diferente do valor anterior"** em aritmetica
  `uint16`, nunca "maior que o anterior".

Criterio adotado enquanto a pendencia P4 nao for fechada: **o registrador 7 tem de mudar ao menos uma
vez a cada 3,0 s.** Estagnacao alem disso, mesmo com quadros validos, declara falha.

---

## 8. Temporizacao

### 8.1 Silencio de enquadramento (t3.5)

O escravo `ModbusRtuSlave` e **codigo puro, sem nocao de tempo**. Todo o enquadramento vive em
`sensor/src/main.cpp`:

```
interFrameGapUs() = max( charTimeUs() * 7 / 2 , 750 )
```

| Grandeza | Valor a 19200 8N1 |
|---|---|
| `charTimeUs()` | **520 us** |
| t3.5 = 520 x 7 / 2 | **1820 us** |
| Piso `kMinGapUs` | 750 us (so morde acima de ~46 kbps) |
| **t3.5 efetivo** | **1,82 ms** |

A deteccao **nao e por interrupcao**: `serviceLink()` poleia com `uart_read_bytes(..., 2 ms)`
(`kReadPollMs = 2`) e so avalia o gap num poll que voltou **vazio**.

**Consequencia para o mestre:** dois pedidos emitidos com menos de 1,82 ms de intervalo sao
**concatenados num unico quadro** pelo escravo e reprovam no CRC. O periodo de polling de 50 ms da
margem de 27x — nao ha risco em regime, mas nao se deve emitir rajadas.

O buffer de RX (`kRxCapacity = 256`) e **zerado incondicionalmente apos cada `handle()`**, inclusive
quando nao houve resposta. Nao ha dessincronizacao acumulada.

### 8.2 Latencia de resposta do escravo

| Componente | Tipico | Pior caso |
|---|---|---|
| Pedido no fio (8 bytes x 10 bits / 19200) | 4,17 ms | 4,17 ms |
| Silencio t3.5 antes de o escravo decidir | 1,82 ms | 1,82 ms |
| Granularidade do poll de 2 ms | 0 | 2,00 ms |
| Jitter do laco (rajada de 6 quadros SPI do SCL3300 a cada 10 ms + console) | ~0 | ~2,00 ms |
| Resposta no fio (21 bytes x 10 bits / 19200) | 10,94 ms | 10,94 ms |
| **Total (fim do pedido -> fim da resposta)** | **~13 ms** | **~21,1 ms** |

### 8.3 Parametros do mestre (UR)

| Parametro | Valor | Justificativa |
|---|---|---|
| Transacao | `0x03`, escravo 1, `start = 0`, `count = 8` | Uma unica transacao traz angulo, status, temperatura e uptime do **mesmo instante** |
| Periodo de polling | **50 ms (20 Hz)** | 21,1 ms de ocupacao de pior caso deixa 58 % do ciclo livre |
| Timeout de resposta | **30 ms** contados do ultimo byte transmitido | 1,4x o pior caso de 21,1 ms |
| Silencio antes do proximo pedido | >= 1,82 ms | Garantido com folga de 27x pelo ciclo de 50 ms |
| Quadros invalidos para declarar falha | **3 consecutivos** (150 ms) | Rapido para falhar |
| Quadros validos para restabelecer | **10 consecutivos** (500 ms) | Lento para confiar (assimetria deliberada) |

**Por que uma transacao unica de 8 registradores e nao duas menores:** `publishTilt()` e
`ModbusRtuSlave::handle()` rodam **sequencialmente no mesmo laco** da sensora (`sensor/src/main.cpp`),
nunca em ISR nem em outra task. A resposta e portanto um **retrato coerente** do mesmo instante de
leitura, sem tearing e sem necessidade de duplo buffer. Duas transacoes separadas trariam angulo e
status de instantes diferentes — exatamente a incoerencia que produz decisao de rele sobre dado
invalido.

### 8.4 Taxa de atualizacao dos registradores

| Grandeza | Valor |
|---|---|
| Periodo de leitura do SCL3300 e republicacao | **10 ms** (`kTiltPeriodMs`), ~100 Hz nominal |
| Taxa efetiva | ~80 a 100 Hz (o laco tambem bloqueia ate 2 ms em `serviceLink` e roda o console) |
| Filtro na sensora | **Nenhum.** Valor instantaneo, sem media nem mediana |

O filtro passa-baixa prometido no manual (secoes 4 e 6) **nao existe na sensora** — tem de ser
implementado na UR. O unico passa-baixa da cadeia hoje e o do proprio SCL3300, definido pelo MODE, que
e constante de compilacao.

---

## 9. Tratamento de erro

### 9.1 Do lado do escravo (implementado)

| Erro | Acao | Observavel |
|---|---|---|
| CRC ruim | Silencio, `badFrames++` | Comando `link` no console local |
| Endereco de outro escravo | Silencio | — |
| Quadro com menos de 4 bytes | Silencio | — |
| Funcao nao suportada | Excecao 0x01 | `exceptions++` |
| `count == 0` ou `start + count > 8` | Excecao 0x02 | `exceptions++` |
| `count > 125` | Excecao 0x03 | `exceptions++` |
| Funcao valida com comprimento != 8 | **Silencio** | — |
| Broadcast (endereco 0) | Silencio, sempre | — |
| Estouro do buffer de RX (>256 bytes sem gap) | Bytes excedentes descartados; o CRC reprova em seguida | `badFrames++` |

### 9.2 Do lado do mestre (requisito para a UR)

| Evento | Classificacao | Acao |
|---|---|---|
| Timeout de 30 ms sem resposta | Quadro invalido | Conta para os 3 de falha |
| CRC de resposta ruim | Quadro invalido | Conta para os 3 de falha |
| Endereco, funcao, `byteCount` ou comprimento errados | Quadro invalido | Conta para os 3 de falha |
| Excecao recebida (`func \| 0x80`) | Quadro invalido | Registrar o codigo para diagnostico; conta para os 3 de falha. **Uma excecao em regime significa erro de programacao do mestre**, nao defeito de campo |
| `STATUS != 0x0001` | Quadro invalido | Conta para os 3 de falha |
| `\|ANG_X\|` ou `\|ANG_Y\|` acima de 900 decimos | **Falha mecanica de faixa**, nao valor a exibir e nao valor a truncar | Caminho de falha proprio: sensor invertido, solto ou fora da orientacao da etiqueta |
| Registrador 7 estagnado por mais de 3,0 s | Quadro invalido (sensora congelada) | Conta para os 3 de falha |
| **3 quadros invalidos consecutivos** | **FALHA DE COMUNICACAO** | Reles e saidas analogicas ao estado de falha definido pelo produto; mensagem no display (manual, item 7) |
| **10 quadros validos consecutivos** | Recuperacao | Retorno a operacao normal; o filtro e recarregado com a primeira amostra boa, **nunca com zero** |

**O que o enlace NAO detecta.** Este contrato detecta sensor mudo, link rompido, A/B invertidos,
sensora congelada e dado insano. **Nao detecta** bobina de rele aberta nem contato colado: nao ha
realimentacao de contato no hardware (`RelayBank::get()` devolve cache de escrita,
`src/drivers/relays.cpp`). Isso e limitacao de hardware e tem de constar do manual, nao ser escondida.

---

## 10. Simulador (fake do `env:native`)

O `env:native` compila sem framework Arduino. O fake do enlace existe para que a logica de aplicacao
da UR (aceitacao, filtro, limites, reles) seja testada **no host**, sem placa, e para que uma
regressao no criterio de validade quebre o build em vez de quebrar um porto.

### 10.1 Interface

O fake implementa `ISerialTransport` (`include/iface/iserial_transport.h`) — **transporte, nao
protocolo**. Assim o `ModbusRtuProtocol` real da UR e exercitado byte a byte, e nao substituido por um
atalho. Para os testes que precisam apenas de angulo, um segundo fake implementa `IRs485Protocol`.

### 10.2 Comportamento obrigatorio (paridade com o escravo real)

O fake **tem de reproduzir exatamente** a tabela de decisao da secao 5.3, incluindo as duas
divergencias do Modbus canonico. Um fake mais "correto" que o hardware e um fake que mente.

| # | Requisito |
|---|---|
| S1 | Banco de **8** registradores `uint16`, indices 0..7, injetaveis pelo teste |
| S2 | Aceitar **somente** 0x03 e 0x04, tratando-os como o mesmo banco |
| S3 | CRC16-MODBUS pela **mesma** funcao `crc16Modbus()` de `lib_shared/depuri_wire` — nunca uma copia local |
| S4 | Registradores **big-endian** na resposta; CRC **lo antes de hi** |
| S5 | Excecao `0x02` para `count == 0` (**nao** 0x03) |
| S6 | Excecao `0x03` para `count > 125` |
| S7 | Excecao `0x02` para `start + count > 8` |
| S8 | Excecao `0x01` para qualquer outra funcao, **avaliada antes** do teste de comprimento |
| S9 | **Silencio** (zero bytes) para: CRC ruim, endereco != 1, `len < 4`, `len != 8` com funcao valida, broadcast |
| S10 | Contadores `requests`, `responses`, `badFrames`, `exceptions` com a mesma semantica do escravo real |

### 10.3 Comportamento de tempo e de falha (injetavel pelo teste)

| # | Requisito |
|---|---|
| T1 | Relogio **virtual**, avancado explicitamente pelo teste. Nenhum `delay()`, nenhum `millis()` real |
| T2 | Latencia de resposta programavel, com valor padrao de **13 ms** e pior caso de **21,1 ms** |
| T3 | Modo **timeout**: engolir o pedido e nao responder, para exercitar os 3 quadros invalidos |
| T4 | Modo **CRC corrompido**: responder com um bit trocado nos dados, CRC nao recalculado |
| T5 | Modo **resposta truncada**: devolver menos bytes que `byteCount` promete |
| T6 | Modo **congelado**: manter angulo e `STATUS = 0x0001` e **parar de incrementar o registrador 7** (reproduz o laco travado da secao 7.3) |
| T7 | Modo **congelado com falha**: `STATUS = 0x0011`, angulos parados (reproduz o `\|=` de `SCL_NOT_RESPONDING` sobre valor congelado) |
| T8 | Modo **autoteste reprovado**: `STATUS = 0x0009` |
| T9 | Modo **boot**: todos os registradores em 0, `STATUS = 0x0000`, `WHO_AM_I = 0x0000` |
| T10 | Modo **saturacao**: `STATUS` sem `DATA_VALID` (a saturacao real nunca publica; o teste confirma que a UR rejeita) |
| T11 | Modo **fora de faixa**: `ANG_X` acima de 900 decimos (ate +/-1800), com `STATUS = 0x0001` — exercita a falha mecanica de faixa |
| T12 | Modo **excecao**: responder `01 83 02 C0 F1` a um pedido bem formado |
| T13 | Modo **eco/colisao**: devolver o proprio pedido como se fosse resposta (falha classica de DE preso ativo) |
| T14 | Incremento do registrador 7 a cada segundo virtual, com **wrap em 65535 exercitado por teste** |

### 10.4 Testes minimos que o fake tem de sustentar

| # | Teste |
|---|---|
| V1 | Transacao normal com `STATUS = 0x0001` produz angulo aceito, com valor exato em decimos |
| V2 | `STATUS = 0x0011` e **rejeitado** (prova de que a UR nao usa mascara de bit) |
| V3 | `STATUS = 0x0009` e **rejeitado** |
| V4 | `STATUS = 0x0000` e **rejeitado** |
| V5 | 3 timeouts consecutivos declaram falha em <= 150 ms virtuais |
| V6 | 2 timeouts seguidos de 1 quadro valido **nao** declaram falha |
| V7 | 9 quadros validos apos falha **nao** restabelecem; o decimo restabelece |
| V8 | Registrador 7 estagnado por 3,0 s com `STATUS = 0x0001` declara falha |
| V9 | Wrap do registrador 7 (65535 -> 0) **nao** declara falha |
| V10 | `ANG_X = +1200` decimos com status bom entra no caminho de falha mecanica de faixa, nao no de medicao |
| V11 | Valores de fronteira `+900`, `-900`, `0`, `+1`, `-1` sobrevivem intactos a ida e volta |
| V12 | Quadro de excecao nao e confundido com resposta normal (comprimento 5 contra 21) |
| V13 | O buffer de recepcao da UR comporta os 21 bytes da resposta completa |

**V13 nao e formalidade.** Hoje `ModbusRtuProtocol::kRxCap = 16` (`src/proto/modbus_rtu.h`) e a
resposta de 21 bytes **nao cabe**: `push()` descarta ao estourar. Este teste falha **antes** da
correcao e passa depois — e a prova executavel da pendencia P2.

---

## 11. Alteracoes de codigo exigidas por este contrato

Nenhuma delas e opcional: sem elas o enlace descrito aqui nao funciona.

| Arquivo | Alteracao | Motivo |
|---|---|---|
| `src/proto/modbus_rtu.h` | `kRegisterCount` 2 -> **8** | Ler status e uptime, nao so X e Y |
| `src/proto/modbus_rtu.h` | `kDataBytes` 4 -> **16** | 8 registradores |
| `src/proto/modbus_rtu.h` | `kResponseLen` 9 -> **21** | 3 + 16 + 2 |
| `src/proto/modbus_rtu.h` | `kRxCap` 16 -> **32** | Hoje a resposta de 21 bytes nao cabe e e descartada |
| `src/proto/modbus_rtu.h` | `kDefaultPollTimeoutMs` 50 -> **30** | Alinhar ao orcamento da secao 8.3 |
| `src/proto/irs485_protocol.h` | `struct Angle` ganha `uint16_t status` e `uint16_t uptimeS` | Sem eles a UR nao tem como aplicar a regra de aceitacao |
| `src/main.cpp` | Protocolo ativo passa a `ModbusRtuProtocol` (hoje `EchoProtocol`) | O quadro do jig nao carrega validade nenhuma |
| `sensor/src/main.cpp` | `g_activeProtocol = &g_modbusSlave` no boot | **Bloqueador P1** |
| `sensor/src/main.cpp` | `g_registers[kRegStatus] = kStsSclNotResponding` (atribuicao, nao `\|=`) | Limpar `DATA_VALID` na falha |
| `sensor/src/main.cpp` | Zerar `ANG_X/Y/Z` ou marca-los invalidos na falha | Nao publicar angulo congelado |
| `sensor/src/main.cpp` | Registrador 6 derivado de `FW_VERSION` e publicado desde o boot | Pendencia P5 |
| `sensor/src/proto/jig_slave.*` | Atras de flag de build do ambiente de fabrica | Ausente do firmware de produto |

---

## 12. Divergencias conhecidas e pendencias

| # | Pendencia | Situacao real hoje | Consequencia se nao for fechada |
|---|---|---|---|
| **P1** | ~~**Bloqueador.** A sensora **boota falando o quadro do jig**, nao Modbus~~ | **FECHADA em 2026-09-01, commit `ed05d95`.** `g_activeProtocol` passou a `&g_modbusSlave`; `proto jig` continua disponivel para o item t3 do jig de fabrica. O banner de boot do console passou a imprimir o protocolo ativo | Custou uma sessao de bancada antes de fechar: a UR mostrava falha de comunicacao com o cabo perfeito e a sensora saudavel, que e o sintoma identico ao de cabo rompido, A e B invertidos e sensora sem +5 V |
| **P2** | O mestre da UR le **2** registradores (`kRegisterCount = 2`) e nunca olha o status; `kRxCap = 16` nao comporta a resposta de 21 bytes | Confirmado | Sem correcao, a UR nao consegue nem receber a leitura completa, nem aplicar a regra de aceitacao |
| **P3** | Em falha do SCL3300 a sensora **congela** os angulos no ultimo valor bom e faz apenas `\|= 0x0010` no status, produzindo `0x0011` = `DATA_VALID` **e** `SCL_NOT_RESPONDING` juntos | Confirmado (`sensor/src/main.cpp`) | Mestre que use mascara de bit aceita angulo velho de sensor morto **indefinidamente**. Enquanto nao for corrigido, a regra `status == 0x0001` exata e **obrigatoria** |
| **P4** | O registrador 7 e uptime em **segundos**, atualizado so em leitura boa. Granularidade grosseira demais para detectar laco travado | Confirmado | Deteccao de sensora congelada leva ~3 s. Proposta a fechar: trocar por **heartbeat incrementado a cada ciclo de 10 ms, fora do ramo de leitura boa**, o que baixaria a deteccao para 150 ms |
| **P5** | Registrador 6 hardcoded em `kFwMajor = 0` / `kFwMinor = 1`, independente de `FW_VERSION` do `platformio.ini`; e so escrito dentro de `publishTilt()` | Confirmado | Subir a versao de build nao muda o registrador; placa com inclinometro morto publica versao `0x0000` |
| **P6** | `kStsWdtReset` (`0x0040`) **nao e escrito por nenhuma linha** do firmware | Confirmado por busca | Reset por watchdog e invisivel para a UR |
| **P7** | Comentarios de cabecalho da sensora citam pinos errados (`rs485.cpp` diz "DE/RE 14"; o correto e 13. `ext_wdt.cpp` diz "WDI IO19"; o correto e 14) | Confirmado | O binario esta certo (usa `board_pins.h`); quem documentar lendo o comentario publica pinagem errada |
| **P8** | Numeracao dos bornes do sensor diverge entre manual (CN1-4..8) e esquematico (CN2A..D). Nao ha mapa no repositorio | Confirmado | Risco de cablar A/B na borneira errada na instalacao. Fechar antes de imprimir etiqueta |
| **P9** | Os 500 m do manual nao valem para a alimentacao remota de +5 Vcc (secao 2.5) | Confirmado por calculo | Sensor nao liga em instalacao longa. Corrigir manual (linha 27 e Tabela 3, terminal 4) |
| **P10** | O manual (item 7) nao da o **texto literal** da mensagem de falha, nem o estado de reles e saidas analogicas durante a falha | Confirmado (manual, linhas 297-298) | Lacuna de **requisito**, nao de implementacao. Tem de ser fechada com decisao de produto e escrita no manual |

---

## 13. Rastreabilidade

| REQ | Enunciado | Origem | Onde este documento atende | Verificacao |
|---|---|---|---|---|
| **REQ-COM-01** | A UR e o Sensor de Inclinacao comunicam-se por interface serial RS-485, com o sensor instalado a ate 500 m da UR; a perda dessa comunicacao e detectada e sinalizada | Manual 2.1 (linhas 27 e 34), 4 (linha 58), 7 (linhas 297-298) | Secao 2 (meio fisico, terminacao, blindagem, 500 m e o limite de 50 m para alimentacao remota); secao 3 (19200 8N1, Modbus RTU, CRC16-MODBUS); secao 4 (escravo 1); secao 5 (funcoes, excecoes e silencios); secao 8 (t3.5 de 1,82 ms, polling de 50 ms, timeout de 30 ms); secao 9.2 (3 quadros invalidos declaram falha em 150 ms; 10 validos restabelecem em 500 ms) | Bancada: 500 m de par trancado terminado, 1 h sem quadro invalido. Host: V5, V6, V7, V12 (secao 10.4). Campo: rompimento de A, inversao de A/B e queda do +5 Vcc, cada um declarando falha em <= 150 ms |
| **REQ-MEA-02** | O angulo e calculado no sensor e transmitido a UR ja convertido em graus, com faixa de +/-90,0 graus por eixo e resolucao de 0,1 grau, e a UR so utiliza leitura comprovadamente valida | Manual 2.1 (linhas 25, 28-29), 5.5 (linha 130), 6.1 | Secao 6 (registradores 0/1/2 como `int16` em decimos de grau; conversao `raw * 900 / 16384` com 18,2 LSB por decimo; faixa util -900..+900; regra de Preset e Sentido como aritmetica inteira exata); secao 7 (palavra de status bit a bit); secao 7.2 (aceitacao so com `status == 0x0001` exato); secao 7.3 (frescor pelo registrador 7); secao 9.2 (fora de faixa como falha mecanica, nao como valor truncado) | Host: V1, V2, V3, V4, V8, V9, V10, V11 (secao 10.4). Bancada: mesa de seno em -90,0 / -45,0 / 0,0 / +45,0 / +90,0 graus por eixo, um eixo por vez (X e Y nao podem valer +/-90 simultaneamente), erro <= 0,1 grau. Sensor desconectado do SCL3300 com a sensora viva: a UR tem de recusar, nao congelar |
