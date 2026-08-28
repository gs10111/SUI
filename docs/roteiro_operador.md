# Roteiro de bancada — DE-PURI-DI261924 REV A

Folha de acompanhamento do operador. O firmware guia item a item pela console (115200 8N1);
esta folha existe para os campos que o firmware nao consegue medir: leitura do multimetro e
inspecao visual dos LEDs de falha do XTR300.

Serie: ______________  Data: ____/____/______  Operador: ______________  FW: __________

Respostas na console: `p` pass · `f` fail · `s` skip · `a` abort.

## Bancada

- Fonte 100-240 VAC e fonte de bancada 24 VCC
- Multimetro (tensao DC, corrente DC, continuidade)
- Carga de 250 ohm (modo corrente)
- Placa sensora PUSI-DI261930 **ou** adaptador USB-RS485 do PC
- Jumpers: J15 (watchdog), J7 (terminador 120 ohm), J4 / J5-J6 / J13-J14 (roteamento da saida),
  J10 / J9 / J8 / J2 (contatos dos reles)

## Teste 0 — alimentacao e identidade (`test t0`)

| Ponto | Onde medir | Esperado | Medido | OK |
|---|---|---|---|---|
| +5 V (VAC) | trilho +5 V | 5,00 V +/-5 % | | |
| +3V3 (VAC) | trilho +3V3 | 3,30 V +/-5 % | | |
| +15 V (VAC) | saida A0515S-2WR3 | +15 V +/-5 % | | |
| -15 V (VAC) | saida A0515S-2WR3 | -15 V +/-5 % | | |
| +5 V (24 VCC) | trilho +5 V, entrada CN1B/CN1C | 5,00 V +/-5 % | | |
| +3V3 (24 VCC) | trilho +3V3 | 3,30 V +/-5 % | | |
| +/-15 V (24 VCC) | saida A0515S | +/-15 V +/-5 % | | |
| Polaridade invertida | entrada 24 VCC invertida | D18 bloqueia, consumo ~0 | | |
| +5 V da sensora | CN2A/CN2B, sensora ligada | 5,00 V +/-5 % | | |
| Consumo VAC | entrada | ______ mA | | |
| Consumo 24 VCC | entrada | ______ mA | | |
| Consumo pior caso | 4 reles + 20 mA + display | ______ mA | | |

FAIL em qualquer trilho reprova a placa e aborta o `selftest`.

## Teste 1 — saida analogica (`test t1`)

Confirmar antes de cada modo: **J4** (forca +5 V = corrente fixa), **J5/J6** e **J13/J14**.
Calibrar antes de usar valores de engenharia: `cal x v`, `cal y v`, `cal x i`, `cal y i`.

Eixo X: CN1L(+)/CN1M(-) · Eixo Y: CN1N(+)/CN1O(-) · Tolerancia +/-0,5 % FE

### Modo tensao (OP_MODE = 0)

| Ponto | X medido | LEDs X acesos (LD1 EFOT / LD2 EFLD / LD3 EFCM) | Y medido | LEDs Y acesos (LD4 / LD5 / LD6) |
|---|---|---|---|---|
| 0 V | | | | |
| 2,5 V | | | | |
| 5 V | | | | |
| 7,5 V | | | | |
| 10 V | | | | |

### Modo corrente (OP_MODE = 1, carga 250 ohm em serie)

| Ponto | X medido | LEDs X acesos | Y medido | LEDs Y acesos |
|---|---|---|---|---|
| 4 mA | | | | |
| 8 mA | | | | |
| 12 mA | | | | |
| 16 mA | | | | |
| 20 mA | | | | |
| Laco aberto | — | **EFLD deve acender** | — | **EFLD deve acender** |

Diafonia (registrar sempre, tolerancia a definir com a primeira placa boa):
X em FE / Y em zero → Y = ________ · Y em FE / X em zero → X = ________

`ao sweep x` e `ao sweep y`: rampa 0 → FE → 0, sem degrau nem glitch. ( ) OK

## Teste 2 — reles (`test t2`)

