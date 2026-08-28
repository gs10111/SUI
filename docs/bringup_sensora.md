# Bring-up da PUSI-DI261930 — sintomas e o que cada um significa

Registro de bancada. O ponto e nao mandar trocar componente por engano: cada sintoma abaixo
separa **firmware**, **fiacao** e **montagem**.

## Gravacao

```
pio run -d sensor -e pusi -t upload --upload-port /dev/ttyUSB0
pio device monitor -b 115200
```

**Abra o J1 antes de gravar.** Com ele fechado o STWD100 reseta o ESP32 a cada ~1,6 s e a gravacao
aborta no meio, porque nada chuta o `WDI` enquanto o bootloader roda. Feche depois.

## Ordem de conferencia no primeiro boot

1. `ver` — confere o pinout efetivamente compilado contra o esquematico
2. `whoami` — tem de dar **0xC1**
3. `status` — `RS` tem de ser `ok`
4. `angle` — os tres eixos e a temperatura

## Tabela de sintomas

| Sintoma | Codigo | O que e | O que NAO e |
|---|---|---|---|
| `WHOAMI: 0x0000` e `IO` | `Err::Io` | MISO em zero fixo: o barramento esta mudo. Peca sem alimentacao, MISO/CS sem continuidade ate o pino do chip, ou pinagem trocada. | Nao e a peca com defeito interno: uma peca ruim ainda responderia alguma coisa. |
| `WHOAMI` diferente de `0xC1`, mas nao zero | `Err::HwFault` | O SPI responde deslocado, ou nao e um SCL3300. Conferir o gap de 10 us entre quadros e o modo 0. | Nao e falta de alimentacao. |
| `CRC` nas respostas | `Err::Crc` | Quadro chega corrompido. Suspeito numero 1: **CS alto por menos de 10 us entre quadros**, que o datasheet diz corromper o dado em silencio. Depois: clock alto demais. | Nao e peca ruim. |
| `RS = 0b11` logo apos reset/troca de modo | esperado | `PWR` (bit 4) e `MODE_CHANGE` (bit 1) sobem em todo start-up normal; `STATUS` tipico e `0x0012`. Por isso o `begin()` so julga a **terceira** leitura de STATUS. | Nao e falha. Um driver que reprove aqui recusa peca saudavel. |
| `ERR_FLAG2` com bits 13/14 | `Err::HwFault` no `selftest` | Conexao de `A_EXTC` / `D_EXTC`: capacitor de 100 nF ausente ou mal soldado. Sao 4 no total (VDD, DVIO, A_EXTC, D_EXTC) e o datasheet os trata como obrigatorios. | Nao e software. |
| `STATUS` bit 6 (`SAT`) aceso | `Err::Range` | Saturacao. Enquanto estiver aceso, **todo** dado e invalido: aceleracao, inclinacao e STO. | Nao e so o eixo saturado. |
| Angulo `0.0` fixo nos tres eixos, com `RS = ok` | `Ok` | `ANG_CTRL` nao foi escrito. E a pergunta mais comum do FAQ oficial. O `reinit` refaz a sequencia inteira. | Nao e sensor parado. |
| Placa reseta sozinha a cada ~1,6 s | — | O `WDI` parou de sair do pino. Ver se alguma biblioteca de SPI reconfigurou o GPIO como entrada. | Nao e watchdog defeituoso. |

## Ferramentas de bancada no proprio console

- `spiprobe pin <n>` — alterna o GPIO a 1 Hz por 10 s. Meca **no pino do SCL3300**, nao no ESP32:
  se o do ESP32 oscila e o do chip nao, e trilha ou solda aberta.
- `spiprobe miso` — mantem CS/SCLK/MOSI compilados e varre o MISO.
- `spiprobe all` — forca bruta na fiacao inteira, procurando quem devolve `0xC1` com CRC valido.
- `spiraw <hex>` — manda um quadro cru de 32 bits e mostra as duas respostas com RS, dado e CRC.
  Lembre do pipeline off-frame: a resposta util e a **segunda**.

## Caso registrado

Primeiro bring-up da PUSI: `WHOAMI 0x0000` com `Err::Io`, pinagem conferida e correta.
**Causa: erro de montagem da placa.** O firmware estava certo. Sequencia que levou ao diagnostico:
`ver` para descartar pinout compilado, `whoami` para confirmar o zero fixo, e daí para o lado
eletrico — que e exatamente o que a primeira linha da tabela acima manda fazer.
