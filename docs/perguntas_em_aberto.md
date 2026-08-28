# Perguntas em aberto e premissas assumidas

Nenhuma delas foi decidida por conta propria de forma silenciosa: cada item abaixo diz **o que o
firmware assumiu para poder existir** e **o que muda quando a engenharia responder**.

| # | Pergunta | Premissa assumida no firmware | O que muda ao responder |
|---|---|---|---|
| 1 | Modelo do display do CN4. | **RESPONDIDA.** Base adotada: a parte de display do repo `CDM4L-DI221651`, que usa **SSD1322 NHD 256x64** por SPI de 4 fios via U8g2. Implementado em `U8g2Display` e selecionado por `-DDISPLAY_DRIVER=DISPLAY_DRIVER_U8G2` no env `esp32dev-ihm`. "DATA CLEAR" no CN4-5 confirma-se como `D/C`. | Se o display montado for outro, trocar so o construtor do U8g2: o resto do jig depende de `IDisplay`, nao do controlador. |
| 2 | Pino `EN` (3) do STWD100: aterrado, flutuante ou em GPIO? | **RESPONDIDA pelo esquematico:** nao esta conectado a GPIO nenhum (fica flutuante, com o pull-down interno habilitando o watchdog). Nao existe `wdt off` por software; o comando e `wdt kick off`. Vale para as duas placas. | Nada a mudar. |
| 3 | Existe pull-up em `WDO` (open-drain)? Qual valor, para qual trilho? | **Assumido presente (10 k, valor recomendado pela ST).** Sem ele, `EN` do ESP32 fica indefinido. | Se nao existir, o `wdt test` falha e a mensagem de FAIL ja cita essa causa. |
| 4 | AVDD do DAC8562 e realmente +5 V? | **RESPONDIDA pelo esquematico (folha 2/2): SIM, AVDD vem do +5 V** (C17 4,7 uF + C18 0,1 uF). Logo `VIH` minimo = 0,7 x 5 = **3,5 V contra os 3,3 V que o ESP32 entrega**: a nao conformidade e REAL, nao hipotese. Clock SPI padrao 1 MHz, ajustavel de 100 kHz a 10 MHz por `dac spi <hz>`. | Decisao de engenharia: alimentar AVDD com 3V3 (perde o ganho x2, FE do DAC cai para 2,5 V) ou inserir 74AHCT125 / 74LVC1T45 em `SYNC`, `SCLK` e `DIN`. |
| 5 | O 0 V de saida do A0515S-2WR3 esta ligado ao 0 V do sistema? | **RESPONDIDA pelo esquematico (folha 2/2): SIM, o pino 0V de saida esta no mesmo `0V` do sistema.** As saidas analogicas **nao sao isoladas galvanicamente**; o conversor so gera os +/-15 V. | O texto de divulgacao e o roteiro (laco de terra com a carga de 250 ohm, tensao de modo comum) precisam ser corrigidos: nao prometer isolacao. |
| 6 | Valores de RSET e da malha do XTR300 (R18, R14/R19 2K2, R17 10K). | **Desconhecidos de proposito.** O firmware nao presume fator de escala: `ao <eixo> <modo> <valor>` so funciona apos `cal`, calibracao de 2 pontos por eixo e por modo. | Com os valores, da para prever a escala e julgar se um desvio e componente errado ou ajuste normal. |
| 7 | Ha bias externo no par RS-485 ou o projeto depende so do failsafe interno do SN65HVD75? | **RESPONDIDA pelo esquematico:** so `R2` de 120 ohm com jumper (J7 na supervisora) e o TVS SM712. **Nao ha bias externo** nas duas placas. Alem disso o `VCC` do SN65HVD75DR e **3V3** nos dois lados, o que limita o diferencial: por isso a faixa de A-B foi corrigida para 2,0-3,3 V (um terminador) e 1,5-3,0 V (dois). | Nada a mudar. |
| 8 | Bobina do AX1RC-5V (tensao/corrente nominais, tensao minima de operacao) e capacidade da fonte chaveada e do LM2575-5. | **~36 mA de bobina**, conforme o calculo de margem: hFE >= 145 necessario. | Fecha o calculo de margem de base do BC337 e o dimensionamento do +5 V com `relay all on`. |
| 9 | Baud, formato e protocolo definitivos do link com a PUSI-DI261930. | **19200 8N1**, `EchoProtocol` (quadro do jig) como padrao e `ModbusRtuProtocol` como stub. Recomendacao: Modbus RTU, funcao 0x03/0x04, angulos X/Y como `int16` em decimos de grau. | Trocar a implementacao de `IRs485Protocol`; nada mais no jig muda. |
| 10 | Formato do numero de serie e onde e gravado. | **ASCII livre ate 23 caracteres**, digitado pelo operador (`serial <sn>`), gravado na NVS e reimpresso no relatorio e na linha CSV. Virgula e sanitizada para nao quebrar o CSV. | Se houver mascara (prefixo, digito verificador), validar no comando `serial`. |

| 11 | IO36 e IO39 estao mesmo sem conexao na placa? | **Assumido que sim.** Sao usados como MISO "de sacrificio" dos dois barramentos SPI, porque o core Arduino religa IO12/IO19 se receber `-1` (ver README). | Se algum deles estiver ligado a algo, escolher outro pino livre — nunca voltar para `-1`. |

| 12 | `LDAC` do DAC8562 esta amarrado em 0 V? | **NAO — o esquematico (folha 2/2) mostra `R15` de 10K puxando `LDAC` para +5 V**, ao contrario do que dizia o roteiro original. Por isso o `begin()` programa o registro LDAC (comando 110, `0x30 0x00 0x03`) para que os dois canais atualizem **independentemente do pino**. | Se a placa for alterada para amarrar `LDAC` em 0 V, o comando continua correto e inofensivo. |
| 13 | Pinout da PUSI-DI261930. | **RESPONDIDA pelo esquematico:** SCL3300 no VSPI (CS IO5, SCLK IO18, MISO IO19, MOSI IO23), RS-485 com RXD1 IO16 / TXD1 IO17 / DE+/RE IO13, `WDI` IO14, LED LD1 em IO2, `WDO` -> J1 -> `EN`. `kPinoutConfirmado` passou a `true`. | Nada a mudar. |

## Pendencias declaradas (TODO)

- **Display**: o controlador foi definido (SSD1322 256x64 via U8g2, base do `CDM4L-DI221651`) e o
  driver `U8g2Display` esta implementado. O env `esp32dev-ihm` ja compila com ele. O teste 4 so
  entra no `selftest` do dia a dia quando o env com `IHM_ENABLED=1` for o de producao.
- **Botoes (teste 5)**: implementados. Nivel ativo **baixo**, confirmado pela convencao do projeto
  irmao `CDM4L-DI221651` (mesma casa, mesmos pinos de UP, watchdog, LED e DE do RS-485). O
  esquematico confirma que **nao ha pull-up na supervisora**: para IO34/IO35 ele tem de vir da IHM,
  e o teste 5 reprova com essa causa escrita quando o nivel de repouso nao e alto.
- Tolerancia de diafonia entre eixos: a definir com a primeira placa boa. O jig sempre **registra o
  valor medido** no relatorio.
