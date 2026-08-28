# Perguntas em aberto e premissas assumidas

Nenhuma delas foi decidida por conta propria de forma silenciosa: cada item abaixo diz **o que o
firmware assumiu para poder existir** e **o que muda quando a engenharia responder**.

| # | Pergunta | Premissa assumida no firmware | O que muda ao responder |
|---|---|---|---|
| 1 | Modelo do display do CN4 (ST7565 / ST7920 / SSD1306 / PCD8544 / outro). "DATA CLEAR" no CN4-5 nao e sinal padrao de display; provavelmente e `D/C`. | **PENDENTE.** Default de build e `DISPLAY_DRIVER_NULL` (`IHM_ENABLED=0`); `RawSpiDisplay` so faz reset + bytes crus, suficiente para provar continuidade dos 5 sinais. Teste 4 devolve `SKIP` com nota de TODO. | Escolher o driver em tempo de compilacao (`-DDISPLAY_DRIVER=...`); nao ha MISO, entao auto-deteccao e impossivel. |
| 2 | Pino `EN` (3) do STWD100: aterrado, flutuante ou em GPIO? | **Flutuante ou aterrado** → watchdog sempre habilitado. Nao existe `wdt off` por software; o comando e `wdt kick off`. | Se `EN` estiver em GPIO, cabe um `wdt off` real e o `IWatchdog` ganha um metodo `enable(bool)`. |
| 3 | Existe pull-up em `WDO` (open-drain)? Qual valor, para qual trilho? | **Assumido presente (10 k, valor recomendado pela ST).** Sem ele, `EN` do ESP32 fica indefinido. | Se nao existir, o `wdt test` falha e a mensagem de FAIL ja cita essa causa. |
| 4 | AVDD do DAC8562 e realmente +5 V? | **Sim.** Entao `VIH` = 3,5 V contra os 3,3 V do ESP32: nao conformidade de projeto. Clock SPI padrao 1 MHz e ajustavel por `dac spi <hz>`. | Se AVDD for 3V3, some o risco mas o fundo de escala do DAC cai para 2,5 V (sem ganho x2) e a calibracao muda de faixa. |
| 5 | O 0 V de saida do A0515S-2WR3 esta ligado ao 0 V do sistema? | **Assumido comum (nao isolado).** O roteiro de medicao nao promete isolacao galvanica. | Se for comum mesmo, o texto de divulgacao e o roteiro (laco de terra com a carga de 250 ohm, tensao de modo comum) precisam ser corrigidos. |
| 6 | Valores de RSET e da malha do XTR300 (R18, R14/R19 2K2, R17 10K). | **Desconhecidos de proposito.** O firmware nao presume fator de escala: `ao <eixo> <modo> <valor>` so funciona apos `cal`, calibracao de 2 pontos por eixo e por modo. | Com os valores, da para prever a escala e julgar se um desvio e componente errado ou ajuste normal. |
| 7 | Ha bias externo no par RS-485 ou o projeto depende so do failsafe interno do SN65HVD75? | **So o failsafe interno.** Criterio de `rs485 idle`: A-B proximo de 0 V com o barramento passivo. | Com bias externo, o esperado passa a ser um diferencial de polarizacao e o criterio muda. |
| 8 | Bobina do AX1RC-5V (tensao/corrente nominais, tensao minima de operacao) e capacidade da fonte chaveada e do LM2575-5. | **~36 mA de bobina**, conforme o calculo de margem: hFE >= 145 necessario. | Fecha o calculo de margem de base do BC337 e o dimensionamento do +5 V com `relay all on`. |
| 9 | Baud, formato e protocolo definitivos do link com a PUSI-DI261930. | **19200 8N1**, `EchoProtocol` (quadro do jig) como padrao e `ModbusRtuProtocol` como stub. Recomendacao: Modbus RTU, funcao 0x03/0x04, angulos X/Y como `int16` em decimos de grau. | Trocar a implementacao de `IRs485Protocol`; nada mais no jig muda. |
| 10 | Formato do numero de serie e onde e gravado. | **ASCII livre ate 23 caracteres**, digitado pelo operador (`serial <sn>`), gravado na NVS e reimpresso no relatorio e na linha CSV. Virgula e sanitizada para nao quebrar o CSV. | Se houver mascara (prefixo, digito verificador), validar no comando `serial`. |

| 11 | IO36 e IO39 estao mesmo sem conexao na placa? | **Sim.** Sao usados como MISO "de sacrificio" dos dois barramentos SPI, porque o core Arduino religa IO12/IO19 se receber `-1` (ver README). | Se algum deles estiver ligado a algo, escolher outro pino livre — nunca voltar para `-1`. |

## Pendencias declaradas (TODO)

- **Display (teste 4)** e **botoes (teste 5)** ficam como TODO por decisao de escopo. A
  infraestrutura existe e compila; os itens devolvem `SKIP` com nota, e o `selftest` segue adiante
  sem eles. Habilitar com `-DIHM_ENABLED=1` e o driver de display correto.
- Nivel ativo dos botoes assumido **ativo em baixo** (fecham para GND). IO34/IO35 sao input-only e
  `pinMode(INPUT_PULLUP)` e silenciosamente ignorado neles: sem pull-up na placa de IHM o nivel de
  repouso fica indefinido, e o teste 5 tem FAIL explicito com essa mensagem.
- Tolerancia de diafonia entre eixos: a definir com a primeira placa boa. O jig sempre **registra o
  valor medido** no relatorio.