| Net | GPIO | Rele | Continuidade | Jumper | LED que acendeu na IHM | Esperado | OK |
|---|---|---|---|---|---|---|---|
| LIM1 | IO32 | RL5 | CN1D/CN1E | J10 | | CN3-6 ("LED LIM3") | |
| LIM2 | IO26 | RL4 | CN1F/CN1G | J9 | | CN3-8 ("LED LIM1") | |
| LIM3 | IO25 | RL3 | CN1H/CN1I | J8 | | CN3-7 ("LED LIM2") | |
| LIM4 | IO33 | RL2 | CN1J/CN1K | J2 | | CN3-5 ("LED LIM4") | |

`relay all on` → +5 V sob carga = ________ V (4 reles energizados).
`relay margin <n>` → 20 ciclos. **Um unico fecho intermitente reprova o item.**
Nota de projeto: base ~0,25 mA exige hFE >= 145 para bobina de ~36 mA. Sem margem no pior caso.

## Teste 3 — RS-485 (`test t3`)

Quantos terminadores no cabo? ( ) 1 — esperado A-B 2,0 a 3,3 V   ( ) 2 — esperado A-B 1,5 a 3,0 V
(o SN65HVD75 e alimentado com 3,3 V: nao existe diferencial acima disso)

| Condicao | A-B medido | OK |
|---|---|---|
| `rs485 drive 1` | | |
| `rs485 drive 0` | | |
| `rs485 idle` (passivo) | esperado ~0 V (failsafe interno) | |
| +5 V da sensora em CN2A/CN2B | | |

Sessao de 30 s a 19200 8N1 — PASS exige **0 erro em 500 quadros**:
frames OK ______ · timeout ______ · CRC ______ · framing ______

Executado com: ( ) placa sensora PUSI-DI261930   ( ) adaptador USB-RS485 do PC

Diferencial baixo com carga leve → suspeitar de TVS CDSOT23-SM712 em curto, J7 e +5 V da sensora.

## Teste 4 — display (`test t4`)

Requer firmware do env `esp32dev-ihm` (SSD1322 256x64 via U8g2).

| Padrao | O que precisa aparecer | OK |
|---|---|---|
| 1 | todos os pixels acesos | |
| 2 | todos os pixels apagados | |
| 3 | tabuleiro de xadrez de 8 x 8 | |
| 4 | texto de identificacao legivel | |
| 5 | contraste minimo (quase apagado) | |
| 6 | contraste maximo (brilho total) | |

## Teste 5 — botoes (`test t5`)

Requer firmware do env `esp32dev-ihm`. Botoes ativos em nivel BAIXO.

Fase 1, sem tocar em nada: o jig le o nivel de repouso dos tres. Qualquer um em nivel baixo reprova
na hora, com a causa provavel impressa (falta de pull-up na IHM em IO34/IO35, botao preso fechado,
ou curto para o 0V do CN3-4).

Fase 2: 3 pressionamentos limpos por botao, sem repique e sem acionamento cruzado.

| Botao | CN3 | GPIO | Pull-up | Presses | Repiques | OK |
|---|---|---|---|---|---|---|
| UP | CN3-1 | IO15 | interno do ESP32 | | | |
| DOWN | CN3-2 | IO34 | **tem de vir da IHM** | | | |
| MENU | CN3-3 | IO35 | **tem de vir da IHM** | | | |

## Teste 6 — watchdog (`test t6`, ultimo)

J15 fechado? ( ) sim ( ) nao. A placa **vai resetar** em ~1,6 s.
Apos o reset a placa deve subir sozinha e o item passa automaticamente no proximo `test t6`.
Se nao resetar em 3 s: verificar J15, pull-up de `WDO` (open-drain) e o pino `EN` do STWD100.

## Fechamento

`selftest` completo · `report show` (legivel) · `report csv` (imprime o bloco entre `#RESULT_BEGIN` e `#RESULT_END` para a planilha).

Veredito final: ( ) PASS ( ) FAIL — Observacoes: ______________________________________
