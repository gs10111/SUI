# DECISIONS.md — Unidade Remota DE-PURI-DI261924

Registro das decisoes de projeto do firmware de aplicacao do Supervisor de Inclinacao
SUI-DI141388XY, modulo Unidade Remota (UR-DI151399).

**Status geral: APROVADO em 2026-08-31.** O dono do produto aprovou as recomendacoes da folha
de aprovacao, sem desvio. O registro esta na secao "Registro de aprovacao" logo abaixo.

## De onde saiu cada coisa

| Fonte | Papel |
|---|---|
| `docs/manual-cliente-sui-2026.txt` | Manual do Cliente rev.2026, texto integral. Toda citacao de secao ou linha aponta para ele |
| `imagens/image (1).png`, `image (3).png` | Esquematico DE-PURI-DI261924 folha 1/2 |
| `imagens/image (2).png` | Esquematico DE-PURI-DI261924 folha 2/2, cadeia analogica |
| `imagens/image.png` | Esquematico PUSI-DI261930, placa sensora |
| `include/board_pins.h` | Pinos da UR, ja validados pelo firmware de teste de fabrica |
| `sensor/include/sensor_map.h`, `sensor/src/proto/modbus_slave.cpp` | Contrato que a sensora **ja** implementa |
| `docs/protocolo-rs485.md` | Contrato do enlace, normativo |
| `docs/portas-propostas.md` | As nove portas da arquitetura hexagonal |
| `docs/ihm-estados.md` | Maquina de estados da IHM |

## Como este documento foi produzido

Cada uma das 12 ambiguidades da secao 4 do briefing recebeu um rascunho, foi atacada por duas
revisoes adversariais independentes (lente de seguranca funcional e lente de fidelidade ao
manual) e depois reconciliada contra as duas criticas. Uma revisao de completude conferiu
cobertura de REQ e contradicao entre decisoes; uma destilacao reduziu as 99 pendencias
resultantes as escolhas que realmente sao do dono do produto; e um revisor auditou a
destilacao e devolveu nove pendencias de seguranca que ela havia perdido, todas reincorporadas.

Onde a revisao adversarial derrubou o rascunho, a decisao final registra o que caiu e por que.

## Uma decisao ja fechada, fora da folha

**A cadeia analogica e bipolar.** O trace da folha 2/2 mostra `R_OS` = R12/R25 de 1K do pino
SET ao VREF de 2,5 V do DAC8562, `R_GAIN` = R17/R29 de 10K entre RG1 e RG2, e nenhum `R_SET`
para 0 V. E a configuracao de saida bidirecional do XTR300 (SBOS336C figura 2, equacoes 2 e 3),
com trilhos de +/-15 V:

```
V_OUT = (R_GAIN / 2) * (V_DAC - V_REF) / R_OS = 5 * (V_DAC - 2,50 V)
V_OUT(D) = 25 * D / 65536 - 12,5 V
```

Zero em `D = 0x8000`; -10,00 V em `D = 6554`; +10,00 V em `D = 58982`. Isso confirma a decisao
D1 do briefing e refuta a escala de 0 a 10 V do prompt do firmware de teste. O cliente
autorizou seguir o esquematico sem medicao previa. Ja corrigido na branch
`fix/saida-analogica-bipolar`, onde `kZeroCode = 0x0000` — que nesta placa vale -12,5 V — virou
`board::kDacZeroCode = 0x8000`.

---


## Registro de aprovacao

**Data:** 2026-08-31. **Aprovado por:** Romeu (engenharia@dieletrons.com).
**Resposta:** "aprovo as recomendacoes" — todas as 15 escolhas seguem a opcao recomendada, sem desvio.

| ID | Opcao aprovada | Situacao |
|---|---|---|
| A1 | B — fail-safe: energizado = saudavel, desenergizado = alarme, falha, boot e queda de energia | **Condicionada a M2** |
| A2 | A — falha em -11,00 V (codigo 3932); modo corrente proibido, `OP_MODE` fixo em tensao | Fechada |
| A3 | B — histerese 0,3 grau, ataque 100 ms, liberacao 3000 ms, teto anti-chatter de 60000 ms apos 20 ataques em 600 s; alarme angular sem latch | **Valor da histerese condicionado a M8** |
| A4 | A — criterio estrito: amostra saturada ou fora de +/-90,0 e invalida; 3 consecutivas levam ao alarme | Fechada, com caminho de migracao para B se M10 autorizar |
| A5 | A — limite em `Off` vai a alarme na falha de enlace | Fechada |
| A6 | B ajustada — filtro fixo em 0,8 s, recarga por salto de 2,0 grau sempre ativa, quatro degraus so no comissionamento atras da senha | Fechada |
| A7 | B com rearme operacional — latch apos 5 entradas em falha em 60 s, rearme pela IHM atras da senha, sem cortar energia | Fechada |
| A8 | C — separar os registros e travar so o que comanda rele | Fechada |
| A9 | A — pacote de guardas, com a formula unica `leitura = clamp(dir * bruto + offset, -900, +900)`, `offset := P - dir * bruto` | Fechada |
| A10 | A — autorizar o pacote completo de ECO de firmware da sensora | Fechada |
| A11 | A — declarar a limitacao por escrito na folha de dados e na proposta comercial; ensaio semestral | Fechada |
| A12 | Atuacao de rele pela assistencia tecnica atras da senha do equipamento, com registro | Fechada |
| A13 | A — efetivacao unica no SAIR; bloqueio de senha de 60 s temporario e volatil | Fechada |
| A14 | A — trim com offset de 5000, verificacao em -10 V, gate de plausibilidade unico | Fechada |
| A15 | A — ECO de serigrafia mais errata; ate o ECO existir o LED nao e canal de sinalizacao valido | Fechada |

### As duas que continuam abertas, e por que isso nao trava o codigo

**A1 depende da medicao M2.** A recomendacao aprovada e o fail-safe, mas ela e explicitamente
condicional: se a fonte de 5 W nao sustentar as quatro bobinas continuas (+5 V abaixo de 4,75 V,
ou consumo acima de 4,0 W) ou o `Vce` do BC337 passar de 0,4 V a 60 C, a decisao cai para a
opcao A com a clausula de monitoramento externo escrita no manual. Aprovar a recomendacao
**nao** dispensa a medicao.

**A3 depende da medicao M8.** Os tempos estao fechados; o valor de 0,3 grau sobe para 0,6 grau
se o ruido residual pico a pico da estrutura passar de 0,1 grau.

Nenhuma das duas trava a implementacao: a polaridade e a banda entram como as constantes
`kRelayFailSafePolarity` e `kHysteresisDeciDeg` da base comum, com teste de dominio nos dois
valores. O que a medicao decide e qual constante vai no build de producao, nao a estrutura do
codigo. Enquanto M2 e M8 nao existirem, o binario de producao nao pode ser liberado.

---

# Parte 1 — Folha de aprovacao

# Folha de aprovacao — Firmware de aplicacao da UR DE-PURI-DI261924

Supervisor de Inclinacao SUI-DI141388XY. Equipamento de seguranca operacional portuario.

Quinze escolhas pedem assinatura do dono do produto. Treze tem impacto alto de seguranca; treze mudam o manual. Nove sub-itens viajam dentro das escolhas e nao precisam de decisao separada. Abaixo da secao de escolhas estao as decisoes que o time executa sem aprovacao, as dez medicoes de bancada que destravam numeros, as dezoito erratas de manual inevitaveis e as catorze frentes que ja podem comecar.

Fato ja fechado pelo cliente e fora de discussao nesta folha: a cadeia analogica e bipolar, V_OUT = 5 x (V_DAC - 2,5 V), zero no codigo 0x8000 (32768), -10,00 V em 6554 e +10,00 V em 58982.

## Tabela de decisao

| ID | Titulo | Impacto de seguranca | Recomendacao | Muda o manual |
|---|---|---|---|---|
| A1 | Polaridade dos quatro reles de limite | Alto | Fail-safe, se M2 aprovar | Sim |
| A2 | Nivel da saida analogica em falha | Alto | -11,00 V; modo corrente proibido | Sim |
| A3 | Histerese e tempos do comparador | Alto | Lento e conservador; alarme sem latch | Sim |
| A4 | Leitura saturada ou fora de +/-90,0 graus | Alto | Criterio estrito ate M10 medir | Sim |
| A5 | Reles em 'Off' durante falha de enlace | Alto | 'Off' vai a alarme | Sim |
| A6 | Filtro: menu do operador ou constante | Alto | Fixo em 0,8 s; degrau no comissionamento | Sim |
| A7 | Falha de enlace intermitente: latch | Alto | Latch com rearme pela IHM | Sim |
| A8 | Configuracao ou calibracao invalida no boot | Alto | Travar parametros, calibracao cai fabrica | Sim |
| A9 | Referencia do operador: PSET e Sentido | Alto | Pacote de guardas, formula unificada | Sim |
| A10 | ECO de firmware da placa sensora | Alto | Autorizar pacote completo | Nao |
| A11 | Ausencia de readback: ensaio e ECOs | Alto | Declarar limitacao, ensaio semestral | Sim |
| A12 | Atuacao de rele pela assistencia tecnica | Medio | Atras da senha, com registro | Nao |
| A13 | Modo Programacao: senha, commit, timeout | Alto | Efetivacao unica no SAIR | Sim |
| A14 | Assistente de Auto Calibracao | Alto | Trim 5000, verificacao em -10 V | Sim |
| A15 | Serigrafia do CN3 e LEDs do painel | Medio | ECO de serigrafia mais errata | Sim |

---

## A1 — Polaridade dos quatro reles de limite

**Por que importa.** Define se a bobina energizada significa 'saudavel' ou 'alarme', e portanto se a UR sem energia ou travada sinaliza alguma coisa para uma maquina. Hoje nenhum canal legivel por CLP denuncia a morte da UR.

**Opcoes**

- **A — Fidelidade ao manual: energizado = limite atingido.** Bobinas so puxam corrente em alarme (0 W em repouso), LED apagado em condicao normal, o texto de 5.9 e a Tabela 4 permanecem validos. Em contrapartida, queda de energia e travamento do ESP32 aparecem para o CLP como 'estrutura nivelada, nenhum limite atingido' em todos os quatro canais. Obriga clausula contratual em negrito: o intertravamento do cliente tem de monitorar a alimentacao da UR por canal externo.
- **B — Fail-safe: energizado = saudavel; desenergizado = alarme, falha, boot e queda de energia.** Unico arranjo em que a UR sinaliza a propria morte. Custa 0,72 W continuos de bobina (a margem da fonte de 5 W cai para cerca de 18 %), BC337 em conducao continua com beta forcado de aproximadamente 144 a 60 C, os quatro LEDs acesos o tempo todo em condicao normal, e reescrita de 5.9, da legenda dos LEDs e da Tabela 4. Uma UR de campo atualizada passa a indicar o oposto do que a fiacao existente espera: exige nota de release e reinspecao de instalacao.
- **C — Opcao A mais um quinto canal de saude por ECO de placa.** Resolve o problema sem consumo continuo, mas nao existe rele nem GPIO livre na revisao atual: e revisao de placa, custo de ECO e nova rodada de homologacao. Nao entrega nada nas unidades ja montadas.

**Recomendacao.** B, condicionada a medicao M2, porque e a unica opcao em que a morte da UR chega ao CLP; se a fonte nao sustentar as quatro bobinas continuas (+5 V abaixo de 4,75 V, consumo acima de 4,0 W) ou o Vce do BC337 passar de 0,4 V a 60 C, cai para A com a clausula de monitoramento externo escrita no manual.

**Destrava.** Decisao 3 (menu e commit dos limites), Decisao 5 (comparador, histerese e temporizacao), Decisao 6 (reles durante programacao e calibracao), Decisao 7 (falha de comunicacao e estado seguro), Decisao 8 (contrato de fio RS-485), Decisao 11 (faixa angular), Decisao 16 (ausencia de readback), e as constantes kRelayFailSafePolarity e kRelayBootLevel da base comum.

---

## A2 — Nivel da saida analogica em falha

**Por que importa.** E o unico numero que diz ao CLP que o angulo publicado nao vale. Zero volt esta proibido, porque e a leitura legitima mais provavel e e a assinatura fisica da queda de energia, entao a escolha e entre sair da faixa publicada ou perder a distincao entre falha e saturacao.

**Opcoes**

- **A — -11,00 V, codigo cru 3932, fora da faixa publicada.** Separacao de 1,00 V (2621 codigos) da saturacao legitima de -10,00 V, dentro da regiao linear do XTR300 e sem acionar EFLD/EFCM. Obriga errata em 2.1 L33, 6.2 L268 e Tabela 3, e obriga confirmar por instalacao que o cartao de entrada analogica do CLP tolera -11 V.
- **B — -10,00 V, dentro da faixa publicada.** Nenhuma errata de faixa e nenhum risco com o cartao do CLP, mas 'falha' passa a ser indistinguivel de '-90,0 graus saturado', e toda a sinalizacao de falha migra para o rele e o display.

**Recomendacao.** A, com kAoFaultCode como constante de compilacao, para que um cliente cujo cartao nao aceite -11 V receba um build em B sem tocar em logica.

**Sub-itens que vao junto**

- **Modo de saida do XTR300: tensao permanente ou os dois modos.**
  - *Opcao A — setMode() deixa de existir e OP_MODE fica fixo em nivel baixo (modo tensao permanente).* Existe um unico estado seguro de saida em todo o produto e o codigo de -11,00 V vale sempre. Custa perder a variante 4 a 20 mA sem novo ECO de firmware.
  - *Opcao B — manter os dois modos, com um segundo codigo de falha em corrente (3,00 mA).* O estado seguro deixa de ser unico: passam a existir dois numeros na Tabela 3, o cartao do CLP precisa de deteccao de sub-faixa, e uma troca de modo em campo muda o significado da saida sem nenhum indicio na tela.
  - *Recomendacao:* A. Enquanto o modo corrente sobreviver, o codigo de falha de -11,00 V nao significa nada naquele modo.

**Destrava.** Decisao 6 (marcadores de override da calibracao), Decisao 7 (estado seguro em falha de link), Decisao 8 (errata da Tabela 3), Decisao 9 (comportamento sem calibracao de fabrica), Decisao 10 (grampo absoluto de codigo), e kDacFaultCode/kDacBootCode da base comum.

---

## A3 — Banda de histerese e tempos do comparador

**Por que importa.** Fixa o instante exato em que cada rele ataca e libera. As decisoes 3 e 5 trazem hoje tres numeros diferentes para os mesmos tres eventos no mesmo rele, e nenhum deles esta no manual.

**Opcoes**

- **A — Rapido e estreito: histerese 0,3 grau, ataque 50 ms, liberacao 500 ms, permanencia minima 1000 ms.** Menor atraso de atuacao e menor atraso para religar o movimento. Aceita mais risco de chatter na borda: 0,3 grau precisa ser maior que o ruido residual medido, o que so as medicoes M8 e M9 dizem.
- **B — Lento e conservador: histerese 0,3 grau, ataque 100 ms, liberacao 3000 ms, sem permanencia minima separada, com teto anti-chatter que estende a liberacao para 60000 ms apos 20 ataques em 600 s.** Todo estado sinalizado dura no minimo 3 s, ou seja, 30 vezes a varredura tipica de um CLP portuario, e o balanco pendular cai de cerca de 27.000 para cerca de 1.400 comutacoes por dia. Cada evento custa 3 s de parada de producao ao religar, e o teto adaptativo pode transformar isso em 60 s.
- **C — Sem histerese, leitura literal de L142.** Fidelidade total ao manual e nenhuma errata de 5.9, mas o rele bate uma vez por ciclo de oscilacao em qualquer estrutura que balance sobre o setpoint. Inaceitavel na pratica; listado so para registrar que foi considerado.

**Recomendacao.** B, porque o teto anti-chatter so prolonga o estado de alarme e nunca o atrasa; o valor de 0,3 grau fica condicionado a medicao M8 (ruido residual pico a pico menor ou igual a 0,1 grau) e sobe para 0,6 grau se M8 reprovar.

**Sub-itens que vao junto**

- **Latch do alarme angular (diferente do latch de enlace de A7).**
  - *Opcao A — sem latch, fiel a L205-L207.* O rele acompanha a inclinacao real: um pico ja passado libera sozinho depois dos 3000 ms de liberacao. A permanencia de 3000 ms ja garante que nenhum evento passe despercebido por um CLP de 100 ms de varredura.
  - *Opcao B — latch com rearme manual por MENU, atras da senha.* Um pico de inclinacao ja normalizado mantem o intertravamento travado ate alguem ir ao painel. Custa reescrita de 5.9 e uma visita a cada evento; converte uma oscilacao normal de patio em parada.
  - *Recomendacao:* A, com contagem de eventos e registro do pico no relatorio de manutencao. O latch angular fica como variante de compilacao para o cliente que pedir por escrito.

**Destrava.** Decisao 3 (itens 25 e 26), Decisao 4 (default do filtro), Decisao 5 (itens 5 e 7), Decisao 16 (contador de vida do contato).

---

## A4 — Leitura saturada ou fora de +/-90,0 graus

**Por que importa.** Para a mesma amostra de 92,0 graus, as decisoes 7, 8 e 11 mandam o equipamento fazer tres coisas incompativeis: falhar em 150 ms com os quatro reles em alarme, congelar os reles por 500 ms, ou nem declarar falha. Isso muda o instante de atuacao e o que aparece no display.

**Opcoes**

- **A — Estrito: qualquer amostra saturada ou com |angulo| maior que 90,0 e invalida; 3 invalidas consecutivas (150 ms) levam os quatro reles ao estado de alarme.** Comportamento mais conservador e um unico criterio em todo o projeto. Cada impacto de carga que sature o SCL3300 produz um evento de alarme completo; se as rajadas forem frequentes, o equipamento fica cronicamente indisponivel e sera ponteado em campo.
- **B — Tres bandas: 0 a 90,0 normal; 90,1 a 95,0 banda de tolerancia indicada como 90,0 sem falha; acima de 95,0 por 1000 ms continuos e falha mecanica; rajada de SATURADO congela o rele no ultimo estado por ate 500 ms.** Tolera o impacto de carga sem derrubar a supervisao, ao custo de uma excecao declarada a idade maxima de 72 ms da base comum (o rele pode ficar 500 ms sem comando novo) e de duas telas novas mais um paragrafo em 6.1 publicando a banda.
- **C — Retencao longa: mantem o ultimo angulo valido por ate 1000 ms antes de declarar falha.** Idade maxima declarada de 1072 ms para um dado que comanda rele de seguranca. Nao ha argumento tecnico que sustente isso num supervisor portuario.

**Recomendacao.** A. Num supervisor de seguranca, o default enquanto nao ha dado tem de ser o conservador, e folga so se compra com medicao na mao: se M10 mostrar menos de 12 rajadas de SATURADO por hora e nenhuma acima de 1000 ms, a folha migra para B com o congelamento de 500 ms escrito no manual como excecao explicita. Sem M10, B declararia um rele de seguranca comandado por dado velho com base em uma suposicao que ninguem mediu.

**Destrava.** Decisao 3 (guarda dura de idade de 250 ms), Decisao 5 (criterio de amostra valida), Decisao 7 (estado INSTAVEL), Decisao 8 (item 7.7), Decisao 11 (itens 6 e 7), kDataMaxAgeMs da base comum.

---

## A5 — Reles programados em 'Off' durante falha de enlace

**Por que importa.** Decide se um canal de intertravamento que o cliente deixou desativado fica silencioso quando a UR fica cega. O manual (L204) diz que 'Off' permanece em repouso; as decisoes 3, 5, 7 e 8 querem desviar disso.

**Opcoes**

- **A — 'Off' vai ao estado de alarme junto com os outros tres.** Todo canal cabeado recebe a indicacao de que a UR nao esta saudavel, inclusive os desativados. Desvio declarado de 5.9 L204, com nota nova no manual. Sob a polaridade fail-safe, 'Off' passa a significar bobina permanentemente energizada em operacao normal, o que contradiz a letra de L204 duas vezes.
- **B — 'Off' permanece em repouso, fiel a L204.** Zero errata neste ponto. Um canal deixado em Off fica indistinguivel entre 'desligado por escolha' e 'UR cega', que e exatamente o modo de falha silencioso que o produto existe para eliminar.

**Recomendacao.** A, porque 'Off' significa 'sem criterio angular' e nao 'canal desligado'; falha de enlace nao e angulo.

**Destrava.** Decisao 3 (pendencia dos reles em Off), Decisao 5 (itens 9 e 10), Decisao 7 (desvio de L204), Decisao 8 (item de falha).

---

## A6 — Filtro: parametro de menu ou constante fixa

**Por que importa.** Muda o atraso de atuacao publicado (de 0,56 s a 3,56 s conforme o ajuste), muda a Tabela 1, a Tabela 2 e o menu, e decide se o cliente pode afrouxar em campo o tempo de resposta de um equipamento de seguranca.

**Opcoes**

- **A — Parametro de menu 'Filtro' com 4 degraus (0,2 / 0,8 / 1,6 / 3,2 s), default 0,8 s.** Cumpre a promessa escrita em L71 e L272. Custa tela nova 'Filtro(s):0,8', linha nova na Tabela 1 e na Tabela 2, coluna nova de latencia por ajuste, e permite que o operador leve o atraso publicado a 3,56 s. Contradiz L45, que vende o produto como dispensando parametros.
- **B — Fixo em 0,2 s (tau de 198,5 ms), ajustavel so por comando de comissionamento.** Nenhum parametro novo no painel, na Tabela 1 ou na etiqueta. Exige errata de L71 declarando que o ajuste e de comissionamento. Com 0,2 s, um balanco de +/-0,5 grau a 1 Hz chega ao comparador com 0,68 grau pico a pico, maior que qualquer histerese em discussao, e o rele bate uma vez por ciclo: o cliente com estrutura oscilante fica sem alternativa alem de desativar o limite.
- **C — Parametro de menu do operador mais recarga por salto de 2,0 grau escrita em 5.9.** Igual a A, mais a garantia publicada de que qualquer excursao de 2,0 grau ou mais atua o rele em 0,43 s em qualquer ajuste. Custa um paragrafo em 5.9 declarando a nao linearidade, e continua entregando ao operador o botao que afrouxa o tempo de resposta.

**Recomendacao.** B com dois ajustes: constante default fixada em 0,8 s (nao em 0,2 s, que produz chatter garantido em estrutura oscilante) e recarga por salto de 2,0 grau sempre ativa. Os quatro degraus (0,2 / 0,8 / 1,6 / 3,2 s) existem, mas atras da senha do equipamento, no fluxo de comissionamento, nunca no menu do operador. Texto impresso em L71 nao e razao para construir um ajuste de campo que leva a 3,56 s o tempo de resposta de um equipamento de seguranca, ainda mais com L45 vendendo o produto como isento de parametros; e as outras 18 erratas ja provam que o manual e o que se corrige.

**Pergunta que fica para o dono.** Qual teto de tempo de resposta a folha de dados publica. Os quatro numeros possiveis sao 0,56 s (filtro 0,2 s), 1,16 s (0,8 s), 1,96 s (1,6 s) e 3,56 s (3,2 s), sempre com o piso de 0,43 s garantido pela recarga por salto de 2,0 grau. A recomendacao publica **1,2 s**, coerente com o default de 0,8 s; qualquer comissionamento acima de 0,8 s obriga a reemitir a folha de dados da unidade.

**Destrava.** Decisao 3 (fechamento das 16 folhas da Tabela 1), Decisao 4 (itens 6, 8 e 12), Decisao 5 (item 12), Decisao 2 (campo do ParamRecord).

---

## A7 — Falha de enlace intermitente: trava ou recupera sozinha

**Por que importa.** Decide se uma UR que entra e sai de falha varias vezes por minuto continua religando os reles sozinha ou trava em alarme ate um humano agir. Sob a polaridade fail-safe, latch travado significa maquina parada.

**Opcoes**

- **A — Sem latch: recuperacao sempre automatica (5 transacoes boas mais 2000 ms de permanencia minima).** Nunca exige visita por um cabo com mau contato. Em compensacao, um enlace degradado produz ciclos repetidos de alarme e liberacao, e o operador ve o painel piscando entre normal e falha sem que nada registre a gravidade.
- **B — Latch por flapping com rearme so por ciclo de energia ou comando de console 'link clear'.** Um enlace ruim para de mascarar a propria intermitencia, mas a unica saida em campo passa a ser cortar a alimentacao de um supervisor de seguranca de portico, gesto que o cliente pode nao ter direito de fazer, e que A8 e A13 tambem exigiriam.
- **C — Sem latch, com contador de enlace degradado exposto no console e no relatorio.** Meio-termo sem custo operacional, mas e a opcao sem protecao nenhuma: nao muda o comportamento dos reles e depende de alguem ler o console.

**Recomendacao.** B com rearme operacional pela IHM: 5 entradas em falha dentro de 60 s travam o estado de falha, a segunda linha mostra 'FALHA TRAVADA - REARMAR NO MENU', e o rearme e feito no painel atras da senha do equipamento (a mesma de A13), sem cortar energia e sem depender de comando de console. Liberar um latch nao e atuar rele, entao esse rearme sobrevive a retirada dos comandos de atuacao tratada nas decisoes de engenharia; e um latch sem saida operacional e a receita do ponteamento em campo que A11 descreve.

**Sub-itens que vao junto**

- **Quais estados de falha existem e o que aparece na tela.**
  - *Opcao A — quatro estados distintos, em duas linhas: 'AGUARDANDO SENSOR', 'FALHA DE COMUNICACAO', 'FALHA DO SENSOR' e 'MEDICAO INSTAVEL'.* O tecnico distingue cabo de sensor antes de subir na estrutura. Custa quatro telas publicadas byte a byte na secao 7 e nada em silicio.
  - *Opcao B — duas linhas com um unico estado agregado de falha.* Menos texto no manual; o tecnico de campo passa a trocar sensora para descobrir que o problema era o cabo.
  - *Opcao C — uma unica linha 'FALHA'.* Menor custo documental e nenhuma informacao de diagnostico para quem esta no cais.
  - *Recomendacao:* A, com os quatro textos sem acentuacao, incluindo 'AGUARDANDO SENSOR' cobrindo a janela entre o boot e a primeira transacao valida.

**Destrava.** Decisao 7 (item 15), Decisao 8 (item 8, balde +3/-1 com 3 consecutivas), Decisao 15 (comandos de console no produto), criterio unico de falha da base comum.

---

## A8 — Configuracao ou calibracao invalida na energizacao

**Por que importa.** Decide se um CRC reprovado na NVS deixa o equipamento operando com valores de fabrica ou o trava em alarme. E o unico caminho em que o firmware pode, sozinho, trocar os quatro setpoints de seguranca de um cliente.

**Opcoes**

- **A — Falha latchada: quatro reles no nivel de alarme, saida analogica no codigo de falha, tela travada em 'CONFIG PERDIDA - REPROGRAMAR'.** Nenhuma reprogramacao silenciosa: o equipamento nunca opera com setpoints que o cliente nao programou e a senha nunca volta a 1234 sozinha. Custa indisponibilidade total ate um tecnico ir ao painel e quatro telas novas no manual.
- **B — Carregar a Tabela 2 automaticamente, gravar e operar, reportando no console.** Equipamento sempre disponivel. Perigoso por construcao: a Tabela 2 entrega Limite 2 e Limite 4 com Operacao 'Off', ou seja, dois dos quatro reles de seguranca ficam desativados sem que nada na tela principal diga isso.
- **C — Falha latchada para os parametros, mas calibracao analogica ausente cai no par de fabrica e opera.** Distingue os dois registros: setpoint de rele nunca e inventado, mas uma calibracao analogica perdida so degrada a precisao da saida, que continua util. Exige que calibracao e parametros vivam em registros NVS separados, contra a Decisao 2, que os poe no mesmo bloco de 52 bytes.

**Recomendacao.** C: separar os registros e travar so o que comanda rele. B esta descartada porque desativa dois reles em silencio.

**Sub-itens que vao junto**

- **O que o Reset Geral faz.** E o gesto de tecla na energizacao para o qual A7, A8 e A13 apontam como saida, e ninguem assinou o que ele repoe.
  - *Opcao A — repoe a Tabela 2 inteira (Limite 2 e Limite 4 com Operacao 'Off') e apaga a calibracao analogica de campo.* Um gesto de tecla desativa em silencio dois dos quatro reles de seguranca e joga fora a calibracao feita em campo.
  - *Opcao B — repoe senha (1234), preset e parametros com os QUATRO limites em Operacao 'Off', tela principal piscando 'LIMITES DESATIVADOS - PROGRAMAR', e PRESERVA a calibracao analogica.* Nenhuma desativacao silenciosa: o estado desativado e visivel e ruidoso, e a calibracao de dois pontos sobrevive. Custa reescrever a Tabela 2 e a secao 5.11.
  - *Opcao C — repoe so senha e preset, mantendo limites e calibracao.* Deixa de ser saida util para 'CONFIG PERDIDA', que e justamente o caso em que os limites estao corrompidos.
  - *Recomendacao:* B.
- **Como se sai do estado 'CONFIG PERDIDA'.**
  - *Opcao A — so pelo Reset Geral.* O tecnico perde os quatro setpoints e reprograma tudo do zero, mesmo sabendo os valores corretos.
  - *Opcao B — aceita tambem entrar em Modo Programacao com a senha de fabrica 1234 enquanto durar o estado, gravando par a par.* Reprograma no painel sem apagar nada, com o estado de alarme mantido ate os quatro pares terem sido gravados e a janela de senha de fabrica registrada no console.
  - *Recomendacao:* B, mantendo os quatro reles em alarme durante toda a reprogramacao.

**Destrava.** Decisao 2 (itens 2, 4, 10 e 11), Decisao 6 (item 17), Decisao 9 (itens 21 a 23), Decisao 13 (senha invalida na NVS), Decisao 1 (pendencias 8 e 9).

---

## A9 — Referencia do operador: PSET e Sentido do Sensor

**Por que importa.** Sao os dois gestos que deslocam os quatro pontos de atuacao sem passar por senha. Hoje a formula aparece com sinais opostos em duas decisoes, e inverter o Sentido com um preset gravado desloca a atuacao em ate 180,0 graus sem nenhum indicio na tela.

**Opcoes**

- **A — Pacote com guardas.** PSET armado por passagem no menu com validade de 120 s, exige dado valido e estabilidade, aplica os dois eixos de uma vez, confirmacao por hold de MENU de 3 s quando o deslocamento passa de 5,0 graus, indicador permanente 'PSET X:-012,0' na tela principal, e a troca de Sentido zera o offset do eixo com aviso obrigatorio de 3 s. Nenhum deslocamento de referencia acontece em silencio, e o sumico do indicador e a prova visivel de que o offset foi zerado. Custa desvio de L161, paragrafo novo em 5.6 e 5.8 e duas telas novas.
- **B — Fidelidade literal ao manual.** Duplo toque aplica no ato, sem armamento e sem confirmacao; a troca de Sentido apenas 'recomenda' reconferir, como diz L199. Zero errata, e um toque duplo acidental no painel de um cais move os quatro pontos de atuacao com o display mostrando numeros plausiveis.
- **C — Pacote de guardas, mas a troca de Sentido forca 'Off' nos dois limites do eixo ate reconfirmacao.** Impede a atuacao deslocada e desativa dois dos quatro reles de seguranca por causa de uma mudanca de convencao de sinal. Troca um perigo por outro maior.

**Recomendacao.** A, ratificando junto a formula unica leitura = clamp(dir x bruto + offset, -900, +900), com offset := P - dir x bruto, que corrige a Decisao 11, onde o sinal esta subtraindo em vez de somar.

**Destrava.** Decisao 1 (itens 6 a 20), Decisao 2 (item 9), Decisao 11 (item 8), Decisao 14 (sentido do sensor), Decisao 3 (pendencia 10).

---

## A10 — Autorizar o ECO de firmware da placa sensora

**Por que importa.** Sem media movel na sensora e com o SCL3300 em MODE 4 nao existe anti-aliasing na cadeia, e o filtro prometido em L71 e cosmetico: a UR amostra a 20 Hz um sinal com conteudo ate 40 Hz. Junto vem a correcao do ramo de falha, que hoje publica DATA_VALID sobre angulo velho.

**Opcoes**

- **A — Autorizar o pacote completo:** MODE 4 (passa-baixa de 10 Hz), media movel publicada nos registradores 0, 1 e 2, registrador 7 como heartbeat, boot direto em Modbus e correcao de sensor/src/main.cpp:151-158. O contrato de fio e preservado byte a byte (FC03, 8 registradores, escravo so-leitura). Custa revalidacao completa da sensora ja homologada, atualizacao da secao 6 de docs/protocolo-rs485.md e uma versao minima aceita pela UR. Sem o boot em Modbus, a sensora nasce no quadro do jig e ignora todo pedido apos qualquer reset.
- **B — Nao autorizar; a sensora permanece como esta.** Zero revalidacao. O produto sai sem anti-aliasing, com vibracao de motor dobrada para dentro da banda base com amplitude arbitraria e sem assinatura no display: alarme falso e alarme perdido ao mesmo tempo. O filtro de L71 nao pode ser publicado como filtro.
- **C — Autorizar so a correcao do ramo de falha e o boot em Modbus, adiando MODE 4 e a media movel.** Corrige o defeito que faz a sensora mentir DATA_VALID e o que a impede de responder apos reset, com revalidacao menor. Deixa o aliasing em aberto e obriga uma segunda rodada de revalidacao depois.

**Recomendacao.** A: sem MODE 4 e media movel, o filtro publicado da UR filtra um sinal que ja chegou corrompido pela decimacao, e nenhum numero de A3 ou A6 tem significado.

**Sub-itens que vao junto**

- **O que a UR faz diante de uma sensora sem o ECO.**
  - *Opcao A — recusar: firmware anterior a 0.2.0 gera falha permanente 'SENSORA DESATUALIZADA' ate a atualizacao.* Nenhuma UR opera sem deteccao de congelamento do sensor; uma sensora antiga em campo para a maquina ate a troca de firmware.
  - *Opcao B — aceitar e operar sem deteccao de congelamento, sinalizando so no console.* A UR publica angulo que pode estar congelado, que e exatamente o modo de falha que o registrador 7 existe para cobrir, e ninguem no cais ve o aviso.
  - *Recomendacao:* A, com a semantica do registrador 7 fixada em um unico criterio: contador de 16 bits incrementado a cada 10 ms, rollover a cada 655,36 s, e sensor declarado congelado quando 3 leituras consecutivas espacadas de 50 ms trazem o mesmo valor (150 ms). O criterio antigo por comparacao de angulo da Decisao 4 e eliminado, porque dispararia sozinho a cada 655,36 s.

**Destrava.** Decisao 4 (estagio 1 da cadeia), Decisao 5 (item 1), Decisao 8 (itens 5, 7 e 17), Decisao 11 (MODE 4 e reinit do SCL3300), Decisao 7 (criterio de aceitacao da amostra).

---

## A11 — Ausencia de readback: ensaio periodico, declaracao e fila de ECO

**Por que importa.** Nao existe um unico sinal de volta em todo o caminho de atuacao: contato colado, bobina aberta, BC337 em curto, cabo analogico rompido e XTR300 em protecao termica sao indetectaveis. O firmware nem pode saber que nao sabe.

**Opcoes**

- **A — Declarar a limitacao em secao propria do manual (6.4), ensaio funcional obrigatorio a cada 6 meses e fila de ECO priorizada.** O intervalo do ensaio e a janela de falha latente, e 6 meses e a menor janela que o cliente aceita operar. Custa secao nova com a lista fechada do que a UR nao detecta, clausula de que o intertravamento tem de usar os dois canais (rele e analogica com deteccao de fora de faixa), procedimento no relatorio de manutencao, e as ECOs A (readback dos quatro contatos auxiliares somados numa entrada ADC em IO36/IO39), B (EFLD/EFCM do XTR300 para GPIO), C (CLR# do DAC8562 no reset do sistema) e D (adequacao de nivel logico 3,3 V para 5 V no SPI do DAC8562).
- **B — Igual a A, com ensaio a cada 12 meses.** Metade do custo de manutencao para o cliente e o dobro da janela em que o painel pode afirmar que um limite esta armado com o contato colado.
- **C — Sem ensaio periodico no manual, apenas a declaracao de limitacao.** Nenhum custo recorrente e nenhuma promessa que o cliente possa descumprir. Deixa o produto sem nenhum mecanismo, automatico ou manual, que detecte falha latente no caminho de atuacao.

**Recomendacao.** A, e declarar por escrito, na folha de dados e na proposta comercial, que enquanto ECO-A e ECO-B nao existirem o canal de rele e de canal unico, sem diagnostico e sem teste automatico, e o produto nao reivindica categoria nem nivel de desempenho de seguranca.

**Destrava.** Decisao 5 (item 14), Decisao 7 (pendencia 5), Decisao 10 (ECOs de diagnostico), Decisao 16 (ausencia de readback), Decisao 15 (o ensaio nao pode depender de comando de atuacao no console).

---

## A12 — Atuacao de rele pela assistencia tecnica

**Pergunta unica:** a assistencia tecnica precisa atuar rele em campo? Se sim, a resposta recomendada e uma so: atras da senha do equipamento, com carimbo de tempo no console e registro no relatorio de manutencao, nunca aberta por cabo USB como hoje. Todo o resto do escopo do binario de producao (Wi-Fi, comandos 'relay', 'ao raw', 'test', 'sim') e higiene de engenharia e esta na secao seguinte.

**Destrava.** Decisao 15 (escopo de software), Decisao 5 (item 14), Decisao 7 (rearme do latch de enlace), Decisao 16 (procedimento do ensaio periodico).

---

## A13 — Modo Programacao: senha, momento da efetivacao e timeout

**Por que importa.** Define o unico instante em que um valor novo passa a comandar rele, e se um erro de digitacao no painel de um cais custa 60 segundos ou uma visita de manutencao.

**Opcoes**

- **A — Efetivacao em instante unico no SAIR**, com tela de revisao 'NOVA CONFIG - CONFIRMA?' e hold de 3 s; timeout de 120 s deixa a edicao pendente e a tela principal pisca 'CONFIG PENDENTE - REVISAR'; login com 5 tentativas e bloqueio temporario de 60 s. Nunca existe valor novo com operacao velha comandando rele, e nenhuma edicao e perdida em silencio. Custa cinco telas novas, um paragrafo em 5.3 sobre o bloqueio e errata de L134, L136 e L137.
- **B — Efetivacao por parametro no hold de 3 s**, timeout de 120 s descarta a edicao com 'Alteracao descartada!', tentativas de senha infinitas conforme L103. Mais proximo da letra do manual e sem telas de revisao. Cria a janela em que o Valor Limite ja foi gravado e a Operacao ainda nao, com o par comandando rele em estado hibrido, e permite tentativa infinita de senha num painel fisicamente acessivel.
- **C — Efetivacao do par inteiro na saida do submenu 'Limite N>'**, sem tela de revisao, com descarte no timeout. Elimina o par hibrido sem inventar tela de revisao, mas deixa cada par sendo efetivado em momento diferente e nao resolve nada para os parametros fora dos limites.

**Recomendacao.** A, com o bloqueio de 60 s explicitamente temporario e volatil: bloqueio permanente transformaria erro de digitacao em visita de manutencao, e a unica saida seria o Reset Geral.

**Sub-itens que vao junto**

- **Digito das centenas do Valor Limite e regra unica de cursor.**
  - *Opcao A — digito das centenas exibido e nao editavel.* O campo nunca alcanca valor invalido, ao custo de uma tela que se comporta diferente de todas as outras, com um digito inerte sob o cursor.
  - *Opcao B — editavel com clamp silencioso na confirmacao.* O tecnico digita 1200 decimos e o equipamento grava 900 sem dizer nada, num setpoint de rele de seguranca.
  - *Opcao C — editavel com recusa explicita:* pisca 'FORA DA FAIXA +/-090,0' por 2000 ms, nao grava e devolve o cursor ao digito.
  - *Recomendacao:* C. Junto entra a regra unica de cursor, valida para senha, valor limite, preset e trim: abre no digito mais a direita, MENU move para a esquerda, rolagem circular dentro do campo.

**Destrava.** Decisao 2 (itens 1, 5 e 8), Decisao 3 (itens 14, 17 a 21 e pendencia 10), Decisao 6 (tetos de permanencia), Decisao 13 (senha, gate de acesso e pendencia 3).

---

## A14 — Assistente de Auto Calibracao: trim, verificacao em -10 V e aviso de saida simulada

**Por que importa.** Durante o assistente a saida analogica deixa de refletir o sensor e passa a ser simulada, com os quatro reles ainda operando sobre o angulo real. As tres decisoes que tocam no campo de 4 digitos escrevem a mesma figura do manual de tres jeitos diferentes.

**Opcoes**

- **A — Trim com neutro em 5000 (faixa -5000 a +4999 LSB)**, telas 'Ajuste 0Vcc:5000' e 'Ajuste 10Vcc:5000' abrindo no valor corrente; passo obrigatorio de verificacao em -10 V com portao de 10,0 mV; aviso 'SAIDA SIMULADA / Bloqueie o CLP' confirmado por hold de 3 s na entrada; gate que recusa o assistente com qualquer limite sinalizado. O tecnico nunca calibra as cegas o ramo negativo e o CLP nunca recebe tensao simulada sem aviso. Custa errata das duas figuras de 5.7 (L172 e L180), quatro telas novas e nota em 5.7 de que os reles seguem o angulo real durante todo o assistente.
- **B — Quinta posicao de sinal no campo ('Ajuste 0Vcc:+0000'), sem passo de verificacao em -10 V e sem tela de aviso.** Mantem a figura abrindo em 0000 com errata menor. Deixa o ramo negativo sem verificacao e a saida simulada sem anuncio, contando com o integrador para bloquear o laco no CLP por conta propria.
- **C — Manter 4 digitos literais com complemento de dez mil (9999 igual a -1).** Nenhuma errata nas figuras e um '9987' na tela de um tecnico no meio de uma calibracao de seguranca. Descartada por legibilidade.

**Recomendacao.** A, com o gate de plausibilidade do commit unificado em um unico criterio, porque hoje as decisoes 6, 9 e 10 usam tres, e uma calibracao legitima aprovada por um e recusada por outro.

**Sub-itens que vao junto**

- **O que acontece com o buffer do assistente no timeout de 120 s.**
  - *Opcao A — o buffer NAO e gravado por timeout nem por qualquer saida que nao seja o passo final de confirmacao; a calibracao anterior permanece integra.* Desvio declarado da letra de L136, escrito em 5.7.
  - *Opcao B — gravar o que houver, conforme L136.* Grava meio par (zero novo com ganho velho), produzindo saida analogica plausivel e errada, sem nenhum indicio na tela ou no relatorio.
  - *Recomendacao:* A. Meio par de calibracao e pior que nenhuma calibracao, porque nao tem assinatura observavel.

**Destrava.** Decisao 6 (itens 3, 6, 7, 9, 12 e 13), Decisao 9 (itens 2, 3, 10 e 11), Decisao 10 (itens 5 a 10), Decisao 2 (onde vive a calibracao na NVS).

---

## A15 — Serigrafia cruzada do CN3 e mapeamento dos LEDs do painel

**Por que importa.** O LIM1 acende o LED serigrafado 'LED LIM3', o LIM2 acende o 'LED LIM1' e o LIM3 acende o 'LED LIM2'. Nao ha correcao possivel em firmware: o LED e a base do BC337 compartilham o mesmo net.

**Opcoes**

- **A — ECO de serigrafia (ou de fiacao do CN3) na placa frontal para a producao nova, mais errata documentando o cruzamento para o parque instalado.** Producao nova sai correta e as unidades entregues ficam com o cruzamento documentado. Custa ECO de painel, novo desenho de serigrafia e errata em 5.9 L202 e na Tabela 4.
- **B — So errata de manual, mantendo o cruzamento como caracteristica documentada.** Custo zero de hardware. Todo operador e todo tecnico de campo passa a depender de uma nota de manual para saber qual limite disparou, para sempre.
- **C — Remapear os quatro GPIOs em firmware para casar com a serigrafia.** Impossivel: o LED pendura no mesmo net do GPIO antes do resistor de base, entao trocar o mapa de LED troca junto o rele que atua. Listado para fechar a duvida.

**Recomendacao.** A, e declarar formalmente que, ate o ECO existir, o LED nao e canal de sinalizacao valido, apenas confirmacao de que o GPIO subiu.

**Sub-itens que vao junto**

- **LED LIG pulsado ou continuo.**
  - *Opcao A — pulsado 900 ms aceso / 100 ms apagado, gerado pela mesma ISR que chuta o watchdog e travado pelo token de liveness de 800 ms.* Quando o firmware trava, o LED para de piscar: e o unico canal que denuncia travamento a um humano no local, o que importa diretamente se A1 ficar na opcao A, que empurra a deteccao de morte da UR para fora do equipamento. Custa uma linha na legenda do manual.
  - *Opcao B — continuo.* Mais simples e mente no travamento: LED aceso com firmware parado e display congelado com imagem valida.
  - *Recomendacao:* A.

**Destrava.** Decisao 3 (pendencia da serigrafia), Decisao 6 (item 4), Decisao 12 (LED LIG, mapeamento e pendencia 4), Decisao 16 (LED como canal de diagnostico).

---

## Decisoes de engenharia (nao precisam de aprovacao)

Executadas pelo time, sem consumir tempo do dono do produto. Ficam registradas aqui so para constar no DECISIONS.md.

1. **src/net fora do build de producao** por build_src_filter. Nenhum REQ depende dele, o manual nao tem uma linha sobre interface sem fio e nao ha antena nem terminal. O ganho e o pior caso do ciclo de seguranca voltar a ser propriedade verificavel: nenhuma tarefa de prioridade 22 ou 23 no core 0.
2. **Comandos de atuacao ('relay', 'ao raw', 'ao mode', 'test', 'cal erase', 'sim', 'wifi') somente em env:factory**, com marcacao BUILD=FACTORY no banner e sufixo -f na versao. O console de diagnostico a 115200 permanece no produto, em nivel de leitura.
3. **Verificacao de release por nm sobre o .elf**, procurando simbolos proibidos. E o que torna o item 1 demonstravel por inspecao do binario, e nao por promessa.
4. **O ensaio funcional periodico de A11 passa a ser feito pelo Modo Programacao**, nao por 'relay on/off'. Corrige a redacao das Decisoes 5 e 7.
5. **Janela da media movel da sensora fixada em 10 amostras a 100 Hz.** E escolha de filtro digital, decidida pela decimacao de 100 Hz para 20 Hz: a janela de 10 poe nulos exatos em todos os multiplos de 10 Hz, que sao justamente as frequencias que aliasam. Fica amarrada a medicao M8; ao dono cabe apenas autorizar ou nao a revalidacao da sensora, o que e a escolha A10.

---

## Medicoes de bancada

| ID | O que mede | Como (resumo e aceitacao) | Fica travado sem ela |
|---|---|---|---|
| M1 | Cadeia analogica completa: viabilidade do nivel de falha, swing, linearidade, acoplamento cruzado, faixa de trim e integridade do SPI | Placa energizada, 60 s de estabilizacao, `ao mode v`, DMM de 5,5 digitos em CN1L/CN1M e CN1N/CN1O. `ao raw x 3932` (esperado -11,00 +/-0,05 V com EFOT/EFLD/EFCM apagados), `ao raw x 0` e `ao raw x 65535` para o swing (cerca de +/-12 V), trilhos do A0515S-2WR3 em C10/C13 e C25/C26 entre 14,0 e 16,0 V em modulo. Onze pontos de -10 a +10 V de 2 em 2 apos calibracao de dois pontos. Acoplamento: V(32768) de um eixo com o outro em 6554, 32768 e 58982, aceitacao 5 mV. Trim Tz e Tg em 3 placas e 2 eixos. Integridade SPI: 10.000 alternancias 6554/58982 a 100 kHz, 1 MHz e 10 MHz, a 25 C e 60 C com a caixa fechada, procurando patamares intermediarios (VIH do DAC8562 exige 3,5 V contra 3,3 V do ESP32). Osciloscopio com persistencia infinita em 20 cortes de AC e 20 energizacoes. Antes de rodar, reescrever src/tests/test_07_rset.cpp para o modelo bipolar | A2 (nivel de -11,00 V), A14 (faixa do trim e portao de -10 V), publicacao da Tabela 3, prioridade da ECO-D |
| M2 | Bobinas, transistor de acionamento e consumo no pior caso: decide se a polaridade fail-safe e fisicamente possivel | `test 8` para resistencia de bobina, trilho 3V3 e corrente de base; corrente por bobina com amperimetro em serie; +5 V com `relay all on` e o sensor puxando 200 mA (carga de 25 ohm em CN1 terminais 4/5); Vce dos quatro BC337 apos 4 h de soak a 60 C com caixa fechada, com termopar tipo K ou camera IR; wattimetro TRMS em CN2 terminais 2/3 a 100 e 240 Vca nas condicoes repouso, `relay all on`, `disp pattern 0` e todas simultaneas. Aceitacao: +5 V maior ou igual a 4,75 V, hFE exigido menor ou igual a 150, Vce menor ou igual a 0,4 V, elevacao menor ou igual a 25 C, consumo menor ou igual a 4,0 W sem Wi-Fi | A1 inteira. Reprovando, o fail-safe e impossivel e a decisao cai para fidelidade ao manual com monitoramento externo |
| M3 | Watchdog externo e escrita de NVS: lacuna de WDI no boot e em regime, tempo de commit e coerencia sob corte de energia | Osciloscopio de 2 canais com persistencia infinita: canal 1 no RST# do STWD100 (pino 1 do CI4, junto ao J15), canal 2 no IO19. Vinte boots, incluindo um com particao NVS virgem, um apos `wdt test`, um com painel conectado e tres com Reset Geral (tecla solta em 3100 ms, em 1500 ms e presa 15000 ms). Cem escritas que forcem apagamento de setor (`cal erase`, `cal x v`, `serial <sn>`, `date <texto>`), rodadas com o chute atual em esp_timer e com o chute por ISR de timer de hardware em IRAM. Duzentos cortes AC assincronos dentro da janela de commit; apagamentos de setor por 5.000 gravacoes. Aceitacao: primeiro pulso abaixo de 500 ms apos o reset, nenhuma lacuna acima de 500 ms no boot, ate 250 ms em regime com IRAM, commit ate 500 ms em 100 de 100, 200 de 200 religacoes coerentes | A8, A13, a ordem de boot inteira e o criterio de 3 transacoes / 150 ms ate declarar falha. Espera-se que a versao em esp_timer reprove: e essa reprovacao que justifica a troca de mecanismo |
| M4 | Enlace RS-485 com o cabo real de 500 m: round-trip, enquadramento no ambiente portuario, alimentacao remota e regressao do escravo apos o ECO | UR e sensora com o cabo real e terminador de 120 ohm em J7 nas duas pontas. Osciloscopio de 2 canais no DE de cada placa (IO14) ou no par A/B. Sobre 10.000 transacoes, medir do ultimo byte do pedido ao primeiro da resposta (previsto 2,05 a 4,55 ms) e o total pedido-resposta (17,9 ms tipico, 21,3 ms pior caso), com `rs485 ping <hex>`, `rs485 stats`, `rs485 sniff` e `status` na sensora. Uma hora de barramento ocioso com o cabo junto da fiacao de potencia, contando bytes espurios. Amperimetro em serie em CN1-4 em repouso e no pico da rajada SPI, e tensao no sensor. Vinte energizacoes apos o ECO 0.2.0; 1000 ciclos com GPIO instrumentado para o custo da media movel. Aceitacao: total ate 25 ms, zero timeouts a 35 ms em 1 h, zero erros de enquadramento, laco ate 1,25 ohm, no minimo 4,75 V no sensor, 20 de 20 boots respondendo. Antes de rodar, corrigir src/proto/modbus_rtu.h | A4, A5 e A7 (todo o criterio de falha de enlace), o poll de 50 ms com timeout de 35 ms, e a errata dos 500 metros |
| M5 | Botoes, granularidade dos gestos e acoplamento do refresh do display nas linhas de tecla | Placa frontal conectada e energizada, DMM em IO15 (BTN_UP), IO34 (BTN_DOWN) e IO35 (BTN_MENU), solto e pressionado; IO34 e IO35 sao input-only sem pull interno e a placa mae nao tem pull-up. Comando `btn` em leitura continua. Osciloscopio de 4 canais nas tres linhas com gatilho em IO18 durante 1 h de rajadas de sendBuffer a 4 Hz (10.000 quadros no minimo), repetido com drive padrao e GPIO_DRIVE_CAP_0, a 4 e 10 MHz. Gerador de pulsos no pino: 1000 prensagens de 30 ms e 1000 duplos toques com display redesenhando e NVS gravando; 50 holds de 3 s com carimbo por GPIO; 1000 ciclos de senha errada. Aceitacao: no minimo 2,5 V solto e ate 0,3 V pressionado, excursao ate 0,8 V durante o refresh, zero teclas fantasma, zero prensagens de 30 ms perdidas, commit entre 3000 e 3250 ms em 50 de 50, zero escritas na NVS no ensaio de senha | A9, A13 e A14 inteiras. Sem nivel de repouso medido, nenhum numero de debounce, duplo toque, hold de 3 s ou timeout de 120 s tem significado |
| M6 | Mapeamento e polaridade dos LEDs do painel frontal e preservacao do modo download | Com a placa frontal conectada, emitir `relay 1 on` a `relay 4 on` um de cada vez e anotar qual LED fisico acende, confrontando com ihmLedLabel de board::kRelayMap (include/board_pins.h:59-64). Confirmar se o LED acende com o GPIO em nivel alto ou baixo (se a frontal for anodo-comum, o fail-safe inverte a legenda de novo). Amperimetro em serie no pino do CN3. `led on` / `led off` em IO2 medindo a corrente em regime com o ciclo pulsado de 900/100 ms, e verificar se a placa ainda entra em modo download com IO0 baixo e o painel conectado. Aceitacao: mapa serigrafia/net documentado e assinado, LED ativo em nivel alto, corrente ate 8 mA, modo download preservado | A15 (ECO de serigrafia e LED LIG pulsado) e a consequencia visivel de A1. Trava a impressao de etiqueta e a publicacao dos rotulos de menu |
| M7 | Display: tempo de quadro, tempo de init, legibilidade sob sol e identidade do modulo em tres lotes | GPIO livre levantado antes e baixado depois de u8g2_.sendBuffer(), osciloscopio, 100 quadros, maximo e mediana; medir tambem u8g2_.begin() inteiro. Medir com o clock que o driver realmente usa: o construtor U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI define o sck_clock_hz internamente e begin() nunca chama setBusClock(), entao board::kDisplaySpiHz = 4 MHz e ficcao. Ler o campo de valor a 1,0 m, 30 graus fora do eixo, sob 100 klx, por tres observadores. Fotografar a etiqueta de tres modulos de lotes diferentes e rodar o autoteste em cada um. Aceitacao: sendBuffer ate 25 ms (o teto de 10 ms da base e impossivel a 4 MHz, piso teorico 16,384 ms), begin() ate 150 ms, sinal e quatro digitos lidos sem erro pelos tres observadores, part number identico nos tres lotes | O orcamento de boot e a liberacao do painel como item de BOM controlado. Divergencia de part number entre lotes bloqueia o release ate ECO: sem MISO, a etiqueta e o unico dado observavel do controlador |
| M8 | Dinamica real da estrutura no ponto de fixacao: espectro, ruido residual com a maquina parada e vies de retificacao de vibracao | Dez minutos de registro a 100 Hz com o equipamento em operacao e dez minutos parado, no ponto real de fixacao, em MODE 1 e em MODE 4; PSD e amplitude pico a pico nas bandas 0,1 a 0,5 Hz, 0,5 a 2 Hz e acima de 10 Hz. Uma hora continua com a estrutura parada em 0,0 e em 45,0 graus, registrando desvio padrao e pico a pico do ultimo digito. Comparar a media do angulo por eixo com a maquina em operacao e parada, na mesma posicao mecanica, sem tocar na montagem. Aceitacao: pico a pico do valor filtrado ate 0,5 grau em operacao normal, ruido parado ate 0,1 grau pico a pico com sigma ate 0,03 grau, diferenca das medias ate 0,03 grau | A3 (a histerese de 0,3 grau so vale se for maior que o ruido residual) e A6 (o valor fixo do filtro; reprovando, sobe um degrau). O vies de retificacao bloqueia a errata de L38, porque nenhum passa-baixa a jusante do atan2 remove vies DC. Fixa tambem a janela da media movel |
| M9 | Latencia ponta a ponta ate o contato do rele, chatter na borda e ausencia de pulso em commit de parametro | Osciloscopio de 2 canais: canal 1 no DE do RS-485 da UR, canal 2 no GPIO do rele. Degrau franco (entrada salta para o dobro do limite) e degrau marginal (parte 5,0 graus aquem e termina 1,0 grau alem), 30 repeticoes em cada ajuste de filtro. Rampa de mesa a 0,1 grau/min cruzando o limite, registrando valor filtrado e estado do rele a cada 50 ms (prova de ausencia de zona morta). Uma hora com a inclinacao em L +/-0,1 grau contando comutacoes em IO32. Cem commits de troca de Sentido com um limite em '+' a 5,0 graus e a estrutura estatica a 3,0 graus. GPIO instrumentado no tick da tarefa ctrl, 100.000 ticks. Aceitacao: ate 500 ms no degrau franco e 750 ms no marginal, no maximo 60 comutacoes em 1 h e nenhum intervalo abaixo de 1000 ms, zero comutacoes nos 100 commits, periodo entre ticks ate 55 ms em 100.000 de 100.000 | A3 e A6 (a coluna de latencia da Tabela 1 e o teto de 1,2 s da folha de dados), A9 (efetivacao atomica sem pulso de rele) e o teto de latencia declarado em 5.9 |
| M10 | Faixa angular, censo de saturacao em operacao real, recuperacao de brown-out do sensor e amarracao fisica de 'Horario' a etiqueta | Mesa divisora ou nivel de precisao, um eixo por vez: -90,0 / -45,0 / 0,0 / +45,0 / +90,0 graus, mais 92,5 graus e 100,0 graus, mais um ensaio com o sensor montado invertido. Na mesma montagem, girar X em +5,0 graus no sentido horario visto pelo observador da Figura 4 e registrar o sinal do registrador 0 com 'Sentido Sensor X = Horario'; repetir para Y, nos dois sentidos, em tres sensoras de lotes diferentes. De 8 a 24 h continuas instaladas no equipamento portuario, registrando contagem e duracao de cada rajada de kStsSaturated (0x0020) e kStsSclStartup (0x0004) correlacionada com o evento mecanico, e o pico e o percentil 99,9 de ACC_X/Y/Z em modulo. Vinte afundamentos do +5 Vcc do sensor (0 V por 50 ms, 200 ms e 2 s) medindo o tempo ate status igual a 0x0001. Aceitacao: erro ate 0,09 grau nos cinco pontos; em 92,5 graus o display indica 90,0 e nenhum rele muda; giro horario produz leitura crescente em 3 de 3 unidades; nenhuma rajada de SATURADO acima de 1000 ms e menos de 12 por hora; recuperacao em 20 de 20 eventos, ate 1700 ms | A4 (e a unica medicao que autoriza migrar do criterio estrito para as tres bandas) e A9 (definicao fisica de Horario). Enquanto a amarracao do sentido nao fechar, dir = +1 e hipotese, e o manual e a etiqueta nao podem ser impressos |

---

## Erratas de manual inevitaveis

Dezoito correcoes que acontecem em qualquer combinacao de respostas desta folha.

1. **L56 e secao 7 L308** — 'EEPROM interna' esta errado. A retencao e em NVS na flash do ESP32-WROOM-32D, sem bateria, com banco duplo e CRC-16/MODBUS. A secao 7 promete retencao sem citar o meio.
2. **L36 e Tabela 3, terminal 4** — os 500 m valem para a sinalizacao RS-485, nao para a alimentacao remota do sensor. Publicar o limite por bitola com orcamento de 1,25 ohm de laco: cerca de 18 m em 20 AWG, 29 m em 18 AWG e 47 m em 16 AWG. Sem isso, uma instalacao longa entrega um sensor que nao liga.
3. **2.1 L33, 6.2 L268 e Tabela 3** — a faixa publicada '-10 a +10 Vcc' esta incompleta. O nivel de falha da saida analogica tem de constar como linha propria, identificado como valor fora da faixa de medicao.
4. **2.1 L38** — os +/-0,1 % da escala tem de ser qualificados como especificacao estatica (maquina parada), estendidos explicitamente a saida analogica e acompanhados da remissao ao vies de retificacao de vibracao, que depende da instalacao mecanica (L272). Nenhum filtro a jusante do atan2 remove vies DC.
5. **5.9 L142 e L205** — o ponto de atuacao esta publicado como exato. Qualquer histerese de liberacao exige nota nova declarando que o ataque e exato e a liberacao tem banda. E inevitavel: sem banda, o rele bate uma vez por ciclo de oscilacao.
6. **5.9 L204 e Tabela 4 L336/L345** — 'o rele permanece em repouso' e a descricao do padrao de fabrica NF tem de ser reescritos assim que a polaridade for assinada, em qualquer das duas opcoes. E preciso dizer por extenso que o jumper NA/NF nao substitui a polaridade: em qualquer posicao, o contato sem energia e identico ao contato em repouso.
7. **Secao 5, autoteste e logomarca** — os 3500 ms de splash bloqueante saem do produto, porque sozinhos ja violam o tWD minimo de 1,12 s do watchdog externo. Passa a splash nao bloqueante de 600 ms de autoteste mais 600 ms de logomarca, e o manual tem de declarar que a supervisao dos quatro limites ja esta ativa durante o splash.
8. **L110 contra L158 e L221** — o manual usa dois gestos diferentes para gravar (hold de MENU de cerca de 3 s em 5.4, clique curto em 5.6 e 5.9). Unificar em hold de 3 s e corrigir 5.6 e 5.9; como esta, o setpoint de um rele de seguranca e gravado pelo mesmo gesto que avanca digito.
9. **Figuras de 5.7 L172 e L180** — 'Ajuste 0Vcc:0000' e 'Ajuste 10Vcc:0000' mudam em qualquer opcao de A14. Um campo de 4 digitos sem sinal abrindo em 0000 nao representa trim bipolar; a figura como esta e irrealizavel.
10. **5.7 L167** — acrescentar a instrucao de colocar o laco analogico do eixo em MANUTENCAO no CLP antes de iniciar o assistente, e declarar que os quatro reles continuam operando sobre o angulo real durante toda a Auto Calibracao.
11. **5.7 e item 8** — declarar que o integrador deve configurar deteccao de fora de faixa no cartao de entrada do CLP. Abertura e curto do cabo analogico sao indetectaveis pela UR.
12. **5.9 L202 e mapeamento dos LEDs** — a serigrafia do CN3 esta cruzada: LIM1 acende o LED rotulado 'LED LIM3', LIM2 acende 'LED LIM1', LIM3 acende 'LED LIM2'. Mesmo com ECO aprovada para producao nova, o parque instalado exige errata.
13. **Secao 7 L306** — o manual exige que o equipamento anuncie falha de comunicacao e nao publica o texto. A tabela de strings de tela entra no manual byte a byte, sem acentuacao, inclusive 'AGUARDANDO SENSOR', que cobre a janela entre o boot e a primeira transacao valida.
14. **Tipografia de toda a IHM** — o manual mistura menu acentuado com mensagens sem acento, e as fontes ASCII do display nao tem acento. Padronizar todas as telas sem acentuacao, corrigir 'Auto Calibracao', 'Operacao Limite', 'Horario' e 'Anti-horario', e trocar os simbolos maior-ou-igual e menor-ou-igual da Tabela 1 por '>=' e '<='.
15. **6.3 L292-L294** — a unica inspecao prevista e antes de energizar, e toda de instalacao. Falta a secao de limitacoes de diagnostico com a lista fechada do que a UR nao detecta (contato colado, bobina aberta, transistor em curto, CN2 solto, cabo analogico rompido, XTR300 em protecao termica, display congelado com imagem valida) e falta o ensaio funcional periodico.
16. **5.3 L82 e L106** — a senha 1234 esta publicada no manual e nao protege nem o PSET (5.6, Modo Normal) nem o Reset Geral (5.11, energizacao). Declarar por extenso que quem alcanca fisicamente o painel move os quatro pontos de atuacao sem digitar senha, e que o painel frontal e area controlada.
17. **6.2 e secao de limitacoes** — declarar que, em travamento sem queda de energia, a saida analogica retem o ultimo angulo por ate cerca de 2,6 s (travamento mais tWD de 1,12 a 2,24 s, mais bootloader e setup), porque o DAC8562 nao e resetado pelo reset do ESP32. A correcao definitiva e ECO de hardware.
18. **Entrega documental fora do manual** — a matriz REQ para secao do manual nao existe publicada. Um grep pelos identificadores encontra apenas dois definidos, ambos em docs/protocolo-rs485.md; LIM-01 a 08, DIR-01/02, PWD-01 a 05, PRG-01 a 04 e PER-01 nao tem contrato provavel em revisao enquanto ela nao for publicada.

---

## O que ja pode comecar sem aprovacao

1. Corrigir src/proto/modbus_rtu.h (kRegisterCount de 2 para 8, kRxCap de 16 para 32) e escrever o teste native que decodifica a resposta de 21 bytes do FC03 com 8 registradores. Hoje a resposta nao cabe no buffer; nenhum ensaio de enlace vale antes disso.
2. Modulo de dominio puro do CRC-16/MODBUS e do serializador/parser do quadro FC03, compilavel em env:native, com vetores de teste do sensormap ja publicado.
3. Cadeia de conversao analogica bipolar em aritmetica inteira: D = 32768 + (mV x 32768) / 12500 e a inversa, grampo da faixa util em 6554 a 58982 e arredondamento nos extremos. A topologia ja esta assinada; o codigo de falha entra como constante parametrizavel.
4. Teste que falha se qualquer caminho do firmware escrever 0x0000 no DAC (vale -12,5 V, satura o XTR300, abre a malha e aciona EFLD). Corrigir src/drivers/xtr300.cpp:12 e src/drivers/dac8562.cpp:21, que hoje usam 0x0000 como zero.
5. Maquina de estados do enlace dirigida por relogio injetado, em env:native: 3 transacoes invalidas consecutivas declaram falha, 5 boas recuperam, permanencia minima de 2000 ms. Os tres numeros estao fixados na base comum; o latch de A7 entra como estrategia plugavel.
6. Chute do watchdog por ISR de timer de hardware em IRAM, com GPIO.out_w1ts / out_w1tc, pulso de 1 ms a cada 250 ms, travado por token de liveness de 800 ms da tarefa ctrl. Substitui o esp_timer atual, que para durante o apagamento de setor da NVS; M3 confirma depois.
7. Ordem de boot canonica dos 13 passos, com SPI.begin(18, -1, 23, -1) antes de u8g2_.begin() para que o U8g2 nao sequestre o IO19 (MISO default do VSPI, que e o WDI), e com o splash convertido em maquina de estados nao bloqueante.
8. Criacao da tarefa ctrl (core 0, prioridade 5, stack de 4096 B, vTaskDelayUntil de 50 ms) como dona exclusiva da transacao Modbus, do filtro, da avaliacao dos limites, da escrita dos quatro GPIOs de rele, da escrita do DAC e do token de liveness; e a regra de que o loop() nunca escreve rele nem DAC, so publica pedido por fila.
9. ParamRecord com banco duplo, CRC-16/MODBUS e carregamento no boot, com testes native para cada ponto de corte de energia. A politica de commit de A13 fica atras de interface.
10. Filtro EMA em Q8 inteiro com passo minimo que elimina a zona morta, mais mediana de 3 amostras, como modulo de dominio puro testado em env:native com o coeficiente k como parametro.
11. Motor de limites como dominio puro (src/app/limit_engine), com os tres modos ('>=', '<=', '+'), histerese e contadores de confirmacao parametrizados, incluindo o caso degenerado de '+' com |L| menor ou igual a 2 decimos, que fica permanentemente atacado.
12. Editor de digitos com cursor abrindo no digito mais a direita, MENU movendo para a esquerda e rolagem circular dentro do campo, como regra unica para senha, limites, preset e trim.
13. Relogio falso injetavel em todo o dominio (enlace, filtro, comparador, gestos), para que todos os tempos do projeto sejam testaveis em env:native sem hardware. E pre-requisito de tudo o que esta acima.
14. Separacao de ambientes no platformio.ini: env:production sem src/net e sem os comandos de atuacao, env:factory com tudo, e o script de release que roda nm sobre o .elf procurando simbolos proibidos. E reversivel, e A12 apenas confirma a politica de acesso da assistencia tecnica.

---

## Como aprovar

Duas formas, as duas validas:

1. Responder **"aprovo as recomendacoes"**. Valem as 15 recomendacoes desta folha, com os sub-itens, e as condicionais ficam automaticas: A1 cai para a opcao A se M2 reprovar, A3 usa 0,6 grau de histerese se M8 reprovar, A4 migra para as tres bandas se M10 aprovar, A6 publica 1,2 s de teto na folha de dados.
2. Responder **"aprovo, com estes desvios"** e listar apenas o que muda, no formato `A<n> = <letra>` (por exemplo: `A2 = B`, `A6 = A`, `A11 = B`). O que nao for citado segue a recomendacao.

A escolha A12 pede uma resposta de uma palavra: **sim** ou **nao** para a assistencia tecnica atuar rele em campo.

Sem essas assinaturas ficam parados: a impressao do manual e da etiqueta, o fechamento da Tabela 1, da Tabela 3 e da Tabela 4, o ECO da placa sensora e o ECO de serigrafia do painel. As 14 frentes da secao anterior seguem independentemente.


---

# Parte 2 — Base comum

Contrato transversal que as 12 decisoes tem de respeitar. Foi fixado depois que a revisao
de completude mostrou os quatro grupos de decisao se contradizendo em base de tempo, ordem
de boot e polaridade de rele.

## 2.1 Base de tempo

BASE DE TEMPO UNICA — POLL 50 ms, TIMEOUT 35 ms, AVALIACAO 50 ms, DONO = TAREFA FreeRTOS PROPRIA

1) ROUND-TRIP REAL, CONTADO BYTE A BYTE (19200 8N1, escravo id 1, FC03, 8 registradores)

Tempo de caractere: 10 bits / 19200 = 520,83 us. O codigo da sensora calcula 520 us em inteiro (sensor/src/drivers/rs485.cpp:313, bits = 1 start + 8 dados + 0 paridade + 1 stop).
Silencio de 3,5 caracteres: interFrameGapUs() = charTimeUs*7/2 = 1820 us, com piso de 750 us (sensor/src/main.cpp:76-79). Vale 1820 us.
Quadro de pedido: [id][0x03][hi][lo][hi][lo][crc][crc] = 8 bytes = 4166,7 us no fio.
Quadro de resposta: [id][0x03][bytecount][16 bytes de dado][crc][crc] = 21 bytes = 10937,5 us no fio.
Tempo de fio total = 4166,7 + 10937,5 = 15104 us = 15,10 ms. Propagacao em 500 m de cabo (~5 ns/m) = 2,5 us, desprezivel.

LATENCIA DO ESCRAVO, DERIVADA DO CODIGO REAL (sensor/src/main.cpp:82-108), nao de catalogo:
 (a) serviceLink() chama g_link.read(chunk, 64, kReadPollMs=2). uart_read_bytes so retorna quando junta 64 bytes OU o timeout de 2 ms estoura. Como o pedido tem 8 bytes, a chamada SEMPRE queima os 2 ms inteiros.
 (b) g_lastByteUs = micros() e carimbado no RETORNO da leitura, nao na chegada do byte (linha 91). Isso envelhece o carimbo em ate 2,0 ms de graca.
 (c) o resto do laco antes de reentrar em serviceLink(): leitura do SCL3300 a cada 10 ms (6 quadros de 32 bits com 12 us de CS alto entre eles, ~0,2 ms) + console.poll() + LED. Orcamento 0,5 ms.
 (d) a proxima serviceLink() queima outros 2,0 ms de timeout, ai sim ve elapsed (2,0 + 0,5 = 2,5 ms) > 1,82 ms de silencio e chama handle() (~0,05 ms de CRC sobre 8 bytes).
 Latencia do escravo: minima 2,05 ms (2,0 do poll de silencio + 0,05 do handle), maxima 4,55 ms (2,0 + 0,5 + 2,0 + 0,05).

Turnaround do DE: o DE nao e chaveado por software nos dois lados — e o pino RTS do periferico em UART_MODE_RS485_HALF_DUPLEX (rs485.cpp, uart_set_mode). O periferico solta o DE apos o stop bit do ultimo byte; o SN65HVD75DR comuta em dezenas de ns. Reserva de 0,6 ms para acomodacao de linha e para o instante em que o mestre arma a recepcao.
Jitter do FreeRTOS na sensora (Wi-Fi e BT desligados, loopTask em prioridade 1, esp_timer do WDI em 22): 1,0 ms.

ROUND-TRIP TOTAL, do primeiro byte do pedido ao ultimo byte da resposta:
 tipico = 15,10 + 2,50 + 0,30 = 17,9 ms
 pior caso = 15,10 + 4,55 + 0,60 + 1,00 = 21,3 ms

2) O TIMEOUT DE 20 ms ESTA MORTO. 20 ms fica ABAIXO do pior caso de 21,3 ms e apenas 2,1 ms acima do tipico. Ele reprova transacao boa por construcao, e cada reprovacao leva os quatro reles ao estado de alarme. As decisoes 5 e 7 estao erradas neste ponto e a base comum as sobrescreve.

3) NUMEROS FIXADOS
 kLinkTimeoutMs = 35 ms. E 1,64x o pior caso calculado e 1,96x o tipico. Nao reprova transacao boa nem com o jitter dobrado.
 kPollPeriodMs = 50 ms (20 Hz). Ocupacao do barramento 15,1/50 = 30 %. Silencio entre transacoes >= 28 ms, quinze vezes o t3,5. Uma transacao que estoure o timeout de 35 ms ainda deixa 15 ms de folga antes do proximo pedido, entao um timeout nunca colide com o quadro seguinte.
 Uma unica transacao por tick: FC03, endereco 0, quantidade 8 — exatamente o sensormap ja publicado. NAO fragmentar em 0..3 rapido e 4..7 lento: economizaria 5 ms num orcamento de 50 ms e criaria um segundo contrato de fio para manter.
 Declaracao de falha: 3 transacoes invalidas consecutivas (timeout ou CRC) = 150 ms. Recuperacao: 5 transacoes boas consecutivas = 250 ms, com permanencia minima de 2000 ms no estado de falha (anti-flapping). 150 ms fica 7,5x abaixo do tWD minimo de 1,12 s do STWD100.
 Idade maxima do dado que comanda rele = kPollPeriodMs + kRoundTripMax = 50 + 21,3 = 71,3 ms.

4) PERIODO DE AVALIACAO DE LIMITE E RELE = 50 ms, no MESMO tick do poll, imediatamente depois da transacao. Nao adianta avaliar mais rapido que o dado chega; avaliar mais devagar so soma atraso. O filtro passa-baixa roda no mesmo tick de 50 ms e tem de ser especificado como CONSTANTE DE TEMPO em ms, convertida em coeficiente nesse periodo — assim a disputa entre as decisoes 4 e 5 (boxcar N=8 x EMA Q8) vira escolha de implementacao, nao de base de tempo.

5) DONO DO CICLO — TAREFA FreeRTOS PROPRIA, nao o loop() cooperativo
 Tarefa "ctrl": core 0 (APP_CPU livre, com Wi-Fi fora), prioridade 5 (o loopTask do Arduino e 1), stack 4096 B, cadencia por vTaskDelayUntil de 50 ms. E dona EXCLUSIVA de: transacao Modbus, filtro, avaliacao dos quatro limites, escrita dos quatro GPIOs de rele, escrita do DAC, e refresh do token de liveness do watchdog. O driver da UART2 e instalado de dentro dela, para a ISR ficar afim ao mesmo core.
 loop() (loopTask, core 1, prioridade 1) fica com: botoes, maquina de estados da IHM, display, console, NVS. NUNCA escreve rele nem DAC — publica pedido de mudanca de parametro por fila/portMUX e a tarefa ctrl aplica.
 Motivo: os orcamentos de 100 ms, 36,4 ms e 200 ms das decisoes 4, 5, 7 e 8 foram afirmados sobre um loop() que tambem empurra 2048 B de framebuffer no SSD1322 e escreve NVS. Sem a tarefa, nenhum desses numeros e propriedade verificavel; com a tarefa, o pior caso do ciclo de seguranca e a soma da transacao (21,3 ms) com a avaliacao (<1 ms), independente da IHM.
 RESSALVA HONESTA: a tarefa NAO protege contra a escrita de NVS. Durante o apagamento de setor a cache e desabilitada e TODA tarefa que executa de flash para, inclusive a ctrl. Por isso o chute do WDI tem de sair de uma ISR de timer de hardware em IRAM (ver ordemDeBoot), e a tarefa ctrl tem de tolerar um tick perdido sem declarar falha — os 3 ciclos ate a falha (150 ms) cobrem uma janela de cache-off de ate ~100 ms; janelas maiores tem de ser medidas (medicao 3) e, se passarem de 100 ms, o commit de NVS tem de ser fatiado ou movido para uma janela em que os reles ja estejam congelados de proposito.

6) CORRECAO DE CODIGO QUE A BASE EXIGE: src/proto/modbus_rtu.h:17 e :22 tem kRegisterCount = 2 e kRxCap = 16. A resposta de 8 registradores tem 21 bytes e NAO CABE em 16. Subir para kRegisterCount = 8 e kRxCap = 32 antes de qualquer teste de enlace.

## 2.2 Ordem de boot

ORDEM CANONICA DO setup() — UMA SEQUENCIA, COM ORCAMENTO POR PASSO

PREMISSA QUE DESARMA A BRIGA DOS TRES "PRIMEIROS":
As tres reivindicacoes sao conciliaveis porque duas delas custam menos de 1 ms e a terceira nao precisa ser primeira.
 (a) O DAC nao precisa ser o primeiro. A saida ja esta no trilho negativo desde a energizacao, durante os ~300 ms de bootloader do ESP32, faca o firmware o que fizer. Adiar a configuracao do DAC em 2 ms para chutar o watchdog antes nao muda nada de fisico.
 (b) Os GPIOs de rele nao precisam ser os primeiros. No reset eles sao entradas, e o pull-down de 1K base-emissor mantem o BC337 cortado e a bobina desenergizada. O estado de hardware ja e o estado de repouso. Dirigi-los cedo e so tornar isso explicito e de baixa impedancia, e custa 0,2 ms.
 (c) O display NAO precisa roubar o IO19. O SPI.begin() sem argumentos do U8g2 e que prende o MISO default do VSPI (IO19 = WDI). O SPIClass::begin() do core ESP32 2.x tem retorno antecipado se o barramento ja estiver aberto: se NOS chamarmos SPI.begin(kDispSclk=18, -1, kDispMosi=23, -1) ANTES de u8g2_.begin(), o begin interno do U8g2 vira no-op e o IO19 nunca e tocado. A lacuna de WDI do display passa a ser ZERO, e o rearmPin() fica como cinto-e-suspensorio.

SEQUENCIA NUMERADA (t = 0 na entrada do setup(), ja ~300 ms apos a liberacao do reset)

 1. WATCHDOG PRIMEIRO. pinMode(IO19, OUTPUT) + LOW + um pulso imediato; em seguida arma o chute por ISR de timer de HARDWARE em IRAM a 1 kHz (nao esp_timer, nao ESP_TIMER_TASK): a cada 250 ticks levanta WDI por GPIO.out_w1ts, no tick seguinte baixa por out_w1tc — pulso de 1 ms, muito acima do glitch de 100 ns e muito abaixo do tPW de 210 ms. Nada de digitalWrite, nada de delayMicroseconds, nada fora da IRAM. A ISR so pulsa enquanto o token de liveness da tarefa ctrl tiver menos de 800 ms; assim o cachorro continua morrendo quando o firmware trava, mas sobrevive a cache-off da NVS. ORCAMENTO 0,5 ms.
 2. RELES AO ESTADO DE BOOT. pinMode OUTPUT + nivel de boot em IO32/26/25/33. Nas duas polaridades o nivel de boot e BAIXO (desenergizado): na polaridade do manual e "repouso/sem alarme"; na polaridade fail-safe e "alarme", que e o correto durante o boot. ORCAMENTO 0,2 ms.
 3. LED LIG (IO2) EM NIVEL BAIXO. Ainda nao acende: IO2 e strapping e o painel pode quebrar o modo download. Vira heartbeat no passo 14. ORCAMENTO 0,05 ms.
 4. CAPTURA DE BOOT. esp_reset_reason() + amostragem dos pinos de strapping + amostragem de IO15 (BTN_UP) para a guarda do gesto de reset de fabrica. ORCAMENTO 1 ms.
 5. DAC8562 E SAIDA ANALOGICA. HSPI begin (IO21/13, MISO -1) + SYNC alto + settle 1 ms + softReset + settle 2 ms + powerUpBoth + refGain2 (0x38/0x0001) + ignoreLdacPin + escrita do CODIGO DE FALHA 3932 nos dois canais (nao 0x0000, que vale -12,5 V, e nao 0x8000, que vale 0,00 V e e uma leitura legitima). Depois OP_MODE (IO22) como saida em nivel BAIXO = modo tensao. A saida sai do trilho negativo AQUI, ~4 ms dentro do setup(), e assume -11,00 V, que o CLP le como "invalido". Acomodacao do XTR300: Cc de 47 nF sobre R_OS de 1K da tau = 47 us, mais ate 40 us internos; 0,5 ms para 0,1 %. ORCAMENTO 6 ms.
 6. CONSOLE SERIAL 115200. Unico caminho de diagnostico se tudo o mais falhar. ORCAMENTO 2 ms.
 7. NVS. nvs_flash_init() + leitura do bloco de parametros com verificacao de CRC. Este e o maior passo do boot e o unico que pode desabilitar a cache. ORCAMENTO 60 ms tipico; 800 ms no pior caso de particao virgem ou corrompida que exige apagamento. Sobrevive porque o chute do passo 1 e ISR/IRAM.
 8. PRE-RESERVA DO VSPI. SPI.begin(18, -1, 23, -1) com MISO = -1. E este passo que impede o U8g2 de sequestrar o IO19. ORCAMENTO 0,2 ms.
 9. DISPLAY. u8g2_.begin() (sequencia de init do SSD1322) + setContrast + clearBuffer + sendBuffer de 2048 B. ORCAMENTO 150 ms (A_MEDIR, medicao 9: o driver nunca chama setBusClock(), entao o clock e o do construtor do U8g2 e board::kDisplaySpiHz = 4 MHz e ficcao neste caminho).
 10. g_wdt.rearmPin(). Cinto-e-suspensorio: se um upgrade de core mudar o comportamento do SPIClass::begin, este passo devolve o IO19. ORCAMENTO 0,05 ms.
 11. BOTOES. pinMode em IO15/34/35 e primeira amostragem. ORCAMENTO 0,5 ms.
 12. RS-485. uart_driver_install + UART_MODE_RS485_HALF_DUPLEX a 19200 8N1. ORCAMENTO 5 ms.
 13. CRIACAO DA TAREFA ctrl (core 0, prio 5, stack 4096). A partir daqui os limites, os reles e a saida analogica estao VIVOS e cadenciados a 50 ms, independentemente do que a IHM fizer. ORCAMENTO 1 ms.
 14. FIM DO setup(). Total bloqueante = 0,5+0,2+0,05+1+6+2+60+0,2+150+0,05+0,5+5+1 = 226 ms tipico; 966 ms no pior caso com NVS patologica.
 15. AUTOTESTE DO DISPLAY E LOGOMARCA (manual secao 5) rodam DEPOIS, no loop(), como maquina de estados NAO BLOQUEANTE: 600 ms de padrao de autoteste + 600 ms de logomarca = 1200 ms. A tarefa ctrl ja esta polando, avaliando limites e comandando reles durante todo o splash. Isto elimina os 3500 ms bloqueantes da decisao 12, que sozinhos ja violavam o tWD de 1,12 s.
 16. TELA PRINCIPAL. Se ainda nao houve quadro valido, o texto e "AGUARDANDO SENSOR", nao "FALHA DE COMUNICACAO" — sai da contradicao DSP-03/04 entre os 3500 ms da decisao 12, os 5,0 s da decisao 7 e os 150 ms da decisao 8: a falha so e declarada pelo criterio unico da tarefa ctrl (3 transacoes invalidas = 150 ms) e o "aguardando" cobre a janela ate a primeira transacao terminar.

MAIOR LACUNA DE PULSO NO WDI PRODUZIDA POR ESTA SEQUENCIA
 Da liberacao do reset ate o primeiro pulso: bootloader do ESP32 + 0,1 ms. Estimativa 300 ms, A_MEDIR (medicao 8). Margem de 3,7x sobre o tWD minimo de 1,12 s; orcamento maximo declarado 500 ms.
 Depois do primeiro pulso: TETO DURO DE 250 ms, imposto pela ISR de timer de hardware em IRAM — inclusive durante o u8g2.begin(), durante o apagamento de setor da NVS e durante qualquer bloqueio do loop(). Nenhum passo desta sequencia produz lacuna maior que 250 ms depois do passo 1.
 Se o chute continuar em esp_timer com ESP_TIMER_TASK (src/drivers/ext_wdt.cpp:41), a maior lacuna passa a ser bootloader + janela de cache-off da NVS, que hoje NAO tem numero. Os "250 ms de chute independente do laco" das decisoes 2, 3 e 9 sao falsos nesse mecanismo. A troca para ISR/IRAM e requisito de base, nao otimizacao.
 CRITERIO DE ACEITACAO: osciloscopio com persistencia infinita no IO19, disparo no RST# do STWD100 (pino 1 / J15), 20 boots incluindo um com particao NVS virgem — nenhum intervalo entre pulsos acima de 500 ms em nenhum instante do boot.

## 2.3 Polaridade do rele

POLARIDADE DO RELE — DUAS OPCOES, CONSEQUENCIAS MEDIDAS, RECOMENDACAO CONDICIONADA. DECISAO DO BIGBOSS.

FATO QUE PRECISA SER DITO PRIMEIRO, PORQUE DERRUBA A SAIDA FACIL:
O jumper NA/NF (J10/J9/J8/J2, padrao de fabrica NF, manual L336/L345) NAO resolve isto. O jumper escolhe apenas se o alarme aparece como contato fechado ou aberto. Em QUALQUER posicao de jumper, o estado do contato com a placa sem energia e IDENTICO ao estado com a bobina em repouso. Logo, se "repouso = sem alarme" (polaridade do manual), a queda de energia produz exatamente a mesma indicacao de "sem alarme", e nenhum jumper conserta isso. So a polaridade do firmware pode fazer a queda de energia sinalizar.

OPCAO A — FIDELIDADE AO MANUAL (bobina energizada = limite atingido = "acionado")
 Base documental: manual 5.9 L195-196 ("Off: o rele permanece em repouso"), src/drivers/relays.h:2 ("Ativo em nivel ALTO; estado seguro (bobinas desligadas) e nivel BAIXO"), src/main.cpp:75 (enterSafeState -> allOff).
 Consumo: as bobinas so puxam corrente em alarme. AX1RC-5V com R medida ~139 ohm da ~36 mA por bobina; quatro em alarme simultaneo (operacao "+" nos dois eixos) = 144 mA em 5 V = 0,72 W, e isso e um estado transitorio-ocasional, nao o normal.
 Termica do BC337: em repouso o transistor esta cortado, dissipacao zero. Em alarme, Ic ~36 mA. Ib = ((3,3 - 0,7 Vbe - 0,7 Vd)/2000) - (0,7/1000) = 0,25 mA (formula de src/tests/test_08_coil.cpp:stepBaseDrive). Beta forcado exigido = 36/0,25 = 144, muito perto do minimo de catalogo do BC337 — mas em regime intermitente.
 LED do painel: apagado em condicao normal, aceso em alarme. E o que a serigrafia e o manual dizem ("LED de sinalizacao" do limite). Os LEDs penduram direto no net do GPIO, antes do resistor de base (lev_display-teclado.md:30), entao a polaridade do LED segue a do rele obrigatoriamente.
 "Off": limite desativado = bobina em repouso = coerente com L195, sem consumo.
 Contato NF de fabrica: normal = NF FECHADO; alarme = NF ABERTO; QUEDA DE ENERGIA = NF FECHADO = indistinguivel de "tudo bem". Travamento do ESP32 antes do STWD100 resetar = os GPIOs mantem o ultimo nivel = os contatos congelam no ultimo estado, tambem sem sinalizacao.
 CUSTO REAL: nao existe, em toda a UR, nenhum canal que denuncie perda de alimentacao a uma maquina. O display apaga e o LED LIG apaga (um humano ve, um CLP nao). A saida analogica vai a 0,00 V quando os trilhos de +/-15 V colapsam, e 0,00 V e uma leitura perfeitamente legitima (0,0 graus). Ou seja: na opcao A, a UR sem energia reporta "estrutura nivelada, nenhum limite atingido" para todos os canais de maquina.

OPCAO B — FAIL-SAFE (bobina energizada = saudavel e sem alarme; desenergizada = alarme, falha de link, dado invalido, boot e queda de energia)
 Consumo: as quatro bobinas ficam energizadas o tempo TODO em operacao normal. 144 mA em 5 V = 0,72 W CONTINUOS. O pior caso deixa de ser eventual e passa a ser o caso normal. Orcamento da fonte de 5 W: 0,72 W de bobinas + ate 1,00 W do sensor (CN1-4, +5 Vcc/200 mA max) + ESP32 ~0,35 W + OLED 256x64 ~0,30 W + A0515S-2WR3 ~0,40 W = 2,77 W no no de 5 V; com ~85 % do LM2575 e ~80 % do estagio AC/DC, da ~4,1 W na entrada, contra os 5 W publicados. Margem de apenas 18 %. ISTO NAO PODE SER APROVADO SEM AS MEDICOES 6 E 12 — e possivel que a fonte simplesmente nao carregue.
 Termica do BC337: conducao CONTINUA com beta forcado ~144, no limite do catalogo. Se o transistor entrar em quase-saturacao, Vce sobe de ~0,2 V para ~1 V e a dissipacao vai de 7 mW para 36 mW por transistor — termicamente ainda pequeno (encapsulamento TO-92, Rth ~200 C/W, ~7 C de elevacao), mas o problema real nao e calor, e MARGEM DE ACIONAMENTO: 0,25 mA de base para 36 mA de coletor, 24 h por dia, a 60 C de ambiente (manual 6.2). Exige a medicao 7 antes de liberar; se Vce medido passar de 0,4 V, o resistor de base de 2K precisa de ECO e a opcao B fica bloqueada.
 LED do painel: os quatro LEDs ficam ACESOS o tempo todo em condicao normal e APAGAM em alarme. Inverte a semantica publicada. Soma alguns mA continuos por GPIO alem da base (o LED nao tem resistor serie na placa mae; o valor esta na placa frontal, cujo desenho nao existe no repo). Exige reescrever a legenda do manual para "LED de permissivo".
 "Off": dilema real. Se Off = energizado, um limite DESATIVADO queima 36 mA para sempre e contradiz L195 ("permanece em repouso"). Se Off = desenergizado, Off vira indistinguivel de alarme. A unica saida coerente e redefinir o rele como linha de seguranca: "fechada/energizada = permissivo", Off = permissivo permanente = energizado — o que reescreve 5.9.
 Contato NF de fabrica: normal = NF ABERTO; alarme, falha e QUEDA DE ENERGIA = NF FECHADO. O cliente le "contato fechado = alarme". Queda de energia e travamento passam a sinalizar.
 CUSTO REAL: manual 5.9 e Tabela 4 reescritos, legenda dos LEDs reescrita, e — o mais perigoso — uma UR de campo que receba esta atualizacao passa a indicar o OPOSTO do que a fiacao existente espera. Exige nota de release em negrito e reinspecao da instalacao.

RECOMENDACAO (para aprovacao humana, nao decidida aqui): OPCAO B, CONDICIONADA a aprovacao das medicoes 6, 7 e 12.
 Motivo: e equipamento de seguranca operacional portuaria e, como mostrado acima, a opcao A nao deixa NENHUM canal legivel por maquina denunciando que a UR morreu — nem rele, nem analogica (0,00 V e leitura legitima), nem nada. A opcao B e a unica que transforma "sem energia" em "alarme". Nao existe terceira via em hardware: nao ha quinto rele de saude, nao ha readback de contato, e os pinos EFOT/EFLD/EFCM do XTR300 so acendem LEDs locais.
 SE qualquer das medicoes 6/7/12 reprovar (fonte nao carrega 4 bobinas continuas, ou Vce do BC337 passa de 0,4 V em 60 C), a opcao B e FISICAMENTE impossivel e a decisao volta para a opcao A — e nesse caso e obrigacao contratual, escrita no manual em negrito, que o intertravamento do cliente monitore a alimentacao da UR por um canal externo, porque a UR nao consegue sinalizar a propria morte.
 Em ambas as opcoes, o boot mantem os quatro reles desenergizados (o estado de hardware do reset), o que na opcao B ja e o estado de alarme correto e na opcao A e o repouso.
 Esta decisao trava as decisoes 3, 5, 7, 8 e 11 e tem de ser assinada antes que qualquer uma delas vire codigo.

## 2.4 Estado seguro da saida analogica

ESTADO SEGURO DA SAIDA ANALOGICA EM FALHA DE LINK — VALOR FORA DE BANDA, -11,00 V, CODIGO 3932

AS TRES OPCOES, JULGADAS PELO QUE O CLP CONSEGUE DISTINGUIR:

 (a) MANTER O ULTIMO VALOR. REJEITADA. O CLP recebe um angulo plausivel, estavel e ERRADO, indistinguivel de uma estrutura parada. E o modo de falha classico de "congelar o ponteiro": o operador ve a estrutura inclinando e o painel jurando que esta a 3,2 graus. Piora com o fato de que o DAC8562 NAO e resetado pelo reset do ESP32 (LDAC amarrado, sem CLR no reset): num travamento com reset pelo STWD100, os reles caem em 1,9 a 2,4 s mas a saida analogica continua exibindo o ultimo angulo valido durante travamento + reset + boot. Congelar de proposito e consagrar esse defeito.

 (b) IR A 0,00 V. E A PIOR DAS TRES, e nao por opiniao — por dois motivos verificaveis. Primeiro: 0,0 grau e o ponto de operacao MAIS COMUM de uma estrutura nivelada, entao 0,00 V e a leitura legitima mais provavel do equipamento. Segundo, e decisivo: quando a UR perde alimentacao, os trilhos de +/-15 V do A0515S-2WR3 colapsam e a saida vai naturalmente a 0 V. Ou seja, 0,00 V E A ASSINATURA FISICA DA QUEDA DE ENERGIA. Escolher 0,00 V como nivel de falha e escolher o unico valor que o hardware ja produz quando esta morto, e que o firmware tambem produz quando esta perfeito. A decisao 10 esta errada neste ponto e a decisao 7 esta certa em chama-la de pior escolha possivel.

 (c) VALOR FORA DE BANDA. ADOTADA. -11,00 Vcc, escrito como CODIGO CRU 3932 (D = 32768 + 2621,44 * (-11,00) = 3932), fora da cadeia de calibracao, nos DOIS eixos.
 Por que -11,00 V e nao outro numero: fica 1,00 V (2621 codigos, ~2600 LSBs de 381,47 uV) abaixo da saturacao legitima de -10,00 V, uma separacao grande demais para qualquer erro de calibracao, deriva ou ruido fechar; e fica ~1,0 V acima do limite de swing do XTR300 ((V-)+3 = -12 V), de modo que o estagio permanece na regiao linear e as flags EFLD/EFCM nao disparam — ao contrario do 0x0000 de hoje, que pede -12,5 V, satura, abre a malha da IA e aciona as flags.

QUANDO O NIVEL DE FALHA E APLICADO (contrato unico, elimina a ambiguidade DSP-03/04):
 - do passo 5 do boot (configuracao do DAC) ate o primeiro quadro Modbus valido: -11,00 V. A saida sai do trilho negativo direto para o nivel de falha, sem passar por 0,00 V, que seria uma mentira momentanea de "estrutura nivelada";
 - apos 3 transacoes invalidas consecutivas (150 ms): -11,00 V;
 - com quadro integro mas status reprovado pelo sensor (kStsSclNotResponding, kStsSclCrcError, kStsSclSelfTestFail): -11,00 V;
 - saida do estado de falha: 5 transacoes boas consecutivas e permanencia minima de 2000 ms.
 Faixa util normal grampeada em 6554..58982 (-10,00 a +10,00 V), conforme manual 5.7 L176 ("Inclinacoes superiores ao fundo de escala mantem a saida saturada em +/-10,00 Vcc"). O grampo e em +/-10,00 V, nao no limite eletrico.

TRADE-OFF, DITO SEM MAQUIAGEM:
 -11,00 V esta FORA da faixa publicada em 2.1 L33, 6.2 L268 e Tabela 3 ("-10 a +10 Vcc"). Isso obriga a duas coisas: (i) acrescentar o nivel de falha explicitamente a Tabela 3 e a secao 6.2 do manual, como "nivel de falha: -11,00 Vcc (fora da faixa de medicao)"; (ii) confirmar, POR INSTALACAO, que o cartao de entrada analogica do CLP do cliente tolera -11 V. Cartoes industriais de +/-10 V costumam ter maximo absoluto de +/-15 V ou +/-30 V, mas isso e presuncao ate alguem ler a folha de dados do cartao — e essa e a parte que precisa de decisao humana.
 SE o cartao do cliente nao aceitar -11 V, o plano B e grampear o nivel de falha em -10,00 V (dentro da faixa publicada), aceitando explicitamente que a saida analogica deixa de distinguir "falha" de "-90 graus saturado", e transferindo toda a sinalizacao de falha para os reles e o display. Por isso o valor tem de ser uma constante de compilacao (kAoFaultCode), para permitir um build especifico de cliente sem tocar em logica.
 LIMITACAO QUE PERMANECE E TEM DE ESTAR NO MANUAL: em queda total de alimentacao a saida vai a 0,00 V, um valor legitimo. Nenhum nivel de falha em firmware resolve isso, porque nao ha firmware rodando. A deteccao de "UR sem energia" depende da polaridade de rele (item 3) ou de monitoramento externo.
 SEGUNDA LIMITACAO: em travamento sem queda de energia, a saida retem o ultimo valor por travamento + tWD (1,12 a 2,24 s) + bootloader (~300 ms) + 6 ms de setup ate o passo 5 — ou seja, ate ~2,6 s exibindo um angulo velho. O manual tem de declarar isso, e o intertravamento tem de usar tambem o contato de rele. A correcao definitiva e ECO de hardware (ligar o CLR# do DAC8562 ao reset do sistema).

## 2.5 Constantes globais

// ============================================================================
// ur_base.h - BASE COMUM da Unidade Remota DE-PURI-DI261924 REV A.
// Todas as 12 decisoes de projeto respeitam ESTES numeros. Onde duas decisoes
// discordaram, este arquivo e o desempate.
// Nada aqui e opiniao: cada constante traz a derivacao no comentario ou a
// marca A_MEDIR, que significa "nao entra em release sem o numero de bancada".
// ============================================================================
#pragma once

#include <stdint.h>

namespace urbase {

// ---------------------------------------------------------------------------
// 1. ENLACE RS-485 COM A PLACA SENSORA (Modbus RTU, escravo id 1, FC03)
//    Contrato de fio ja implementado em sensor/include/sensor_map.h.
// ---------------------------------------------------------------------------
constexpr uint32_t kLinkBaud            = 19200;  // manual + 500 m de cabo; nao subir sem reensaiar
constexpr uint8_t  kSlaveId             = 1;
constexpr uint16_t kPollStartReg        = 0;      // sensormap::kRegAngleX
constexpr uint16_t kPollRegCount        = 8;      // sensormap::kRegCount: uma unica transacao, sem
                                                  // poll rapido/lento separado (economizaria 5 ms
                                                  // num orcamento de 50 ms e criaria 2 contratos)

constexpr uint32_t kCharTimeUs          = 521;    // 10 bits / 19200 = 520,83 us
constexpr uint32_t kT35Us               = 1820;   // 3,5 caracteres; a sensora usa max(7*Tc/2, 750 us)
constexpr uint16_t kRequestBytes        = 8;      //  8 * 520,83 =  4166,7 us no fio
constexpr uint16_t kResponseBytes       = 21;     // 21 * 520,83 = 10937,5 us no fio (3 + 2*8 + 2)
constexpr uint32_t kWireTimeUs          = 15104;  // 4166,7 + 10937,5

// Latencia do escravo derivada do laco real (sensor/src/main.cpp:82-108):
// uart_read_bytes(cap=64, 2 ms) sempre queima os 2 ms; g_lastByteUs e carimbado
// no RETORNO da leitura, nao na chegada do byte.
constexpr uint32_t kSlaveLatencyMinUs   = 2050;   // 2000 (poll de silencio) + 50 (handle/CRC)
constexpr uint32_t kSlaveLatencyMaxUs   = 4550;   // 2000 (carimbo tardio) + 500 (laco) + 2000 + 50

constexpr uint32_t kRoundTripTypUs      = 17900;  // A_MEDIR (medicao 6): 15104 + 2500 + 300
constexpr uint32_t kRoundTripMaxUs      = 21300;  // A_MEDIR (medicao 6): 15104 + 4550 + 600 + 1000

constexpr uint32_t kLinkTimeoutMs       = 35;     // 1,64x o pior caso. 20 ms REPROVA transacao boa
                                                  // (pior caso 21,3 ms) e levaria os 4 reles a alarme.
constexpr uint32_t kPollPeriodMs        = 50;     // 20 Hz. Ocupacao do barramento 30 %; silencio
                                                  // entre transacoes >= 28 ms (15x o t3,5).
constexpr uint8_t  kFailsToFault        = 3;      // 150 ms ate declarar falha (7,5x abaixo do tWD min)
constexpr uint8_t  kGoodsToRecover      = 5;      // 250 ms para sair da falha
constexpr uint32_t kFaultMinDwellMs     = 2000;   // permanencia minima em falha (anti-flapping)
constexpr uint32_t kDataMaxAgeMs        = kPollPeriodMs + 22;  // 72 ms: idade maxima do dado que
                                                               // comanda rele

// Correcao obrigatoria no mestre: src/proto/modbus_rtu.h:17 tem kRegisterCount = 2
// e :22 tem kRxCap = 16. A resposta de 8 registradores tem 21 bytes e NAO CABE.
constexpr uint16_t kModbusRxCapBytes    = 32;
constexpr uint16_t kModbusTxCapBytes    = 16;

// ---------------------------------------------------------------------------
// 2. DONO DO CICLO DE SEGURANCA
//    Tarefa FreeRTOS propria. O loop() cooperativo NAO comanda rele nem DAC.
// ---------------------------------------------------------------------------
constexpr uint32_t kCtrlPeriodMs        = kPollPeriodMs;  // poll + filtro + limite + rele + DAC
constexpr uint8_t  kCtrlTaskPriority    = 5;      // loopTask do Arduino e prioridade 1
constexpr uint8_t  kCtrlTaskCore        = 0;      // APP_CPU livre (Wi-Fi fora do produto);
                                                  // a UART2 e instalada de dentro da tarefa
constexpr uint32_t kCtrlTaskStackBytes  = 4096;
constexpr uint32_t kFilterPeriodMs      = kCtrlPeriodMs;  // o filtro e especificado como CONSTANTE
                                                  // DE TEMPO em ms e convertido neste periodo.
                                                  // Fixo ou parametro de menu = DECISAO HUMANA
                                                  // (o texto do manual promete "adequar o tempo de
                                                  // resposta", mas a Tabela 1 nao lista o parametro).

// ---------------------------------------------------------------------------
// 3. WATCHDOG EXTERNO STWD100YNYWY3F (WDI em IO19)
//    O chute NAO pode sair de esp_timer com ESP_TIMER_TASK: essa tarefa executa
//    de flash e para durante o apagamento de setor da NVS (cache desabilitada).
//    Chute por ISR de timer de HARDWARE em IRAM, com GPIO.out_w1ts/out_w1tc.
// ---------------------------------------------------------------------------
constexpr uint32_t kWdtMinTimeoutMs     = 1120;   // tWD min do datasheet - o numero que manda
constexpr uint32_t kWdtTypTimeoutMs     = 1600;
constexpr uint32_t kWdtMaxTimeoutMs     = 2240;
constexpr uint32_t kWdtResetPulseMs     = 210;    // tPW
constexpr uint32_t kWdiIsrTickHz        = 1000;   // ISR em IRAM a 1 kHz
constexpr uint32_t kWdiKickPeriodMs     = 250;    // 4,48x de margem sobre o tWD minimo
constexpr uint32_t kWdiPulseMs          = 1;      // alto num tick, baixo no seguinte: sem busy-wait,
                                                  // 10.000x acima do glitch de 100 ns
constexpr uint32_t kCtrlLivenessDeadlineMs = 800; // a ISR PARA de pulsar se a tarefa ctrl nao
                                                  // renovar o token: o cachorro continua morrendo
                                                  // quando o firmware trava
constexpr uint32_t kBootWdiGapBudgetMs  = 500;    // orcamento declarado (2,24x sob o tWD minimo)
constexpr uint32_t kBootloaderDeadTimeMs = 300;   // A_MEDIR (medicao 5): reset liberado -> setup()
constexpr uint32_t kBootWdiGapMeasuredMs = 0;     // A_MEDIR (medicao 5): maior lacuna real no boot
constexpr uint32_t kNvsWdiGapMeasuredMs  = 0;     // A_MEDIR (medicao 4): maior lacuna em escrita NVS

// ---------------------------------------------------------------------------
// 4. ORCAMENTO DO setup() (ms). Sequencia canonica, um passo por constante.
// ---------------------------------------------------------------------------
constexpr uint32_t kBootStep01WdtMs      = 1;     // 1. WDI OUTPUT + primeiro pulso + arma a ISR
constexpr uint32_t kBootStep02RelaysMs   = 1;     // 2. 4 GPIOs de rele ao nivel de boot
constexpr uint32_t kBootStep03LedLigMs   = 1;     // 3. IO2 em nivel baixo (strapping)
constexpr uint32_t kBootStep04BootInfoMs = 1;     // 4. reset reason, strapping, IO15 (reset fabrica)
constexpr uint32_t kBootStep05DacMs      = 6;     // 5. HSPI + DAC8562 + codigo de falha + OP_MODE
constexpr uint32_t kBootStep06ConsoleMs  = 2;     // 6. Serial 115200
constexpr uint32_t kBootStep07NvsMs      = 60;    // 7. NVS: 60 tipico, 800 no pior caso (A_MEDIR)
constexpr uint32_t kBootStep07NvsWorstMs = 800;
constexpr uint32_t kBootStep08VspiMs     = 1;     // 8. SPI.begin(18,-1,23,-1): impede o U8g2 de
                                                  //    sequestrar o IO19 (MISO default do VSPI)
constexpr uint32_t kBootStep09DisplayMs  = 150;   // 9. u8g2.begin + clear + sendBuffer (A_MEDIR, 10)
constexpr uint32_t kBootStep10RearmMs    = 1;     // 10. rearmPin(): cinto-e-suspensorio
constexpr uint32_t kBootStep11ButtonsMs  = 1;     // 11. IO15/34/35
constexpr uint32_t kBootStep12Rs485Ms    = 5;     // 12. uart_driver_install + half-duplex
constexpr uint32_t kBootStep13CtrlMs     = 1;     // 13. cria a tarefa ctrl: seguranca fica VIVA aqui
constexpr uint32_t kBootBlockingTypMs    = 231;   // soma tipica
constexpr uint32_t kBootBlockingWorstMs  = 971;   // soma com NVS patologica
// Splash NAO BLOQUEANTE, depois do setup(), com a tarefa ctrl ja polando:
constexpr uint32_t kSplashSelftestMs     = 600;   // autoteste do display (manual secao 5)
constexpr uint32_t kSplashLogoMs         = 600;   // logomarca "exibida temporariamente"

// ---------------------------------------------------------------------------
// 5. CADEIA ANALOGICA BIPOLAR (DAC8562 -> XTR300, folha 2/2)
//    V_OUT = 5 * (V_DAC - 2,5) = 25*D/65536 - 12,5 V   [R_OS = 1K, R_GAIN = 10K,
//    R_SET AUSENTE, eq. (2) do SBOS336C]. Inverso: D = 32768 + 2621,44 * V_OUT.
//    include/board_pins.h:79 (kXtrRSetNominalOhms = 2500) e a equacao (1) do
//    datasheet: MODELO ERRADO para esta placa. APAGAR apos a medicao 1.
// ---------------------------------------------------------------------------
constexpr float    kAoChainGain          = 5.0f;  // A_MEDIR (medicao 1): 5 (esquematico) x 2
                                                  // (board_pins.h). Nao e disputa de opiniao.
constexpr uint16_t kDacZeroCode          = 32768; //  0x8000 ->   0,000 V EXATO
constexpr uint16_t kDacMinus10VCode      = 6554;  //  0x199A -> -10,000 V
constexpr uint16_t kDacPlus10VCode       = 58982; //  0xE666 -> +10,000 V
constexpr uint16_t kDacFaultCode         = 3932;  //          -> -11,000 V, FORA DE BANDA
constexpr int32_t  kDacCodesPerVoltQ1000 = 2621440; // 2621,44 codigos/V * 1000, para conta inteira:
                                                    // D = 32768 + (mV * 32768) / 12500
constexpr float    kDacLsbMicroVolt      = 381.47f; // 25 V / 65536
// PROIBIDO em qualquer caminho desta placa: 0x0000 vale -12,5 V (saturado em ~-12 V), aciona
// EFLD e, no modo corrente, pede -25 mA. src/drivers/xtr300.cpp:12 (kZeroCode) e
// src/drivers/dac8562.cpp:21 (kDataZero) TEM de deixar de ser 0x0000.
constexpr uint16_t kDacBootCode          = kDacFaultCode;  // sai do trilho negativo direto para o
                                                  // nivel de falha, sem passar por 0,00 V (que
                                                  // seria uma mentira de "estrutura nivelada")
constexpr int16_t  kAngleClampDeciDeg    = 900;   // grampo em +/-90,0 graus (manual 5.7 L176)
constexpr uint32_t kXtrSettleUs          = 500;   // Cc 47 nF sobre R_OS 1K: tau 47 us; 0,1 % em
                                                  // 0,25 ms; + ate 40 us internos do XTR300
constexpr float    kAoFaultVolts         = -11.00f; // A_APROVAR: fora dos -10..+10 V publicados em
                                                  // 2.1 L33, 6.2 L268 e Tabela 3. Exige (a) errata
                                                  // do manual e (b) confirmar que o cartao do CLP
                                                  // do cliente tolera -11 V. Plano B: -10,00 V,
                                                  // perdendo a distincao falha x -90 graus.
// NUNCA usar 0,00 V como nivel de falha: e a leitura legitima mais provavel E a assinatura fisica
// da queda de energia (os trilhos de +/-15 V colapsam e a saida vai a 0 V sozinha).

// ---------------------------------------------------------------------------
// 6. RELES DE LIMITE (RL2..RL5, BC337, bobina AX1RC-5V)
//    POLARIDADE = DECISAO HUMANA PENDENTE. Uma constante inverte tudo.
// ---------------------------------------------------------------------------
constexpr bool     kRelayFailSafePolarity = true; // A_APROVAR (bigboss). true = bobina energizada
                                                  // e o estado SAUDAVEL (unico canal que denuncia
                                                  // queda de energia; reescreve manual 5.9 e a
                                                  // legenda dos LEDs; exige medicoes 7, 8 e 9).
                                                  // false = fidelidade ao manual (acionado =
                                                  // energizado), sem consumo continuo, mas a UR
                                                  // sem energia reporta "sem alarme" em TODOS os
                                                  // canais de maquina.
// O jumper NA/NF (J10/J9/J8/J2, padrao NF) NAO substitui esta escolha: em qualquer posicao, o
// contato sem energia e igual ao contato em repouso.
constexpr bool     kRelayHealthyLevel    = kRelayFailSafePolarity;        // nivel do GPIO
constexpr bool     kRelayAlarmLevel      = !kRelayFailSafePolarity;
constexpr bool     kRelayBootLevel       = false; // LOW nas duas polaridades: e o estado de
                                                  // hardware do reset (pull-down de 1K na base)
constexpr float    kCoilOhmMeasured      = 0.0f;  // A_MEDIR (medicao 7); esperado 50..1000 ohm
constexpr float    kCoilCurrentMaMeasured = 0.0f; // A_MEDIR (medicao 7); previsto ~36 mA a 139 ohm
constexpr float    kBaseCurrentMa        = 0.25f; // A_MEDIR (medicao 7): (3,3-0,7-0,7)/2K - 0,7/1K
constexpr float    kHfeRequiredMax       = 150.0f;// reprova acima disto (BC337 no limite)
constexpr float    kBc337VceSatMaxV      = 0.4f;  // A_MEDIR (medicao 8), so se a opcao B for aprovada
constexpr float    kSupplyBudgetW        = 5.0f;  // manual L29 e L315
constexpr float    kSupplyAcceptanceW    = 4.0f;  // A_MEDIR (medicao 9): 20 % de margem

// ---------------------------------------------------------------------------
// 7. RECOMENDACOES DE ESCOPO QUE A BASE ASSUME (decisao humana)
// ---------------------------------------------------------------------------
#define UR_WIFI_ENABLED 0   // A_APROVAR: src/net fora do produto de aplicacao. Dezenas de KB de
                            // RAM, bloqueio nao caracterizado no laco e superficie de ataque num
                            // equipamento de seguranca. Se ficar, roda na core 1 e NUNCA na ctrl.

}  // namespace urbase


---

# Parte 3 — As 12 decisoes

## Decisao 1 - A tecla ▲ no Modo Normal: gesto de PSET armado e gesto de Reset Geral

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto  
**REQ afetados:** MAN-2.1-L40, MAN-5.2-L93..94, MAN-5.4-L127, MAN-5.6-L149, MAN-5.6-L155..157, MAN-5.6-L161, MAN-5.6-L162, MAN-5.7-L185, MAN-5.8-L199, MAN-5.9-L213, MAN-5.9-L223, MAN-5.11-L240, MAN-5.11-L243..248, TAB1-L110..128, TAB2-L250..268, HW-board_pins.h:27 (kBtnUp = IO15), HW-board_pins.h:44 (kStrappingPins inclui 15), HW-buttons.h:13 (kBtnDebounceMs = 20), SENSOR-sensor_map.h:11 (kRegStatus), SENSOR-tilt.h:15..21, NRM-01, NRM-02, PST-01..03, RST-01, RST-02, DIR-01, DIR-02

### O que o manual diz

Todas as citacoes sao de `/home/ubuntu/repos/SUI/docs/manual-cliente-sui-2026.txt`.

- 5.2, L93-94: "Tecla ▲" / "Não possui função neste modo de operação."
- 5.6, L149: "A configuração do Preset é realizada em duas etapas: programação do valor de referência e ativação do Preset."
- 5.6, L155-157: "Utilize a tecla MENU para selecionar o dígito a ser alterado." / "Utilize a tecla ▲ para modificar o valor do dígito selecionado." / "Utilize a tecla ▼ para alterar o sinal (+ ou –) do valor programado."
- 5.6, L161: "Após programar o valor de referência, posicione o equipamento monitorado na posição correspondente a esse valor e execute o comando de preset por meio de um duplo acionamento da tecla ▲ (PSET). O display piscará, indicando que o comando foi aceito, e a leitura passa a ser apresentada em relação ao valor programado."
- 5.7, L185: "Inclinações superiores ao fundo de escala mantêm a saída saturada em ±10,00 Vcc."
- 5.9, L213: "Utilize a tecla MENU para selecionar o dígito desejado, as teclas ▲ e ▼ para alterar seu valor e a tecla ▼ para definir o sinal (+ ou –) do ângulo programado."
- 5.9, L223: "Importante: Os valores programados nos Limites 1 a 4 são sempre expressos em graus, com resolução de 0,1°, e referem-se à leitura apresentada no display, ou seja, já consideram o Preset e o Sentido do Sensor configurados para o respectivo eixo."
- 5.11, L240: "Além dos parâmetros de programação, o procedimento também restaura os ajustes de calibração das saídas analógicas realizados durante o processo de fabricação."
- 5.11, L243, L245, L247: "Com o equipamento desligado, pressione e mantenha pressionada a tecla ▲." / "Aguarde a exibição da mensagem \"RESET DE FABRICA\" no display." / "Após a exibição da mensagem, libere a tecla ▲."
- 2.1, L40: "Restauração dos parâmetros de fábrica por meio das teclas de programação;" (plural)
- Tabela 2, L256-263: apos o Reset Geral, Operacao Limite 1 = "+ (módulo)" com Limite 1 = "+005,0°", Operacao Limite 2 = "Off", Operacao Limite 3 = "+ (módulo)" com Limite 3 = "+005,0°", Operacao Limite 4 = "Off".

### A lacuna

O manual nega funcao a ▲ no Modo Normal (L94) e, em duas outras secoes, entrega a ela as duas acoes mais destrutivas do produto: efetivar o PSET, que pela L223 desloca simultaneamente os quatro pontos de atuacao de rele, e o Reset Geral, que pela Tabela 2 leva dois dos quatro limites para Off e repoe os outros dois em +005,0° modulo. Ficam sem contrato: (a) em que modo o duplo toque e aceito; (b) a temporizacao do duplo toque; (c) se ▲ sozinha faz algo; (d) a duracao e a frequencia do "piscará" da L161; (e) se o PSET e aceito com dado congelado, com dado velho ou com a estrutura vibrando; (f) se o PSET vale para um eixo ou para os dois; (g) o que impede o gesto de ser executado com a estrutura FORA da posicao de referencia, que e o unico erro que corrompe a referencia de forma silenciosa e permanente; (h) o tempo de retencao de ▲ na energizacao, o que fazer com tecla presa e se o reset ocorre sem a soltura; (i) o estado dos reles e da saida analogica durante toda a janela do gesto de reset; (j) se o gesto e de uma tecla ou combinacao (L40 diz "teclas").

### Proposta

**A. Escopo e teclas**

1. DESVIO DO MANUAL (5.2, L94): ▲ TEM funcao no Modo Normal. E a tecla de PSET. A L94 e uma tabela descritiva; a L161 e um procedimento operacional que exige o equipamento posicionado e a leitura na tela — condicao que so existe no Modo Normal. Procedimento vence descricao. O manual tem de ser corrigido na L94 para "Tecla ▲ — Executa o comando de Preset (PSET) por duplo acionamento, conforme o item 5.6."
2. Toque simples de ▲ no Modo Normal: NENHUMA acao. Preserva a intencao da L94 e impede efeito por toque acidental isolado.
3. Esta decisao NAO institui regra universal de edicao. ▼ permanece a tecla de sinal nos editores de Preset e de Limite, conforme L157 e L213, sob pena de a faixa -90,0 a +90,0° da Tabela 1 virar inatingivel. O contrato completo do editor de campo e da decisao 3 (menu); aqui fica travado apenas que ▼ nao pode perder a funcao de sinal.
4. O gesto de Reset Geral e de tecla UNICA (▲). DESVIO DO MANUAL (2.1, L40): o plural "teclas" e corrigido para "tecla". Justificativa de hardware, nao de gosto: ▲ e kBtnUp = IO15 (`include/board_pins.h:27`), o unico dos tres botoes com pull-up interno; IO34 (▼) e IO35 (MENU) sao input-only e ignoram INPUT_PULLUP (`src/drivers/buttons.h:2`). Com o cabo do painel solto, ▲ le SOLTA e o reset nao dispara. Qualquer combinacao que envolva ▼ ou MENU faz o reset depender de um nivel indefinido.

**B. Amostragem das teclas — EMENDA DECLARADA A BASE COMUM**

5. CONTRADIZ A BASE COMUM (secao "DONO DO CICLO", que atribui os botoes ao `loop()`), de forma estreita e deliberada: a AMOSTRAGEM dos tres botoes sai do `loop()` e passa a uma tarefa FreeRTOS "btn" — core 1, prioridade 3, stack 2048 B, cadencia por `vTaskDelayUntil` de 5 ms — que aplica os 20 ms de debounce (`kBtnDebounceMs`, `src/drivers/buttons.h:13`) e enfileira cada borda com carimbo de `millis()`. A MAQUINA DE ESTADOS da IHM continua no `loop()` e consome bordas ja carimbadas. Motivo: no `loop()` a granularidade de qualquer gesto com numero e igual ao pior bloqueio do laco — ate 150 ms de `u8g2_.begin()` e ate 10 ms de `sendBuffer` (base comum, passo 9 e medicao 10) e a janela de cache-off da NVS. Uma prensagem de 30 ms seria simplesmente perdida, e nenhum dos numeros de C e D abaixo teria significado. A tarefa "btn" nao toca rele, DAC nem NVS, entao nao invade a exclusividade da tarefa ctrl.

**C. Gesto de PSET no Modo Normal**

6. ARMAMENTO. DESVIO DO MANUAL (5.6, L161, que nao pede armamento): o duplo toque so e aceito se, no ciclo de energizacao corrente, o operador tiver saido do Modo Programacao apos visitar a tela "Preset X" ou "Preset Y". A validade do armamento e de 120000 ms contados a partir da saida do Modo Programacao. Fora dessa janela, o duplo toque nao faz absolutamente nada e nao exibe nada. Alinhamento com o manual: a propria L149 define o PSET como a segunda de duas etapas cuja primeira e a programacao do valor; o armamento apenas torna essa sequencia obrigatoria em vez de recomendada. O numero 120000 ms espelha o unico timeout de IHM que o manual publica (~2 minutos, 5.3 L96 e 5.4 L127), entao o operador nao aprende temporizacao nova.
7. GESTO, medido sobre os carimbos da tarefa "btn", ja depois do debounce de 20 ms: duas prensagens completas de ▲, cada uma com 30 ms <= t <= 600 ms; intervalo entre a soltura da primeira e a prensagem da segunda <= 400 ms; duracao total do gesto <= 1600 ms. Qualquer borda de ▼ ou de MENU dentro da janela anula o gesto. Um terceiro toque dentro da janela tambem anula, para que repique mecanico nao vire PSET.
8. O PSET vale para os DOIS EIXOS ao mesmo tempo, com P_X e P_Y ja gravados. Justificativa: a L161 manda posicionar o equipamento monitorado — uma unica posicao fisica define as duas referencias, e a L161 usa um unico gesto. Aplicacao atomica dos dois offsets.
9. GUARDA DE DADO VALIDO: o gesto so e aceito se a idade da ultima transacao Modbus valida for <= 72 ms (kDataMaxAgeMs da base comum) E o registrador 3 valer EXATAMENTE 0x0001. Motivo verificado no codigo da sensora: em falha de leitura do SCL3300 ela faz `g_registers[kRegStatus] |= kStsSclNotResponding` sem limpar `kStsDataValid` e sem atualizar os angulos (`sensor/src/main.cpp:151-159`), publicando 0x0011 sobre angulos congelados. Presetar sobre angulo congelado corrompe a referencia dos quatro limites em silencio. A exigencia de 0x0001 exato tambem barra `kStsSaturated` (0x0020) e `kStsSclStartup` (0x0004), e e alcancavel porque `publishTilt` sobrescreve o status inteiro a cada leitura boa (`sensor/src/main.cpp:67-76`).
10. GUARDA DE ESTABILIDADE: buffer circular estatico de 8 amostras por eixo, alimentado pela tarefa ctrl a cada 50 ms — janela de 400 ms. Custo 2 x 8 x int16 = 32 bytes, sem heap. O gesto so e aceito se o pico-a-pico das 8 amostras for <= 5 decimos de grau (0,5°) NOS DOIS EIXOS, calculado como `max - min` em int16. Recusa exibe, por 3000 ms, o pico-a-pico medido: `Instavel! ppX:000,8 ppY:000,3` (DESVIO DO MANUAL: tela inventada, sem acentuacao, no padrao das telas do manual).
11. GUARDA DE MAGNITUDE. DESVIO DO MANUAL (5.6, L161, que nao preve confirmacao): se `max(|offsetX_novo - offsetX_vigente|, |offsetY_novo - offsetY_vigente|) > 50` decimos de grau (5,0°), o PSET NAO e aplicado pelo duplo toque. A UR exibe `Novo PSET X:-012,0` na primeira linha e `Segure MENU 3s` na segunda (DESVIO: telas inventadas) e so aplica apos hold de MENU por 3000 ms — o mesmo gesto de confirmacao que o manual ja usa em 5.4, 5.7, 5.8 e 5.10. Sem confirmacao em 10000 ms, cancela sem aplicar. Este e o unico item que enderecca o erro real: gesto executado com a estrutura fora da posicao de referencia. Um deslocamento de mais de 5,0° na referencia nao e ajuste fino, e sempre suspeito de posicao errada, e no comissionamento legitimo custa uma confirmacao unica.
12. RECUSA POR DADO: quando a guarda 9 reprova, exibe `PSET recusado!` por 3000 ms (DESVIO: tela inventada).
13. MATEMATICA, INTEIRA E EXATA, sem float em nenhum ponto do caminho de rele. Tudo em int16 de decimos de grau. Ordem de aplicacao fixada aqui e a ser repetida identica na decisao dona de DIR-01/02: `leitura = clamp(dir * bruto + offset, -900, +900)`, com `dir` em {+1, -1}. No aceite do gesto: `offset := P - dir * bruto`, por eixo. Somente subtracao e troca de sinal, logo o erro de arredondamento introduzido pelo PSET e 0,0°.
14. FAIXAS, todas dentro de int16: `bruto` em [-900, +900]; `P` em [-900, +900] (Tabela 1); `offset` em [-1800, +1800]; `dir*bruto + offset` em [-2700, +2700]; leitura exibida e comparada em [-900, +900]. No instante do aceite a soma vale exatamente P, entao nao ha saturacao possivel no aceite.
15. O COMPARADOR DE RELE USA A LEITURA GRAMPEADA em +/-90,0°, a MESMA exibida no display, conforme L223 e o grampo de L185. O rascunho pedia comparar sobre o valor nao saturado; ficou derrubado (ver secao seguinte). O tratamento da condicao "fora de faixa acima de 90,0°" nao pertence a esta decisao e fica com a decisao 11.
16. ACEITE VISUAL: o campo de medicao pisca 3 ciclos de 200 ms aceso / 200 ms apagado, total 1200 ms — implementa literalmente "O display piscará" da L161. Os reles continuam sendo avaliados durante o piscar.
17. INDICACAO PERMANENTE. DESVIO DO MANUAL (5.2, que nao especifica o layout da tela principal): enquanto o offset do eixo exibido for diferente de zero, a tela principal mostra em permanencia `PSET X:-012,0` (formato +/-XXX,X da L140), acompanhando a selecao de eixo feita por ▼ (NRM-02). Motivo: o piscar de 1200 ms desaparece, e o operador do turno seguinte precisa saber que a leitura e relativa e de quanto.
18. APLICACAO ATOMICA. O par (offsetX, offsetY) e publicado do `loop()` para a tarefa ctrl por fila/portMUX como uma unica estrutura. A tarefa ctrl aplica os dois offsets, recalcula as duas leituras, reavalia os quatro limites e escreve os quatro GPIOs de rele DENTRO DO MESMO tick de 50 ms. Nao existe tick em que um eixo esteja na referencia nova e o outro na velha, nem em que um rele tenha sido escrito com a leitura nova e outro com a velha. A nova referencia entra em vigor em no maximo 50 ms (kCtrlPeriodMs) apos o aceite, nao em 10 ms.
19. PERSISTENCIA: os dois offsets e os dois P sao gravados na NVS pelo `loop()` imediatamente apos a publicacao do item 18 — publica primeiro, grava depois. Assim a leitura e os reles mudam em <= 50 ms independentemente da latencia da flash, e o que sobrevive a um reset do STWD100 e o que o operador acabou de ver. A tarefa ctrl tolera o tick perdido pela cache-off, dentro dos 3 ciclos ate declarar falha (base comum).
20. INTERACAO COM O SENTIDO DO SENSOR. DESVIO DO MANUAL (5.8, L199, que apenas "recomenda" refazer o Preset): ao gravar uma mudanca de `Sentido Sensor X` ou `Sentido Sensor Y`, o firmware zera o offset do eixo correspondente e o desarma. Motivo: o offset e definido contra o `dir` vigente; manter um offset calculado com o sinal oposto desloca os pontos de atuacao daquele eixo em ate 180,0° sem nenhum indicio. Zerar e deterministico e visivel — o marcador do item 17 some da tela.

**D. Reset Geral (5.11), temporizacao fechada e NAO BLOQUEANTE**

21. O gesto NAO bloqueia o boot. IO15 e amostrado no passo 4 da ordem de boot canonica (captura de boot, ~t = 2 ms). Se ▲ estiver prensada, arma-se apenas um flag; o `setup()` prossegue integralmente — reles no nivel de boot (passo 2), DAC no codigo de falha 3932 = -11,00 V (passo 5), NVS lida (passo 7), tarefa ctrl criada (passo 13). Isto responde a lacuna (i): durante TODA a janela do gesto a tarefa ctrl ja esta polando a 50 ms, avaliando os quatro limites e comandando os quatro reles, exatamente como em operacao normal. Nenhum passo desta decisao acrescenta lacuna de WDI, porque nenhum passo desta decisao bloqueia.
22. ABORTO POR ASSINATURA DE CABO: se ▼ ou MENU tambem lerem prensadas na amostragem do passo 4, o gesto e abortado e o boot segue normal. Tres teclas prensadas na energizacao e cabo em curto, nao operador.
23. CONFIRMACAO: exige ▲ prensada CONTINUAMENTE, verificada pela tarefa "btn" a 5 ms, desde o passo 4 ate t = 3000 ms contados da entrada no `setup()`. Solturas anulam. Os 3000 ms espelham o hold de 3 s que o manual ja usa em 5.2, 5.4, 5.7, 5.8 e 5.10.
24. O splash da base comum roda normalmente durante a espera (600 ms de autoteste + 600 ms de logomarca). Em t = 3000 ms a tela e substituida por `RESET DE FABRICA` — string byte a byte da L246, caixa alta, sem acento — exibida por no minimo 2000 ms.
25. EXECUCAO SO NA SOLTURA, conforme L247. O apagamento e a regravacao dos defaults da Tabela 2 ocorrem quando ▲ e SOLTA, nunca antes.
26. TECLA PRESA: se ▲ continuar prensada 10000 ms apos o surgimento da mensagem (t = 13000 ms), o reset e ABORTADO, exibe-se `TECLA PRESA` por 3000 ms (DESVIO: tela inventada) e o boot segue normal SEM apagar nada.
27. CORRECAO DE PREMISSA: o Reset Geral NAO apaga a calibracao analogica de fabrica. A L240 diz "restaura os ajustes de calibração das saídas analógicas realizados durante o processo de fabricação" — restaurar, nao apagar. Esta decisao passa a concordar com a decisao 9 item 11 (cal_fab imutavel, copiada de volta) e retira a justificativa errada do rascunho. A guarda 26 permanece, com justificativa mais forte: pela Tabela 2 (L256-263) o Reset leva `Operação Limite 2` e `Operação Limite 4` para Off e repoe os Limites 1 e 3 em "+ (módulo)" +005,0°. Uma tecla ▲ em curto desativaria dois dos quatro reles de seguranca e reapontaria os outros dois A CADA ENERGIZACAO, e o equipamento se degradaria a cada queda de energia sem ninguem perceber. Isso e pior do que perder calibracao analogica.
28. Apos executar o Reset Geral, a nova Tabela 2 e publicada para a tarefa ctrl como um unico conjunto (mesmo mecanismo do item 18), e o manual tem de passar a exigir recomissionamento do intertravamento, porque dois limites saem desativados.
29. IO15 E PINO DE STRAPPING (`include/board_pins.h:44`, `kStrappingPins` inclui 15). Manter ▲ prensada na energizacao mantem IO15 em nivel BAIXO no reset, o que silencia o log de boot da ROM do ESP32 na U0TXD. Nao impede o boot e nao muda o modo de operacao. Efeito ACEITO e declarado: durante um Reset Geral nao ha log de ROM; o console de 115200 do passo 6 do boot continua funcionando normalmente.

### Por que

O PSET e o Reset sao as duas operacoes que movem, de uma so vez, os quatro pontos de atuacao de rele de um equipamento de seguranca portuario, e o manual entrega as duas a uma tecla que ele proprio declara sem funcao. O nucleo da proposta e que essas duas operacoes deixem de ser executaveis por um gesto solto: o PSET passa a exigir intencao previa registrada (armamento, item 6), dado comprovadamente vivo (item 9), estrutura parada (item 10) e confirmacao explicita quando a correcao e grande (item 11); o Reset passa a exigir 3000 ms de retencao continua e a soltura da tecla (itens 23 e 25). Tudo o mais e consequencia de aritmetica inteira e da base de tempo de 50 ms ja fixada.

### O que a revisao adversarial derrubou

**Cedido a lente de seguranca:**

- *"As guardas protegem o perigo errado."* Correto e decisivo. As guardas 4 e 5 do rascunho passam limpas no caso do guindaste parado a 12,0° com P = 0,0. A alternativa (b) do rascunho foi descartada sobre premissa falsa. Cedido nos itens 6 (armamento) e 11 (confirmacao acima de 5,0°), que sao exatamente as correcoes (a) e (c) da critica. A confirmacao nao vira tela obrigatoria em todo PSET — so acima de 5,0° — para preservar a L161 no ajuste fino, que e o caso de uso frequente.
- *"1,2 s de pisca-pisca em OLED sob sol de cais como unica evidencia."* Correto. Cedido no item 17: indicacao permanente do offset em vigor na tela principal, mantendo tambem o piscar da L161.
- *"0,2 grau pico-a-pico e constante congelada sem medida."* Correto, e a propria critica de completude repete. O quantum da sensora e 0,1° e o dithering de quantizacao sozinho consome 2 decimos. Cedido: limiar unico de 5 decimos (0,5°), janela expressa em ms (400 ms), pico-a-pico medido exibido na tela de recusa, e medicao de bancada exigida para confirmar o numero contra a vibracao real.
- *"A justificativa da guarda 10 e anulada pela decisao 9 item 11."* Correto. Cedido no item 27: a L240 diz restaurar, nao apagar; a guarda muda de justificativa e fica mais forte (dois limites vao para Off pela Tabela 2).
- *"Nenhuma palavra sobre reles e saida analogica entre o reset e t = 3000 ms."* Correto e era a lacuna mais grave do rascunho. Cedido no item 21: o gesto deixa de ser bloqueante, a tarefa ctrl esta viva desde o passo 13 do boot e a saida analogica esta em -11,00 V desde o passo 5. A janela deixa de existir como janela.

**Refutado da lente de seguranca:**

- *"Forcar os quatro reles ao estado de ALARME durante a aplicacao do PSET e sustentar 500 ms."* Refutado. A premissa e que a transicao nao tem estado definido; ela tem. Pelo item 18 a aplicacao dos dois offsets, o recalculo das duas leituras e a escrita dos quatro GPIOs acontecem dentro de um unico tick da tarefa ctrl, sob portMUX. Nao existe tick com estado misto. E o salto nao e um dado ruim: apos o aceite a leitura vale exatamente P, que e a leitura verdadeira na nova referencia — os reles passam de "corretos na referencia velha" para "corretos na referencia nova", sem instante intermediario. Alem disso, comutar quatro saidas de seguranca para alarme sem que nada tenha acontecido na estrutura e, num cais, capaz de derrubar carga ou disparar parada de emergencia: e criar um perigo para cobrir um perigo que a atomicidade ja cobre. O que se aceita da critica e a parte documental: o manual passa a exigir que o laco de intertravamento daquele equipamento esteja em manutencao durante o PSET.
- *"Armar tambem por acesso ao Modo Programacao com senha no ciclo corrente."* Parcialmente refutado. Entrar no Modo Programacao para mexer na senha ou num limite nao e intencao de presetar. O armamento fica restrito a passagem pela tela "Preset X" ou "Preset Y" (item 6), que e a etapa 1 que a propria L149 exige.

**Cedido a lente de fidelidade:**

- *"A regra unica apaga a tecla de sinal e torna Preset e Limite negativos improgramaveis."* Correto e grave: sem ▼ como sinal, a faixa -90,0 a +90,0 da Tabela 1 fica inatingivel. Cedido no item 3: esta decisao nao institui regra universal nenhuma; ▼ permanece o sinal por L157 e L213, e o contrato do editor fica com a decisao 3.
- *"O comparador sobre a leitura nao saturada contradiz a L223, e a justificativa (limite >= 95,0°) e impossivel pela Tabela 1."* Correto nas duas partes. Cedido no item 15. Verificacao adicional que confirma a cedencia: com os limites travados em [-90,0; +90,0], o grampo nunca suprime uma atuacao — para "≥", uma leitura real acima de +90,0 grampeia em +90,0 e continua satisfazendo qualquer limite <= +90,0; para "+ (módulo)", |+90,0| continua >= qualquer |limite|; o unico efeito residual do grampo e fazer "≤ +90,0" atuar com leitura real de +150,0, o que erra para o lado do alarme.
- *"20 amostras a 100 Hz e a nova referencia em <= 10 ms usam uma taxa que nao existe."* Correto: 10 ms e o laco interno da sensora (`sensor/src/main.cpp`, kTiltPeriodMs). Cedido nos itens 10 e 18, agora ancorados na base comum: poll de 50 ms, janela de estabilidade de 400 ms = 8 amostras, referencia em vigor em <= 50 ms. A critica estimou ~55-65 Hz para o enlace; esse numero tambem esta errado, e a base comum o corrige — o round-trip e de 17,9 ms tipico e 21,3 ms de pior caso, mas a cadencia adotada e 50 ms (20 Hz) por decisao de ocupacao de barramento, nao pelo tempo de fio.
- *"As telas inventadas nao vieram marcadas como desvio."* Correto. Cedido: as quatro telas novas (`PSET recusado!`, `Instavel! ppX:000,8 ppY:000,3`, `Novo PSET X:-012,0` + `Segure MENU 3s`, `TECLA PRESA`) estao explicitamente marcadas DESVIO DO MANUAL e escritas sem acentuacao, no mesmo padrao de `Alteracao bem sucedida!` (L184) e `RESET DE FABRICA` (L246).
- *"Citacoes deslocadas."* Correto. Todas as citacoes foram refeitas contra `/home/ubuntu/repos/SUI/docs/manual-cliente-sui-2026.txt`: 5.2 em L93-94, PSET em L161, calibracao analogica em L240, procedimento de reset em L243-247, plural em L40.
- *"IO15 e strapping."* Correto, `board_pins.h:44` inclui 15. Cedido e documentado no item 29.

**Cedido a critica de completude:**

- *"30..600 ms por toque e 400 ms de intervalo nao vem de medida."* Parcialmente cedido. Os numeros permanecem, mas deixam de ser afirmacao vazia: o item 5 cria a tarefa "btn" a 5 ms, de modo que 30 ms sao 20 ms de debounce mais duas amostras — o menor gesto que a cadeia consegue reconhecer com determinismo. Sem essa tarefa, os mesmos numeros teriam granularidade de ate 150 ms e seriam ficcao, exatamente como a critica apontou no risco "Granularidade e responsividade da IHM". A validacao da nao perda de prensagem entra como medicao de bancada.

### Precisa de decisao humana

1. **Existencia do armamento do gesto de PSET (item 6).** Opcao A: sem armamento, duplo toque sempre aceito — fidelidade literal a L161, mas o gesto fica disponivel a qualquer momento a qualquer pessoa no painel. Opcao B: armado por passagem em "Preset X"/"Preset Y" no ciclo corrente, validade 120000 ms. RECOMENDACAO: opcao B. A L149 ja define o PSET como etapa 2 de duas; o armamento so torna a etapa 1 obrigatoria. Exige nota no 5.6.
2. **Janela de armamento.** Opcoes: 60000 ms, 120000 ms, sem timeout. RECOMENDACAO: 120000 ms, por espelhar os ~2 minutos de L96 e L127 — o unico timeout de IHM ja publicado. Se o posicionamento da estrutura levar mais que isso em campo, o operador reentra no Modo Programacao; friccao recuperavel, ao contrario de referencia corrompida.
3. **Confirmacao por hold de MENU quando o deslocamento da referencia excede 5,0° (item 11).** Opcao A: sem confirmacao, fidelidade a L161. Opcao B: confirmacao acima de 50 decimos. RECOMENDACAO: opcao B. Exige acrescentar um paragrafo ao 5.6.
4. **Limiar de estabilidade em 5 decimos (0,5°) sobre 400 ms (item 10), fixo em firmware.** Opcao alternativa: parametro de menu, o que o acrescenta a Tabela 1 e a Tabela 2 e muda o manual. RECOMENDACAO: fixo, revisto apos a medicao de vibracao. Um parametro a mais no menu de um equipamento de seguranca e um parametro a mais para alguem afrouxar em campo.
5. **Indicacao permanente do offset na tela principal (item 17).** Consome area da tela principal, que ainda nao tem layout contratado (NRM-02 sem dono). RECOMENDACAO: adotar. Sem ela, a leitura relativa e indistinguivel da absoluta para quem chega depois.
6. **PSET aplica os dois eixos de uma vez (item 8) ou so o eixo exibido.** RECOMENDACAO: os dois, porque a L161 manda posicionar o equipamento uma unica vez e usa um unico gesto.
7. **Zerar o offset ao trocar o Sentido do Sensor (item 20).** Opcao A: zerar, com o marcador da tela sumindo. Opcao B: manter o offset e apenas avisar, como a L199 recomenda. RECOMENDACAO: opcao A; a opcao B deixa os quatro pontos de atuacao deslocados em ate 180,0° sem indicio. Tem de ser repetido identico na decisao dona de DIR-01/02.
8. **Assinar que o Reset Geral restaura, e nao apaga, a calibracao analogica de fabrica (item 27),** encerrando a contradicao RST-02 entre esta decisao e a decisao 9. RECOMENDACAO: assinar como restaura, que e a leitura literal de L240.
9. **Aborto por tecla presa apos 13000 ms (item 26).** Se existir procedimento de fabrica com jig prendendo ▲, o reset nunca completa e a placa sai de linha com parametros de usuario. RECOMENDACAO: manter o aborto e proibir jig que prenda ▲ no roteiro de fabrica.
10. **Emenda a base comum: tarefa "btn" a 5 ms no core 1, prioridade 3 (item 5),** contra a base que atribui os botoes ao `loop()`. RECOMENDACAO: aprovar; sem ela nenhum numero de gesto desta decisao e verificavel.

### Precisa de medicao de bancada

1. **MEDICAO 11 da base comum (nivel de repouso dos botoes), BLOQUEANTE para esta decisao inteira.** IO34 e IO35 sao input-only e nao ha pull-up externo na placa mae; se qualquer um flutuar, os 20 ms de debounce, os 400 ms de intervalo, os 3000 ms de retencao e os 13000 ms de aborto perdem significado. Aceitacao ja fixada na base: >= 2,5 V solto e <= 0,3 V prensado, estavel, com a placa frontal conectada. Acrescentar: medir tambem IO15 durante os 13000 ms do gesto de reset, com o painel conectado, para confirmar que o pull-up interno sozinho sustenta o nivel.
2. **MEDICAO 14 (nova) - PICO-A-PICO REAL DO ANGULO EM REGIME, valida o limiar de 5 decimos do item 10.** Com a UR e a sensora montadas no equipamento real, registrar por console 100 janelas de 400 ms (8 amostras a 50 ms) em tres condicoes: estrutura parada e cais em silencio; estrutura parada com equipamento adjacente em operacao; estrutura parada com o proprio equipamento em marcha lenta. Registrar o pico-a-pico de X e de Y em cada janela. ACEITACAO: percentil 95 do pico-a-pico <= 5 decimos nas duas primeiras condicoes. Se reprovar, o limiar sobe para o percentil 99 medido e o numero entra no relatorio, nunca por estimativa. Se aprovar com folga grande (percentil 95 <= 2 decimos), o limiar pode ser reapertado.
3. **MEDICAO 15 (nova) - GRANULARIDADE DO GESTO SOB CARGA, valida os 30 ms, 400 ms e 1600 ms do item 7.** Com a tarefa "btn" a 5 ms implementada, injetar 1000 prensagens de 30 ms e 1000 duplos toques com 400 ms de intervalo por gerador de pulsos ligado ao pino do botao, com a UR simultaneamente redesenhando o display em laco continuo e executando escritas de NVS que forcem apagamento de setor. ACEITACAO: zero prensagens de 30 ms perdidas e erro de carimbo de tempo das bordas <= 10 ms em 1000 gestos. Se reprovar, a cadencia da tarefa "btn" cai de 5 ms para 2 ms ou a amostragem vai para ISR de timer.
4. **MEDICAO 5 da base comum (lacuna de WDI no boot), ESCOPO AMPLIADO.** Incluir, entre os 20 boots exigidos, tres boots com o gesto de Reset Geral: um com ▲ solta em t = 3100 ms (reset executado, com a gravacao dos defaults da Tabela 2 na NVS), um com ▲ solta em t = 1500 ms (gesto abortado) e um com ▲ prensada por 15000 ms (aborto por tecla presa do item 26). ACEITACAO inalterada: nenhum intervalo entre pulsos de WDI acima de 500 ms em nenhum instante, inclusive durante a regravacao dos defaults.

---

## Decisao 2 - Momento da gravacao e momento da efetivacao dos parametros

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto  
**REQ afetados:** MAN-2.1-L41, MAN-3-L56, MAN-5.4-L110, MAN-5.4-L111, MAN-5.4-L134, MAN-5.4-L136, MAN-5.4-L137, MAN-5.6-L161, MAN-5.6-L162, MAN-5.7-L182, MAN-5.8-L198, MAN-5.9-L232, MAN-5.10-L235, MAN-5.10-L237, MAN-5.11-L239, MAN-5.11-L240, MAN-5.11-L249, MAN-TAB2-L250..L267, MAN-7-L308, MAN-7-L309, PWD-01, PWD-05, RST-02

### O que o manual diz

O manual afirma tres coisas incompativeis sobre o mesmo evento.

(a) Gravacao no ato da confirmacao. Item 5.4, L110: "Apos concluir a alteracao, mantenha a tecla MENU pressionada por aproximadamente 3 segundos para confirmar e gravar o novo valor."; L111: "Os parametros configurados sao armazenados na memoria EEPROM do microcontrolador, garantindo sua preservacao mesmo na ausencia de alimentacao eletrica." O mesmo verbo aparece em 5.7 passo 9 (L182: "Mantenha a tecla MENU pressionada por aproximadamente 3 segundos para gravar a calibracao na memoria EEPROM."), em 5.8 passo 5 (L198: "...para gravar o parametro na memoria EEPROM.") e em 5.10 (L235: "...mantenha a tecla MENU pressionada por aproximadamente 3 segundos para gravar a nova senha na memoria EEPROM.").

(b) Gravacao so na saida do modo. Item 5.4, L136: "A Unidade Remota tambem pode sair automaticamente do Modo Programacao por timeout, quando nenhuma tecla for acionada durante aproximadamente 2 minutos. Ao concluir a saida do modo de programacao, o equipamento grava na memoria EEPROM todos os parametros alterados, preservando as configuracoes mesmo apos o desligamento da alimentacao."

(c) Os dois ATENCAO, que sao textos distintos e frequentemente citados trocados. 5.4, L137: "ATENCAO: Em caso de falha de energia antes da gravacao na EEPROM, as alteracoes realizadas nao serao armazenadas. Nessa situacao, os parametros modificados deverao ser reprogramados apos o restabelecimento da alimentacao." Item 7, L309: "ATENCAO: Caso ocorra uma falha de energia antes da gravacao dos novos valores na EEPROM, ou seja, antes do retorno ao Modo Normal, sera necessario reprogramar os parametros alterados."

Contexto que a decisao tem de respeitar: L41 ("Retencao permanente dos parametros e configuracoes em memoria EEPROM, dispensando o uso de bateria de backup"); L56 ("O microcontrolador possui memoria EEPROM interna para armazenamento nao volatil dos parametros de configuracao..."); L134 ("Para retornar ao Modo Normal, selecione o parametro SAIR e clique na tecla MENU."); L161 (PSET: "...execute o comando de preset por meio de um duplo acionamento da tecla ▲ (PSET). O display piscara, indicando que o comando foi aceito, e a leitura passa a ser apresentada em relacao ao valor programado."); L162 ("O valor do Preset e gravado na memoria EEPROM e mantido apos o desligamento do equipamento."); L232 (5.9: "Apos concluir a configuracao, pressione a tecla MENU para gravar o novo valor na memoria."); L237 (5.10: "A nova senha somente passara a ser utilizada nos proximos acessos ao Modo Programacao."); L239/L240 (5.11 Reset Geral restaura a Tabela 2 e "tambem restaura os ajustes de calibracao das saidas analogicas realizados durante o processo de fabricacao"); Tabela 2, L250..L267, com QUATRO linhas de valor e QUATRO de operacao (Operacao Limite 2 = Off E Limite 2 (X2) = +000,0 graus; Operacao Limite 4 = Off E Limite 4 (Y2) = +000,0 graus).

### A lacuna

1. QUANDO o dado sai da RAM para a memoria nao volatil: no hold de 3 s de cada parametro (L110) ou so na saida do modo (L136). O manual diz as duas coisas em paragrafos vizinhos.
2. QUANDO o parametro passa a COMANDAR o rele e a saida analogica. O manual nunca separa gravar de valer. Como valor e operacao sao dois itens de menu distintos (Tabela 1), qualquer efetivacao por item produz, durante a navegacao, um conjunto hibrido - Limite 1 novo com Operacao Limite 1 velha - comandando rele com gente trabalhando.
3. O que acontece se a energia cair NO MEIO da gravacao. Nao ha atomicidade, verificacao de integridade nem valor de fallback no manual.
4. O que o equipamento faz no boot quando a memoria vem vazia ou corrompida. O manual so autoriza carregar a Tabela 2 como ato deliberado (5.11).
5. O que os reles fazem durante os ate 2 minutos de Modo Programacao e durante a simulacao interna da Auto Calibracao.
6. O suporte fisico descrito em L56 ("EEPROM interna") nao existe na DE-PURI-DI261924: e ESP32-WROOM-32D, NVS em setor de flash de 4 KB, com apagamento por setor, desgaste por ciclo e desabilitacao de cache durante a escrita.

### Proposta

**1) SEPARAR DURABILIDADE DE EFETIVACAO.** Sao dois eventos com dois gatilhos, e cada um tem um instante unico:
   - DURABILIDADE (o byte vai para a flash): no hold de MENU de kHoldCommitMs = 3000 ms de cada parametro, conforme L110.
   - EFETIVACAO (o conjunto passa a comandar rele e saida analogica): num unico instante, na saida por SAIR, com tela de revisao e novo hold de 3000 ms.
   Enquanto o Modo Programacao estiver aberto, o conjunto que a tarefa ctrl usa e o CONJUNTO VIGENTE congelado no instante da entrada no modo. Nunca existe conjunto hibrido comandando rele.

**2) UNIDADE DE GRAVACAO: um registro unico ParamRecord de 52 bytes**, escrito inteiro, nunca campo a campo. Layout com deslocamentos fixos e tudo inteiro (sem float):
```
off  0  uint32 magic        = 0x44505231
off  4  uint16 version      = 1
off  6  uint16 seq          // 1..65535, 0 reservado para "nenhum"
off  8  uint16 consumedSeq  // no registro vigente: seq do registro de edicao ja consumido
off 10  uint8  kind         // 0 = vigente, 1 = edicao pendente
off 11  uint8  reserved0
off 12  int16  presetDeci[2]        // valor programado de Preset X/Y, decimos de grau
off 16  int16  presetOffsetDeci[2]  // offset em vigor produzido pelo PSET (5.6 L161)
off 20  int16  limitDeci[4]         // X1, X2, Y1, Y2, decimos de grau
off 28  int16  calFsAngleDeci[2]    // angulo de fundo de escala por eixo
off 32  uint16 calZeroCode[2]       // codigo de DAC do zero calibrado
off 36  uint16 calFsCode[2]         // codigo de DAC do fundo de escala calibrado
off 40  uint16 password             // 0..9999
off 42  uint8  limitOp[4]           // 0=Off, 1=>=, 2=<=, 3=+ (modulo)
off 46  uint8  sensorDir[2]         // 0=Horario, 1=Anti-horario
off 48  uint8  reserved1[2]
off 50  uint16 crc                  // crc16Modbus sobre os bytes 0..49
```
`static_assert(sizeof(ParamRecord) == 52)`, no padrao ja usado em src/drivers/calibration.cpp:10. CRC = `crc16Modbus()` de lib_shared/depuri_wire/include/proto/crc16.h:7. Angulos em decimos de grau int16, codigos de DAC em uint16: nenhum float no registro e nenhum float no caminho de decisao de rele.

**3) TRES CHAVES NVS no namespace "depuri1" (NvsStore, src/platform/nvs_store.h:12).** "par_a" e "par_b" formam o banco duplo do registro VIGENTE. "par_e" guarda o registro de EDICAO PENDENTE (banco simples: perde-lo custa apenas edicoes ainda nao efetivadas, e o CRC ja o protege).

**4) DURABILIDADE, passo a passo.** Ao completar o hold de 3000 ms sobre um parametro: copiar o conjunto de edicao da RAM para um ParamRecord com kind = 1 e seq = seq_edicao_anterior + 1 (wrap 65535 -> 1, 0 nunca usado); putBlob em "par_e"; commit; reler "par_e" e conferir magic, version e CRC. So depois da releitura aprovada o firmware exibe "Alteracao bem sucedida!" por 1500 ms. Se a releitura reprovar ou a escrita exceder kNvsCommitBudgetMs = 500 ms, exibir "FALHA DE GRAVACAO" por 3000 ms, restaurar na RAM o valor anterior do campo e devolver Err::Storage. O parametro nunca fica pela metade.

**5) EFETIVACAO, instante unico.** Ao selecionar SAIR e clicar MENU (L134), o firmware exibe a TELA DE REVISAO, listando apenas os campos em que a edicao difere do vigente, ate 4 campos por pagina, paginados por ▲/▼:
```
NOVA CONFIG - CONFIRMA?
MENU 3s CONFIRMA  BAIXO 3s DESCARTA
```
Hold de MENU de 3000 ms confirma: o firmware monta o registro final (todos os campos vindos da edicao, EXCETO presetOffsetDeci[2], que vem sempre do vigente - ver item 9), grava-o com kind = 0, seq = seq_vigente + 1 e consumedSeq = seq do "par_e" consumido, no banco vigente escolhido pela regra do item 6, releitura com CRC, e so entao a tarefa ctrl troca o conjunto ativo, num unico tick de 50 ms. Depois disso "par_e" e removido. Hold de ▼ de 3000 ms (kHoldDiscardMs) descarta: remove "par_e" e volta ao Modo Normal com o conjunto vigente intacto. Toque curto em ▲/▼ pagina; nenhum toque curto efetiva nem descarta.

**6) REGRA DE BANCO, validade ANTES de sequencia, nesta ordem, sem excecao:** (a) se exatamente um banco valida (magic, version e CRC), escrever no OUTRO; (b) se os dois validam, escrever no de seq mais antiga pela diferenca em int16 (`(int16_t)(seqA - seqB) < 0` => A e o mais antigo); (c) se nenhum valida, escrever em "par_a". NUNCA ler o campo seq de um banco reprovado no CRC.

**7) BOOT.** Validar "par_a" e "par_b"; adotar como VIGENTE o valido de maior seq. Ler "par_e": se for valido E `par_e.seq != vigente.consumedSeq`, ha edicao pendente - o conjunto vigente continua comandando e a tela principal exibe o aviso do item 8. Caso contrario, "par_e" e removido (isto e o que descarta um "par_e" ja consumido cuja remocao foi interrompida por queda de energia).

**8) EDICAO PENDENTE E ANUNCIADA, NUNCA SILENCIOSA.** Saida do Modo Programacao por timeout de kProgTimeoutMs = 120000 ms de inatividade (L136; cada tecla rearma o timeout) NAO efetiva nada: o conjunto vigente permanece em vigor e a tela principal passa a exibir, piscando em 1000 ms aceso / 1000 ms apagado, ate que um tecnico entre no Modo Programacao e confirme ou descarte:
```
CONFIG PENDENTE - REVISAR
```
Ao reentrar no Modo Programacao, a edicao recomeca dos valores de "par_e", nao do vigente.

**9) PSET (5.6 L161/L162) E O UNICO CAMINHO QUE ALTERA O VIGENTE FORA DO SAIR.** O duplo acionamento de ▲ efetiva a referencia imediatamente (o manual exige: "a leitura passa a ser apresentada em relacao ao valor programado") e grava imediatamente (L162). Regras: (i) so altera presetOffsetDeci[eixo]; (ii) a gravacao so ocorre se o offset calculado diferir do gravado - offset igual nao gera escrita; (iii) intervalo minimo de kPresetWriteMinIntervalMs = 10000 ms entre duas gravacoes de PSET; (iv) o PSET usa o presetDeci do conjunto VIGENTE, nunca o de uma edicao pendente; (v) a escrita preserva consumedSeq, de modo que uma edicao pendente sobrevive a um PSET; (vi) o PSET e recusado enquanto houver falha de tecla presa (item 13).

**10) NVS INVALIDA NO BOOT = FALHA LATCHADA. NAO carrega Tabela 2, NAO grava nada, NAO vai para a tela principal.** Se nenhum banco vigente valida: os quatro reles vao ao nivel `urbase::kRelayAlarmLevel`, a saida analogica dos dois eixos vai ao codigo de falha `urbase::kDacFaultCode` = 3932 (-11,00 V) e o display trava em
```
CONFIG PERDIDA - REPROGRAMAR
```
A unica saida deste estado e o Reset Geral do item 5.11 (desligar, segurar ▲, energizar), que e o ato deliberado que o manual ja define. DESVIO DO MANUAL: o manual nao preve este estado nem esta tela.

**11) AUTO-CURA LIMITADA.** Se exatamente um banco vigente valida, o firmware copia-o para o outro UMA unica vez por energizacao e somente apos 30000 ms de uptime. Consequencia deliberada: um equipamento em boot loop (reset do STWD100 a cada 1,12 a 2,24 s) nunca alcanca 30 s e portanto nunca escreve na flash - a amplificacao de escrita por boot loop e ZERO.

**12) RESET GERAL (5.11) E O UNICO CARREGADOR DE PADROES.** Carrega a Tabela 2 literal - Preset X = +000,0; Preset Y = +000,0; Operacao Limite 1 = + (modulo); Limite 1 (X1) = +005,0; Operacao Limite 2 = Off; Limite 2 (X2) = +000,0; Operacao Limite 3 = + (modulo); Limite 3 (Y1) = +005,0; Operacao Limite 4 = Off; Limite 4 (Y2) = +000,0; Sentido Sensor X = Horario; Sentido Sensor Y = Horario; Senha = 1234 - ou seja, limitDeci[1] = limitDeci[3] = 0 ALEM de limitOp[1] = limitOp[3] = Off. Os campos de calibracao (calZeroCode, calFsCode, calFsAngleDeci) NAO sao chutados: sao copiados de um registro de fabrica imutavel "cal_fab", gravado uma unica vez na producao e nunca reescrito em campo, conforme L240. Se "cal_fab" tambem estiver invalido, o Reset Geral falha e o equipamento permanece na falha latchada do item 10 - calibracao invalida e equipamento inoperante, nao equipamento com calibracao nominal chutada.

**13) GUARDA DE TECLA PRESA.** Qualquer botao continuamente ativo por kStuckKeyMs = 30000 ms e declarado falha de entrada; nesse estado sao recusados a entrada no Modo Programacao, o hold de confirmacao e o PSET. Reaproveita restLevelStable/restNoise de src/drivers/buttons.h:151-155 e cobre o fato de que IO34/IO35 sao input-only sem pull-up interno.

**14) CONSUMO DO PRESSIONAMENTO.** Atingidos os 3000 ms e executada a acao, o firmware exige a soltura da tecla (borda para o nivel de repouso estavel) antes de aceitar qualquer novo evento de MENU. Flag holdConsumed por transicao de tela. Sem isto, um unico dedo encadearia zero -> angulo de fundo de escala -> ganho em 5.7.

**15) GRANULARIDADE DECLARADA DO HOLD.** O hold e medido por carimbo em millis() da borda debounciada (debounce de 20 ms, src/drivers/buttons.h:11), avaliado a cada passagem do loop(); a granularidade e o periodo do loop(), nao o debounce. Teto declarado: 250 ms, ou seja, o commit ocorre entre 3000 e 3250 ms. Isso e conforme "aproximadamente 3 segundos" (L110). O teto so se sustenta porque a base comum tirou o Modbus do loop() (tarefa ctrl) e o Wi-Fi do produto (UR_WIFI_ENABLED 0), restando o quadro do display (medicao 10, aceitacao <= 10 ms).

**16) ORCAMENTO DE TEMPO E RELACAO COM O WATCHDOG.** Uma gravacao = putBlob de 52 B + commit + releitura. Tipico 15 ms; teto declarado kNvsCommitBudgetMs = 500 ms. Durante o apagamento de setor a cache e desabilitada e TODA tarefa que executa de flash para, inclusive a tarefa ctrl: os reles e o DAC mantem o ultimo estado escrito (o GPIO e o DAC8562 sao latches de hardware) e nenhuma avaliacao ocorre. Contrato explicito para a tarefa ctrl na volta: se o atraso do vTaskDelayUntil for menor ou igual a 500 ms E a flag `nvsCommitInProgress` estiver setada, o tick perdido NAO conta como transacao invalida e nao mexe nos contadores de falha; se o atraso exceder 500 ms, ou nao houver commit em curso, a ctrl declara falha de enlace imediatamente (reles ao nivel de alarme, saida analogica em 3932). O chute do WDI e a ISR de timer de hardware em IRAM da base comum, com token de liveness de 800 ms - 500 ms de cache-off cabem nos 800 ms, com 300 ms de folga. Este numero e A_MEDIR (medicao 4): se a janela real de cache-off passar de 500 ms, o commit tem de ser fatiado; nao ha alternativa por argumento.

**17) VIDA UTIL.** Pior caso de uso: 6 PSET por eixo por dia (2 eixos) = 12 gravacoes/dia = 4380/ano, mais 80/ano de comissionamento = 4460/ano = 133.800 gravacoes em 30 anos. Um registro de 52 B ocupa ~3 entradas de 32 B na pagina de 4 KB da NVS (126 entradas), entao ~40 gravacoes por pagina antes de compactacao; com no minimo 3 paginas de dados na particao nvs, sao ~3.345 apagamentos distribuidos, ou <= 1.115 ciclos por setor, contra 100.000 especificados. Margem 89x.

**18) DESVIOS DO MANUAL, consolidados e declarados.**
   - DESVIO DO MANUAL: L136 deixa de ser verdadeira. A gravacao e no hold de 3 s (L110), nao na saida do modo; a saida por SAIR passa a ser o instante da EFETIVACAO, nao da gravacao.
   - DESVIO DO MANUAL: L137 e L309 deixam de ser verdadeiras na parte "sera necessario reprogramar". Uma queda de energia perde, no maximo, a ultima confirmacao ainda em andamento; o conjunto anterior completo sempre sobrevive.
   - DESVIO DO MANUAL: L134 ("selecione o parametro SAIR e clique na tecla MENU") passa a exigir, apos o clique, a tela de revisao e um hold de MENU de 3 s.
   - DESVIO DO MANUAL: tres telas novas, com grafia fixada byte a byte e sem acentuacao, seguindo a convencao das mensagens do manual ("RESET DE FABRICA", "Alteracao bem sucedida!"): "NOVA CONFIG - CONFIRMA?" + "MENU 3s CONFIRMA  BAIXO 3s DESCARTA", "CONFIG PENDENTE - REVISAR", "CONFIG PERDIDA - REPROGRAMAR", "FALHA DE GRAVACAO".
   - DESVIO DO MANUAL: a tela "Alteracao bem sucedida!" (grafia exata de L183, sem cedilha e sem til), que o manual usa apenas no passo 9 de 5.7, passa a ser exibida por 1500 ms em toda confirmacao de parametro (Preset, Limites, Operacao de Limite, Sentido do Sensor, Senha e Auto Calibracao). Sem ela, o tecnico nao tem evidencia de que a escrita foi verificada.
   - DESVIO DO MANUAL: L232 e L161 de 5.9/5.6 usam "clique" onde a regra geral de L110 usa hold de 3 s; adota-se o hold de 3 s em todos os parametros (dependencia da decisao 1).
   - DESVIO DO MANUAL: L56 ("memoria EEPROM interna" do microcontrolador) e falsa. A retencao e NVS em flash do ESP32-WROOM-32D, sem bateria. A promessa comercial de L41 permanece verdadeira; a descricao tecnica de L56 precisa de errata.
   - DESVIO DO MANUAL: o manual nao preve o estado de configuracao perdida (item 10), em que o equipamento fica inoperante com reles em alarme ate um Reset Geral.
   - NAO e desvio: o PSET grava em Modo Normal (L162 manda gravar) e o Reset Geral restaura a calibracao de fabrica (L240).

### Por que

1. A janela de perda da versao (b) do manual e de ate 2 minutos mais o tempo de digitacao. Num supervisor de inclinacao portuario isso permite "o tecnico ajustou o Limite 1 de 5,0 para 2,0 graus, faltou energia, o equipamento voltou com 5,0 e ninguem foi avisado". O commit-on-confirm reduz a janela de perda ao tempo de UMA escrita verificada.
2. Atomicidade de bits nao e atomicidade de comportamento. Efetivar por parametro cria, na RAM, viva, comandando rele, a combinacao "Limite 1 novo com Operacao Limite 1 velha" - exatamente a combinacao que o proprio rascunho usou como motivo para rejeitar chaves NVS separadas. Congelar o conjunto ativo ate um instante unico de troca elimina o hibrido sem abrir mao da durabilidade.
3. Cinco secoes (5.4, 5.7, 5.8, 5.9 na intencao e 5.10) descrevem o hold de 3 s como o ato de gravar; uma unica frase (L136) diz o contrario. A decisao adota a versao majoritaria e a que o operador percebe, e paga o preco em errata.
4. Banco duplo + CRC + seq e a unica forma de a promessa de L41 ser verdadeira numa memoria que apaga por setor. Sem isso, uma queda de energia no meio do putBlob devolve, no boot seguinte, um registro parcialmente escrito, e limites de rele lidos de lixo.
5. Carregar a Tabela 2 automaticamente troca setpoints de seguranca em silencio: um portico comissionado com trip em 1,5 grau passaria a operar com trip em 5,0 graus, e a mensagem de 5 s as 3 da manha nao existe. O manual so autoriza a Tabela 2 em 5.11, ato deliberado com acesso fisico.
6. O mecanismo ja existe e esta validado no repo: crc16Modbus de lib_shared/depuri_wire, registro de tamanho fixo com static_assert e chave versionada, no padrao do CalRecord (src/drivers/calibration.h:34-42), e NvsStore com namespace unico (src/platform/nvs_store.h:12).

### O que a revisao adversarial derrubou

**Cedido - atomicidade de comportamento (seguranca 1).** Estava certa e derruba o rascunho. Corrigido pelos itens 1, 5 e 8: durabilidade por confirmacao, efetivacao num unico instante sobre o conjunto congelado, e nunca conjunto hibrido comandando rele.

**Cedido - o chute do watchdog nao era independente (seguranca 2, fidelidade, critica de completude).** Estava certa: ext_wdt.cpp:41 registra o callback com ESP_TIMER_TASK, que executa de flash e para com a cache desabilitada. O argumento "250 ms independentes do laco" era falso. A base comum ja substituiu o mecanismo por ISR de timer de hardware em IRAM com token de liveness de 800 ms; o item 16 desta decisao passa a orcar 500 ms contra esses 800 ms e manda medir (medicao 4). REFUTADA, porem, a alternativa proposta pela critica de gerar o pulso de WDI por LEDC ou RMT em loop: um periferico que pulsa incondicionalmente mantem o STWD100 alimentado mesmo com o firmware morto, ou seja, desliga o watchdog. So serve o chute condicionado ao token de liveness.

**Cedido - boot sem configuracao valida (seguranca 3).** Estava certa. Item 10: falha latchada, reles em alarme, saida em 3932, tela travada, sem restaurar Tabela 2 nem senha nem calibracao. O caminho de padroes permanece onde o manual o colocou (5.11).

**Cedido - regra de banco indefinida (seguranca 4).** Estava certa: ler seq de um banco reprovado no CRC pode direcionar a escrita para o unico banco bom. Item 6 fixa validade antes de sequencia, com a ordem (a)(b)(c) e a proibicao explicita de ler seq de banco invalido.

**Parcialmente cedido - PSET (seguranca 5).** Cedido: item 9 acrescenta deduplicacao (offset igual nao grava), intervalo minimo de 10 s e recusa sob tecla presa; item 13 acrescenta a guarda de tecla presa que cobre IO34/IO35 sem pull-up. REFUTADO no que a critica queria decidir aqui: exigir tela de confirmacao para o PSET reescreve 5.6 L161 ("O display piscara, indicando que o comando foi aceito") e e materia da decisao 1 e do bigboss, nao desta decisao. A decisao 2 nao pode nem congelar o PSET: L161 manda a leitura mudar no ato.

**Cedido - consumo do pressionamento (seguranca 6).** Item 14.

**Parcialmente cedido - granularidade do hold (seguranca 7).** Cedido que "3000..3020 ms" era ficcao: o debounce nao define granularidade, o periodo do loop() define. REFUTADA a magnitude "centenas de ms" no sistema final: a base comum tirou a espera do Modbus do loop() (tarefa ctrl propria) e o Wi-Fi do produto, e a NVS so escreve DEPOIS que o hold ja terminou. Item 15 declara o numero honesto: 3000..3250 ms, dentro de "aproximadamente 3 segundos" de L110.

**Cedido - saida por timeout descartava a edicao em silencio (seguranca 8).** Item 8: o timeout nao efetiva e nao descarta; a edicao fica persistida como pendente e a tela principal anuncia "CONFIG PENDENTE - REVISAR" piscando ate um tecnico resolver.

**Cedido, com uma refutacao dentro (seguranca 9).** Cedido: item 1 declara por escrito que os reles CONTINUAM sendo avaliados pela tarefa ctrl a cada 50 ms, sobre o angulo medido real e sobre o conjunto vigente congelado, em TODAS as telas do Modo Programacao. REFUTADA a excecao que a critica queria abrir para a Auto Calibracao: L165 diz que "a Unidade Remota simula internamente a inclinacao informada, permitindo calibrar a saida sem movimentar o equipamento monitorado" - a simulacao e da SAIDA ANALOGICA, e nada no manual autoriza suspender os reles. Os reles seguem o angulo real tambem durante a Auto Calibracao; apenas a saida analogica e sobreposta.

**Fora do escopo (seguranca 10).** Histerese, banda morta, tempo minimo de acionamento e periodo de avaliacao sao das decisoes 4 e 5, e a base comum ja fixou o periodo em 50 ms com dono unico (tarefa ctrl). A decisao 2 nao fixa comparador; o que ela devia ao sistema era o comportamento dos reles durante a gravacao e durante o menu, e isso esta nos itens 1 e 16.

**Cedido - amplificacao de escrita em boot loop (seguranca 11).** Item 11: nenhuma escrita nos primeiros 30 s de uptime e no maximo uma auto-cura por energizacao; como nao ha mais gravacao automatica de padroes no boot (item 10), um boot loop de 1,12 a 2,24 s produz ZERO escritas. Isto e mais forte que o contador persistente proposto pela critica, e nao gasta uma escrita para contar escritas.

**Cedido - vida util subestimada (seguranca 12).** Item 17 refaz a conta incluindo PSET por turno: 133.800 gravacoes em 30 anos, <= 1.115 ciclos de apagamento por setor, margem 89x.

**Cedido - fidelidade (a) telas inventadas.** Item 18 declara todas as telas novas como desvio, com grafia byte a byte.

**Cedido - fidelidade (b) "Alteracao bem sucedida!".** Item 18 declara a extensao como desvio, em vez de assumi-la.

**Cedido - fidelidade (c) Tabela 2.** Item 12 escreve os defaults com as OITO linhas: limitDeci[1] = limitDeci[3] = 0 (+000,0 graus) alem de limitOp[1] = limitOp[3] = Off.

**Cedido - fidelidade (d) tecla ▲ em Modo Normal.** O PSET em Modo Normal (5.6 L161) contradiz 5.2 ("Tecla ▲: Nao possui funcao neste modo de operacao"). A contradicao e do manual; o item 9 escolhe L161 e o item 18 registra o lado escolhido. A escolha do gesto e da decisao 1.

**Cedido - fidelidade (e) citacao trocada.** A secao "O que o manual diz" agora cita os dois ATENCAO com o texto correto de cada um: L137 e o de 5.4, L309 e o do item 7.

### Precisa de decisao humana

1. **Momento da efetivacao.** Opcoes: (a) efetivar por parametro no hold de 3 s (rascunho e decisao 3); (b) congelar o conjunto ativo e efetivar num instante unico no SAIR, com tela de revisao (proposto aqui); (c) gravar e efetivar so na saida do modo, letra de L136. RECOMENDACAO: (b). E a unica que nunca deixa valor novo com operacao velha comandando rele, e a unica que mantem a durabilidade de L110.
2. **Saida por timeout de 120 s.** Opcoes: (a) descartar a edicao pendente, letra de L137; (b) manter a edicao persistida como pendente e anunciar "CONFIG PENDENTE - REVISAR" piscando (proposto). RECOMENDACAO: (b). A alternativa devolve a perda silenciosa que a decisao existe para eliminar.
3. **NVS invalida no boot.** Opcoes: (a) carregar a Tabela 2, gravar e operar (rascunho); (b) falha latchada, reles em alarme, saida em -11,00 V, display travado (proposto). RECOMENDACAO: (b). Trocar setpoint de seguranca em silencio e inaceitavel em equipamento portuario.
4. **Saida do estado de configuracao perdida.** Opcoes: (a) somente pelo Reset Geral 5.11, que exige desligar e religar com ▲ pressionada (proposto); (b) aceitar tambem a entrada no Modo Programacao com a senha de fabrica 1234 enquanto durar o estado. RECOMENDACAO: (a); a opcao (b) e a unica saida se o cliente nao puder cortar a alimentacao da UR em campo.
5. **Textos das telas novas.** Aprovar byte a byte "NOVA CONFIG - CONFIRMA?", "MENU 3s CONFIRMA  BAIXO 3s DESCARTA", "CONFIG PENDENTE - REVISAR", "CONFIG PERDIDA - REPROGRAMAR", "FALHA DE GRAVACAO", e a errata que os acrescenta ao item 5 e ao item 5.11 do manual. RECOMENDACAO: aprovar como escritos, sem acentuacao, seguindo a convencao de "RESET DE FABRICA".
6. **Extensao de "Alteracao bem sucedida!".** Opcoes: (a) exibir em toda confirmacao, por 1500 ms (proposto); (b) restringir ao passo 9 de 5.7, como no manual. RECOMENDACAO: (a); sem ela o tecnico nao sabe se a escrita foi verificada.
7. **RST-02, Reset Geral e calibracao de fabrica.** Opcoes: (a) manter "cal_fab" imutavel e copia-lo de volta (proposto, conforme L240); (b) apagar a calibracao e exigir recalibracao. RECOMENDACAO: (a).
8. **PSET grava em Modo Normal.** Opcoes: (a) grava no ato, com deduplicacao e intervalo minimo de 10 s (proposto, conforme L161/L162); (b) mover a efetivacao do Preset para dentro do Modo Programacao, contrariando 5.6. RECOMENDACAO: (a).
9. **Errata do manual.** Aprovar a reescrita de L56 (EEPROM interna -> NVS em flash do ESP32-WROOM-32D, sem bateria), de L136, de L137 e de L309, e o acrescimo do hold de 3 s a L134. RECOMENDACAO: aprovar; publicar firmware novo com manual velho ensina o cliente a reprogramar parametros que ja estao corretos.

### Precisa de medicao de bancada

1. **MEDICAO 4 da base comum (lacuna de WDI durante escrita de NVS), com o alvo desta decisao.** Alem do procedimento ja fixado, registrar separadamente o tempo de parede de putBlob(52 B) + commit + releitura, em 100 escritas consecutivas que forcem apagamento de setor, e a maior janela de cache-off. ACEITACAO: maior lacuna de WDI <= 250 ms com o chute em IRAM, e tempo total de commit <= 500 ms em 100 de 100 amostras. Se o commit passar de 500 ms, o item 16 obriga a fatiar a escrita antes de qualquer release.
2. **Ensaio de corte de energia no meio da escrita.** 200 cortes de alimentacao AC assincronos, disparados aleatoriamente dentro da janela de commit (usar rele externo comandado por gerador), alternando cortes durante a escrita de "par_a", de "par_b" e de "par_e". ACEITACAO: em 200 de 200 religacoes, o boot adota um conjunto COMPLETO e coerente, nenhum campo hibrido, nenhuma entrada no estado de configuracao perdida. Complementar com teste nativo em test/native simulando cada corte (banco B virgem, banco B com seq 0xFFFF e CRC ruim, corte no meio de A, corte no meio de B, corte entre a gravacao do vigente e a remocao de "par_e").
3. **Granularidade real do hold de 3 s.** Instrumentar um GPIO livre levantado na deteccao da borda de MENU e baixado no instante do commit; osciloscopio; 50 gestos, com o display redesenhando. ACEITACAO: commit entre 3000 e 3250 ms em 50 de 50 amostras. Se exceder, a amostragem dos botoes tem de sair do loop().
4. **MEDICAO 11 da base comum (nivel de repouso dos botoes) como pre-requisito.** Enquanto IO34/IO35 nao tiverem nivel de repouso estavel medido (>= 2,5 V solto, <= 0,3 V pressionado), nenhum numero desta decisao que dependa de gesto - hold de 3000 ms, hold de descarte, tecla presa de 30000 ms - tem significado.
5. **Vida util da particao nvs.** Contar, com o comando de console de status, o numero de apagamentos de setor apos 5.000 gravacoes de ParamRecord em bancada. ACEITACAO: <= 150 apagamentos por 5.000 gravacoes (ou seja, >= 33 gravacoes por apagamento), que e a premissa do item 17. Se ficar abaixo, a margem de 89x cai e o intervalo minimo do PSET tem de subir.


---

## Decisao 3 - Menu de dois niveis: "Operacao Limite" e folha do submenu "Limite N>", com commit do PAR INTEIRO na saida do submenu

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto  
**REQ afetados:** MAN-5.4-L112 (tela literal do menu), MAN-5.4-L115 (mapeamento X1/X2/Y1/Y2), MAN-5.4-L116..133 (Tabela 1, 16 parametros), MAN-5.4-L135..137 (SAIR, timeout de 2 min, gravacao na saida), MAN-5.4-L110..111 (hold de 3 s grava), MAN-5.5-L142 (ponto de atuacao exato), MAN-5.6-L148 (submenu "Preset>"), MAN-5.6-L158 (clique curto grava), MAN-5.7-L183 ("Alteracao bem sucedida!"), MAN-5.8-L195 (trocar sentido exige reconferir limites), MAN-5.9-L202 (LED por limite), MAN-5.9-L204..208 (semantica Off/>=/<=/+), MAN-5.9-L211..218 (procedimento e telas de valor), MAN-5.9-L221 (clique curto grava), MAN-5.9-L223 (limites sobre a leitura com Preset e Sentido), MAN-5.11-L256..259 (Tabela 2, defaults), MAN-7-L306..309 (falha de comunicacao e perda de energia), HW-board_pins.h:59-64 (kRelayMap, serigrafia cruzada do CN3), HW-buttons.h:13 (kBtnDebounceMs=20), HW-sensor/src/main.cpp:156 (kStsDataValid nao e limpo na falha)

### O que o manual diz

Menu impresso, item 5.4, linha 112, dez itens:
`Menu>Voltar   Ajusta Preset   Auto Calibração   Limite 1   Limite 2   Limite 3   Limite 4   Sentido Sensor   Senha   Sair`

Unico submenu impresso, item 5.6, linha 148:
`Preset>Voltar   Preset X   Preset Y`

Tabela 1 (linhas 116-133) lista DEZESSEIS parametros e nao lista "Voltar". Entre eles, linha 121: `Operação Limite 1 (X1) | Off, ≥, ≤, + (módulo)` e linha 122: `Limite 1 (X1) | –90,0 a +90,0° (passo de 0,1°)`.

Item 5.9, linha 211: "Utilize as teclas ▲ e ▼ para selecionar o parâmetro Operação Limite ou Limite (1 a 4), conforme o eixo e a função que deseja configurar." Linha 218: "Após ajustar o valor do limite, selecione o parâmetro Operação Limite correspondente e escolha, com as teclas ▲ e ▼, a condição de acionamento desejada (Off, ≥, ≤ ou +)."

Semantica, item 5.9, linhas 204-207: "Off: limite desativado; o relé permanece em repouso, independentemente do ângulo medido."; "≥ (maior ou igual): o relé é acionado sempre que o ângulo medido for maior ou igual ao valor programado."; "≤ (menor ou igual): o relé é acionado sempre que o ângulo medido for menor ou igual ao valor programado."; "+ (módulo): o relé é acionado sempre que o módulo do ângulo medido for maior ou igual ao módulo do valor programado, ou seja, para inclinações em qualquer um dos dois sentidos."

Item 5.9, linha 223: "Os valores programados nos Limites 1 a 4 [...] referem-se à leitura apresentada no display, ou seja, já consideram o Preset e o Sentido do Sensor configurados para o respectivo eixo."

Item 5.5, linha 142: "A resolução de 0,1° aplica-se tanto à indicação no display quanto à programação dos limites e do preset, garantindo que o ponto de atuação dos relés seja ajustado exatamente no ângulo desejado."

Telas literais do editor de valor, linhas 214-217: `Valor Limite X1(°):+000,0`, `Valor Limite X2(°):+000,0`, `Valor Limite Y1(°):+000,0`, `Valor Limite Y2(°):+000,0`. Exemplo, linha 220: `Valor Limite Y2(°):+025,0`.

Gravacao e saida, item 5.4, linhas 110-111 e 135-136: "mantenha a tecla MENU pressionada por aproximadamente 3 segundos para confirmar e gravar o novo valor"; "Os parâmetros configurados são armazenados na memória EEPROM"; "Para retornar ao Modo Normal, selecione o parâmetro SAIR e clique na tecla MENU."; "A Unidade Remota também pode sair automaticamente do Modo Programação por timeout, quando nenhuma tecla for acionada durante aproximadamente 2 minutos. Ao concluir a saída do modo de programação, o equipamento grava na memória EEPROM todos os parâmetros alterados". Em sentido contrario, item 5.9, linha 221: "Após concluir a configuração, pressione a tecla MENU para gravar o novo valor na memória."

### A lacuna

Os oito parametros "Operacao Limite 1..4" e "Limite 1..4" da Tabela 1 nao tem entrada no menu impresso, que traz quatro itens "Limite N". O manual manda selecionar "o parametro Operacao Limite" (L211, L218) sem nunca mostrar onde ele aparece. Fica indefinido: (a) se "Limite N" e folha ou abre submenu, e onde ficam as variantes X/Y de Auto Calibracao e Sentido Sensor, igualmente ausentes do menu impresso; (b) a tela literal do editor de Operacao, que o manual nunca imprime; (c) a acentuacao das telas — o menu de L112 vem acentuado e todas as mensagens literais vem sem acento ("Alteracao bem sucedida!" L183, "RESET DE FABRICA" L246); (d) a diferenca entre "Voltar" e "Sair", ambos no mesmo menu; (e) o que os quatro reles fazem durante os ate 2 minutos de menu aberto, com que latencia e sobre qual criterio de validade do dado; (f) se o par valor+operacao entra em vigor junto ou uma metade de cada vez; (g) o que acontece com uma edicao abandonada no timeout; (h) qual o comportamento do comparador na borda exata, com vibracao de cais; (i) o que acontece quando o campo de 3 digitos inteiros permite digitar 999,9 e a Tabela 1 trava em 90,0.

### Proposta

**A. ESTRUTURA DO MENU**

1. Menu de DOIS niveis. Nivel 1 com os dez itens da L112, na ordem impressa, sem acrescentar nem remover item.
2. DESVIO DO MANUAL: politica unica de tipografia — todas as strings de display em ASCII, sem acento e sem cedilha. O grau (`°`) e mantido, porque aparece nas telas literais L214-217 e existe no conjunto latin-1 do U8g2. Consequencia: a legenda da L112 vira `Menu>Voltar   Ajusta Preset   Auto Calibracao   Limite 1   Limite 2   Limite 3   Limite 4   Sentido Sensor   Senha   Sair` (perde o til de "Calibração") e exige errata da figura de 5.4. Motivo: as unicas telas de mensagem que o manual imprime sao sem acento (L183, L246), o que e evidencia de fonte sem acentuacao; prometer literalidade acentuada no nivel 1 e escrever o nivel 2 sem acento e incoerencia interna, nao fidelidade.
3. DESVIO DO MANUAL: quatro submenus nao impressos, no mesmo padrao do "Preset>" da L148, com cabecalho, "Voltar" como primeiro item e sem rolagem circular:
   - `Preset>Voltar   Preset X   Preset Y` (literal da L148)
   - `Auto Cal>Voltar   Auto Calibracao X   Auto Calibracao Y`
   - `Limite 1>Voltar   Valor Limite X1   Operacao Limite X1`
   - `Limite 2>Voltar   Valor Limite X2   Operacao Limite X2`
   - `Limite 3>Voltar   Valor Limite Y1   Operacao Limite Y1`
   - `Limite 4>Voltar   Valor Limite Y2   Operacao Limite Y2`
   - `Sentido>Voltar   Sentido Sensor X   Sentido Sensor Y`
   Folhas diretas no nivel 1: `Senha` e `Sair`.
4. CONFERENCIA DE FECHAMENTO: 2 (Preset) + 2 (Auto Cal) + 8 (4 limites x valor+operacao) + 2 (Sentido) + 1 (Senha) + 1 (Sair) = 16 folhas, exatamente a Tabela 1, nem um item a mais nem a menos.
5. Ordem dentro do submenu de limite: Valor antes de Operacao, ordem literal do procedimento da L218.
6. DESVIO DO MANUAL: o submenu insere um segundo toque de MENU nos procedimentos de 5.7, 5.8 e 5.9. Errata: acrescentar o passo "selecione o item Limite N e clique MENU" antes do passo 2 de 5.9, e o equivalente em 5.7 e 5.8.
7. Navegacao: ▲ sobe, ▼ desce, lista NAO circular em nenhum nivel (o primeiro item para com ▲, o ultimo para com ▼). No nivel 1, "Voltar" e "Sair" fazem a mesma coisa: retornam ao Modo Normal. Os dois sao mantidos porque ambos estao impressos na L112; recomenda-se remover "Voltar" do nivel 1 na proxima revisao do manual.
8. DESVIO DO MANUAL: gesto de confirmacao unico em todo o Modo Programacao — clique curto em MENU avanca digito / entra em item; hold de MENU por 3000 ms confirma. Isso segue L110 e contradiz L158 e L221 ("pressione a tecla MENU para gravar"), que dao ao mesmo evento (clique curto) dois significados na mesma tela. Setpoint de rele nao pode ser gravado pelo mesmo gesto que avanca digito. Debounce de 20 ms, o kBtnDebounceMs ja existente em src/drivers/buttons.h:13.

**B. TELAS NOVAS (todas DESVIO DO MANUAL, byte a byte)**

9. Editor de Operacao, duas linhas no display de 256x64:
   - linha 1: `Operacao Limite X1:` (analogas: `Operacao Limite X2:`, `Operacao Limite Y1:`, `Operacao Limite Y2:`)
   - linha 2, uma das quatro: `Off (desativado)` / `>= (maior ou igual)` / `<= (menor ou igual)` / `+ (modulo)`
   ▲/▼ percorrem as quatro opcoes em lista NAO circular, nesta ordem, sem dar a volta de "+" para "Off". DESVIO: uso de `>=` e `<=` em ASCII no lugar dos glifos ≥ e ≤ da Tabela 1 e da L218; a extensao por extenso ao lado, copiada de L205-207, remove a ambiguidade. Exige errata da Tabela 1.
10. `Falha de gravacao!` exibida por 3000 ms quando o commit em NVS reprova.
11. `Alteracao descartada!` exibida por 3000 ms quando o timeout de inatividade abandona uma edicao.
12. Na confirmacao bem sucedida de um limite, reutiliza-se a string literal ja existente `Alteracao bem sucedida!` (L183), por 3000 ms. DESVIO: o manual so a usa em 5.7.

**C. EDICAO, VALIDACAO E TEMPOS**

13. Faixa: valor em decimos de grau, int16, em [-900, +900] (Tabela 1, L122). DESVIO DO MANUAL: o digito das centenas do campo `Valor Limite X1(°):+000,0` e exibido e NAO editavel, fixo em `0`; e qualquer toque de ▲/▼ que componha |valor| > 900 decimos e ignorado (o digito nao avanca). Assim nunca existe valor invalido na tela nem na confirmacao, e nao ha clamp silencioso de setpoint de seguranca.
14. Timeout de inatividade de 120000 ms (os "aproximadamente 2 minutos" da L136) vale em TODO nivel: menu de nivel 1, submenu e editor de campo. Qualquer borda de tecla o rearma. Ao estourar: descarta cfg_edit, exibe `Alteracao descartada!` por 3000 ms e retorna ao Modo Normal.
15. DESVIO DO MANUAL: teto absoluto de 600000 ms no Modo Programacao, independente de tecla. O manual so tem timeout de inatividade (L136), que qualquer toque rearma para sempre; sem teto, o gate de senha fica aberto indefinidamente num painel de cais. Ao estourar: mesmo tratamento do item 14.
16. Orcamento de bloqueio do commit em NVS: 100 ms (A_MEDIR, medicao 4). Durante a janela de cache-off a tarefa ctrl perde ate 2 ticks de 50 ms, o que fica abaixo dos 3 ticks (150 ms) que declaram falha de enlace pela base comum. Uma gravacao por limite (par inteiro), nao uma por folha — metade do desgaste de flash.

**D. COMMIT DO PAR INTEIRO (a correcao que a revisao adversarial impos)**

17. Tres copias por limite, todas em inteiro: `cfg_ativa[4]` e `cfg_edit[4]`, com `{int16_t valorDeciDeg; uint8_t op;}`, op em {0=Off, 1=GE, 2=LE, 3=MOD}. O comparador le SEMPRE `cfg_ativa`. O editor escreve SEMPRE `cfg_edit`.
18. Ao ENTRAR no submenu `Limite N>`, `cfg_edit[n] = cfg_ativa[n]`. A confirmacao de uma folha (hold de 3000 ms) grava so em `cfg_edit[n]` e NAO altera nada do que atua.
19. A efetivacao ocorre em UM unico ponto: ao selecionar `Voltar` no submenu `Limite N>`, o par INTEIRO `cfg_edit[n]` (valor E operacao) e copiado para `cfg_ativa[n]`. A copia e feita pela tarefa ctrl, no topo de um tick, antes da avaliacao, sob portMUX; a IHM apenas publica o pedido pela fila (a base comum proibe o loop() de tocar rele e DAC). Latencia de efetivacao <= 50 ms (um tick de ctrl). Se nenhuma folha foi confirmada, `Voltar` e no-op.
20. Depois da efetivacao, o loop() grava o par em NVS. Se a gravacao reprovar, o loop publica pedido de rollback, a tarefa ctrl restaura `cfg_ativa[n]` ao valor anterior no proximo tick, e o display exibe `Falha de gravacao!` por 3000 ms. O que esta na tela e sempre o que esta gravado e o que esta atuando.
21. Consequencia assumida e declarada: o unico caminho de DESCARTE explicito de uma edicao de limite e o timeout de 120000 ms (item 14) ou a saida do Modo Programacao por `Sair`/teto absoluto. Nao ha item "Descartar" no submenu, porque ele quebraria o fechamento em 16 folhas da Tabela 1.
22. DESVIO DO MANUAL da L136 ("Ao concluir a saída do modo de programação, o equipamento grava [...] todos os parâmetros alterados"): grava-se na saida de cada submenu, nunca na saida do modo. Ancorado em L110, L182 e L235, que ja descrevem gravacao na confirmacao, e elimina a janela de perda de dados admitida em L309.

**E. COMPARADOR — ARITMETICA INTEIRA, HISTERESE ASSIMETRICA**

23. Entrada: `int16_t leitura` em decimos de grau, ja filtrada, ja com Preset e Sentido aplicados (L223), grampeada em ±900 (urbase::kAngleClampDeciDeg). Nenhum float em nenhum ponto do caminho de decisao de rele.
24. Saida: `bool alarme[4]`, um estado LOGICO. O nivel de GPIO sai de `alarme[n] ? urbase::kRelayAlarmLevel : urbase::kRelayHealthyLevel`. Nenhuma linha deste comparador conhece polaridade — uma constante da base comum inverte tudo.
25. Com `v = cfg_ativa[n].valorDeciDeg`, `h = 3` (0,3°) e `a = |leitura|`, `m = |v|`:
   - `Off`: `alarme = false`.
   - `GE`: se nao esta em alarme, `alarme = (leitura >= v)`; se ja esta em alarme, permanece enquanto `leitura >= v - h`.
   - `LE`: se nao esta em alarme, `alarme = (leitura <= v)`; se ja esta em alarme, permanece enquanto `leitura <= v + h`.
   - `MOD`: `mrel = (m >= h) ? (m - h) : m`; se nao esta em alarme, `alarme = (a >= m)`; se ja esta em alarme, permanece enquanto `a >= mrel`. A supressao da histerese para `m < h` evita banda maior que o proprio setpoint, que tornaria a liberacao impossivel.
   O modulo e obtido por negacao condicional em int16, sem `abs()` de biblioteca e sem float.
26. Temporizacao, contada em ticks de 50 ms da tarefa ctrl (urbase::kCtrlPeriodMs):
   - ATUACAO: 1 tick. O alarme e assertado no PRIMEIRO tick em que a condicao e verdadeira. Sem tempo de confirmacao.
   - LIBERACAO: 10 ticks (500 ms) consecutivos com a condicao falsa.
   - PERMANENCIA MINIMA EM ALARME: 20 ticks (1000 ms) contados da assercao.
   - PERMANENCIA MINIMA SEM ALARME: 0. Qualquer permanencia neste estado atrasaria um alarme, e atrasar alarme e o erro perigoso.
27. Latencia total do firmware, da amostra no SCL3300 ate a borda no GPIO do rele: <= 72 ms em regime (urbase::kDataMaxAgeMs), e <= 172 ms durante uma gravacao de NVS (72 + 100 ms de cache-off), sem declarar falha, porque 100 ms = 2 ticks e a falha exige 3. Somar a isso o tempo de comutacao do AX1RC-5V (A_MEDIR, medicao 7). O atraso do filtro passa-baixa e propriedade da decisao 4/5, nao desta.

**F. RELES DURANTE O MENU E CRITERIO DE VALIDADE DA AMOSTRA**

28. Os quatro reles continuam sendo avaliados sobre o angulo REAL, com `cfg_ativa`, em toda a navegacao de menu, em toda a edicao, durante a tela de senha e durante as mensagens de 3000 ms. Sem congelamento e sem supressao. A garantia nao vem de disciplina do laco: vem da tarefa ctrl, que e dona exclusiva do ciclo e roda no core 0 a 50 ms, indiferente ao que a IHM faz no core 1. A unica excecao e a Auto Calibracao, que simula angulo internamente (5.7) e e tratada pela decisao 9.
29. CRITERIO DE CONSUMO DA AMOSTRA (complementa a base comum, nao a contradiz): o comparador so aceita uma amostra se, cumulativamente, (i) o registrador 3 tem `kStsDataValid` setado, (ii) nenhum bit de {`kStsSclCrcError`, `kStsSclStartup`, `kStsSclSelfTestFail`, `kStsSclNotResponding`} esta setado, e (iii) a idade da amostra e <= 72 ms. Fora disso a amostra e descartada e o tick nao muda o estado dos reles.
30. GUARDA DURA DE IDADE: se a idade da ultima amostra aceita passar de 250 ms (5 ticks), os quatro reles vao ao estado de ALARME, independentemente do contador de transacoes invalidas. Necessario porque durante um bloqueio da tarefa ctrl nenhuma transacao e TENTADA, logo o contador de 3 falhas da base comum nao avanca e o dado envelhece em silencio. 250 ms tem 45 % de margem sobre o pior caso de 172 ms do item 27 e fica 4,5x abaixo do tWD minimo de 1120 ms do STWD100.
31. DESVIO DO MANUAL de L204 ("Off: [...] o relé permanece em repouso"): na falha de enlace, no dado invalido e na guarda de idade, os QUATRO reles vao a alarme, inclusive os programados em `Off`. Motivo: `Off` e uma configuracao do usuario sobre o ANGULO; falha de comunicacao nao e um angulo, e a perda do proprio equipamento, ja declarada como condicao anormal em L306. Deixar um canal em "sem alarme" enquanto a UR esta cega e o mesmo modo de falha que a base comum rejeitou na saida analogica ao proibir 0,00 V como nivel de falha.
32. CORRECAO OBRIGATORIA NA SENSORA: sensor/src/main.cpp:156 faz `g_registers[kRegStatus] |= kStsSclNotResponding` e NAO limpa `kStsDataValid`, publicando angulos congelados com o bit de validade ainda ligado. Tem de LIMPAR `kStsDataValid` no mesmo ponto. Sem isso o criterio (i) do item 29 nao tem valor.

### Por que

A Tabela 1 e o contrato de parametros; a tela da L112 e a figura de UMA tela de um display de 256x64, que nao comporta 16 itens. A prova de que o equipamento ja e de dois niveis esta no proprio manual: a L148 imprime `Preset>Voltar   Preset X   Preset Y`, cujos dois itens tambem nao aparecem na L112 e sao folhas da Tabela 1. Aplicar a mesma regra a Auto Calibracao, Sentido Sensor e Limites resolve as tres omissoes com um unico mecanismo e faz a contagem fechar em 16 exatamente.

O commit do par inteiro na saida do submenu e imposto pela aritmetica do risco, nao por elegancia: as duas metades do limite so tem significado juntas. Trocar o Limite 1 de (+, 5,0°) para (>=, 45,0°) confirmando folha a folha deixa o par ativo em (+, 45,0°) — o eixo X fica desprotegido entre 5° e 45° por todo o tempo que o operador levar ate a segunda folha, ou para sempre se ele sair antes. Encenar o limite inteiro elimina a combinacao que ninguem programou.

A histerese de 0,3° so na borda de liberacao preserva a promessa da L142 ("o ponto de atuacao [...] exatamente no angulo desejado"): a ATUACAO continua no decimo exato programado. E as L205-207 dizem "o rele e acionado sempre que" — condicao suficiente, nao equivalencia — de modo que manter o rele acionado 0,3° abaixo do setpoint nao contradiz o texto. Sem banda nenhuma, com quantum de 0,1° e vibracao de cais, a leitura reatravessa o limite e o rele comuta na cadencia do tick: alarme falso e alarme perdido a 20 Hz ao mesmo tempo, vida mecanica do AX1RC-5V destruida, e chaveamento continuo num BC337 cujo beta forcado exigido ja e ~144 (base comum, item 6).

### O que a revisao adversarial derrubou

**Cedido — a critica de seguranca estava certa em quatro pontos:**

1. *"O commit atomico e atomico so na memoria, nao na semantica"* — certo, e e a falha mais grave do rascunho. O item 6 do rascunho copiava o par inteiro na confirmacao de CADA folha, o que carrega a operacao velha junto com o valor novo. Corrigido nos itens 17-21: o commit passou da folha para a SAIDA do submenu `Limite N>`.
2. *"Comparador sem histerese e sem permanencia minima"* — certo. O rascunho vendia como virtude ("zero arredondamento") exatamente o defeito. Corrigido nos itens 25-26.
3. *"Define o comportamento durante o menu e nao durante a falha que de fato ocorre"* — certo. Corrigido nos itens 29-32, incluindo a correcao na sensora, que e o achado mais concreto da critica: `|= kStsSclNotResponding` sem limpar `kStsDataValid` (sensor/src/main.cpp:156, verificado no repo).
4. *"Serigrafia cruzada do CN3"* — certo e nao corrigivel em firmware: o LED e a base do BC337 compartilham o mesmo net (board_pins.h:59-64, `{"LIM1", kLim1, "RL5", "CN1D/CN1E", "J10", "CN3-6 (serigrafia \"LED LIM3\")"}`). Segue como bloqueio de etiqueta, item de decisao humana.

**Cedido — a critica de fidelidade estava certa em tres pontos:**

5. *Acentuacao incoerente entre nivel 1 e nivel 2* — certo. Corrigido no item 2 com politica unica, declarada como desvio, em vez de prometer literalidade e quebra-la.
6. *"Falha de gravacao!" inventada e nao marcada como desvio* — certo. Marcada nos itens 10-12, junto com as outras duas telas novas.
7. *O submenu insere um segundo MENU nos procedimentos de 5.7/5.8/5.9* — certo. Declarado no item 6 como errata.

**Refutado — onde a critica de seguranca errou:**

8. *"Enquanto o submenu estiver aberto vale, por eixo, o par MAIS RESTRITIVO entre o antigo e o em edicao"* — REJEITADO. O valor em edicao nao e um valor programado: e uma sequencia de teclas em transito, digito a digito. Fazer o rele responder ao OU logico do par antigo com o par meio digitado poe o contato seguindo os dedos do operador — precisamente o chaveamento por borda que o item 1 da propria critica condena. O par antigo, que e completo e foi autorizado por alguem, continua atuando ate o commit; isso nao degrada nada em relacao ao estado anterior a abertura do menu.
9. *"Confirmacao de 300 ms para atuar"* — REJEITADO. A base comum ja especifica o filtro passa-baixa como constante de tempo aplicada no mesmo tick de 50 ms; empilhar 300 ms de confirmacao sobre o filtro conta o mesmo ruido duas vezes e atrasa o alarme em 6 ticks sem ganho. Atuacao em 1 tick (item 26).
10. *"Liberacao em 2 s"* e *"permanencia minima de 1 s em CADA estado"* — REJEITADOS parcialmente. Liberacao fixada em 500 ms (10 ticks), nao 2 s: 2 s de rele preso somam a permanencia minima de 1 s e produzem ate 3 s de alarme residual apos a estrutura voltar a posicao, o que treina o operador portuario a ignorar o painel. Permanencia minima mantida SO no estado de alarme; no estado sem alarme ela atrasaria a atuacao.
11. *"Ultima transacao Modbus bem-sucedida ha menos de 300 ms"* e *"apos 1 s de tolerancia"* — REJEITADOS. Contradizem a base comum, que fixa idade maxima de 72 ms e falha em 150 ms. Adotados 72 ms (item 29) e guarda dura em 250 ms (item 30), ambos derivados da base.
12. *"Fixar a polaridade em board_pins.h: bobina energizada no estado saudavel"* — REJEITADO como decisao desta proposta, e a propria critica admite ("e decisao de produto do bigboss"). A base comum ja isolou isso em `urbase::kRelayFailSafePolarity` (A_APROVAR), com as medicoes 7, 8 e 9 como condicao. Esta decisao se limita a produzir um estado LOGICO de alarme (item 24), de modo que nenhuma linha do comparador precise mudar quando a polaridade for assinada.

**Numeros do rascunho que a BASE COMUM sobrescreveu:**

13. Rascunho item 6: "latencia de efetivacao <= 10 ms (um ciclo de controle)" — FALSO sob a base comum, que fixa o ciclo em 50 ms. Corrigido para <= 50 ms (item 19).
14. Rascunho item 8: "o chute de 250 ms e de esp_timer [...] e nao depende do laco" — FALSO. O callback esta em `ESP_TIMER_TASK` (src/drivers/ext_wdt.cpp:41) e executa de flash; durante o apagamento de setor da NVS a cache e desabilitada e a tarefa nao roda. A base comum trocou o mecanismo por ISR de timer de hardware em IRAM. O orcamento de 100 ms permanece (item 16), mas agora vale porque o chute e IRAM e porque 2 ticks perdidos ficam abaixo do limiar de 3.

### Precisa de decisao humana

1. **Histerese de liberacao e permanencia minima.** Opcoes: (a) 0,3° de histerese so na liberacao + 500 ms de confirmacao para liberar + 1000 ms de permanencia minima em alarme, atuacao no decimo exato; (b) sem histerese nenhuma, leitura mais literal possivel de L142; (c) 0,5° com liberacao em 2 s, como pediu a critica de seguranca. RECOMENDACAO: (a). Sem banda o rele bate na borda; 0,5° perde 10 % do setpoint num limite de 5,0° (o default de fabrica da Tabela 2, L257).
2. **Momento da efetivacao.** Opcoes: (a) commit do par inteiro na saida do submenu `Limite N>`, com descarte so por timeout; (b) commit por folha confirmada, como o rascunho; (c) gravacao so na saida do Modo Programacao, leitura literal de L136. RECOMENDACAO: (a). (b) cria o par valor-novo/operacao-velha; (c) e a versao que o proprio manual contradiz em L110, L182 e L235 e que produz a perda de dados admitida em L309.
3. **Item "Descartar" no submenu de limite.** Opcoes: (a) nao existe — descarte so por timeout de 120 s ou por `Sair` (proposto); (b) acrescentar um item `Descartar` a cada submenu de limite, quebrando o fechamento em 16 folhas e exigindo errata da Tabela 1. RECOMENDACAO: (a), assumindo o custo de usabilidade, porque a contagem em 16 e o unico criterio objetivo de completude do menu.
4. **Tipografia das telas.** Opcoes: (a) ASCII sem acento em todas as telas, com errata da figura da L112 ("Auto Calibracao") e da Tabela 1 (`>=` e `<=` no lugar de ≥ e ≤); (b) reproduzir a mistura do manual — menu acentuado, mensagens sem acento — e depender de fonte latin-1 no U8g2. RECOMENDACAO: (a). E a unica coerente com as tres unicas telas de mensagem que o manual imprime (L183, L246).
5. **Gesto de confirmacao.** Opcoes: (a) clique curto avanca digito, hold de 3000 ms grava (L110); (b) clique curto grava (L158, L221). RECOMENDACAO: (a). Em (b) o mesmo evento tem dois significados na mesma tela e um setpoint de rele de seguranca e gravado pelo gesto mais barato do teclado. Exige errata de 5.6 e 5.9.
6. **Teto absoluto de 600 s no Modo Programacao.** Opcoes: (a) existe, 600 s, independente de tecla; (b) nao existe, so o timeout de inatividade da L136. RECOMENDACAO: (a). Nenhum rele e degradado no Modo Programacao, entao isto e controle de acesso, nao seguranca funcional — mas o gate de senha nao pode ficar aberto indefinidamente num painel de cais. Nao existe no manual: exige acrescimo em 5.4.
7. **Reles em `Off` durante falha de enlace.** Opcoes: (a) vao a alarme junto com os outros tres (proposto); (b) permanecem em repouso, leitura literal de L204. RECOMENDACAO: (a). Em (b) um canal cabeado ao intertravamento do cliente reporta "sem alarme" enquanto a UR esta cega. Exige errata de 5.9. Depende tambem da polaridade: se `kRelayFailSafePolarity = true` for aprovada, "Off" passa a significar bobina permanentemente energizada, o que contradiz L204 de forma ainda mais direta e obriga a reescrever a definicao de `Off` como "permissivo permanente".
8. **Digito das centenas do campo de valor.** Opcoes: (a) exibido e nao editavel, fixo em `0`, com recusa de qualquer composicao acima de |900| decimos (proposto); (b) editavel, com clamp silencioso na confirmacao; (c) editavel, com mensagem de recusa (mais uma tela nova). RECOMENDACAO: (a). Clamp silencioso de setpoint de seguranca e inaceitavel; (c) acrescenta tela sem ganho sobre (a).
9. **Serigrafia cruzada do CN3 e mapa de bornes.** `LIM1` (IO32, rele RL5) acende o LED serigrafado "LED LIM3"; `LIM2` acende "LED LIM1"; `LIM3` acende "LED LIM2" (board_pins.h:59-64). Nao ha correcao possivel em firmware: o LED e a base do BC337 estao no mesmo net. Opcoes: (a) ECO de serigrafia da placa frontal; (b) remapeamento de fiacao do CN3; (c) errata de manual documentando o cruzamento. RECOMENDACAO: (a). Junto vai a amarracao dos bornes CN2 do manual (terminais 14/15, 17/18, 20/21, 23/24, Tabela 4) contra CN1D..CN1K do esquematico, que nao existe em lugar nenhum do repositorio. **Isto trava a impressao da etiqueta e a publicacao dos rotulos do menu.**
10. **Aviso apos troca do Sentido do Sensor.** L195 apenas RECOMENDA ao operador reconferir Preset e Limites depois de inverter o sentido, o que desloca os quatro pontos de atuacao em silencio. Opcoes: (a) firmware nao faz nada, fidelidade literal; (b) firmware exibe aviso na confirmacao do Sentido; (c) firmware forca `Off` nos dois limites do eixo alterado ate o operador reconfirmar cada um. RECOMENDACAO: (b), com o texto a ser fixado pela decisao dona do parametro Sentido (DIR-01/02, hoje sem dono). Registrado aqui porque muda os setpoints geridos por esta decisao.

### Precisa de medicao de bancada

- **MEDICAO 11 (base comum) — nivel de repouso dos botoes em IO15/IO34/IO35 com a placa frontal conectada.** BLOQUEIA ESTA DECISAO INTEIRA: IO34 e IO35 sao input-only sem pull interno e nao ha pull-up externo na placa mae (buttons.h:2). Se qualquer linha flutuar, o hold de 3000 ms, o timeout de 120000 ms e o debounce de 20 ms nao tem significado, e nenhum item de A a D pode virar codigo.
- **MEDICAO 12 (base comum) — polaridade e mapeamento dos LEDs do painel.** Fecha o item 9 de decisao humana e confirma se o LED e ativo em nivel alto; se a placa frontal for anodo-comum, a polaridade fail-safe deixa os quatro LEDs acesos em condicao normal e apagados em alarme, invertendo a legenda de novo.
- **MEDICAO 7 (base comum), com acrescimo** — corrente e margem de acionamento das bobinas, MAIS o tempo de comutacao (pull-in e drop-out, em ms) do AX1RC-5V, que hoje nao tem numero e entra somado aos 72 ms do item 27 para formar a latencia total ate o contato.
- **MEDICAO 4 (base comum) — lacuna de WDI durante escrita de NVS.** Valida o orcamento de 100 ms do item 16 e, por consequencia, a latencia de 172 ms do item 27 e a guarda de 250 ms do item 30. Se a janela de cache-off medida passar de 100 ms, o commit do par tem de ser fatiado.
- **MEDICAO 10 (base comum) — tempo de quadro do display.** Define quantos redesenhos por segundo o menu pode fazer sem roubar tempo da amostragem de botao no core 1; o criterio de aceitacao de 10 ms por `sendBuffer` e o que sustenta a granularidade dos gestos de 3000 ms e 120000 ms.
- **MEDICAO 14 (NOVA) — chatter do comparador na borda de atuacao.** Procedimento: UR + sensora montadas, sensora fixada em mesa vibratoria ou na propria estrutura, com um limite programado em `+` a 5,0° (o default de fabrica da Tabela 2, L257) e a inclinacao mecanica ajustada para 5,0° ± 0,1°; osciloscopio ou contador em IO32 (LIM1); 1 hora continua de aquisicao, repetida com o filtro em cada um dos degraus que a decisao 4/5 definir. Registrar o numero total de comutacoes e o menor intervalo entre duas comutacoes. ACEITACAO: no maximo 60 comutacoes em 1 hora e nenhum intervalo entre comutacoes abaixo de 1000 ms (a permanencia minima do item 26). Se reprovar, a histerese de 0,3° e menor que o ruido residual do filtro e o par histerese/constante de tempo tem de ser reaberto junto com a decisao 4/5. Comandos: `relay` e `status` do console para leitura dos contadores.


---

## Decisao 4 - Filtro passa-baixa: EMA Q8 dirigido por tempo, com recarga por salto, parametro "Filtro" de 4 degraus e default 0,8 s

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto  
**REQ afetados:** REQ-MEA-02 (docs/protocolo-rs485.md:630), REQ-COM-01 (docs/protocolo-rs485.md:629, consumo do dado e criterio de amostra valida), MAN-4 (manual L71), MAN-6.1 (manual L272), MAN-2.1 (manual L37, L38, L45), MAN-5.4 e Tabela 1 (manual L107, L112, L115-134), MAN-5.5 (manual L142), MAN-5.9 (manual L201, L205-207, L223), MAN-5.11 e Tabela 2 (manual L238, L250-268), DIR-01/02 e PSET (ordem de aplicacao em relacao ao filtro), NRM-02 (o valor exibido em cada eixo e o mesmo do filtro)

### O que o manual diz
Item 4, `docs/manual-cliente-sui-2026.txt:71`: "Com o objetivo de reduzir oscilações provocadas por vibrações mecânicas ou pequenas variações transitórias na medição, o Supervisor de Inclinação dispõe de um filtro passa-baixa, permitindo adequar o tempo de resposta do equipamento às características de aplicação."

Item 6.1, `:272`: "O equipamento dispõe de filtros capazes de reduzir os efeitos de pequenas oscilações e vibrações presentes na aplicação. Contudo, esses recursos não substituem a necessidade de uma instalação mecânica adequada."

Item 5.9, `:223`: "Importante: Os valores programados nos Limites 1 a 4 são sempre expressos em graus, com resolução de 0,1°, e referem-se à leitura apresentada no display, ou seja, já consideram o Preset e o Sentido do Sensor configurados para o respectivo eixo."

Item 5.5, `:142`: "Observação: A resolução de 0,1° aplica-se tanto à indicação no display quanto à programação dos limites e do preset, garantindo que o ponto de atuação dos relés seja ajustado exatamente no ângulo desejado."

Item 5.9, `:205`: "≥ (maior ou igual): o relé é acionado sempre que o ângulo medido for maior ou igual ao valor programado."

Item 2.1, `:45`: "Indicação e programação sempre em graus, com uma casa decimal fixa (0,1°), dispensando parâmetros de unidade livre, constante de conversão, número de dígitos ou casas decimais;" e `:38`: "Precisão, linearidade e repetibilidade: ±0,1% da escala máxima;"

### A lacuna
O manual promete o filtro duas vezes (L71 e L272) e usa "permitindo adequar o tempo de resposta", que so tem sentido se houver ajuste — mas o menu de L112 nao tem item de filtro, a Tabela 1 (L115-134) nao lista parametro de filtro, tempo de resposta ou media, e a Tabela 2 de fabrica (L250-268) nao tem default. O manual nao da constante de tempo, faixa, frequencia de corte, periodo de amostragem, nem diz se o filtro atua sobre display, saida analogica, comparacao de rele ou os tres, nem declara o atraso de atuacao que o filtro necessariamente introduz. Do lado do firmware a lacuna e total: a sensora publica o valor instantaneo do SCL3300 a cada 10 ms, sem media, mediana ou filtro (`sensor/src/main.cpp:26,151-158`), e nao ha uma linha de filtro em `sensor/src/` nem em `src/`. Hoje o unico passa-baixa da cadeia e o do SCL3300, escolhido pelo MODE, fixo em compilacao em MODE 1 (`sensor/src/drivers/scl3300.h:25`, LPF 40 Hz), amostrado a 100 Hz — Nyquist 50 Hz contra joelho em 40 Hz, ou seja, aliasing aberto na origem.

### Proposta
Cadeia de tres estagios. Estagio 1 na sensora (fixo, sem menu, sem escrita Modbus). Estagios 2 e 3 na UR, dentro da tarefa ctrl, no mesmo tick de 50 ms da transacao (BASE COMUM). Aritmetica inteira ponta a ponta: decimos de grau em `int16`, estado do filtro em `int32` Q8. Nenhum `float` em nenhum ponto.

1. **Ts = 50 ms, dono = tarefa ctrl.** Um passo de filtro por tick, imediatamente apos a transacao Modbus, antes da avaliacao dos limites. Sem periodo proprio, sem tarefa propria, sem timer adicional (BASE COMUM, `kFilterPeriodMs = kCtrlPeriodMs = 50`).

2. **ESTAGIO 1 — anti-aliasing na origem, onde as amostras existem.** (a) SCL3300 em MODE 4 (LPF 10 Hz, baixo ruido), o que depende da decisao 11 e custa uma linha em `sensor/src/main.cpp:37`; (b) media movel inteira de 5 amostras VALIDAS consecutivas de 10 ms (janela 50 ms), rolante, atualizada a cada tick de 10 ms da sensora, publicada nos registradores 0/1/2. Soma maxima 5 x 1800 = 9000 em `int32`; divisao por 5 com arredondamento meio-para-longe-do-zero, uma unica vez. **Nao e parametro, nao entra na Tabela 1, nao exige nenhuma funcao de escrita Modbus** (o escravo continua so-leitura, `sensor/src/proto/modbus_slave.cpp:104-141`). Motivo quantitativo: o boxcar de 5 a 100 Hz tem zeros exatos em 20, 40, 60, 80 e 100 Hz, que sao precisamente os centros das bandas que dobram na reamostragem de 20 Hz da UR — e o decimador CIC correto para esta cadeia. Se qualquer uma das 5 leituras reprovar, o acumulador e descartado e nada e publicado como valido.

3. **Criterio de amostra valida (porta de entrada do filtro).** So entra no filtro a transacao cujo registrador 3 valer `0x0001` EXATO (REQ-MEA-02, `docs/protocolo-rs485.md:630`, secao 7.2). Isso neutraliza, do lado da UR, o defeito conhecido da sensora que publica `kStsDataValid | kStsSclNotResponding` sobre angulo velho (`sensor/src/main.cpp:151-158`). Adicionalmente, se `kRegUptimeS` (registrador 7) DIMINUIR entre duas transacoes, a sensora reiniciou: o filtro e recarregado (item 9), nunca rampado a partir do valor de antes do reset.

4. **ESTAGIO 2 — mediana de 3 amostras cruas consecutivas**, por eixo, janela de 150 ms, atraso de grupo de 2 ciclos (100 ms) para degrau. Tres comparacoes, ring estatico de 3 `int16`, sem heap. Existe por uma razao dura: e ela que torna seguro o item 6. Um unico quadro corrompido que passe no CRC-16 nunca sobrevive a uma mediana de 3 e portanto nunca dispara a recarga por salto.

5. **ESTAGIO 3 — EMA de 1 polo em Q8, SEM banda morta.** Estado `y` em `int32`, escala 256 (Q8), entrada `x` em decimos de grau:
   ```
   int32_t diff = (int32_t)x_med * 256 - y;
   int32_t step = diff >> k;                 // shift aritmetico, arredonda para -inf
   if (step == 0 && diff > 0) step = 1;      // passo minimo: mata a zona morta
   y += step;
   int16_t out = (y >= 0) ? (int16_t)((y + 128) / 256)
                          : (int16_t)(-((-y + 128) / 256));   // meio-para-longe-do-zero
   ```
   `|y|` maximo = 1800 x 256 = 460 800, cabe em `int32` com 4 ordens de folga. Zero float, zero divisao por variavel, zero alocacao. O par (shift para -inf) + (passo minimo 1 quando `diff > 0`) garante convergencia monotonica EXATA nos dois sentidos: com `diff = -1` o shift ja da -1; com `diff = +1` o passo minimo da +1; em `diff = 0` o estado e estavel, sem ciclo limite. Em regime permanente `y = x*256` exatamente e `out = x` bit a bit — **o filtro nao adiciona nenhum erro de quantizacao em regime permanente**. O trecho de convergencia lenta e limitado a `2^k - 1` LSB de Q8: no maximo 3 ciclos (0,15 s) no ajuste 0,2 s e 63 ciclos (3,15 s) no ajuste 3,2 s, para fechar os ultimos 0,025 grau.

6. **DESVIO DO MANUAL: recarga por salto (nao linearidade declarada).** Se `|x_med*256 - y| >= 20 decimos * 256` (2,0 grau), o estado e RECARREGADO: `y = x_med * 256` no mesmo tick. Nao e um segundo caminho de decisao: e o proprio e unico valor que salta, e display, saida analogica e comparacao de rele veem o salto juntos, no mesmo tick. Ancorado no texto do manual: L272 promete filtrar "pequenas oscilações e vibrações" e L71 fala de "pequenas variações transitórias" — uma excursao de 2,0 grau (20x a resolucao publicada, 4x a histerese de liberacao da decisao 5) nao e pequena e seguir-la de imediato e o comportamento fiel. Consequencia: **qualquer excursao de 2,0 grau ou mais atua o rele em 0,43 s em QUALQUER ajuste de filtro**, inclusive em 3,2 s. Este item tem de ser escrito no manual (item 12).

7. **Ordem canonica da cadeia, um unico valor.** `bruto da sensora (ja media de 5) -> mediana de 3 -> EMA/recarga -> Sentido do Sensor (troca de sinal, exata) -> Preset (soma inteira, exata) -> grampo em +/-900 decimos -> UNICO int16 publicado no tick`. Esse `int16` alimenta, sem excecao e sem copia paralela: display, saida analogica e comparacao dos quatro limites. Cumpre L223 e L142 ao pe da letra. Duas consequencias deliberadas: (a) o grampo e a ULTIMA operacao, para que o numero exibido nunca saia de +/-90,0 (5.7, "não altera a faixa de indicação do display, que permanece em ±90,0°"); (b) o filtro esta A MONTANTE de Sentido e Preset, que sao lineares (troca de sinal e soma de constante) e comutam com o EMA — assim o commit de Preset ou de Sentido produz um degrau EXATO e instantaneo no valor exibido, com transitorio de filtro igual a ZERO. Nenhuma recarga, nenhuma rampa, nenhum rele atuado por artefato de filtro.

8. **DESVIO DO MANUAL: parametro de menu "Filtro", global (um so para os dois eixos), 4 degraus.** Inserido entre "Sentido Sensor" e "Senha" na sequencia de L112. Edicao pela regra geral de 5.4 (L109-110): clique em MENU habilita, ▲/▼ trocam a opcao, MENU pressionada ~3 s grava. Tela de edicao, texto novo, especificado aqui byte a byte para que firmware e manual coincidam: `Filtro(s):0,8`. Valores e coeficientes (Ts = 50 ms, tau = -Ts/ln(1-2^-k)):

   | Rotulo | k | tau real | fc (-3 dB) | Atenuacao a 1 Hz | Atenuacao a 0,3 Hz |
   |---|---|---|---|---|---|
   | `Filtro(s):0,2` | 2 | 0,174 s | 0,916 Hz | -3,4 dB | -0,4 dB |
   | `Filtro(s):0,8` | 4 | 0,775 s | 0,205 Hz | -13,9 dB | -5,0 dB |
   | `Filtro(s):1,6` | 5 | 1,575 s | 0,101 Hz | -20,0 dB | -9,9 dB |
   | `Filtro(s):3,2` | 6 | 3,175 s | 0,050 Hz | -26,0 dB | -15,7 dB |

   **Default de fabrica: `Filtro(s):0,8`** (k = 4). Nova linha na Tabela 1 ("Filtro | 0,2 / 0,8 / 1,6 / 3,2 s") e na Tabela 2 ("Filtro | 0,8 s"). **Nao existe opcao 0,0 s no menu**: o bypass e um comando de console de fabrica, nao um item exposto ao operador — e a Auto Calibracao nao precisa dele, porque durante ela a UR "passa a simular internamente a inclinação informada" (5.7, L180) e a saida analogica nao vem do sensor.

9. **Reinicializacao — lista fechada, exaustiva.** O estado do filtro (y e a janela da mediana) so e recarregado em tres situacoes: (a) primeira transacao valida apos o boot; (b) primeira transacao valida apos sair da falha de comunicacao (5 transacoes boas consecutivas, BASE COMUM); (c) reset da sensora detectado pelo item 3. Recarga = `y = x*256` e mediana preenchida com 3 copias de `x`; nunca ha rampa a partir de valor velho. **Em nenhuma outra circunstancia o filtro e invalidado** — em particular, entrar ou sair do Modo Programacao NAO reinicializa nada, e trocar o proprio parametro Filtro NAO reinicializa nada: `k` e o unico dado que muda, `y` continua nas mesmas unidades e a mudanca so altera a velocidade de convergencia a partir do tick seguinte. Consequencia: **o commit de parametro nao pulsa rele nenhum**.

10. **Filtro dirigido por TEMPO, nao por amostra.** Todo tick de 50 ms executa exatamente um passo do EMA. Em tick sem transacao valida, o passo usa a ultima amostra valida (retencao de ordem zero) e um contador de retencoes e incrementado. Assim o tau publicado e o tau real: um enlace que perca 2 de cada 3 quadros nao triplica a constante de tempo — soma apenas o atraso de transporte da informacao (~75 ms para uma cadencia efetiva de 150 ms). O criterio de falha continua sendo o unico do projeto: 3 transacoes invalidas consecutivas (150 ms), 5 boas para recuperar, 2000 ms de permanencia minima (BASE COMUM).

11. **Diagnostico de enlace degradado (nao muda rele nenhum).** Contador em janela movel de 200 ciclos (10 s): se as transacoes invalidas passarem de 10 % da janela sem chegar a 3 consecutivas, um flag `LINK DEGRADADO` e exposto no console e nos contadores de diagnostico. Nao altera rele, nao altera saida analogica, nao inventa texto de display (o manual nao define nenhuma tela para isso).

12. **DESVIO DO MANUAL: publicar o orcamento de latencia por ajuste.** Cadeia fixa, independente de k: sensor MODE 4 (resposta ao degrau ~80 ms) + decimador de 5 (atraso de grupo 20 ms) + idade maxima do dado 72 ms (BASE COMUM: 50 ms de poll + 21,3 ms de round-trip) + mediana de 3 (100 ms) + confirmacao de ataque e operacao do rele (110 ms, decisao 5) = **382 ms**, mais o termo do filtro:

   | Ajuste | Excursao >= 2,0 grau (recarga) | Movimento em rampa (atraso = tau) | Degrau de 1,0 grau cruzando limite a 0,5 grau |
   |---|---|---|---|
   | 0,2 s | 0,43 s | 0,56 s | 0,50 s |
   | 0,8 s | 0,43 s | 1,16 s | 0,92 s |
   | 1,6 s | 0,43 s | 1,96 s | 1,47 s |
   | 3,2 s | 0,43 s | 3,56 s | 2,58 s |

   Esses numeros entram na Tabela 1 como coluna, e a nota geral do caso degrau pequeno tambem: para um degrau de amplitude A acima do valor filtrado, cruzando um limite a distancia d, o atraso do filtro e `tau * ln(A/(A-d))`, que cresce sem limite quando d se aproxima de A — propriedade de qualquer passa-baixa, e a razao pela qual a recarga do item 6 existe.

13. **Orcamento de erro.** Em regime permanente o filtro contribui com 0,000 grau (item 5). Sentido e Preset sao exatos em inteiro. O grampo e exato. Logo o orcamento de +/-0,1 % de fundo de escala de L38 (+/-0,09 grau) continua consumido apenas pelo formato de decimo de grau (+/-0,05 grau) e pelo sensor — a decisao 4 nao gasta nada dele. Durante transitorio o filtro apresenta, por construcao, um valor atrasado, nao um valor errado.

14. **Persistencia.** O parametro Filtro e gravado na NVS junto com os demais, no mesmo bloco e sob o mesmo CRC-16/MODBUS. Valor invalido no bloco = 0,8 s (default de fabrica).

15. **Fronteira com a decisao 5, declarada para acabar com o contrato duplo.** A decisao 4 e dona da cadeia ate a producao do `int16` do item 7 e do flag de validade. A decisao 5 e dona da comparacao: operacao (Off, >=, <=, +), histerese de liberacao, confirmacao de ataque, permanencia minima e polaridade. A decisao 5 le APENAS o `int16` do item 7 e nada mais. **A decisao 4 retira do projeto os seus proprios itens 7 e 8 do rascunho**: a histerese de 3 decimos e a liberacao em 500 ms sao substituidas pelos 5 decimos e pelos tempos da decisao 5 (um numero por parametro), e o trip rapido deixa de existir, substituido pela recarga do item 6.

16. **Custo.** Estado por eixo: 3 x `int16` (mediana) + 1 x `int32` (y) + 2 bytes de flags = 12 B; 24 B para os dois eixos. Execucao: 3 comparacoes, 1 subtracao, 1 shift, 2 adicoes e 1 arredondamento por eixo por tick, orcados em <= 20 us para os dois eixos, dentro dos 50 ms do tick com 4 ordens de margem. Modulo de dominio puro (`src/app/tilt_filter.{h,cpp}`), compilavel em `env:native`, no molde de `src/drivers/calibration.cpp`.

### Por que
A escolha de EMA em vez de boxcar nao e estetica: com Ts fixo em 50 ms pela BASE COMUM, o EMA custa 4 bytes de estado e um shift por eixo, contra 128 bytes e uma soma corrente que precisa ser mantida coerente num boxcar de 64; e o boxcar tem primeiro lobo lateral em -13 dB, ou seja, nao rejeita banda estreita — a critica a decisao 5 esta certa nesse ponto e ela vale contra o boxcar, nao contra o EMA. O default 0,8 s foi escolhido pelo espectro, nao pela latencia: um balanco estrutural de +/-0,5 grau a 1 Hz chega ao comparador com +/-0,34 grau no ajuste 0,2 s (pico a pico 0,68 grau, MAIOR que a histerese de 0,5 grau da decisao 5, logo o rele bate uma vez por ciclo) e com +/-0,10 grau no ajuste 0,8 s (pico a pico 0,20 grau, dentro da banda, logo o rele nao bate). O ajuste de 3,2 s permanece no menu porque e o unico que sobrevive a balanco pendular de portico a 0,3 Hz (-15,7 dB contra -5,0 dB no default), e a alternativa real para o cliente nesse caso nao e um filtro menor — e desligar o limite. Quatro degraus rotulados em segundos, e nao um campo continuo de 0,0 a 9,9 s, porque cada degrau e um `k` inteiro (shift), auditavel no ensaio de recebimento, e porque 100 valores de latencia que o operador nao tem como medir sao 100 formas de configurar um perigo. Filtro global e nao por eixo porque os dois eixos veem a mesma estrutura e a mesma vibracao, e cada parametro a mais num teclado de tres teclas e um erro de campo a mais. O estagio 1 na sensora e obrigatorio porque nenhum filtro na UR remove o que ja dobrou para dentro da banda antes de a UR ver a amostra: a UR amostra a 20 Hz um sinal que, em MODE 1, tem conteudo ate 40 Hz.

### O que a revisao adversarial derrubou
**Cedido, e a proposta acima ja incorpora:**
- *Caminho paralelo (seguranca 1, fidelidade 2).* As duas lentes estao certas e o rascunho estava errado: o trip rapido decidia rele sobre a mediana enquanto o display seguia o EMA, violando L223 e contradizendo o proprio item 5 do rascunho. **O trip rapido foi eliminado** e substituido pela recarga por salto do item 6, que e a correcao (a) proposta pela lente de seguranca: um unico valor, que salta inteiro. O beneficio operacional foi preservado (0,43 s para qualquer excursao >= 2,0 grau, em qualquer ajuste) sem criar um segundo numero.
- *Banda morta = alarme perdido (seguranca 2, fidelidade 3).* Certo, e o rascunho ainda errou a conta: a banda de k=2 e 4/256 decimo = 0,0016 grau, nao os 0,006 grau declarados (0,006 grau e o valor de k=4) — as duas lentes acharam o mesmo erro. **A banda morta foi eliminada na raiz** pelo passo minimo do item 5 (correcao (b) da lente de seguranca), e o argumento de "verificar no ensaio de rampa" foi retirado: o ensaio de rampa vira confirmacao, nao condicao de aceitacao.
- *Filtro dirigido por amostra (seguranca 3).* Certo. **Item 10** torna o passo dirigido por tempo, com retencao de ordem zero. Refuto apenas a correcao acessoria: declarar falha por "3 retencoes em 10 ciclos" contradiz a BASE COMUM (3 invalidas CONSECUTIVAS, 5 boas para recuperar, permanencia de 2000 ms) e nao e necessario, porque com passo dirigido por tempo o tau nao se degrada — a perda esparsa custa atraso de transporte (~75 ms), nao 3x tau. Em lugar dela entrou o diagnostico do item 11, que informa sem mexer em rele.
- *Default de 0,2 s nao sobrevive a vibracao real (seguranca 4).* Certo, com a conta refeita e confirmada acima. **Default trocado para 0,8 s.** Nao adotei a grade proposta (0,0 / 0,8 / 1,6 / 3,2 com 0,0 bloqueado): o 0,0 s simplesmente nao existe no menu, o que e mais simples e mais seguro do que um item bloqueado por modo, e o degrau de 0,2 s permanece porque e o unico util em estrutura rigida sem balanco.
- *Transicao indefinida na saida da programacao (seguranca 5).* Certo, e o rascunho criava o defeito sozinho. **Itens 9 e 15**: o filtro nunca e invalidado por commit de parametro nem por entrada/saida do Modo Programacao, logo nao ha pulso de rele. O que os reles fazem DURANTE o Modo Programacao nao e da decisao 4 — e a pendencia humana "Momento da efetivacao dos parametros editados" (decisoes 3 e 5).
- *Aliasing deixado em aberto, e a alternativa (c) descartada por motivo errado (seguranca 6).* Certo: uma decimacao FIXA na sensora nao e parametro de menu e nao exige nenhuma funcao de escrita Modbus, entao a justificativa do rascunho era falsa. **Adotado no item 2**, com o argumento quantitativo que faltava (os zeros do boxcar de 5 caem exatamente nos centros de alias da reamostragem de 20 Hz).
- *Retificacao de vibracao (seguranca 7).* Certo: o angulo sai de um `atan2` e oscilacao simetrica de aceleracao produz vies DC de angulo, que nenhum passa-baixa a jusante remove. Isso reforca o MODE 4, porque o LPF de 10 Hz do SCL3300 atua sobre a ACELERACAO, antes do calculo do angulo — e o unico ponto da cadeia onde a retificacao pode ser atacada. Virou medicao obrigatoria (medicao 15).
- *Preset mal definido (seguranca 8, fidelidade 4).* Certo: "subtracao inteira" nao diz subtracao de que, e 5.6 define o Preset como o valor que deve ser INDICADO. A decisao 4 nao e dona do Preset (isso e PSET/decisao 1), mas fixa o que lhe cabe: **o filtro fica a montante de Sentido e Preset (item 7)**, o que torna o commit de Preset um degrau exato sem transitorio de filtro, e a definicao do offset persistente e obrigacao da decisao 1.

**Refutado, com evidencia:**
- *Fidelidade (1): "a histerese contradiz L205 e a proposta mente ao chamar o manual de omisso".* A critica esta certa sobre o manual — L205 e uma comparacao pura — e por isso a decisao 4 **retirou a histerese do seu escopo** (item 15). Mas a critica esta errada ao sugerir que a solucao e nao ter banda nenhuma: com o angulo parado exatamente no limite e ruido de sensor da ordem do proprio decimo, a comparacao literal bate o contato do AX1RC indefinidamente. A banda tem de existir, tem de ser UMA (5 decimos, decisao 5) e tem de estar escrita no manual como nota nova em 5.9 — o que e pendencia humana, nao licenca do firmware.
- *Fidelidade (5): "ate 5,91 s de atuacao e inaceitavel".* Aceito o diagnostico, rejeito a correcao proposta ("teto de 0,8 s no menu, retirando 3,2 s"). Retirar o 3,2 s nao resolve o caso fisico que o exige (balanco a 0,3 Hz), e empurra o cliente para desligar o limite — que e pior. A recarga do item 6 baixa o pior caso real para 0,43 s em qualquer ajuste para excursoes >= 2,0 grau, e a coluna de latencia do item 12 publica o resto. O numero deixa de ser escondido, que era a objecao legitima.
- *Fidelidade (4 do rascunho da decisao 5) e citacoes.* As linhas citadas pelo rascunho (62, 133, 192, 214, 262, 263) vem do arquivo de trabalho do levantamento, nao do manual do repositorio. Todas foram reconferidas e reescritas contra `/home/ubuntu/repos/SUI/docs/manual-cliente-sui-2026.txt`: filtro em L71 e L272, resolucao/ponto de atuacao em L142, limites em L201/L205-207/L223, menu em L112, Tabela 1 em L115-134, Tabela 2 em L250-268.

### Precisa de decisao humana
1. **Filtro como parametro de menu ou fixo em 0,8 s.** Opcao A (recomendada): parametro "Filtro" com 4 degraus, nova linha na Tabela 1 e na Tabela 2, nova tela `Filtro(s):0,8`, errata do manual. Opcao B: filtro fixo em 0,8 s, sem item de menu, e reescrita de L71 para "filtro passa-baixa fixo, com tempo de resposta de 0,8 s", perdendo a promessa de "adequar o tempo de resposta". Recomendacao: A, porque a promessa de ajuste esta escrita duas vezes; contra-argumento honesto que o bigboss precisa pesar: L45 vende o produto como "dispensando parâmetros" e cada parametro novo e um erro de campo a mais.
2. **Default de fabrica: 0,8 s (recomendado) ou 0,2 s.** 0,8 s e o menor ajuste que mantem um balanco de +/-0,5 grau a 1 Hz dentro da histerese de 0,5 grau e evita o batimento do contato; custa 1,16 s de atraso em rampa contra 0,56 s. Recomendacao: 0,8 s.
3. **Manter o degrau de 3,2 s no menu.** Manter (recomendado): unico ajuste que rejeita balanco pendular de 0,3 Hz (-15,7 dB), com 3,56 s de atraso em rampa publicados na Tabela 1. Retirar: teto de 1,6 s, latencia maxima 1,96 s, e o cliente com portico oscilante fica sem opcao alem de desativar o limite. Recomendacao: manter e publicar o numero.
4. **Recarga por salto de 2,0 grau: aprovar como comportamento de produto e escrever no manual.** Aprovar (recomendado): e a unica forma de limitar a latencia sem criar um segundo numero comandando rele; exige paragrafo novo em 5.9 e nota na Tabela 1. Nao aprovar: o ajuste 3,2 s passa a valer integralmente para toda excursao, com os atrasos da coluna "rampa" da tabela do item 12.
5. **Alteracao do firmware da sensora (MODE 4 + media rolante de 5).** Aprovar (recomendado): sem ela nao existe anti-aliasing na cadeia e o "filtro passa-baixa" de L71 e cosmetico. Custo: uma linha em `sensor/src/main.cpp:37`, ~20 linhas de acumulador, revalidacao da sensora, e atualizacao de `docs/protocolo-rs485.md` secao 6 para declarar que os registradores 0/1/2 passam a ser media de 5 amostras de 10 ms e nao mais valor instantaneo (o contrato de fio nao muda; a semantica do conteudo muda). Nao aprovar: a decisao 4 fica com um estagio 1 inexistente e o aliasing permanece como risco aberto e nao mitigavel na UR.
6. **Errata do manual.** Confirmar que a Tabela 1 recebe a linha do Filtro MAIS a coluna de latencia de atuacao por ajuste, e que 5.9 recebe a nota da recarga por salto. Sem isso, o equipamento passa a ter um atraso de ate 3,56 s que o manual nao declara.

### Precisa de medicao de bancada
- **MEDICAO 14 — ESPECTRO DE INCLINACAO E VIBRACAO NO PONTO DE FIXACAO (bloqueia o default e a grade de degraus).** Registrar 10 min com o equipamento monitorado em operacao e 10 min parado, com a sensora publicando a 100 Hz pelo console (dado cru, MODE 1 e MODE 4, um registro para cada), no ponto real de fixacao do sensor. Calcular a PSD e a amplitude pico a pico nas bandas 0,1-0,5 Hz (balanco pendular), 0,5-2 Hz (modo estrutural) e acima de 10 Hz (motor/redutor). ACEITACAO: com o ajuste de 0,8 s, a amplitude pico a pico do valor filtrado tem de ficar <= 0,5 grau (a histerese da decisao 5) na condicao de operacao normal. Se reprovar, o default sobe para 1,6 s. Se a banda acima de 10 Hz tiver amplitude comparavel a util, o MODE 4 e obrigatorio, nao recomendado.
- **MEDICAO 15 — VIES DE RETIFICACAO DE VIBRACAO (entra no orcamento de +/-0,1 %FE de L38).** Dos mesmos dois registros da medicao 14, comparar a MEDIA do angulo com a maquina em operacao e com a maquina parada, na mesma posicao mecanica, por eixo. ACEITACAO: diferenca <= 0,03 grau. Se passar, o texto de L38 tem de ser revisto — nao o filtro, porque nenhum passa-baixa a jusante do `atan2` remove vies DC.
- **MEDICAO 16 — TAU FIM A FIM E LATENCIA DE ATUACAO POR AJUSTE (valida a Tabela 1).** Mesa de inclinacao com degrau mecanico rapido, ou injecao de quadro Modbus sintetico pelo console (`rs485` do firmware de teste), aplicando: (a) degrau de 6,0 grau contra limite de 5,0 grau (dispara a recarga do item 6) e (b) degrau de 1,0 grau cruzando limite a 0,5 grau (nao dispara). Cronometrar com osciloscopio de dois canais: canal 1 no DE do RS-485 da UR (instante do quadro que carrega o degrau), canal 2 no GPIO do rele correspondente. 20 repeticoes por ajuste, nos 4 ajustes. ACEITACAO: caso (a) <= 0,50 s em todos os quatro ajustes; caso (b) dentro de +/-10 % dos valores publicados no item 12.
- **MEDICAO 17 — RAMPA LENTA SOBRE O LIMITE (prova da ausencia de zona morta).** Mesa de inclinacao a 0,1 grau/min cruzando o limite programado, nos ajustes 0,2 s e 3,2 s, com o valor filtrado e o estado do rele registrados pelo console a cada 50 ms. ACEITACAO: o valor filtrado alcanca o valor cru em todo ponto da rampa (diferenca <= 1 decimo em regime) e o rele atua com atraso <= tau + 0,382 s em relacao ao cruzamento fisico. Reprovacao aqui invalida o passo minimo do item 5.
- **MEDICAO 18 — TEMPO DE EXECUCAO DO TICK DA TAREFA ctrl COM O FILTRO (orcamento do ciclo).** GPIO livre levantado na entrada e baixado na saida do tick de 50 ms da tarefa ctrl, com transacao + mediana + EMA + avaliacao dos limites + escrita de rele e DAC; osciloscopio, 1000 ticks, registrar maximo e mediana. ACEITACAO: contribuicao do filtro <= 20 us para os dois eixos, e tick completo <= 25 ms (metade do periodo).


---

## Decisao 5 - Filtro, histerese e temporizacao dos reles de limite

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto
**REQ afetados:** MAN-2.1-L38 (+/-0,1% da escala maxima), MAN-2.1-L44 (quatro saidas a rele, limites em angulo com resolucao de 0,1 grau), MAN-4-L71 (filtro passa-baixa e tempo de resposta), MAN-5.4-L115..L134 (Tabela 1), MAN-5.5-L142 (ponto de atuacao "exatamente no angulo desejado"), MAN-5.8-L199 (inversao de sinal pelo Sentido do Sensor), MAN-5.9-L201, MAN-5.9-L204..L208 (Off, >=, <=, + e o exemplo de 5,0 graus), MAN-5.9-L223 (limites referem-se a leitura do display), MAN-5.11-L250..L268 (Tabela 2), MAN-6-L272 (filtros contra vibracao), MAN-7-L306 (falha de comunicacao), MAN-8-Tabela-4-L331..L345 (NA/NF, padrao de fabrica NF), DIR-01/DIR-02 (ordem Sentido -> Preset), src/app/limit_engine.{h,cpp} (a criar), src/app/ctrl_task.{h,cpp} (a criar), src/drivers/relays.cpp, include/iface/idigital_output_bank.h, sensor/src/main.cpp:26,37,151-158, sensor/src/drivers/scl3300.h:25

### O que o manual diz

Numeracao verificada por `grep -n` no arquivo real /home/ubuntu/repos/SUI/docs/manual-cliente-sui-2026.txt (todos os rascunhos dec_01..dec_12 citam linhas deslocadas em ~9; use estas):

- L204: "Off: limite desativado; o relé permanece em repouso, independentemente do ângulo medido."
- L205: "≥ (maior ou igual): o relé é acionado sempre que o ângulo medido for maior ou igual ao valor programado."
- L206: "≤ (menor ou igual): o relé é acionado sempre que o ângulo medido for menor ou igual ao valor programado."
- L207: "+ (módulo): o relé é acionado sempre que o módulo do ângulo medido for maior ou igual ao módulo do valor programado, ou seja, para inclinações em qualquer um dos dois sentidos."
- L208: "Exemplo: com a Operação do Limite 1 programada em + e o Limite 1 ajustado em 5,0°, o relé do Limite 1 é acionado sempre que a inclinação do eixo X ultrapassar 5,0° para qualquer um dos lados."
- L223: "Importante: Os valores programados nos Limites 1 a 4 são sempre expressos em graus, com resolução de 0,1°, e referem-se à leitura apresentada no display, ou seja, já consideram o Preset e o Sentido do Sensor configurados para o respectivo eixo."
- L142: "Observação: A resolução de 0,1° aplica-se tanto à indicação no display quanto à programação dos limites e do preset, garantindo que o ponto de atuação dos relés seja ajustado exatamente no ângulo desejado."
- L71: "Com o objetivo de reduzir oscilações provocadas por vibrações mecânicas ou pequenas variações transitórias na medição, o Supervisor de Inclinação dispõe de um filtro passa-baixa, permitindo adequar o tempo de resposta do equipamento às características de aplicação."
- L272: "O equipamento dispõe de filtros capazes de reduzir os efeitos de pequenas oscilações e vibrações presentes na aplicação."
- L38: "Precisão, linearidade e repetibilidade: ±0,1% da escala máxima;"
- L199: "Observação: A alteração do sentido do sensor inverte o sinal da leitura."

### A lacuna

O manual define **apenas o ponto de ataque**, por comparacao instantanea de igualdade. Nao define:
(a) ponto de repouso — com "a >= L" literal, o rele libera no mesmo decimo em que atua;
(b) tempo de confirmacao antes de atuar;
(c) duracao minima do estado sinalizado (o CLP tem de enxergar o evento);
(d) teto de taxa de comutacao (vida do contato);
(e) constante de tempo do filtro passa-baixa de L71 — nao existe parametro de filtro na Tabela 1 (L115..L134) nem na Tabela 2 (L250..L268), e **nao existe filtro nenhum no firmware da sensora** (sensor/src/main.cpp:151-158 publica o valor cru a 100 Hz);
(f) ordem de aplicacao Sentido/Preset/grampo/filtro/comparacao;
(g) o que a comparacao faz com amostra invalida, e o que "Off" significa quando o enlace cai;
(h) o caso degenerado "+" com limite 0,0 graus.

Lacuna adicional, nao vista pelo rascunho e **capital**: a UR amostra a 20 Hz (base comum, poll de 50 ms) um sinal publicado a 100 Hz cujo unico passa-baixa e o do SCL3300 em MODE 1, com corte de 40 Hz (sensor/src/drivers/scl3300.h:25, mode = 1 por default de construtor). Toda energia acima de 10 Hz dobra para dentro da banda base **antes** de qualquer filtro digital da UR. Nenhum filtro a jusante remove isso.

### Proposta

Motor de limites como dominio puro em src/app/limit_engine.{h,cpp}: int16 em decimos de grau, sem float, sem heap, sem dependencia de Arduino, compilavel e testavel em env:native (molde de src/drivers/calibration.cpp). Executa dentro da tarefa `ctrl` (base comum: core 0, prioridade 5, periodo 50 ms). O loop() nunca escreve rele.

1. **CADEIA ANTI-ALIASING, PRE-REQUISITO DE TUDO (muda a sensora).** (1a) SCL3300 em **MODE 4** (passa-baixa de 10 Hz, 12000 LSB/g), fixado como constante nomeada em sensor/include/board_pins.h e passado explicitamente em sensor/src/main.cpp:37 — nao mais como default de construtor. Faixa e resolucao nao mudam (escala fixa 90/2^14, ver lev_scl3300-faixa.md); custa 100 ms de settle no boot em vez de 25 ms. (1b) A sensora passa a publicar em kRegAngleX/Y/Z a **media movel de N = 10 leituras** (janela de 100 ms), acumulada no dominio bruto do sensor (LSB de 0,0055 grau) e arredondada uma unica vez para decimo de grau na publicacao, atualizada a cada 10 ms. O mapa de registradores (sensor/include/sensor_map.h) **nao muda**: o contrato de fio e preservado byte a byte. (1c) Correcao obrigatoria de sensor/src/main.cpp:151-158: no ramo de falha, **atribuir** o status inteiro (nao `|=`) e invalidar os angulos; hoje o registrador 3 pode valer DATA_VALID e NOT_RESPONDING ao mesmo tempo sobre angulo velho. Justificativa numerica: a media de 10 a 100 Hz tem nulos exatos em 10, 20, 30, 40... Hz, isto e, **em todas as frequencias que aliasam para DC na decimacao de 100 Hz para 20 Hz** — e o desenho classico de decimador. Em 15 Hz (pior meio-lobo) a media da -13,1 dB e o LPF de 10 Hz do MODE 4 da -5,1 dB, total -18,2 dB, e o residuo cai em 5 Hz na banda base, onde o filtro da UR (item 2) da mais -16 dB.

2. **FILTRO DA UR: EMA DE UM POLO, INTEIRO, Q8.** Estado `int32 s` em unidades de 1/256 de decimo de grau, por eixo. Atualizacao uma vez por ciclo de 50 ms: `s += (((int32)x << 8) - s) * 57 >> 8` (deslocamento aritmetico). **kEmaAlphaQ8 = 57** (alfa = 0,22266), unico numero do filtro; em T = 50 ms isso da **tau efetiva = 198,5 ms** (nominal 200 ms). Saida em decimos de grau por arredondamento simetrico unico: `y = (s >= 0) ? (s + 128) >> 8 : -((-s + 128) >> 8)`. Resposta declarada: -3 dB em **0,802 Hz**; **-8,6 dB em 2 Hz**, **-19,5 dB em 7,5 Hz**, **-21,9 dB em 10 Hz**, monotonica, sem lobos laterais. Zona morta do truncamento: <= 5/256 de decimo = 0,002 grau, 25x abaixo do quantum de display — nao ha acumulador de residuo, por desnecessario. Faixas: x limitado a +/-1800 decimos, `x << 8` = +/-460800 e o produto maximo 5,25e7, ambos folgados em int32.

3. **ORDEM CANONICA, ESCRITA NO CABECALHO DE limit_engine.h:** amostra do fio -> **teste de validade** -> **EMA sobre o valor BRUTO** -> arredondamento unico para decimo -> **Sentido do Sensor** (troca de sinal, exata) -> **Preset** (soma do offset, exata) -> **grampo em +/-900 decimos** -> valor unico que alimenta display, saida analogica e os quatro comparadores. Filtrar o bruto (e nao o valor exibido) e o que torna commit de Preset/Sentido um degrau instantaneo, sem rampa de artefato, e coloca o grampo depois do filtro, resolvendo a ordem em disputa entre as decisoes 5 e 7. Cumpre L223 por construcao: o numero comparado e literalmente o numero exibido.

4. **VALIDADE DA AMOSTRA (fecha o dominio).** Amostra so entra no filtro se `(status & kStsDataValid) != 0` **e** `(status & (kStsSclCrcError | kStsSclSelfTestFail | kStsSclNotResponding | kStsSaturated)) == 0` (sensor/include/tilt.h:15-21). Amostra invalida: nao entra no filtro, **congela** os contadores de confirmacao (nao os zera) e nao muda estado de rele. Tres transacoes invalidas consecutivas (150 ms, base comum) entregam os quatro reles ao caminho de falha da decisao 7/8. Na recuperacao, o filtro e **recarregado** com a primeira amostra boa (`s = x << 8`), sem rampa, e os contadores sao zerados.

5. **HISTERESE = 3 decimos de grau (0,3 grau), assimetrica, so na liberacao.** O ataque e exato no valor programado, nos tres modos:
   - `>=`: ataca em `a >= L`; libera em `a <= L - 3`.
   - `<=`: ataca em `a <= L`; libera em `a >= L + 3`.
   - `+`: ataca em `|a| >= |L|`; libera em `|a| <= |L| - 3` quando `|L| >= 3`; quando `|L| < 3` nao existe ponto de liberacao (ver item 6).
   Um numero, nao dois: 0,3 grau e o menor multiplo inteiro do digito do display que supera com 3x de folga o ruido residual pico a pico previsto da cadeia do item 1 (<= 1 digito). Nao ha ganho em pagar os 0,5 grau do rascunho: histerese nao resolve balanco pendular (item 7 resolve), so ruido.

6. **DEGENERADO "+" COM |L| <= 2 decimos.** DESVIO DO MANUAL: nenhum. Leitura literal de L207 — `|a| >= 0` e sempre verdadeiro, entao o rele fica **permanentemente atacado**, e a liberacao nunca ocorre. Comportamento especificado, coberto por teste native, e sem efeito de fabrica (Tabela 2, L258-259 e L262-263, entrega Limite 2 e Limite 4 em +000,0 graus **com Operacao Off**).

7. **TEMPORIZACAO.** Base de tempo = 50 ms (base comum), avaliacao no mesmo tick do poll.
   - **Confirmacao de ataque: 2 ciclos consecutivos = 100 ms** (kAttackConfirmCycles = 2).
   - **Confirmacao de liberacao: 60 ciclos consecutivos = 3000 ms** (kReleaseConfirmCycles = 60).
   - **Nao existe parametro separado de permanencia minima.** Os 3000 ms de liberacao ja garantem que todo estado sinalizado dure no minimo 3000 ms a partir da atuacao — 30x a varredura tipica de 100 ms de um CLP portuario. Um parametro a menos que o rascunho (que tinha 200 ms de liberacao + 1000 ms de permanencia) e a ambiguidade da critica de completude some.
   - **TETO DE COMUTACAO (anti-chatter):** cada rele mantem um contador deslizante de ataques nos ultimos **600 s** (12 baldes de 50 s). Enquanto esse contador for **>= 20**, a confirmacao de liberacao daquele rele passa de 3000 ms para **60000 ms**; volta a 3000 ms quando cair a **<= 5**. A extensao sempre prolonga o estado de ALARME, nunca o atrasa: o erro fica no sentido seguro. Efeito: o pior caso de balanco pendular cai de ~27.000 para ~1.400 comutacoes/dia.
   - **CONTADOR DE VIDA:** contagem total de ataques por rele em uint32, mantida em RAM pela tarefa ctrl, persistida em NVS pelo loop() a cada **100** comutacoes (aprox. 1000 escritas por rele em toda a vida util) e alerta de fim de vida em **100000** comutacoes, visivel no console e na tela de diagnostico.

8. **COMMIT DE PARAMETRO (Preset, Sentido, Limite, Operacao).** A edicao mexe so em rascunho; no commit, a tarefa ctrl reavalia **no mesmo tick** com os contadores zerados. A confirmacao de ataque de 100 ms continua valendo; a temporizacao de liberacao de 3000 ms **nao se aplica a transicao causada por commit** (acao deterministica do operador, nao medida ruidosa). Consequencia pratica: programar Operacao = Off libera o rele imediatamente, sem 3 s de espera.

9. **"Off".** DESVIO DO MANUAL (L204): o rele fica no estado NAO sinalizado, que sob a polaridade fail-safe recomendada na base comum (kRelayFailSafePolarity = true) e **bobina energizada**, e nao "em repouso" como diz L204. Sob a polaridade A o texto de L204 e cumprido literalmente. A escolha e a mesma decisao de polaridade do bigboss; esta decisao apenas para de esconder a consequencia atras de referencia cruzada.

10. **"Off" em falha de enlace.** DESVIO DO MANUAL (parcial, L204): declarada a falha (3 transacoes invalidas = 150 ms), **os quatro reles vao ao estado de alarme/nao-permissivo, inclusive os programados em Off**. L204 diz "independentemente do ângulo medido" e isso continua verdadeiro: falha de enlace nao e angulo, e uma falha do equipamento. Um canal cabeado e deixado em Off recebe a indicacao de que a UR nao esta saudavel, que e o sentido seguro.

11. **LATENCIA DE ATAQUE, DOIS CENARIOS DEFINIDOS, MEDIDOS NO CONTATO** (medicao 14): (i) **degrau franco** (entrada salta para o dobro do limite): 80 ms (MODE 4) + 45 ms (media de 10 da sensora) + 138 ms (tau*ln2) + 100 ms (confirmacao) + 72 ms (idade maxima do dado, base comum) + 15 ms (operacao do rele) = **450 ms calculado, teto de projeto 500 ms**; (ii) **degrau marginal** (parte de 5,0 graus aquem e termina 1,0 grau alem do limite): 80 + 45 + 356 (tau*ln6) + 100 + 72 + 15 = **668 ms calculado, teto de projeto 750 ms**. Reprovar a medicao 14 e reprovar esta decisao, nao ajustar o texto.

12. **FILTRO FIXO, COM AJUSTE DE COMISSIONAMENTO PELO CONSOLE.** DESVIO DO MANUAL (L71): a Tabela 1 nao tem parametro de filtro e nao passara a ter. kEmaAlphaQ8 = 57 (200 ms) e o **default de fabrica**; o comando de console `filtro <50..2000>` (ms, passo de 50) grava a constante de tempo em NVS, e a tarefa ctrl converte para alfa Q8 uma unica vez, no commit. Isso entrega literalmente "adequar o tempo de resposta do equipamento as caracteristicas de aplicacao" (L71) no comissionamento, sem tocar no menu de painel, sem novo item na Tabela 1 e sem novo digito na Tabela 2. Exige errata de L71 declarando o default de 200 ms e que o ajuste e de comissionamento, nao de operacao.

13. **ERRATA OBRIGATORIA DE 5.9.** Acrescentar em 5.9, apos L207: histerese de 0,3 grau **somente na liberacao** (o ponto de ataque permanece exato, o que preserva L142), confirmacao de ataque de 100 ms, permanencia minima de 3000 ms no estado sinalizado, teto de latencia de ataque de 750 ms, e a instrucao de que os limites devem ser programados fora da faixa de oscilacao normal da estrutura.

14. **ENSAIO FUNCIONAL PERIODICO.** DESVIO DO MANUAL (acrescimo): nao ha readback em ponto nenhum do caminho de atuacao (RelayBank::get() devolve cache de escrita; nao ha realimentacao de contato). Contato colado ou BC337 em curto e indetectavel por firmware. O manual passa a exigir ensaio funcional dos quatro limites a cada **6 meses**, pelo proprio Modo Programacao (programar o limite abaixo da leitura corrente, conferir rele e LED do painel, restaurar), registrado no relatorio de manutencao.

15. **O LED DO PAINEL SEGUE O RELE POR HARDWARE.** Os LEDs penduram no proprio net do GPIO, antes do resistor de base (lev_display-teclado.md:30, include/board_pins.h:59-64). Toda temporizacao acima vale identicamente para o LED: nao existe possibilidade de LED rapido com rele lento.

### Por que

O defeito capital do rascunho nao era histerese nem permanencia, era **aliasing**: sem o item 1, o valor "filtrado" da UR carrega vibracao de motor e modo estrutural dobrada para dentro da banda base, com amplitude arbitraria e sem assinatura no display — alarme falso e alarme perdido ao mesmo tempo. Media de 10 a 100 Hz coloca nulos exatos em todos os multiplos de 10 Hz, que sao exatamente as frequencias que aliasam na decimacao 100 Hz -> 20 Hz; MODE 4 custa zero em faixa e em resolucao e ainda reduz o ruido RMS pela metade (lev_scl3300-faixa.md).

O EMA de um polo substitui a media movel porque boxcar **nao e passa-baixa**: N = 8 tem primeiro lobo lateral em -13 dB, entao vibracao de banda estreita passa quase intacta em pontos previsiveis. O EMA e monotonico e a rejeicao pode ser declarada por frequencia (-8,6 / -19,5 / -21,9 dB em 2 / 7,5 / 10 Hz), que e o que um documento de seguranca precisa afirmar. Em Q8 com estado em int32 o custo e uma multiplicacao e um deslocamento por eixo por ciclo, e o erro de arredondamento acontece **uma unica vez, no mesmo ponto em que o display arredonda** — nao ha segunda quantizacao somando ao orcamento de +/-0,1% de L38.

Histerese de 0,3 grau em vez de 0,5 grau porque a funcao dela e vencer ruido residual, nao balanco pendular. Balanco e inclinacao **real** e nao pode ser filtrado; quem protege o contato e a temporizacao de liberacao de 3000 ms mais o teto de comutacao adaptativo — e ambos erram no sentido de manter o alarme por mais tempo. Filtro rapido para atacar (o ataque nao espera 3 s) e lento para desalarmar: todo o erro no sentido seguro.

Fundir permanencia minima e confirmacao de liberacao num unico numero elimina o conflito apontado pela critica de completude (dois parametros discordantes entre as decisoes 4 e 5) e reduz o contrato a cinco constantes: 57, 3, 2, 60 e 50 ms.

### O que a revisao adversarial derrubou

**Lente de seguranca — cedido integralmente nos itens 1, 2, 4, 5, 6, 7 e 8:**
- (1) Anti-aliasing: **procede e era o defeito capital**. Corrigido no item 1 da proposta, com a filtragem indo para onde estao as amostras (a sensora), com numeros (nulos em multiplos de 10 Hz, -18,2 dB no pior meio-lobo de 15 Hz).
- (2) "Media movel nao e passa-baixa": **procede**. Boxcar N=8 substituido por EMA de um polo, com -3 dB **e** rejeicao declarados ponto a ponto.
- (3) Balanco pendular e vida do contato: **procede**. Adotados o teto de comutacao adaptativo (janela de 600 s, entra em 20, sai em 5, liberacao estendida para 60 s) e o contador de vida em NVS com alerta em 100000. **Refutado apenas o latch com rearme manual**: latch converte o rele em memoria de evento, e L205-L207 definem os quatro modos como condicao de nivel ("acionado sempre que"); alem disso nao existe tecla de rearme no manual — MENU e acesso a programacao, protegido por senha, e um rearme por senha significa que ninguem rearma no turno. Fica registrado como pendencia do bigboss, com recomendacao de nao adotar.
- (4) Degrau de Preset/Sentido: **procede o risco, mas a correcao proposta (recarregar o buffer) e desnecessaria** na ordem adotada. Filtrando o valor **bruto** e aplicando Sentido/Preset depois, o commit e um degrau exato no valor exibido, sem rampa e sem valores intermediarios que nao existiram. Sobra apenas a reavaliacao com contadores zerados (item 8).
- (5) Ordem filtro x grampo indefinida: **procede**. Fixada no item 3 e obrigada a viver no cabecalho de limit_engine.h.
- (6) Amostra invalida dentro da janela: **procede**. Fechada no item 4, com os bits reais de sensor/include/tilt.h:15-21, o que torna o motor um dominio fechado e testavel em native.
- (7) Degenerado "+ com L = 0": **procede**. Especificado no item 6 como permanentemente atacado, com teste.
- (8) Periodo de 25 ms invalido: **procede**. Substituido por 50 ms conforme a base comum, e todos os numeros derivados foram refeitos (nao apenas reescalados): o filtro passou a ser especificado como constante de tempo (198,5 ms) e convertido em alfa nesse periodo, exatamente como a base comum exige.
- (i) Ensaio de prova periodico: **procede**. Item 14, com intervalo declarado de 6 meses.

**Lente de fidelidade — cedido nos itens 1, 2, 3 e 5; refutado no item 4:**
- (1) O rascunho declarava L71 cumprido com filtro fixo: **procede a acusacao**. Agora o item 12 e explicitamente DESVIO DO MANUAL, entrega o ajuste de tempo de resposta pelo console no comissionamento e pede errata de L71.
- (2) "Off" sob polaridade fail-safe contradiz L204 e o rascunho escondia isso atras da decisao 7: **procede**. Item 9, dito na cara.
- (3) Histerese e permanencia contra L142 e L205-L207: **procede em parte**. O ataque continua exato — L142 fala do **ponto de atuacao**, e ele nao se desloca em nenhum dos tres modos. O que se desloca e a liberacao, que o manual nunca especificou. Ainda assim a errata do item 13 e obrigatoria, porque o rele fica acionado por ate 3000 ms com a condicao de L205-L207 ja falsa.
- (4) Correcao das citacoes: **a critica esta certa de que o rascunho errou, e errada nos numeros que propos**. Verificado por `grep -n` no arquivo real: o texto do filtro esta em **L71** (nao 62 nem 63), "O equipamento dispoe de filtros" esta em **L272** (nao 262 nem 263), a Tabela 1 ocupa **L115..L133** com legenda em **L134** (nao 106-123 nem 107-123), 5.9 vai de **L200** a **L223** (nao 192-214) e a observacao da resolucao esta em **L142** (nao 133). O deslocamento de ~9 linhas contamina **todos** os rascunhos dec_01..dec_12; esta decisao usa a numeracao real e recomenda revisar as demais.
- (5) Orcamento de exatidao: **a critica esta errada no mecanismo**. O meio decimo extra so existia porque o boxcar dividia por 8 no caminho de saida. No EMA em Q8 a fracao fica no estado (int32) e o arredondamento acontece uma unica vez, no mesmo ponto em que o display arredonda: nao ha segunda quantizacao. O orcamento contra L38 (+/-0,09 grau em 90 graus) continua consumido em +/-0,05 grau pelo formato de decimo de grau da sensora, e mais nada e somado pela UR — e por isso Preset e Sentido tem de continuar sendo soma e troca de sinal em inteiro, jamais ganho fracionario.

**Critica de completude — "decisoes 4 e 5 sao duas decisoes para o mesmo parametro":** cedido. Esta decisao absorve o parametro por inteiro e escolhe: forma do filtro da decisao 4 (EMA Q8), politica de filtro fixo da decisao 5 (sem item novo na Tabela 1), histerese de 0,3 grau da decisao 4, temporizacao da decisao 5 refeita em 50 ms. **Rejeitado o "trip rapido" da decisao 4**: um segundo caminho, mais rapido, comandando o mesmo rele cria um segundo numero de decisao que nao e o numero do display, contra L223.

### Precisa de decisao humana

1. **Filtro fixo x parametro de menu.** Opcoes: (a) fixo em 200 ms com ajuste de comissionamento pelo console (proposta, item 12) — Tabela 1 e Tabela 2 intactas, errata em L71; (b) novo parametro "Filtro" na Tabela 1 e na Tabela 2 — muda menu, manual, etiqueta e a decisao 3, que ja fecha em 16 parametros. **Recomendacao: (a).**
2. **Valor da histerese.** Opcoes: 0,3 grau (proposta) x 0,5 grau (rascunho). **Recomendacao: 0,3 grau**, condicionada a medicao 16 (ruido residual parado <= 1 digito).
3. **Confirmacao de liberacao de 3000 ms.** Se o contato for usado para **religar** movimento, esses 3 s viram atraso de producao a cada evento. Opcoes: 3000 ms (proposta) x 500 ms (mais rapido, com o teto de comutacao trabalhando muito mais). **Recomendacao: 3000 ms.**
4. **Latch de alarme com rearme manual.** Opcoes: sem latch (proposta, nivel puro, fiel a L205-L207) x latch com rearme por MENU (exige senha, exige reescrever 5.9). **Recomendacao: sem latch.**
5. **Semantica de "Off" sob a polaridade escolhida.** Amarrada a decisao de polaridade do bigboss. Sob a opcao B (fail-safe), "Off" = bobina energizada, contra a letra de L204, e 5.9 tem de ser reescrito. **Recomendacao: aprovar junto com a polaridade, num unico ato.**
6. **"Off" vai a alarme em falha de enlace.** Opcoes: sim (proposta, item 10) x manter Off inerte a falha. **Recomendacao: sim** — um canal cabeado em Off passa a denunciar a UR doente.
7. **Tetos de latencia de ataque: 500 ms (degrau franco) e 750 ms (degrau marginal).** Exige que o cliente confirme que a estrutura monitorada nao alcanca condicao perigosa nesse tempo. Se alcançar, kEmaAlphaQ8 sobe para 73 (tau = 149 ms), custando 1,15x mais ruido. **Recomendacao: aprovar 500/750 ms e declara-los no manual.**
8. **Alteracao do firmware da sensora, hoje validado (MODE 4, media de 10, correcao do ramo de falha de main.cpp:151-158).** Sem isso o item 1 nao existe e os numeros de rejeicao desta decisao sao ficcao. **Recomendacao: aprovar; o mapa Modbus nao muda e a UR nao precisa saber.**
9. **Ordem Sentido -> Preset (DIR-01/DIR-02).** Esta decisao fixa `bruto -> sentido -> preset`, porque o offset de Preset e gravado ja em coordenada de display e L199 recomenda refazer o Preset apos trocar o sentido — o que so faz sentido nesta ordem. **Recomendacao: ratificar na decisao dona do Preset, sem duplicar a regra.**
10. **Intervalo do ensaio funcional periodico dos quatro limites.** Opcoes: 6 meses (proposta) x 12 meses. **Recomendacao: 6 meses**, por nao existir readback nenhum no caminho de atuacao.

### Precisa de medicao de bancada

- **MEDICAO 14 (nova) — LATENCIA PONTA A PONTA, ANGULO -> CONTATO.** Mesa divisora ou gabarito de degrau mecanico na sensora; canal 1 do osciloscopio num gatilho mecanico solidario ao degrau, canal 2 no contato seco do rele do Limite 1. Dois cenarios, 30 repeticoes cada: (i) degrau franco para o dobro do limite programado; (ii) degrau marginal, de 5,0 graus aquem para 1,0 grau alem do limite. ACEITACAO: (i) <= 500 ms, (ii) <= 750 ms, em todas as repeticoes. Reprovou, reprova a decisao.
- **MEDICAO 15 (nova) — REJEICAO DE VIBRACAO E ANTI-ALIASING.** Sensora em excitador eletrodinamico, amplitude angular de 1,0 grau pico, varredura por pontos em 2, 5, 7,5, 10, 15, 20, 25, 30, 50 e 60 Hz, com a UR completa no laco. Registrar o **desvio do valor exibido em relacao a leitura estatica** e a contagem de comutacoes com um limite programado 0,5 grau acima da leitura estatica, por 10 min em cada frequencia. ACEITACAO: desvio <= 0,1 grau (1 digito) em todas as frequencias acima de 10 Hz e **zero** comutacao. Rodar antes e depois do item 1 da proposta: espera-se que a configuracao atual (MODE 1, sem media na sensora) REPROVE — e a reprovacao medida que justifica mexer no firmware da sensora.
- **MEDICAO 16 (nova) — RUIDO RESIDUAL COM A ESTRUTURA PARADA (valida a histerese de 0,3 grau).** Sensora em bancada de granito, 1 hora continua, log do valor exibido pelo console a 1 Hz, nos dois eixos, em 0,0 grau e em 45,0 graus. ACEITACAO: excursao pico a pico <= 1 digito (0,1 grau) e desvio padrao <= 0,03 grau. Se der mais que 1 digito, a histerese sobe para 0,5 grau e o item 2 das pendencias humanas muda de resposta.
- **MEDICAO 6 (base comum) — round-trip Modbus com o cabo de 500 m.** Pre-requisito: se o poll de 50 ms nao se sustentar, toda a conversao de tau para alfa (kEmaAlphaQ8 = 57) tem de ser refeita.
- **MEDICAO 7 (base comum) — corrente e margem de acionamento das bobinas.** Bloqueia a polaridade, que por sua vez fecha os itens 9 e 10 da proposta.
- **MEDICAO 12 (base comum) — polaridade e mapeamento dos LEDs do painel.** Os LEDs seguem o GPIO do rele por hardware; sem esse mapa, a temporizacao acima e correta e a sinalizacao visivel pode ser a do limite errado (include/board_pins.h:59-64 ja documenta o cruzamento LIM1 -> serigrafia "LED LIM3").

---

## Decisao 6 - Estado dos reles e das saidas analogicas durante o Modo Programacao e a Auto Calibracao
**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto  
**REQ afetados:** CAL-01 (wizard 5.7), CAL-02 (campo de trim de 4 digitos), CAL-03 (angulo de fundo de escala), CAL-04 (gravacao atomica do par zero+ganho), CAL-05 (retorno automatico ao Modo Normal), AO-02 (saida analogica do eixo em calibracao), AO-03 (saida analogica do eixo nao calibrado), REL-06 (os quatro reles durante o Modo Programacao), PRG-03 (timeout de inatividade e gravacao na saida do modo). Codigo: src/drivers/xtr300.cpp:12 (kZeroCode), src/drivers/dac8562.cpp:21 (kDataZero), src/drivers/calibration.h (CalRecord/CalibrationStore), include/board_pins.h:79 (kXtrRSetNominalOhms), src/app/limit_engine.* (a criar), src/app/ctrl_task.* (a criar).

### O que o manual diz
Todas as citacoes conferidas byte a byte contra /home/ubuntu/repos/SUI/docs/manual-cliente-sui-2026.txt. As linhas citadas pelo rascunho e pelas duas criticas estao TODAS erradas (o arquivo do repo nao e o mesmo /tmp/manual.txt usado no levantamento); valem estas.

- 5.2, L87: "Durante a operação neste modo, as saídas analógicas dos eixos X e Y permanecem atualizadas, fornecendo um sinal proporcional às respectivas inclinações medidas." Vale so para o Modo Normal.
- 5.4, L136: "A Unidade Remota também pode sair automaticamente do Modo Programação por timeout, quando nenhuma tecla for acionada durante aproximadamente 2 minutos. Ao concluir a saída do modo de programação, o equipamento grava na memória EEPROM todos os parâmetros alterados, preservando as configurações mesmo após o desligamento da alimentação."
- 5.7, L164: "Durante o procedimento, a Unidade Remota simula internamente a inclinação informada, permitindo calibrar a saída sem movimentar o equipamento monitorado."
- 5.7, L165: "A saída analógica é bipolar e simétrica em relação ao zero."
- 5.7, L171 (passo 2): "A Unidade Remota passa a simular a inclinação de 0,0° e exibe a tela de ajuste do zero:" ; L172 (tela, impressa duplicada no txt): "Ajuste 0Vcc:0000 Ajuste 0Vcc:0000".
- 5.7, L173 (passo 3): "Utilize a tecla MENU para selecionar o dígito desejado e as teclas ▲ e ▼ para alterar seu valor, acompanhando a leitura do voltímetro até que a tensão de saída seja de 0,00 Vcc." Identico no passo 8, L181, com "+10,00 Vcc".
- 5.7, L174 (passo 4): "Mantenha a tecla MENU pressionada por aproximadamente 3 segundos para confirmar o ajuste do zero."
- 5.7, L177 (tela, duplicada no txt): "Angulo fim de escala X(°):+045,0Angulo fim de escala X(°):+045,0".
- 5.7, L179 (passo 7): "A Unidade Remota passa a simular internamente a inclinação informada e exibe a tela de ajuste do ganho:" ; L180: "Ajuste 10Vcc:0000 Ajuste 10Vcc:0000".
- 5.7, L182 (passo 9): "Mantenha a tecla MENU pressionada por aproximadamente 3 segundos para gravar a calibração na memória EEPROM." ; L183: "Alteracao bem sucedida!Alteracao bem sucedida!".
- 5.7, L184: "Concluída a calibração, o equipamento retorna automaticamente ao Modo Normal de Operação".
- 5.7, L185: "Inclinações superiores ao fundo de escala mantêm a saída saturada em ±10,00 Vcc."
- 5.7, L187: "Importante: O ajuste do zero deve ser sempre realizado antes do ajuste do ganho, pois a referência de 0,00 Vcc é utilizada no cálculo da proporção da saída analógica."
- 5.9, L202: "Cada limite dispõe de uma saída a relé independente, com contato NA/NF para 5 A / 250 Vca máx., e de um LED de sinalização no painel frontal da Unidade Remota." ; L204: "Off: limite desativado; o relé permanece em repouso, independentemente do ângulo medido."
- 7, L309: "ATENÇÃO: Caso ocorra uma falha de energia antes da gravação dos novos valores na EEPROM, ou seja, antes do retorno ao Modo Normal, será necessário reprogramar os parâmetros alterados."

### A lacuna
O manual nao diz uma palavra sobre os quatro reles fora do Modo Normal, e nao diz se a inclinacao simulada da Auto Calibracao entra na cadeia de decisao de rele. Nao especifica: o estado da saida analogica do eixo que NAO esta sendo calibrado; o que os 4 digitos de "Ajuste 0Vcc:0000" e "Ajuste 10Vcc:0000" representam num D/A de 16 bits, nem sua faixa, passo e sinal; o que acontece no aborto por tecla, por timeout ou por falta de energia no meio do wizard; se o par (zero, ganho) e gravado junto ou em dois momentos; e se existe teto de duracao para a saida sob comando manual — L136 e timeout de INATIVIDADE e cada tecla o rearma.

### Proposta
1. SEPARACAO DURA ENTRE SIMULACAO E DOMINIO. A "simulacao interna" de L164/L171/L179 atua EXCLUSIVAMENTE sobre o codigo escrito no DAC do eixo em calibracao. Ela nao alimenta o display, nao alimenta o motor de limites e nao alimenta os reles. O angulo que comanda limites e reles e sempre o angulo REAL da sensora, ja com Preset e Sentido aplicados. DESVIO DO MANUAL: L164 diz "simula internamente a inclinação informada" sem qualificar o alcance da simulacao; a errata de 5.7 tem de dizer que a simulacao afeta apenas a saida analogica do eixo em calibracao.
2. RELES SEMPRE VIVOS, SEM EXCECAO. Da tela de login ao retorno ao Modo Normal — login, navegacao de menu, edicao de senha, edicao de limites e o wizard inteiro de Auto Calibracao — a tarefa ctrl (core 0, prioridade 5, periodo 50 ms, base comum) continua polando a sensora, filtrando, avaliando os quatro limites e escrevendo os quatro GPIOs de rele. Nao existe congelamento nem desenergizacao de rele em nenhum ponto do Modo Programacao. A IHM roda no loop() (core 1) e NUNCA escreve rele nem DAC: publica pedidos por fila para a tarefa ctrl.
3. A EDICAO NAO CONTAMINA O COMPARADOR. Todo campo em edicao vive em buffer de rascunho na RAM da IHM; o conjunto ativo de limites so e trocado no commit da folha. Um limite meio digitado nunca chega ao comparador de rele.
4. O LED DO PAINEL NAO E CANAL DE SINALIZACAO NESTA REVISAO. Retirada a afirmacao do rascunho de que a sinalizacao visual "continua funcionando de graca": include/board_pins.h:59-64 documenta a serigrafia CRUZADA (LIM1 -> CN3-6 "LED LIM3", LIM2 -> CN3-8 "LED LIM1", LIM3 -> CN3-7 "LED LIM2"), contra L202, e sob a polaridade fail-safe da base comum os LEDs ainda ficariam invertidos. O unico canal confiavel durante o wizard e o proprio contato de rele, que nunca para (itens 2 e 3).
5. SAIDA ANALOGICA DO EIXO NAO CALIBRADO: rastreia o angulo real, sem interrupcao, durante todo o procedimento, exatamente como em Modo Normal (L87).
6. MARCADOR DE ENTRADA DO OVERRIDE. Ao entrar no wizard, a tarefa ctrl escreve no eixo em calibracao o codigo 3932 (-11,00 V, fora de banda, base comum) e o mantem por 1000 ms antes de qualquer tela de medicao. DESVIO DO MANUAL: acrescentar a 5.7.
7. SAIDA ANALOGICA DO EIXO EM CALIBRACAO, ESTADO POR ESTADO. So as duas telas de medicao poem a saida num valor de faixa util; todo o resto do wizard fica no codigo 3932 (-11,00 V):
   - marcador de entrada (1000 ms): 3932;
   - tela "Ajuste 0Vcc" (L172): codigo = 32768 + (F - 5000), F = valor do campo;
   - tela "Angulo fim de escala" (L177), passos 5 e 6: 3932;
   - tela "Ajuste 10Vcc" (L180): codigo = (zero ajustado no passo 4) + 26214 + (F - 5000);
   - mensagem "Alteracao bem sucedida!" (L183), aborto por tecla, timeout, teto absoluto ou rejeicao de plausibilidade: 3932 por 1000 ms (marcador de saida), e so entao volta a rastrear o angulo real, em no maximo 50 ms (um tick da tarefa ctrl).
   Em todos os casos o codigo entregue ao DAC e grampeado na faixa 6554..61342 (-10,00 a +10,90 V). DESVIO DO MANUAL: acrescentar a 5.7 que a saida do eixo em calibracao nao representa a inclinacao real durante o procedimento, que o sistema a jusante deve ser inibido, e que os dois marcadores de -11,00 V delimitam a janela de override.
8. TETO ABSOLUTO DE OVERRIDE = 300 s, contados da entrada no wizard, independentes de tecla. O timeout de inatividade de 120 s de L136 continua valendo em paralelo; vence o que estourar primeiro. Estourando qualquer um dos dois: aborto sem gravar, marcador de saida, retorno ao Modo Programacao. DESVIO DO MANUAL: 5.7 nao preve termino do procedimento sem confirmacao.
9. CAMPO DE TRIM COM NEUTRO EM 5000. O campo de 4 digitos permanece 0000..9999, sem sinal, com MENU selecionando o digito e ▲/▼ alterando o valor, exatamente como L173 e L181 exigem. O valor F mapeia trim = F - 5000 LSB do DAC, ou seja -5000..+4999 LSB = -1,907 V a +1,906 V, passo de 1 LSB = 381,47 uV. Neutro (sem correcao) = 5000. DESVIO DO MANUAL: as telas impressas em L172 e L180 mostram o campo em 0000; com neutro em 5000 elas passam a ler "Ajuste 0Vcc:5000" e "Ajuste 10Vcc:5000". Errata das duas figuras de 5.7.
10. MATEMATICA INTEIRA, SEM FLOAT NO CAMINHO DE ATUACAO. O registro de calibracao por eixo passa a ser {int16 fullScaleDeciDeg (1..900), uint16 codeZero, uint16 codeFullScale, uint8 valid}. A conversao em regime e D = codeZero + ((int32)(codeFullScale - codeZero) * anguloDeciDeg) / fullScaleDeciDeg, com arredondamento simetrico, tudo em int32 (produto maximo 26214*900 = 23,6e6, cabe), grampeado em 6554..58982 (-10,00 a +10,00 V) conforme L185. Isto substitui os float a/b de src/drivers/calibration.h, que sao artefato do firmware de fabrica, e realiza literalmente L165 (simetria) e os 22,2 mV/0,1° de L184 com fundo de escala 45,0°.
11. COMMIT ATOMICO DO PAR, UM UNICO INSTANTE DE ESCRITA. O passo 4 (L174) confirma o zero APENAS no buffer de rascunho em RAM; nada e gravado. A unica escrita em NVS do wizard ocorre no passo 9 (L182) e grava o registro completo {fullScale, codeZero, codeFullScale, valid} do eixo, num unico put, com magic, versao e CRC (CalRecord ja tem os tres campos e CalibrationStore::setFromPoints ja recebe os dois pontos de uma vez). Nunca existe estado persistente combinando zero novo com ganho velho. Falta de energia entre os passos 4 e 9 preserva integralmente o par anterior, e isso e exatamente o que L309 e o Obs. de L186 previnem.
12. GATE DE PLAUSIBILIDADE NO COMMIT. O commit e recusado se (codeFullScale - codeZero) sair da faixa 20971..31457, isto e, mais de +/-20 % de erro de escala sobre os 26214 codigos nominais de 0 a +10,00 V. Recusa = aborto sem gravar + marcador de saida. Um erro de escala real de cadeia com resistores de 1 % vale ~2 % e com 5 % vale ~10 %; +/-20 % so e alcancavel com voltimetro no borne errado ou eixo trocado. DESVIO DO MANUAL: exige uma tela nova, texto proposto "CALIBRACAO REJEITADA" (mesma grafia sem acento das demais mensagens: "Alteracao bem sucedida!", "RESET DE FABRICA"), com 2000 ms de permanencia.
13. GATE DE ENTRADA. O wizard e recusado enquanto QUALQUER um dos quatro limites estiver no estado sinalizado, com a tela nova "CALIBRACAO BLOQUEADA" por 2000 ms e retorno ao Modo Programacao. Nao ha gate por estado do enlace: calibrar a cadeia analogica com a sensora ausente e procedimento legitimo de bancada, e o wizard escreve codigo cru, sem depender de angulo. DESVIO DO MANUAL: tela e regra novas em 5.7.
14. TIMEOUT NAO GRAVA O WIZARD. Na saida do Modo Programacao por timeout (L136), os parametros ja confirmados folha a folha sao gravados como manda o manual; o buffer de rascunho do wizard de Auto Calibracao NAO e gravado. DESVIO DO MANUAL, declarado: L136 diz "grava na memória EEPROM todos os parâmetros alterados", e um par (zero, ganho) incompleto nao e parametro alterado — grava-lo produziria o par hibrido que o item 11 existe para impedir. Pedir excecao escrita em 5.4 para o wizard de 5.7.
15. MODO CORRENTE PROIBIDO NO FIRMWARE DE APLICACAO. O produto vende apenas saida em tensao (2.1 e Tabela 3). A aplicacao nunca chama Xtr300AnalogOutput::setMode(); a sequencia "zerar antes de comutar OP_MODE" deixa de existir junto com o modo. O pino OP_MODE (IO22) e dirigido uma unica vez, no passo 5 da ordem de boot, em nivel BAIXO. Com isso a calibracao perde a dimensao AoMode e o registro do item 10 e um por eixo, nao um por eixo/modo.
16. PONTO UNICO DO CODIGO DE ZERO. Some a constante local kZeroCode = 0x0000 de src/drivers/xtr300.cpp:12 e kDataZero = 0x0000 de src/drivers/dac8562.cpp:21. Passam a existir tres constantes, todas em ur_base.h: kDacZeroCode = 32768 (0,00 V), kDacFaultCode = 3932 (-11,00 V) e kDacBootCode = kDacFaultCode. Nenhum caminho desta placa pode escrever 0x0000, que vale -12,5 V pedidos e satura em ~-12 V.
17. REGISTRO DE CALIBRACAO AUSENTE OU COM CRC REPROVADO NO BOOT: o eixo assume o par de fabrica da Tabela 2 (codeZero = 32768, codeFullScale = 58982, fullScale = 450 decimos de grau, isto e 0,0° = 0,00 Vcc e 45,0° = +10,00 Vcc) e opera normalmente; a condicao e registrada e reportada pelo comando de console `status`. Nao vai para o nivel de falha: os reles, que sao o canal primario de seguranca, nao dependem da calibracao analogica, e parkear o eixo em -11,00 V converteria uma degradacao de exatidao de ~2 % na perda total do canal analogico.

### Por que
A protecao nao pode ter janela cega. O Modo Programacao pode durar 120 s de inatividade mais o tempo de login e de digitacao; desligar ou congelar quatro reles de limite nessa janela, num supervisor de inclinacao portuario, e falha de seguranca pura. Manter os reles no angulo REAL tambem elimina o alarme falso obvio do caminho literal: informar 45,0° de fundo de escala dispararia na hora o Limite 1 de fabrica (+ modulo, 5,0°, Tabela 2). A saida analogica do eixo calibrado e o unico sinal que PRECISA mentir — e a definicao do procedimento em L164 — entao e o unico que mente: um eixo so, por no maximo 300 s, delimitado por dois marcadores de -11,00 V que uma maquina le sem depender de o operador ter lido o manual. O commit em par e imposto por L187 ("o ajuste do zero ... é utilizada no cálculo da proporção"): zero e ganho sao um unico coeficiente, e meio coeficiente gravado e uma saida plausivel e errada, que e o pior modo de falha possivel num canal de intertravamento.

### O que a revisao adversarial derrubou
- LED como canal de sinalizacao (ambas as criticas): CEDIDO INTEIRAMENTE. A afirmacao do rascunho era falsa contra include/board_pins.h:59-64 e contra L202; foi apagada (item 4) e a serigrafia cruzada virou pendencia propria de ECO/errata, com a medicao 12 da base comum como evidencia.
- Timeout de 120 s usado como teto de duracao (critica de seguranca 2, e a critica de completude): CEDIDO. L136 e inatividade e cada tecla o rearma. Entrou teto absoluto de 300 s (item 8), independente de tecla.
- Commit nao atomico (critica de seguranca 4, apontada como a falha mais grave): CEDIDO no comportamento, com uma correcao de fato: CalRecord (src/drivers/calibration.h) JA tem magic, version, crc e valid, e setFromPoints JA recebe os dois pontos de uma vez — o defeito nunca esteve no store, estava no wizard, que podia gravar no passo 4. O item 11 proibe qualquer escrita antes do passo 9. A critica pediu ainda "nivel de falha ate recalibrar" para par invalido; RECUSADO no item 17, com justificativa: os reles nao dependem da calibracao, e o par de fabrica invalido-para-nominal e a propria Tabela 2 do manual, nao uma invencao.
- 0,00 V ambiguo (critica de seguranca 3): PARCIALMENTE CEDIDO. A critica esta certa de que 0,00 V e a assinatura do "perfeitamente nivelado" e da queda de energia, e esta certa de que aviso em manual nao e barreira. Mas nao ha como calibrar o zero sem por a saida em 0,00 V: L173 manda o tecnico medir 0,00 Vcc com voltimetro. A conciliacao e temporal e nao textual: a saida so fica em torno de 0,00 V enquanto a tela "Ajuste 0Vcc:0000" esta ativa, todo o resto do wizard fica em -11,00 V, e a janela inteira e emoldurada por dois marcadores de 1000 ms em -11,00 V (itens 6 e 7). O CLP passa a ver bordas fora de banda no inicio e no fim do override — sinal, nao frase. RECUSADA a tela de confirmacao "INIBIR SISTEMA A JUSANTE?" proposta pela critica: e mais um texto que depende do operador ler, e o marcador resolve o mesmo problema para a maquina.
- Troca 0x0000 -> 0x8000 parcial e setMode() usando kZeroCode (critica de seguranca 5): CEDIDO no ponto unico (item 16) e resolvido pela raiz no item 15 — proibido o modo corrente, a sequencia de comutacao deixa de existir e a pergunta "qual o codigo seguro em corrente" some do firmware de aplicacao. Fica registrada a medicao 13 da base comum (jumpers J3/J4/J5/J6/J13/J14), porque se os jumpers estiverem fixos em hardware setMode() ja nao tinha efeito nenhum.
- Sinal na tecla ▼ (critica de fidelidade 1 e 2): CEDIDO. L173 e L181 dizem literalmente "as teclas ▲ e ▼ para alterar seu valor", e as telas de L172 e L180 nao tem campo de sinal, ao contrario de L177 e das telas de limite. O sinal por ▼ era invencao. Substituido pelo neutro em 5000 (item 9), que preserva os 4 digitos sem sinal, preserva as duas setas alterando valor, e resolve a consequencia funcional que a critica apontou: com o campo comecando em 0000 e sem sinal, offset negativo era incorrigivel. O custo e a errata de duas figuras, que e menor que reescrever a convencao de teclas de dois passos.
- Contradicao com L136 nao declarada (critica de fidelidade 3): CEDIDO. Item 14 declara o desvio e pede excecao escrita em 5.4.
- Reinterpretacao de "simula internamente" sem pedido de alteracao de manual (critica de fidelidade 6): CEDIDO. O item 1 agora carrega o desvio explicito.
- Citacoes de linha erradas (critica de fidelidade 5): CEDIDO no espirito, mas a critica tambem errou. As linhas que ela propoe (155, 176, 178, 154-178) nao batem com /home/ubuntu/repos/SUI/docs/manual-cliente-sui-2026.txt, que e o arquivo normativo desta tarefa: 5.7 vai de L163 a L187, "simula internamente" esta em L164, a saturacao em L185 e o "ajuste do zero antes do ganho" em L187. Todas as citacoes desta versao foram extraidas diretamente do arquivo do repo.
- Latencia de retorno de 25 ms do rascunho: DERRUBADA pela base comum, nao pelas criticas. O dono do ciclo e a tarefa ctrl a 50 ms; o retorno ao rastreio real e de ate 50 ms (um tick), nunca 25 ms.
- Trim de +/-9999 LSB do rascunho e o plano de "passo de 2 LSB" do item de risco: DESCARTADOS. Com neutro em 5000 a faixa e -5000..+4999 LSB (-1,907 a +1,906 V), que cobre 19x o erro de zero esperado (~50 mV) e 1,9x o pior erro de ganho com resistores de 5 % (~1,0 V), mantendo o passo minimo em 1 LSB.

### Precisa de decisao humana
1. Mapeamento do campo de trim de 4 digitos. Opcoes: (a) neutro em 5000, faixa -5000..+4999 LSB, telas passam a "Ajuste 0Vcc:5000" e "Ajuste 10Vcc:5000" (errata de duas figuras de 5.7); (b) manter "0000" impresso e por sinal na tecla ▼ (contradiz L173 e L181); (c) acrescentar um campo de sinal "+" as duas telas (reescreve as figuras e a sequencia de teclas). RECOMENDACAO: (a) — e a unica que preserva a letra de L173/L181 e ainda corrige offset negativo.
2. Errata das duas figuras de 5.7 (L172 e L180) decorrente do item 1. RECOMENDACAO: aprovar junto com o item 1.
3. Excecao escrita em 5.4 (L136) declarando que o buffer do wizard de Auto Calibracao nao e gravado na saida por timeout. Opcoes: (a) excecao no manual; (b) gravar o par incompleto conforme a letra de L136. RECOMENDACAO: (a) — (b) cria o par zero-novo/ganho-velho, saida plausivel e errada.
4. Marcadores de entrada e saida em -11,00 V (codigo 3932), 1000 ms cada. Depende da aprovacao pendente de kAoFaultVolts = -11,00 V na base comum e de o cartao de entrada analogica do CLP do cliente tolerar -11 V. Opcoes: (a) -11,00 V; (b) plano B em -10,00 V, perdendo a distincao entre marcador e eixo saturado. RECOMENDACAO: (a), com a mesma constante de compilacao do estado seguro (kAoFaultCode), para permitir build especifico de cliente.
5. Teto absoluto de override de 300 s. Opcoes: (a) 300 s; (b) so os 120 s de inatividade de L136. RECOMENDACAO: (a) — (b) permite override indefinido, porque cada tecla do trim rearma o contador.
6. Gate de entrada recusando o wizard com limite sinalizado, tela nova "CALIBRACAO BLOQUEADA" (2000 ms). Opcoes: (a) recusar; (b) permitir sempre. RECOMENDACAO: (a).
7. Gate de plausibilidade do commit (+/-20 %, faixa 20971..31457) e tela nova "CALIBRACAO REJEITADA" (2000 ms). Opcoes: (a) com gate; (b) gravar o que o tecnico confirmou. RECOMENDACAO: (a).
8. Registro de calibracao ausente ou com CRC reprovado no boot. Opcoes: (a) par de fabrica da Tabela 2 e operacao normal, condicao reportada em `status`; (b) eixo no nivel de falha (-11,00 V) ate recalibrar, como pediu a critica de seguranca. RECOMENDACAO: (a).
9. Proibicao do modo corrente no firmware de aplicacao (setMode() deixa de existir; OP_MODE fixo em BAIXO). Opcoes: (a) proibir; (b) manter os dois modos e definir o codigo seguro em corrente. RECOMENDACAO: (a) — o produto so vende tensao e (b) abre um segundo mapa de codigos sem cliente.
10. Serigrafia cruzada do CN3 (include/board_pins.h:59-64) contra L202. Opcoes: (a) ECO de serigrafia/fiacao do painel; (b) errata de manual documentando o cruzamento. Enquanto nao resolvido, o LED nao pode ser citado como canal valido em nenhuma decisao. RECOMENDACAO: (a) para producao nova, (b) para o parque instalado.
11. Textos das duas telas novas ("CALIBRACAO BLOQUEADA", "CALIBRACAO REJEITADA") e sua permanencia de 2000 ms, e a permanencia de 1000 ms de "Alteracao bem sucedida!" (o manual nao especifica nenhuma). RECOMENDACAO: aprovar como escrito, mantendo a grafia sem acento das demais mensagens de tela.

### Precisa de medicao de bancada
- MEDICAO 1 da base comum (ganho da cadeia analogica, 2 x 5). BLOQUEIA os itens 7, 9, 10, 12 e 16 inteiros: sem ela nenhum dos codigos 32768, 58982, 26214, 3932, 6554 e 61342 tem significado fisico.
- MEDICAO 3 da base comum (swing real do XTR300 e viabilidade de -11,00 V). Valida os marcadores dos itens 6 e 7 e o grampo superior de 61342 (+10,90 V), que so e legitimo se o swing positivo medido for >= +11,50 V com EFOT/EFLD/EFCM apagados.
- MEDICAO 12 da base comum (polaridade e mapeamento dos LEDs do painel). Fecha o item 4 e a pendencia humana 10.
- MEDICAO 13 da base comum (jumpers J3/J4/J5/J6/J13/J14 e J10/J9/J8/J2). Confirma o item 15: se os jumpers de modo estiverem fixos por montagem, setMode() ja nao tinha efeito e a proibicao e apenas a documentacao do fato.
- MEDICAO 14 (NOVA) - EXCURSAO REAL DE TRIM EXIGIDA PELA CADEIA. Procedimento: em TRES placas, com a placa energizada e 60 s de estabilizacao termica, `ao mode v`; DMM de 5,5 digitos em CN1L(+)/CN1M(-); emitir `ao raw x 32768` e registrar V0; emitir `ao raw x 58982` e registrar V10; calcular o trim de zero necessario Tz = -V0 / 381,47 uV e o trim de ganho Tg = (10,000 - V10) / 381,47 uV, ja incluindo Tz; repetir em CN1N(+)/CN1O(-) com `ao raw y`. ACEITACAO: |Tz| <= 1000 LSB e |Tg| <= 4000 LSB nas seis medidas, o que confirma que a faixa -5000..+4999 do item 9 cobre a producao com margem >= 1,25x, e que o gate de plausibilidade de +/-20 % do item 12 nao reprova placa boa. Se |Tg| passar de 4000 LSB em qualquer placa, o item 9 volta a discussao com passo de 2 LSB e a faixa dobra para +/-3,81 V, ao custo de degrau minimo de 763 uV.
- MEDICAO 15 (NOVA) - TEMPO DE COMMIT DO REGISTRO DE CALIBRACAO CONTRA A LACUNA DE WDI. Procedimento: instrumentar o passo 9 do wizard (put unico do registro do item 10) e medir com osciloscopio no IO19 a maior lacuna entre pulsos de WDI durante 100 commits consecutivos, com a ISR de chute em IRAM ja instalada. ACEITACAO: <= 250 ms, o mesmo criterio da medicao 4 da base comum; e a tarefa ctrl nao pode perder mais de 2 ticks consecutivos (a declaracao de falha e em 3 ticks, 150 ms), sob pena de o commit da calibracao levar os quatro reles ao estado de falha.


---

## Decisao 7 - Falha de comunicacao com a sensora: deteccao, classificacao e estado seguro

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto  
**REQ afetados:** MAN-7 (Falhas e Falta de Energia, manual L305-309), MAN-5 (energizacao, L75-77), MAN-5.2 (L87, "as saidas analogicas permanecem atualizadas"), MAN-5.7 (L185, saturacao em +/-10,00 Vcc; L165-183, wizard de Auto Calibracao), MAN-5.9 (L204, "Off ... o rele permanece em repouso"), MAN-2.1 (L42, "sinal bipolar de -10 a +10 Vcc"), MAN-6.2 (L277, faixa de operacao), MAN-Tabela-3 (L322 e L324), MAN-Tabela-4 (L345, NA/NF padrao NF), DSP-03/04 (o que o display mostra antes do primeiro quadro valido). Codigo: `src/proto/modbus_rtu.h`, `src/proto/irs485_protocol.h` (struct `Angle` em float), `src/drivers/xtr300.cpp`, `src/drivers/dac8562.cpp`, `src/drivers/relays.cpp`, `sensor/src/main.cpp:41`, `sensor/src/core/console.cpp:300`.

### O que o manual diz

Item 7, L306 (integral): "Durante o Modo Normal de operação, caso ocorra uma falha de comunicação entre a Unidade Remota (UR-DI151399) e o Sensor de Inclinação (SI-DI141389XY), será exibida no display a mensagem de falha de comunicação. As causas mais comuns são: rompimento ou mau contato do cabo, inversão dos sinais A e B da interface RS485, ausência da alimentação de +5 Vcc do sensor ou blindagem do cabo não aterrada."

L307: "Nessa condição, verifique as conexões elétricas conforme o item 8 e corrija a causa identificada, para que o equipamento retorne à sua condição normal de medição."

L87 (5.2): "Durante a operação neste modo, as saídas analógicas dos eixos X e Y permanecem atualizadas, fornecendo um sinal proporcional às respectivas inclinações medidas."

L204 (5.9): "Off: limite desativado; o relé permanece em repouso, independentemente do ângulo medido."

L185 (5.7): "Inclinações superiores ao fundo de escala mantêm a saída saturada em ±10,00 Vcc."

L42 (2.1) / L277 (6.2) / L322 e L324 (Tabela 3): a saida analogica e declarada como "sinal bipolar de –10 a +10 Vcc".

L75-77 (item 5): "Ao energizar a Unidade Remota, o equipamento executa automaticamente uma rotina de inicialização... é realizado um autoteste do display... a logomarca da Di-Elétrons é exibida temporariamente no display. Em seguida... apresentando a tela principal de medição."

L345 (obs. da Tabela 4): "A configuração dos contatos NA/NF de cada limite é feita por jumper na placa da Unidade Remota, sendo o padrão de fábrica NF (normalmente fechado)."

### A lacuna

O item 7 tem duas frases e nao define: (a) o texto literal da mensagem — e a unica tela do manual sem legenda; (b) o criterio de validade de um quadro; (c) o tempo de deteccao; (d) o estado dos quatro reles na falha — o manual nunca fala de rele fora do Modo Normal; (e) o estado das saidas analogicas na falha — L87 so garante atualizacao em operacao normal; (f) a histerese de recuperacao; (g) a janela entre energizar e o primeiro quadro valido (item 5 nao da tempo nenhum e nao diz o que reles e saidas fazem la dentro); (h) o que acontece com leitura fora de faixa (a sensora publica ate +/-180,0 graus e o display so vai a +/-90,0).

Pior: o contrato real da sensora e traicoeiro. Em `sensor/src/main.cpp:157`, quando a leitura do SCL3300 falha, ela NAO republica os angulos (ficam congelados no ultimo valor bom) e apenas faz `|= kStsSclNotResponding` sobre o registrador 3, que ja continha `kStsDataValid` da publicacao anterior. O barramento entrega, entao, um quadro perfeitamente integro, com CRC bom, angulo plausivel e status 0x0011 — DATA_VALID ligado junto com SCL_NOT_RESPONDING. E o mestre atual da UR le 2 registradores (`src/proto/modbus_rtu.h:17`) e nunca olha o status.

### Proposta

1. **TRANSACAO.** FC 0x03, escravo 1, endereco inicial 0, quantidade 8 (`sensormap::kRegCount`). Uma unica transacao por tick, periodo `kPollPeriodMs = 50 ms`, timeout `kLinkTimeoutMs = 35 ms`, dona exclusiva = tarefa `ctrl` (core 0, prio 5). Todos esses numeros vem da BASE COMUM e nao sao renegociados aqui.

2. **CORRECAO DE BUFFER, PRE-REQUISITO DE QUALQUER ENSAIO.** `src/proto/modbus_rtu.h:17` `kRegisterCount = 2` -> 8 e `:22` `kRxCap = 16` -> 32. A resposta de 8 registradores tem 21 bytes e hoje nao cabe.

3. **CORRECAO DE TIPO, PRE-REQUISITO DA ARITMETICA INTEIRA.** `src/proto/irs485_protocol.h` declara `struct Angle { float x; float y; bool valid; }`. Passa a `struct Sample { int16_t xDeci; int16_t yDeci; int16_t zDeci; uint16_t status; uint16_t uptimeS; uint32_t stampMs; bool wireOk; }`. Nenhum float no caminho que decide rele.

4. **CRITERIO DE VALIDADE, EM DOIS NIVEIS SEPARADOS.**
   - **Nivel de fio (wireOk):** resposta completa dentro de 35 ms, id do escravo = 1, funcao = 0x03, byte-count = 16, CRC16-MODBUS integro, e nenhum dos tres registradores de angulo fora de -1800..+1800 decimos. Qualquer reprovacao aqui e falha de COMUNICACAO.
   - **Nivel de dado (dataOk):** com `wireOk` verdadeiro, exige `registrador 3 == 0x0001 EXATO`. Nao basta mascarar DATA_VALID: 0x0011 e o angulo congelado de `sensor/src/main.cpp:157` e passaria no mascaramento. Reprovacao aqui e falha do SENSOR, nao do cabo.

5. **DETECCAO E RECUPERACAO (numeros da BASE COMUM).** 3 transacoes invalidas consecutivas = 150 ms declaram falha. 5 transacoes validas consecutivas = 250 ms recuperam, com permanencia minima de 2000 ms em falha. Ao recuperar, o filtro da decisao 4/5 e recarregado com a primeira amostra boa, nunca com zero, e os contadores reiniciam.

6. **QUATRO ESTADOS DE LINK, COM QUATRO TEXTOS DISTINTOS.** A atuacao e IDENTICA nos estados 2, 3 e 4 (reles no estado de alarme, saida analogica no codigo 3932); so o texto muda, porque acusar o cabo quando o sensor se declarou doente manda o tecnico ao lugar errado.
   - **AGUARDANDO** — do boot ate o primeiro quadro com `dataOk`, e tambem quando o unico defeito e `kStsSclStartup` (0x0004), que aparece em TODA energizacao normal da sensora. Linha 1: `AGUARDANDO SENSOR`
   - **COMUNICACAO** — 3 transacoes consecutivas com `wireOk` falso. Linha 1: `FALHA DE COMUNICACAO` / Linha 2: `Verifique cabo RS485 e +5V`
   - **SENSOR** — 3 quadros consecutivos com `wireOk` verdadeiro e `dataOk` falso por `kStsSclNotResponding` (0x0010), `kStsSclCrcError` (0x0002) ou `kStsSclSelfTestFail` (0x0008). Linha 1: `FALHA DO SENSOR` / Linha 2: `Sensor de inclinacao em falha`
   - **INSTAVEL** — quadros com `wireOk` verdadeiro e status contendo apenas `kStsSaturated` (0x0020) e/ou `kStsSclStartup` (0x0004). Linha 1: `MEDICAO INSTAVEL`
   Nenhuma das quatro mensagens pisca (ver item 12). Nenhuma linha passa de 29 caracteres.

7. **RETENCAO LIMITADA NO ESTADO INSTAVEL — `kStaleHoldMs = 1000 ms` (20 ciclos).** Enquanto os quadros forem do tipo INSTAVEL, a UR mantem o ultimo angulo valido comandando reles e saida analogica por no maximo 1000 ms consecutivos; passado isso, cai em SENSOR. O contador zera a cada quadro `dataOk`. **DESVIO DO MANUAL:** nao ha manual a desviar aqui — o desvio e da BASE COMUM: `kDataMaxAgeMs = 72 ms` vale no caminho normal; neste caminho a idade maxima do dado que comanda rele sobe a 1072 ms, e isso esta declarado de proposito. Motivo: `kStsSaturated` e `kStsSclStartup` sao transitorios auto-limpantes do proprio SCL3300, e `sensor/src/drivers/scl3300.cpp:304-308` limpa DATA_VALID em ambos os casos; sem a retencao, cada arranque de icamento levaria os quatro reles a alarme e treinaria o operador a ignorar a sinalizacao.

8. **SAIDA ANALOGICA NA FALHA: codigo 3932 (-11,00 V) nos DOIS eixos**, fora da cadeia de calibracao, aplicado em AGUARDANDO, COMUNICACAO e SENSOR, e a partir do passo 5 da ordem de boot. Faixa util grampeada em 6554..58982. **DESVIO DO MANUAL:** -11,00 V esta fora dos -10 a +10 Vcc publicados em L42, L277, L322 e L324. Exige errata explicita nesses quatro pontos e na Tabela 3, com a linha "nivel de falha: -11,00 Vcc (fora da faixa de medicao)". O valor e constante de compilacao (`kAoFaultCode`) para permitir build de cliente em -10,00 V se o cartao do CLP nao tolerar -11 V.

9. **QUATRO RELES NO ESTADO DE ALARME**, inclusive os limites programados em `Off`, em AGUARDANDO, COMUNICACAO e SENSOR. Qual nivel de GPIO e "alarme" vem de `kRelayFailSafePolarity` (decisao de polaridade, pendente do bigboss) e nao e redecidido aqui. **DESVIO DO MANUAL:** contradiz L204 ("Off: limite desativado; o relé permanece em repouso, independentemente do ângulo medido"). Exige nota em 5.9: "Em falha de comunicacao ou de sensor, os quatro reles vao ao estado de alarme, inclusive os limites programados em Off." `Off` significa "sem criterio angular", nao "saida desconectada", e a morte do supervisor tem de sair por todos os canais.

10. **JANELA DE ENERGIZACAO.** Reles no nivel de boot no passo 2 e DAC no codigo 3932 no passo 5 da ordem de boot canonica (~7 ms dentro do `setup()`). O splash de 1200 ms (600 ms de autoteste + 600 ms de logomarca, L75-77) roda NAO BLOQUEANTE no `loop()`, com a tarefa `ctrl` ja polando. **Isto fecha DSP-03/04:** nao existe "5,0 s" nem "3500 ms"; existe um unico criterio (3 transacoes invalidas = 150 ms) e a tela principal abre com `AGUARDANDO SENSOR` se ainda nao houve quadro valido. Os 5,0 s do rascunho estao mortos.

11. **FRESCOR PELO REGISTRADOR 7 (uptime), EM ARITMETICA UNSIGNED.** `delta = (uptime_atual - uptime_anterior) & 0xFFFF`. Se `delta == 0` por mais de 5000 ms de quadros `dataOk` consecutivos, declara SENSOR. Se `delta >= 0x8000` (decremento aparente), a sensora reiniciou: evento contabilizado e impresso no console, sem mudar reles. Nunca comparar com `>`: o registrador e `millis()/1000` truncado em 16 bits (`sensor/src/main.cpp:75`) e da a volta a cada 18,2 h.

12. **FORA DE FAIXA.** Depois de Preset e Sentido do Sensor, `|angulo| > 900` decimos e grampeado em +/-900 para display, saida analogica (saturada em +/-10,00 V, conforme L185) e comparacao de limite; a comparacao usa o valor GRAMPEADO. A leitura grampeada e marcada com o caractere `>` imediatamente antes do sinal, no proprio campo (ex.: `>+090,0`), **sem piscar** — L161 ja usa o piscar do display com outro significado ("O display piscará, indicando que o comando foi aceito", efetivacao do Preset). Angulo cru fora de -1800..+1800 decimos e quadro invalido (item 4, nivel de fio).

13. **PRECEDENCIA SOBRE O WIZARD DE 5.7.** Durante a Auto Calibracao, o eixo em calibracao esta sob comando manual de codigo de DAC e IGNORA o nivel de falha ate o wizard terminar (commit, aborto ou timeout de inatividade, que pertencem a decisao 6). O OUTRO eixo segue a logica de falha normalmente. Os QUATRO reles seguem a logica de falha nos dois casos — eles nao dependem do DAC. Sem esta regra, uma queda de link no meio do ajuste com voltimetro dos passos 3 e 8 arrancaria a saida para -11 V e inviabilizaria um procedimento que nao depende do sensor para nada.

14. **ESCOPO E MODO PROGRAMACAO.** A deteccao roda SEMPRE, nao so no Modo Normal. Em Modo Programacao a mensagem nao rouba a tela, mas reles e saida analogica vao ao estado de falha do mesmo jeito, e a mensagem aparece ao voltar ao Modo Normal — coerente com L306, que descreve a EXIBICAO como do Modo Normal, nao a deteccao.

15. **LATCH POR FLAPPING: 5 entradas em falha dentro de uma janela de 60 s travam o estado de falha** ate ciclo de energia ou comando de console `link clear`. Enquanto travado, a linha 2 passa a `Falha travada - religue a UR`. Com permanencia minima de 2000 ms e recuperacao em 250 ms, o ciclo mais rapido de flapping e 2,25 s (0,44 Hz), nao os 3 Hz que a critica temia; o latch existe para o cabo intermitente que sobrevive a isso.

16. **TRAVAMENTO DE FIRMWARE — ARITMETICA REFEITA.** Com o chute por ISR/IRAM condicionado ao token de liveness (`kCtrlLivenessDeadlineMs = 800 ms`, `kWdiKickPeriodMs = 250 ms`), o ultimo pulso sai no maximo 750 ms apos a tarefa `ctrl` parar (ticks em 250, 500 e 750 ms ainda veem idade < 800 ms). Somando `tWD` de 1120 a 2240 ms: **reset entre 1,87 s e 2,99 s apos o travamento**, e os reles caem ao nivel de boot no instante do reset (GPIO volta a entrada, pull-down de 1K corta o BC337). A saida analogica so e corrigida no passo 5 do boot: bootloader (~300 ms) + 6 ms, ou seja, **ate 3,30 s exibindo um angulo velho**. Isto refina — nao contradiz — a estimativa de "~2,6 s" da base sobre estado seguro da saida analogica, que nao somou os 750 ms do token de liveness. O manual tem de declarar os 3,30 s e exigir que o intertravamento use tambem o contato de rele.

17. **DEPENDENCIA BLOQUEANTE NA SENSORA.** `sensor/src/main.cpp:41` sobe com `g_activeProtocol = &g_jigSlave` e o seletor e comando de console sem persistencia (`sensor/src/core/console.cpp:300`, `proto [jig|modbus]`). Antes desta decisao valer: Modbus RTU como UNICO protocolo do build de producao da sensora, JIG em build de fabrica separado, e o comando `proto` removido do build de producao. Nenhum comando de console pode desligar o link de seguranca.

18. **CONTADORES OBRIGATORIOS NO CONSOLE** (`rs485 stats`), um por causa: timeout, CRC, enquadramento/id/funcao, status reprovado por bit, entradas em falha, latches, resets de sensora detectados pelo item 11. Sem readback de contato, esses contadores sao o unico diagnostico que a UR tem.

### Por que

A deteccao em 150 ms fica 7,5x abaixo do `tWD` minimo de 1,12 s do STWD100 e no mesmo tick de 50 ms em que os limites sao avaliados: perder o link e tao rapido quanto cruzar um limite, e nenhum dos dois depende do que a IHM esta fazendo. A assimetria 3-para-falhar contra 5-para-confiar poe todo o erro no sentido seguro. O criterio `registrador 3 == 0x0001 exato` nao e preciosismo: e a unica forma de nao aceitar para sempre o angulo congelado que `sensor/src/main.cpp:157` publica com DATA_VALID ligado. Separar COMUNICACAO de SENSOR de INSTAVEL custa tres strings e evita o unico modo de falha que nenhum firmware conserta depois: o operador aprender a ignorar a mensagem. -11,00 V usa a folga que a topologia bipolar deixa (swing do XTR300 ate ~-12 V) sem colidir com nenhuma leitura legitima; 0,00 V seria a pior escolha possivel, porque e ao mesmo tempo o angulo mais comum de uma estrutura nivelada e a assinatura fisica da queda de alimentacao.

### O que a revisao adversarial derrubou

**Cedido — a critica de seguranca estava certa:**
- **Temporizacao.** 25 ms de poll com timeout de 20 ms era aritmeticamente impossivel: o round-trip real e 17,9 ms tipico e 21,3 ms de pior caso, entao 20 ms reprovava transacao boa por construcao e levava os quatro reles a alarme. Substituido pelos numeros da BASE COMUM: poll 50 ms, timeout 35 ms.
- **Permanencia minima.** O rascunho nao tinha nenhuma. Agora tem 2000 ms mais latch por flapping (item 15).
- **Frescor pelo registrador 7.** O rascunho usava ">" sobre um contador que da a volta a cada 18,2 h — falha falsa uma vez por dia. Trocado por diferenca unsigned modulo 2^16 (item 11).
- **Reset a quente do DAC8562.** O rascunho so tratava o POR. Agora ha numero (3,30 s de angulo velho, item 16), obrigacao de manual e a ECO de hardware identificada (ligar o CLR# do DAC8562 ao reset do sistema).
- **Seletor de protocolo da sensora.** O item 13 do rascunho pedia so "subir em Modbus". Insuficiente: um comando de console derruba o link de seguranca ate o proximo ciclo de energia. Agora o comando some do build de producao (item 17).
- **Cry-wolf de SATURADO e STARTUP.** O rascunho transformaria cada arranque de icamento e cada energizacao em "FALHA DE COMUNICACAO — verifique cabo RS485". Corrigido com os estados INSTAVEL e AGUARDANDO e a retencao limitada de 1000 ms.
- **Ausencia de readback.** Verdadeiro e sem solucao em firmware nesta placa. Roteado para decisao humana (ensaio de prova periodico) e para ECO candidata (levar EFLD/EFCM do XTR300 a IO36/IO39, hoje livres).

**Cedido — a critica de fidelidade estava certa:**
- **-11 V sem declarar desvio.** O rascunho so pedia alteracao de manual para a mensagem. Agora o item 8 nomeia os quatro pontos do manual (L42, L277, L322, L324) e a Tabela 3.
- **Piscar a 1 Hz.** Colidia com L161, onde piscar ja significa "PSET aceito". Trocado pelo marcador `>` fixo, e as mensagens de falha tambem deixaram de piscar.
- **Precedencia com a decisao 6.** Nenhuma das duas declarava quem manda. Resolvido no item 13, a favor do wizard, apenas no eixo em calibracao e apenas na saida analogica.
- **Aritmetica do watchdog.** O "1,12 a 2,24 s" do rascunho era impossivel. Refeito no item 16 sobre o mecanismo novo (ISR/IRAM + token de liveness): 1,87 a 2,99 s ate o reles cairem, ate 3,30 s de saida analogica velha.
- **`Off` na falha.** O rascunho admitia a contradicao com L204 mas nao a roteava para o documento. Agora e pedido explicito de nota em 5.9 (item 9).

**Refutado — a critica de seguranca errou em dois pontos:**
- **"Reduzir o poll a 4 registradores (0..3) e por uptime/temperatura/whoami em poll lento de 1 Hz".** Rejeitado. A economia e de ~5 ms num orcamento de 50 ms (a resposta cai de 21 para 13 bytes) e o preco e um segundo contrato de fio para manter e versionar, mais a perda do frescor do item 11, que so existe porque o registrador 7 ja vem de graca na mesma transacao. A BASE COMUM fixou transacao unica de 8 registradores; nao ha motivo tecnico para abri-la.
- **"'REG3 == 0x0001 exato' confunde falha de link com saturacao dinamica".** O diagnostico esta certo, mas a causa apontada esta errada, e isso muda a correcao. Em `sensor/src/drivers/scl3300.cpp:304-308`, `valid` exige `!saturated`, entao um quadro saturado ja vem com DATA_VALID em ZERO: o mascaramento de bit reprova esse quadro exatamente como o teste exato. Trocar "exato" por "mascara" nao resolveria nada e ainda abriria a porta para o 0x0011 congelado. A correcao certa nao e afrouxar o criterio de validade — e classificar a causa e reter o dado por tempo limitado (itens 6 e 7), que e o que foi feito.

### Precisa de decisao humana

1. **Nivel de falha da saida analogica.** (a) -11,00 V / codigo 3932, fora da faixa publicada, com errata em L42, L277, L322, L324 e Tabela 3, e confirmacao por instalacao de que o cartao de entrada do CLP tolera -11 V; (b) -10,00 V, dentro da faixa publicada, aceitando que a saida analogica deixa de distinguir "falha" de "-90 graus saturado". **Recomendacao: (a)**, com `kAoFaultCode` como constante de compilacao para permitir build especifico de cliente em (b).
2. **Desvio de L204 (`Off` vai ao alarme na falha).** (a) manter o desvio e emitir nota em 5.9; (b) manter os limites em `Off` fora do estado de alarme durante a falha. **Recomendacao: (a)** — `Off` significa "sem criterio angular", nao "canal desligado".
3. **Latch por flapping (item 15).** (a) travar apos 5 entradas em falha em 60 s, exigindo religamento ou comando de console; (b) sem latch, so a permanencia minima de 2000 ms. **Recomendacao: (a)** — o custo e uma parada que exige manutencao, o beneficio e nao operar por horas com um cabo intermitente.
4. **Retencao de 1000 ms no estado INSTAVEL (item 7).** (a) reter ate 1000 ms; (b) nao reter, indo a falha em 150 ms como qualquer outro quadro invalido. **Recomendacao: (a)**, condicionada a MEDICAO 14 — se `kStsSaturated` for raro em operacao real, (b) fica preferivel por ser mais simples e mais conservador.
5. **Ensaio de prova dos reles.** Nao existe readback em lugar nenhum do caminho de atuacao (contato colado, bobina aberta, BC337 em curto sao todos indetectaveis). Opcoes: (a) declarar no manual um ensaio de prova manual periodico com o intertravamento em manutencao, usando `relay <1..4> <on|off>`; (b) ECO levando EFLD/EFCM do XTR300 a IO36/IO39 e acrescentando readback de contato. **Recomendacao: (a) agora, com intervalo de 12 meses declarado no manual, e (b) na proxima revisao de placa.**
6. **Textos das quatro telas (item 6).** Sao invencao desta decisao — o manual nao da nenhum deles. Precisam ser aprovados e entrar no item 7 do manual como legendas de tela, junto com a nota de que a UR distingue falha de cabo de falha de sensor.
7. **Herdada e bloqueante: polaridade dos reles.** O item 9 diz "estado de alarme"; qual nivel de GPIO e esse depende de `kRelayFailSafePolarity`, que e decisao do bigboss e trava tambem as decisoes 3, 5, 8 e 11.

### Precisa de medicao de bancada

- **MEDICAO 6** — round-trip Modbus real com o cabo de 500 m. Valida o timeout de 35 ms e o poll de 50 ms. Aceitacao: maximo <= 25 ms e ZERO timeouts em 1 hora continua. **Bloqueia os itens 1 e 5.**
- **MEDICAO 3** — swing real do XTR300 e viabilidade do codigo 3932. Aceitacao: |V(3932) + 11,00| <= 50 mV, trilhos entre 14,0 e 16,0 V em modulo, EFOT/EFLD/EFCM apagados. **Bloqueia o item 8.**
- **MEDICAO 4** — lacuna de WDI durante escrita de NVS, com o chute em ISR/IRAM. Aceitacao: <= 250 ms. Se a janela de cache-off passar de 100 ms, os 3 ciclos ate a falha (150 ms) declaram falha falsa a cada gravacao de parametro. **Bloqueia o item 5.**
- **MEDICAO 5** — lacuna de WDI no boot inteiro e tempo de bootloader. Alimenta o numero de 3,30 s do item 16 e a declaracao de manual sobre saida analogica invalida no boot. **Bloqueia o item 16.**
- **MEDICAO 14 (NOVA) — CENSO DE FLAGS SATURADO E STARTUP EM OPERACAO REAL.** Procedimento: com a UR e a sensora instaladas no equipamento portuario, registrar por 8 h continuas de operacao normal, via console, a contagem e a DURACAO de cada rajada de quadros com `kStsSaturated` (0x0020) e com `kStsSclStartup` (0x0004), correlacionada com o evento mecanico (arranque de icamento, choque de spreader, translacao). Aceitacao: nenhuma rajada de SATURADO com duracao acima de 1000 ms, e menos de 12 rajadas por hora. Se qualquer rajada passar de 1000 ms, a retencao do item 7 nao cobre a realidade e a opcao (b) da decisao humana 4 tem de ser adotada, ou o valor de `kStaleHoldMs` tem de ser reancorado nesta medida. **Bloqueia o item 7.**
- Herdadas e bloqueantes pela polaridade do rele (item 9): **MEDICAO 7** (corrente e margem de acionamento das bobinas), **MEDICAO 8** (termica e saturacao do BC337 em conducao continua) e **MEDICAO 9** (consumo total contra os 5 W publicados).


---

## Decisao 8 - Contrato de fio RS-485: Modbus RTU 19200 8N1, escravo 1, FC 0x03 start 0 count 8, aceitacao em inteiro e falha em 150 ms

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto  
**REQ afetados:** manual 2.1 L43 (interface RS485 sensor-UR), manual 2.1 L36 (distancia de ate 500 metros), manual 4 L67 (transmissao da inclinacao instantanea), manual 5.5 L139 (angulo calculado no sensor, ±90,0° com resolucao de 0,1°), manual 7 L306 (falha de comunicacao e mensagem no display), manual 8 / Tabela 3 L317-L321 (CN1 terminais 4 a 8), manual 8 / Tabela 4 L326 (rele do Limite 1, jumper NA/NF padrao NF), manual 5.9 L204-L205 (Off = repouso, acionado = limite atingido), DSP-03/04 (o que o display mostra antes do primeiro quadro valido), MEA-04/05 (uso dos registradores 4 TEMP e 5 WHO_AM_I)

### O que o manual diz
Item 2.1, linha 43: "Interface de comunicação serial RS485 para comunicação entre o Sensor de Inclinação e a Unidade Remota;"
Item 2.1, linha 36: "Sensor de inclinação em módulo independente da Unidade Remota, permitindo instalação a uma distância de até 500 metros;"
Item 4, linha 67: "Sempre que ocorrer uma rotação em torno de qualquer um dos eixos monitorados, o sensor mede a inclinação instantânea e transmite os valores correspondentes à Unidade Remota por meio da interface de comunicação RS485."
Item 5.5, linha 139: "O Sensor de Inclinação calcula o ângulo internamente, por meio de um chip acelerômetro digital com saída angular, e transmite à Unidade Remota o valor já convertido em graus (°), através da interface RS485. A faixa de medição é de ±90,0° em cada eixo, com resolução de 0,1°."
Item 7, linha 306: "Durante o Modo Normal de operação, caso ocorra uma falha de comunicação entre a Unidade Remota (UR-DI151399) e o Sensor de Inclinação (SI-DI141389XY), será exibida no display a mensagem de falha de comunicação. As causas mais comuns são: rompimento ou mau contato do cabo, inversão dos sinais A e B da interface RS485, ausência da alimentação de +5 Vcc do sensor ou blindagem do cabo não aterrada."
Tabela 3, linhas 317-321: "4 | Vermelho — +5 Vcc | Saída de tensão regulada e isolada, +5 Vcc / 200 mA máx. — alimentação do Sensor de Inclinação"; "5 | Amarelo — 0 V | Referência (0 V) da alimentação do Sensor de Inclinação"; "6 | Laranja — A | RS485, sinal A — comunicação com o Sensor de Inclinação"; "7 | Marrom — B | RS485, sinal B — comunicação com o Sensor de Inclinação"; "8 | Blindagem — BL | Blindagem do cabo de comunicação".
Item 5.9, linha 204: "Off: limite desativado; o relé permanece em repouso, independentemente do ângulo medido."

### A lacuna
O manual nomeia dois fios (A e B) e uma mensagem de falha sem texto. Nao especifica: velocidade, formato de caractere, protocolo, endereco de escravo, funcao, mapa de registradores, unidade, endianness, CRC, silencio de enquadramento, periodo de poll, timeout, quantos quadros perdidos declaram falha, criterio de recuperacao, o TEXTO da mensagem de falha, nem — o mais grave — o estado dos quatro reles e das duas saidas analogicas durante a falha. Tambem nao especifica terminacao, bias, bitola do cabo, nem em que ponto a blindagem e aterrada. No firmware a lacuna e pior que omissao, e contradicao verificavel: src/proto/modbus_rtu.h:1-2 declara "Mapa de registradores e formato da PUSI-DI261930 ainda em aberto"; o mestre le kRegisterCount = 2 (modbus_rtu.h:17) e nunca olha o status; kRxCap = 16 (modbus_rtu.h:22) nao comporta a resposta de 21 bytes; struct Angle (src/proto/irs485_protocol.h:9-13) e float; a sensora boota no quadro do jig (sensor/src/main.cpp:41, g_activeProtocol = &g_jigSlave) e ignora todo pedido Modbus apos qualquer reset, sem persistencia da escolha; e quando o SCL3300 falha a sensora faz apenas OR de 0x0010 no status (sensor/src/main.cpp:157), deixando angulo congelado com DATA_VALID ainda setado (0x0011).

### Proposta

**1. Camada fisica.** RS-485 half duplex, 2 fios + blindagem. A -> CN1-6, B -> CN1-7, blindagem -> CN1-8, aterrada SOMENTE na UR. Terminacao de 120 Ohm nas duas extremidades: J7 montado na UR e o terminador de placa da sensora (sensor/include/board_pins.h:37, kRs485TerminatorOnBoard = true). SEM bias externo (sensor/include/board_pins.h:38, kRs485ExternalBias = false): o SN65HVD75DR tem failsafe de barramento ocioso. DE/RE pelo periferico UART via RTS em UART_MODE_RS485_HALF_DUPLEX, nunca por software (src/drivers/rs485.cpp:89-106).

**2. Parametros de linha, fixos e nao configuraveis:** 19200 bps, 8 bits, sem paridade, 1 stop. Nao entram no menu nem na Tabela 1. Tc = 520,83 us (o codigo calcula 520 us em inteiro, sensor/src/drivers/rs485.cpp:313); t3,5 = 1820 us. Produto baud x distancia = 9,6e6, uma ordem de grandeza abaixo do limite pratico de 1e8 para par trancado terminado.

**3. Protocolo.** Modbus RTU, UR = mestre unico, sensora = escravo id 1 fixo. Uma unica transacao por tick de 50 ms: FC 0x03, start 0, quantidade 8. Pedido de 8 bytes (4166,7 us no fio), resposta de 21 bytes (10937,5 us). CRC16-MODBUS, polinomio 0xA001 refletido, semente 0xFFFF, lo antes de hi. Registradores big-endian. O mestre nunca emite escrita, nunca emite FC 0x04, nunca emite leitura parcial e nunca fragmenta o pedido.

**4. Comportamento real do escravo, documentado como e (nao como se gostaria).** FC 0x03 e 0x04 sao aliases do MESMO banco de 8 registradores (sensor/src/proto/modbus_slave.cpp:126-133). Excecoes: 0x01 para funcao ilegal, 0x02 para count == 0 ou start+count > 8, 0x03 para count > 125. SILENCIO TOTAL, sem excecao, em: CRC ruim, endereco de outro escravo, quadro com menos de 4 bytes, broadcast, e funcao valida com comprimento != 8 bytes (modbus_slave.cpp:135-137). Duas divergencias do Modbus canonico ficam registradas e NAO sao corrigidas: count == 0 devolve 0x02 (o padrao manda 0x03) e len != 8 e descartado em silencio. Consequencia normativa: o mestre so se recupera por TIMEOUT, nunca por excecao.

**5. Mapa de registradores congelado** (sensor/include/sensor_map.h, com uma alteracao no registrador 7):
- 0 ANG_X — int16 em decimos de grau, faixa util -900..+900
- 1 ANG_Y — int16 em decimos de grau
- 2 ANG_Z — int16 em decimos de grau (diagnostico; nao entra no caminho de rele nem no DAC)
- 3 STATUS — uint16 bitfield: 0x0001 DATA_VALID, 0x0002 SCL_CRC_ERROR, 0x0004 SCL_STARTUP, 0x0008 SCL_SELFTEST_FAIL, 0x0010 SCL_NOT_RESPONDING, 0x0020 SATURADO, 0x0040 WDT_RESET
- 4 TEMP — int16 em decimos de grau Celsius
- 5 WHO_AM_I — uint16, 0x00C1 apos leitura boa do SCL3300, 0x0000 antes
- 6 FW_VER — uint16, (major<<8)|minor, derivado do FW_VERSION do build
- 7 HEARTBEAT — uint16, incrementado a CADA tick de 10 ms do laco da sensora, fora do ramo de leitura boa, envolvendo em 65536 (655,36 s)

**6. Temporizacao do mestre (identica a BASE COMUM, sem numero novo).** kPollPeriodMs = 50 ms; kLinkTimeoutMs = 35 ms contados do inicio do PRIMEIRO byte transmitido do pedido ate o ultimo byte recebido da resposta — mesma referencia usada na derivacao do pior caso de 21,3 ms da base. Ocupacao do barramento 15,1/50 = 30 %; silencio entre transacoes >= 28 ms, quinze vezes o t3,5, logo o silencio de enquadramento e satisfeito por construcao e nao precisa de temporizador dedicado.

**7. Regra de aceitacao — sete itens, TODOS em aritmetica inteira. Uma amostra que reprova qualquer item nao entra no filtro, nao entra no rele e nao entra no DAC:**
1. resposta completa dentro dos 35 ms;
2. CRC16 correto;
3. addr == 1, func == 0x03, byteCount == 16, comprimento total == 21 bytes (uma resposta de excecao de 5 bytes reprova aqui e e contada como ciclo invalido, com lastException registrado no console);
4. (reg3 & ~0x0040) == 0x0001 — DATA_VALID setado, nenhum bit de erro setado, nenhum bit reservado setado, e o unico bit tolerado e WDT_RESET (0x0040), cuja borda de subida e contada e impressa no console mas nao invalida o dado;
5. reg7 DIFERENTE do valor do ciclo anterior, comparado como uint16 (imune ao wrap; nunca comparar por magnitude);
6. reg5 == 0x00C1 e reg6 >= 0x0002;
7. |reg0| <= 900 e |reg1| <= 900 decimos.

**8. Declaracao e recuperacao da falha — balde furado que reproduz exatamente a regra da BASE COMUM no caso consecutivo.** Contador inteiro saturado em 0..30: +3 por ciclo invalido, -1 por ciclo valido. FALHA DE COMUNICACAO e declarada com contador >= 9, que para tres ciclos invalidos consecutivos ocorre em 150 ms — o mesmo numero da base (kFailsToFault = 3). A recuperacao exige TRES condicoes simultaneas: contador == 0, 5 ciclos validos consecutivos (kGoodsToRecover = 5) e permanencia minima de 2000 ms no estado de falha (kFaultMinDwellMs). Num link limpo o balde zera em 450 ms, dentro dos 2000 ms de permanencia, entao a regra so morde no link intermitente.

**9. Comportamento na falha (o manual e omisso; esta e a parte que nao pode ficar em aberto).**
- Reles: os quatro vao para `urbase::kRelayAlarmLevel`, o nivel de alarme definido pela constante de polaridade da BASE COMUM. Este documento NAO reabre a polaridade e NAO usa as palavras "energizado" ou "desenergizado" para descrever o estado de falha.
- DESVIO DO MANUAL: os reles com Operacao = Off tambem vao ao nivel de alarme na falha de comunicacao, contra o item 5.9, linha 204 ("o relé permanece em repouso, independentemente do ângulo medido"). Justificativa: "Off" desliga um ponto de atuacao por angulo; falha de comunicacao nao e uma condicao de angulo, e a UR nao sabe medir. Exige nota no 5.9.
- Saidas analogicas: as duas vao ao codigo cru 3932 (-11,00 V, `urbase::kDacFaultCode`), escrito por `Xtr300AnalogOutput::setRaw` (src/drivers/xtr300.h:18), NUNCA por `setEngineering`/`CalibrationStore::codeFor`. Nenhum caminho de falha pode passar pela calibracao.
- Display: substitui a tela principal por duas linhas literais, sem acentos: `FALHA DE COMUNICACAO` e `SENSOR SI-DI141389XY`. DESVIO DO MANUAL: o item 7, linha 306, exige a mensagem mas nao publica o texto; este e o texto proposto e entra na errata. Antes do primeiro quadro valido a tela e `AGUARDANDO SENSOR`, conforme o passo 16 da ordem de boot da base, e nao "FALHA DE COMUNICACAO".
- LED: esta decisao NAO acende, apaga nem pisca nenhum LED. O IO2 permanece com a funcao que a BASE COMUM lhe da (nivel baixo no boot, heartbeat no passo 14); os LEDs do painel seguem os reles pelo hardware (CN3, sem resistor de base no caminho).

**10. Registradores de diagnostico (fecha MEA-04/05).** reg2 (ANG_Z), reg4 (TEMP), reg5 (WHO_AM_I) e reg6 (FW_VER) sao lidos, guardados na ultima amostra valida e publicados no comando de console `status`. reg4 gera UMA acao e apenas uma: mensagem de console quando reg4 > 700 (70,0 °C), que e 10,0 °C acima do limite de ambiente de 60 °C do item 6.2. Nenhum rele, nenhuma saida analogica e nenhuma tela reagem a temperatura. reg5 e reg6 entram na regra de aceitacao (item 7.6) e nao tem outra funcao.

**11. Aritmetica: nenhum float entre o quadro Modbus e a decisao de rele.**
```
struct Angle {
    int16_t  xDeci;      // decimos de grau, sign-extended do big-endian
    int16_t  yDeci;
    int16_t  zDeci;
    int16_t  tempDeciC;
    uint16_t status;
    uint16_t whoAmI;
    uint16_t fwVer;
    uint16_t heartbeat;
    bool     valid;
};
```
`kDeciDegreeToDegree` (src/proto/modbus_rtu.cpp:9) e as multiplicacoes de src/proto/modbus_rtu.cpp:218-219 desaparecem. Conversao para o DAC em inteiro: D = 32768 + (mV * 32768) / 12500, grampeada em 6554..58982.

**12. Alteracoes de codigo exigidas por este contrato.**
UR: src/proto/modbus_rtu.h:17 kRegisterCount 2 -> 8; :19 kDataBytes 4 -> 16; :20 kResponseLen 9 -> 21; :22 kRxCap 16 -> 32; :23 kDefaultPollTimeoutMs 50 -> 35; src/proto/irs485_protocol.h:9-13 struct Angle inteira (item 11); src/proto/modbus_rtu.cpp:205-226 valida os sete itens do item 7 antes de aceitar o quadro; src/main.cpp:54 protocolo ativo passa a ModbusRtuProtocol.
Sensora (ECO de firmware, versao 0.2.0, reg6 = 0x0002): sensor/src/main.cpp:41 g_activeProtocol = &g_modbusSlave; sensor/src/main.cpp:157 troca `g_registers[kRegStatus] |= kStsSclNotResponding` por atribuicao que LIMPA DATA_VALID (`= kStsSclNotResponding`); sensor/src/main.cpp:75 reg7 vira heartbeat de 10 ms incrementado fora do ramo de leitura boa; sensor/src/main.cpp:30-31,74 reg6 amarrado ao FW_VERSION do platformio.ini e publicado desde o boot; o quadro do jig fica atras de flag de build do ambiente de fabrica, ausente do firmware de produto.
O escravo NAO e alterado no conjunto de funcoes aceitas: FC 0x04 e leituras parciais continuam sendo respondidas.

**13. Cabo e alimentacao remota.** Par trancado blindado de 100-120 Ohm para A/B; par separado para +5 V / 0 V. DESVIO DO MANUAL: os 500 metros da linha 36 valem para a SINALIZACAO, nao para a alimentacao do terminal 4. Orcamento fixado: resistencia de laco do par de alimentacao <= 1,25 Ohm, que e 0,25 V de queda na corrente publicada de 200 mA da Tabela 3 (chegada de 4,75 V no sensor). Isso da 18 m em 20 AWG (0,0333 Ohm/m), 29 m em 18 AWG (0,0209 Ohm/m) e 47 m em 16 AWG (0,0132 Ohm/m). Acima disso a alimentacao do sensor e local, por conversor isolado, e o fio de 0 V do CN1-5 PERMANECE conectado como referencia de modo comum do RS-485, passando a nao conduzir corrente de alimentacao. Nao existe instalacao sem o 0 V ligado: sem ele a janela de modo comum de +/-12 V do SN65HVD75DR nao tem referencia.

### Por que
19200 8N1 e Modbus RTU nao sao inercia: sao o unico contrato ja implementado, testado (sensor/test/native/test_modbus) e compartilhado pelas duas placas, e a conta fecha com folga (30 % de ocupacao, 9,6e6 baud.m contra 1e8). Uma unica transacao de 8 registradores garante que angulo, status e heartbeat venham do MESMO instante: publishTilt() e handle() rodam sequencialmente no mesmo laco da sensora (sensor/src/main.cpp:148-162), entao a resposta e um retrato coerente, sem tearing e sem duplo buffer. A regra de status quase-exato e a unica que o comportamento real do escravo permite: hoje uma falha do SCL3300 produz 0x0011 sobre angulo congelado, e um mestre que testasse `status & 0x0001` comandaria rele com angulo de sensor morto por tempo indefinido. O heartbeat de 10 ms cobre o que o status nao cobre: laco da sensora travado com o esp_timer ainda chutando o STWD100, situacao em que nada nos registradores muda e o CRC continua correto — deteccao em 150 ms em vez dos 2 a 3 s que o uptime de 1 s exigiria. O balde furado detecta o defeito que o manual nomeia na linha 306 ("mau contato do cabo"), que produz perda intermitente e nunca tres seguidas.

### O que a revisao adversarial derrubou
**Cedido, a critica estava certa:**
1. *O estado de falha proposto era eletricamente identico ao estado normal sem alarme.* Correto e fatal. O rascunho chamava "bobina desenergizada / contato fechado" de estado sinalizado; o manual 5.9 L204-L205 e src/drivers/relays.h:2 dizem o oposto. A proposta atual nao decide polaridade: escreve o estado de falha como `kRelayAlarmLevel`, a constante da BASE COMUM, e a escolha entre fidelidade e fail-safe fica onde ela pertence, na decisao de polaridade do bigboss.
2. *Float no caminho de decisao de rele.* Correto: o rascunho listava as mudancas de tamanho de buffer e esquecia struct Angle (irs485_protocol.h:9-13) e a multiplicacao por 0.1f (modbus_rtu.cpp:9,218-219). Corrigido no item 11, com a struct inteira escrita por extenso.
3. *N consecutivos nao pega mau contato.* Correto. Adotado o balde +3/-1, teto 30, disparo em 9 — que coincide exatamente com os 3 ciclos consecutivos da BASE COMUM, e por isso corrige a lacuna sem contradizer a base.
4. *Os -11,50 V podiam nao sair.* Correto e o achado mais util da revisao: `setEngineering` -> `CalibrationStore::codeFor` devolve NotCalibrated (src/drivers/calibration.cpp:164-166) ou Range (calibration.cpp:57-59) e a saida SEGURA o ultimo angulo. O nivel de falha passa a ser escrito por `setRaw` (src/drivers/xtr300.h:18), fora da calibracao.
5. *Dois valores para a mesma classe de evento.* Correto: enterSafeState() usava zeroAll() = 0,00 V (src/main.cpp:75-81). Unificado no codigo 3932 da base — todo caminho de estado seguro escreve o mesmo codigo.
6. *Janela de energizacao nao especificada.* Correto no rascunho; ja resolvido pela ordem de boot da BASE COMUM (passo 5, saida em 3932 aos ~4 ms; passo 2, reles no nivel de boot). Esta decisao apenas se ancora nela.
7. *LED LIG inventado.* Correto: IO2 e kLedTest, pino de strapping, e o manual L76 define o LED LIG como indicador de energizacao. O pisca de 2 Hz foi retirado inteiro.
8. *Armadilha do status exato com 0x0040.* Correto como risco, ainda que kStsWdtReset nao seja escrito por nenhuma linha da sensora hoje. Em vez de confiar em "ninguem escreve", a regra virou `(reg3 & ~0x0040) == 0x0001`: bit de reset de watchdog e informativo e contado, todo o resto invalida.
9. *O contrato nao batia com o escravo real.* Correto: o rascunho dizia "nenhuma funcao de escrita e aceita nem emitida" e omitia que FC 0x04 e leituras parciais sao atendidas (modbus_slave.cpp:126-137). O item 4 agora documenta o escravo como ele e, inclusive as duas divergencias do Modbus canonico e os cinco casos de silencio.
10. *Numeros de temporizacao do rascunho.* O timeout de 30 ms e a recuperacao em 10 ciclos foram substituidos pelos numeros da BASE COMUM: 35 ms e 5 ciclos com permanencia de 2000 ms. A critica de completude aponta a mesma disputa (25/20 ms contra 50/30 ms); a base a resolve e este documento a segue sem numero proprio.
11. *Valor analogico de falha.* O -11,50 V / codigo 2621 do rascunho cai; vale o -11,00 V / codigo 3932 da BASE COMUM, que fica 1,00 V acima do limite de swing do XTR300 ((V-)+3 = -12 V) e nao aciona EFLD/EFCM, ao contrario do -11,5 V que se aproximava da saturacao.

**Recusado, com evidencia:**
- *"Nao especificar tensao nem codigo de falha enquanto perguntas_em_aberto item 6 estiver aberto."* Recusado. Deixar o nivel de falha em aberto significa que o estado de falha da saida analogica hoje e 0x0000 = -12,5 V saturado (src/drivers/dac8562.cpp kDataZero, src/drivers/xtr300.cpp kZeroCode), o pior estado possivel, em producao. O valor sai da reta bipolar do esquematico ja fixada na BASE COMUM (R_OS = 1K, R_GAIN = 10K, sem R_SET), e a medicao 1 esta agendada para confirma-la. Uma constante escrita e mensuravel e melhor que uma lacuna que hoje resolve para -12,5 V.
- *"Alimentar o sensor localmente destroi a isolacao / amarra os dois terras."* Parcialmente recusado. O 0 V do CN1-5 ja e a referencia de modo comum do RS-485 na propria Tabela 3, linha 318: os dois nos ja compartilham referencia por projeto, com ou sem alimentacao remota. O que a proposta muda e apenas quem fornece a corrente. A correcao real, incorporada, e que o fio de 0 V continua conectado e passa a nao conduzir corrente de alimentacao, o que MELHORA a referencia em vez de piora-la. A parte certa da critica — "resolver por bitola, nao por improviso" — virou o orcamento de 1,25 Ohm de laco do item 13.
- *"Adicionar no escravo a recusa explicita de count != 8 com excecao 0x03."* Recusado. Existe um unico mestre neste barramento e ele e o nosso firmware, governado por este documento; tornar o escravo nao padrao quebra `rs485 ping`, o test_modbus nativo e qualquer ferramenta de bancada, e nao remove nenhum modo de falha do produto. O risco que a critica quer cobrir e coberto do lado do mestre pelo item 7.3 (byteCount == 16 e comprimento 21) e pelo item 7.6 (reg6 >= 0x0002).
- *"Definir o estado de falha dos reles como bobina ENERGIZADA, coerente com 5.9."* Recusado como decisao desta folha: a polaridade e decisao humana ja aberta na BASE COMUM e bloqueia as decisoes 3, 5, 7, 8 e 11. Esta decisao expressa o estado de falha em nivel logico de alarme, o que a torna correta nas duas polaridades e evita criar uma terceira posicao.

### Precisa de decisao humana
1. **Reles com Operacao = Off durante a falha de comunicacao.** (a) Vao ao nivel de alarme junto com os outros tres — DESVIO do 5.9 L204, exige nota no manual; (b) permanecem em repouso, fieis ao 5.9, e um limite desativado fica silencioso quando a UR fica cega. RECOMENDACAO: (a). Falha de comunicacao nao e condicao de angulo, e um contato de limite desativado normalmente nao esta cabeado — se estiver, alarme e o resultado correto.
2. **Texto literal da tela de falha.** O manual exige a mensagem (L306) e nao publica o texto. Proposto: `FALHA DE COMUNICACAO` na primeira linha e `SENSOR SI-DI141389XY` na segunda, sem acentos, coerente com "RESET DE FABRICA". Alternativa: uma unica linha `FALHA DE COMUNICACAO`. RECOMENDACAO: duas linhas — a segunda identifica qual dos dois modulos esta mudo e reproduz o codigo que o proprio item 7 usa. Entra na errata do item 7.
3. **Errata dos 500 metros (L36 e Tabela 3 terminal 4).** (a) Publicar o limite real de alimentacao remota (18 m em 20 AWG, 29 m em 18 AWG, 47 m em 16 AWG, orcamento de 1,25 Ohm de laco) e manter os 500 m so para a sinalizacao; (b) manter o texto atual e assumir o risco de sensor que nao liga em instalacoes longas. RECOMENDACAO: (a). Nao ha bitola pratica que leve 200 mA a 500 m com 0,25 V de queda.
4. **Nivel de falha da saida analogica em -11,00 V.** Ja aberto na BASE COMUM e repetido aqui porque esta decisao o aciona: exige (i) errata acrescentando o nivel de falha a Tabela 3 e ao 6.2 e (ii) confirmacao, por instalacao, de que o cartao de entrada do CLP tolera -11 V. Plano B: -10,00 V, perdendo a distincao entre falha e -90° saturado. RECOMENDACAO: -11,00 V, com `kAoFaultCode` como constante de compilacao para permitir build especifico de cliente.
5. **Versao minima da sensora aceita (reg6 >= 0x0002).** (a) Sensora com firmware anterior ao heartbeat e recusada e a UR fica em falha permanente ate a atualizacao; (b) a UR aceita e opera sem deteccao de congelamento. RECOMENDACAO: (a). Sem heartbeat nao existe deteccao de laco travado, e essa e a unica defesa contra angulo congelado com CRC correto.
6. **Acao sobre a temperatura do sensor (reg4).** (a) Apenas mensagem de console acima de 70,0 °C; (b) indicacao no display; (c) alarme em rele. RECOMENDACAO: (a). O manual nao publica limite de temperatura do sensor nem canal de indicacao; (b) e (c) criariam requisito novo e um segundo numero comandando rele.
7. **ECO de firmware da sensora (versao 0.2.0).** A sensora esta validada em fabrica e este contrato exige quatro alteracoes nela (boot em Modbus, limpeza de DATA_VALID na falha, reg7 heartbeat, reg6 amarrado ao build). Precisa de autorizacao para reabrir e revalidar o firmware da sensora. Sem isso o contrato inteiro nao existe: hoje a sensora nasce falando o quadro do jig e ignora todo pedido Modbus apos qualquer reset.

### Precisa de medicao de bancada
1. **MEDICAO 6 da BASE COMUM (round-trip Modbus com o cabo real de 500 m).** Valida o timeout de 35 ms e o poll de 50 ms. Aceitacao: maximo do round-trip <= 25 ms e ZERO timeouts em 1 hora continua.
2. **Enquadramento e failsafe da linha ociosa com 500 m e os dois terminadores de 120 Ohm.** Barramento ocioso por 1 hora com o cabo passado junto da fiacao de potencia do equipamento portuario; contar bytes espurios e erros de enquadramento no contador do driver (`rs485 stats`). Aceitacao: zero quadros espurios e zero erros de enquadramento. Motivo: sem bias externo, dois terminadores em paralelo carregam o failsafe interno do SN65HVD75DR com 60 Ohm.
3. **Queda de tensao no par de alimentacao.** Medir a corrente real do sensor em CN1-4 com amperimetro em serie (repouso e pico durante a rajada SPI do SCL3300) e a tensao que chega ao sensor com o cabo real. Aceitacao: resistencia de laco medida <= 1,25 Ohm e tensao no sensor >= 4,75 V. Valida o item 13 e a errata do item 3 das decisoes humanas.
4. **Regressao do escravo apos o ECO 0.2.0.** Confirmar, com osciloscopio nos DE das duas placas, que a latencia do escravo continua na faixa prevista de 2,05 a 4,55 ms depois da troca do reg7 e do boot em Modbus; e 20 ciclos de energizacao confirmando que a sensora responde ao primeiro pedido Modbus apos cada reset, sem intervencao de console. Aceitacao: 20/20 boots respondendo e latencia maxima <= 4,55 ms.


---

## Decisao 9 - Campos de 4 digitos da Auto Calibracao sao TRIM em codigos do DAC, com zero eletrico em 0x8000

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto
**REQ afetados:** MAN-2.1-L42, MAN-5.3-L136, MAN-5.7-L164, MAN-5.7-L165, MAN-5.7-L172, MAN-5.7-L173, MAN-5.7-L174, MAN-5.7-L177, MAN-5.7-L178, MAN-5.7-L180, MAN-5.7-L181, MAN-5.7-L182, MAN-5.7-L183, MAN-5.7-L184, MAN-5.7-L185, MAN-5.7-L187, MAN-5.9-L223, MAN-5.11-L240, MAN-Tab1-L119, MAN-Tab1-L120, MAN-Tab2-L254, MAN-Tab2-L255, MAN-6.1-L277, HW-xtr300.cpp:11, HW-dac8562.cpp:21, HW-board_pins.h:79, HW-calibration.h:32, HW-test_07_rset.cpp

### O que o manual diz

Todas as citacoes abaixo sao de `/home/ubuntu/repos/SUI/docs/manual-cliente-sui-2026.txt`, conferidas linha a linha neste arquivo (as linhas usadas no rascunho e as propostas pela critica de fidelidade estao ambas erradas para este arquivo).

- L164 (5.7): "A função Auto Calibração ajusta a saída analógica de cada eixo em dois pontos: o zero, em que 0,0° corresponde a 0,00 Vcc, e o ganho, em que o ângulo de fundo de escala informado pelo operador corresponde a +10,00 Vcc. Durante o procedimento, a Unidade Remota simula internamente a inclinação informada, permitindo calibrar a saída sem movimentar o equipamento monitorado."
- L165 (5.7): "A saída analógica é bipolar e simétrica em relação ao zero. Por exemplo, ajustando-se o fundo de escala em 45,0° para +10,00 Vcc, a inclinação de –45,0° resultará em –10,00 Vcc e a posição de 0,0° em 0,00 Vcc."
- L172 (5.7, passo 2), tela literal: `Ajuste 0Vcc:0000`
- L173 (5.7, passo 3): "Utilize a tecla MENU para selecionar o dígito desejado e as teclas ▲ e ▼ para alterar seu valor, acompanhando a leitura do voltímetro até que a tensão de saída seja de 0,00 Vcc."
- L174 (5.7, passo 4): "Mantenha a tecla MENU pressionada por aproximadamente 3 segundos para confirmar o ajuste do zero."
- L177 (5.7, passo 5), tela literal: `Angulo fim de escala X(°):+045,0`
- L178 (5.7, passo 6): "...informando o ângulo desejado com resolução de 0,1° (no exemplo, 45,0°). Confirme mantendo a tecla MENU pressionada."
- L180 (5.7, passo 7), tela literal: `Ajuste 10Vcc:0000`
- L181 (5.7, passo 8): "...acompanhando o voltímetro até que a tensão de saída seja de +10,00 Vcc."
- L182 (5.7, passo 9): "Mantenha a tecla MENU pressionada por aproximadamente 3 segundos para gravar a calibração na memória EEPROM."
- L183 (5.7), tela literal: `Alteracao bem sucedida!`
- L184 (5.7): "Concluída a calibração, o equipamento retorna automaticamente ao Modo Normal de Operação e a saída analógica passa a acompanhar o ângulo medido de forma proporcional e simétrica: com fundo de escala em 45,0°, cada 0,1° corresponde a aproximadamente 22,2 mV; com fundo de escala em 90,0°, a aproximadamente 11,1 mV."
- L185 (5.7): "Obs.: O ângulo de fundo de escala define apenas a proporção da saída analógica e não altera a faixa de indicação do display, que permanece em ±90,0°. Inclinações superiores ao fundo de escala mantêm a saída saturada em ±10,00 Vcc."
- L187 (5.7): "Importante: O ajuste do zero deve ser sempre realizado antes do ajuste do ganho, pois a referência de 0,00 Vcc é utilizada no cálculo da proporção da saída analógica."
- L42 (2.1): "Duas saídas analógicas isoladas, uma para cada eixo monitorado, com sinal bipolar de –10 a +10 Vcc, simétrico em relação ao zero (0,0° = 0,00 Vcc), com ângulo de fundo de escala programável por eixo e ajuste integral por software (sem trimpots), através de conversor D/A de 16 bits;"
- L119 e L120 (Tabela 1): "Auto Calibração X | Ajuste do zero (0,0° = 0,00 Vcc) e do ganho no ângulo de fundo de escala informado (0,1 a 90,0°)" (idem para Y).
- L254 e L255 (Tabela 2): "Auto Calibração X | Calibração de fábrica: 0,0° = 0,00 Vcc e fundo de escala de 45,0° = +10,00 Vcc" (idem para Y).
- L240 (5.11): "Além dos parâmetros de programação, o procedimento também restaura os ajustes de calibração das saídas analógicas realizados durante o processo de fabricação."
- L136 (5.3): "A Unidade Remota também pode sair automaticamente do Modo Programação por timeout, quando nenhuma tecla for acionada durante aproximadamente 2 minutos."
- L223 (5.9): "...referem-se à leitura apresentada no display, ou seja, já consideram o Preset e o Sentido do Sensor configurados para o respectivo eixo."

### A lacuna

Os dois campos tem 4 digitos decimais (0000 a 9999) para comandar um conversor de 16 bits declarado em L42 (0 a 65535). 4 digitos decimais nao endereçam o espaco de codigos: o campo NAO pode ser o codigo absoluto. O manual nao diz o que os 4 digitos representam, qual a faixa aceita, quanto vale um incremento, nem por que as DUAS telas abrem em `0000` enquanto a tela de senha abre no valor corrente (`Edita senha:1234`, L231). Tambem nao diz: o que acontece se o fundo de escala for digitado como `000,0` (divisao por zero na proporcao de L184); se o campo de fundo de escala aceita sinal (a tela L177 imprime `+045,0` mas a Tabela 1 declara 0,1 a 90,0°); o que os quatro reles e a saida analogica do OUTRO eixo fazem enquanto a UR simula internamente 0,0° e depois o fundo de escala (L164); e o que se faz quando nao existe calibracao de fabrica gravada.

### Proposta

**A. Semantica e edicao do campo**

1. Os 4 digitos sao um TRIM (correcao), nao um codigo. **Unidade: 1 contagem = 1 codigo do DAC8562** (1 LSB). A unidade e definida no dominio do CODIGO, nao no dominio da tensao — e portanto independe do ganho da cadeia, que so a medicao 1 fecha.
2. DESVIO DO MANUAL: o campo ganha uma quinta posicao a esquerda, selecionavel por MENU como se fosse um digito, onde ▲/▼ alternam entre `+` e `-`. As telas passam a ser `Ajuste 0Vcc:+0000` e `Ajuste 10Vcc:+0000`. Um caractere a mais que as telas literais de L172 e L180; exige errata nessas duas linhas. O valor neutro continua sendo `+0000`, exatamente como o manual imprime o estado inicial das duas telas.
3. Faixa do trim de zero: **T0 ∈ [-999, +999] contagens**. Faixa do trim de ganho: **TG ∈ [-2621, +2621] contagens**. Sem auto-repeat: cada toque em ▲/▼ incrementa/decrementa uma unidade do digito selecionado. A cada toque o valor e recomposto, **grampeado na faixa** e reexibido; nenhum digito "da a volta" para fora da faixa (com T0 no maximo, a tela mostra `+0999` e ▲ no digito de milhar nao tem efeito).
4. Campo de fundo de escala (L177): grafia `+XXX,X` preservada byte a byte, com a posicao do sinal **travada em `+`** (▲/▼ sem efeito ali), faixa **F ∈ [1, 900] decimos de grau**, conforme Tabela 1 L119/L120 (0,1 a 90,0°). `000,0` e impossivel por construcao — o campo para em `+000,1` — o que elimina a divisao por zero.
5. DESVIO DO MANUAL: nas telas de trim, ▼ e tecla de VALOR (como L173 e L181 exigem literalmente), nao tecla de sinal. Isto refuta, para esta tela especifica e com evidencia literal, a proposta de por o sinal na tecla ▼.

**B. Modelo eletrico e conversao**

6. Modelo adotado, identico ao da BASE COMUM: `V_OUT = 5 * (V_DAC - 2,5 V) = 25*D/65536 - 12,5 V`; `D = 32768 + 2621,44 * V_OUT`; LSB = 381,47 uV. Codigos nominais: **32768** (0,000 V), **58982** (+10,000 V), **6554** (-10,000 V). O fator 5 e `R_GAIN/(2*R_OS) = 10000/(2*1000)`, equacao (2) do SBOS336C na variante com offset (o `/2` esta na propria equacao do datasheet; ver "O que a revisao adversarial derrubou", item 2).
7. **Nenhuma das constantes do item 6 entra em firmware antes da MEDICAO 1** (ganho da cadeia analogica, base comum). Ate la elas sao previsao, e a previsao concorrente e ganho 2 (`include/board_pins.h:79`). O que NAO depende da medicao 1: a semantica de trim, a unidade de 1 codigo, a aritmetica inteira, e o fato de que 0x0000 nao e zero.
8. Codigos calibrados por eixo: `C0 = 32768 + T0` e `CFS = 58982 + TG`; vao `S = CFS - C0`. Com as faixas do item 3, `C0 ∈ [31769, 33767]`, `CFS ∈ [56361, 61603]` e `S ∈ [22594, 29834]` — sempre positivo, sempre monotonico, sem nenhuma constante adicional.
9. Conversao em servico, **aritmetica inteira, sem float**, sobre a mesma leitura em decimos de grau que alimenta os reles (L223: ja com Preset e Sentido do Sensor aplicados), rodando na tarefa `ctrl` a cada 50 ms:
   ```
   int16_t  a  = clamp(leituraDeciDeg, -F, +F);      // saturacao de L185
   int32_t  n  = 2 * (int32_t)a * (int32_t)S;
   int32_t  q  = (n >= 0) ? (n + F) / (2*F) : (n - F) / (2*F);   // simetrico
   uint16_t code = (uint16_t)((int32_t)C0 + q);
   ```
   Pior caso do produto: `2 * 900 * 29834 = 53.701.200`, cabe em int32 com 40x de folga. Erro de arredondamento <= 0,5 LSB.
10. Consequencia provada do item 9: o codigo emitido em servico fica sempre no intervalo `[2*C0 - CFS, CFS] ⊂ [5243, 61603]`. O extremo superior 61603 fica abaixo do codigo do limite de swing de +12 V (64487), entao o XTR300 nunca sai da regiao linear e EFLD/EFCM nao disparam. O extremo inferior 5243 fica **1311 contagens (0,500 V nominais) acima do codigo de falha 3932 (-11,00 V)** da BASE COMUM: o nivel de falha permanece inconfundivel com qualquer saida legitima, inclusive na pior calibracao aceita.
11. Guarda unica no commit, derivada do item 10 e nao de nenhum ganho presumido: aceita se e somente se `(2*C0 - CFS) >= 5243`. Se reprovar, DESVIO DO MANUAL: exibe `Calibracao recusada!` por **3000 ms**, permanece na tela de ajuste do ganho com os valores digitados preservados e NAO grava. Essa combinacao so e alcancavel com zero e ganho trimados em sentidos opostos e no extremo — o operador ja teria visto o voltimetro fora de +10,00 V muito antes.
12. A guarda de plausibilidade do rascunho (`S ∈ [23593, 28835]`, isto e 26214 ±10 %) e **retirada**. Ver "O que a revisao adversarial derrubou", item 3.

**C. Fluxo do procedimento (5.7)**

13. Sequencia imposta pelo firmware, cumprindo L187: nao ha como entrar na Etapa 2 sem confirmar a Etapa 1. Um unico wizard, nao dois parametros independentes.
14. Durante a Etapa 1 o eixo em calibracao emite **diretamente `C0` em edicao** (sem passar pela formula do item 9), o que e exato e equivalente. Entre a confirmacao do zero (L174) e o passo 7 (L179) o eixo permanece em `C0` confirmado (0,0° simulado, como L171 declara). Durante a Etapa 2 emite **diretamente `CFS` em edicao**.
15. A IHM roda no `loop()` e **nao escreve o DAC** — isto respeita a BASE COMUM, nao a contradiz. Cada toque de ▲/▼ publica o codigo pedido por fila para a tarefa `ctrl`, que o aplica no proprio tick de 50 ms. Latencia maxima entre a tecla e a tensao no borne: 50 ms de tick + `kXtrSettleUs` = 500 us de acomodacao. Irrelevante para o procedimento, porque o tempo de integracao de um multimetro de 5,5 digitos e duas ordens de grandeza maior.
16. Commit **atomico e em par**: `(T0, TG, F)` do eixo sao gravados numa unica transacao no passo 9 (L182). Abortar em qualquer ponto — VOLTAR, queda de energia, ou os 120 s de inatividade de L136 — deixa a calibracao anterior **integralmente intacta** e devolve o eixo ao angulo real imediatamente. Nunca existe em NVS a combinacao "zero novo com ganho velho".
17. Fim do procedimento exatamente como o manual manda: exibe `Alteracao bem sucedida!` (L183) por **3000 ms** e retorna automaticamente ao Modo Normal (L184). O manual nao especifica essa duracao; 3000 ms e o mesmo numero do hold de confirmacao, para nao criar um segundo tempo de tela.

**D. Reles e o outro eixo durante a calibracao (o manual e omisso)**

18. O angulo simulado alimenta **exclusivamente a saida analogica do eixo em calibracao**. Os quatro comparadores de limite continuam rodando na tarefa `ctrl`, a 50 ms, sobre o **angulo REAL** do sensor, sem interrupcao e sem mascaramento — inclusive os dois limites do eixo que esta sendo calibrado. A saida analogica do OUTRO eixo continua acompanhando o angulo real.
19. Justificativa da regra 18 em numeros: o padrao de fabrica da Tabela 2 (L256-L257 e L260-L261) poe Limite 1 e Limite 3 em `+ (módulo)` a `+005,0°`. Se os reles seguissem o angulo simulado, a Etapa 2 com fundo de escala de 45,0° dispararia os dois em cima de uma estrutura parada; e se fossem mascarados, ficariam cegos a uma inclinacao real durante todo o procedimento.
20. DESVIO DO MANUAL (acrescimo a 5.7): a saida analogica do eixo em calibracao **mente para o CLP durante todo o procedimento**. O manual tem de instruir a colocar o laco analogico daquele eixo em manutencao no CLP antes de iniciar a Auto Calibracao, junto da instrucao de conectar o voltimetro que ja esta em L167.

**E. Persistencia e Reset Geral**

21. Dois registros distintos em NVS, cada um com CRC16-MODBUS como o `CalRecord` atual: **`cal_fab`** (T0, TG, F por eixo, escrito UMA vez pelo jig de fabrica, jamais pela IHM) e **`cal_usr`** (os mesmos campos, escrito pela Auto Calibracao do cliente). Formato: `int16_t t0[2]; int16_t tg[2]; int16_t fsDeci[2];` com `kCalVersion = 2` (hoje `src/drivers/calibration.h:32` esta em 1) e `static_assert` de tamanho, como o registro atual.
22. O Reset Geral **copia `cal_fab` sobre `cal_usr`**, que e exatamente o verbo de L240 ("restaura os ajustes de calibração das saídas analógicas realizados durante o processo de fabricação"). `cal_fab` e imutavel pela IHM. Isto resolve a ambiguidade RST-02 apontada pela critica de completude: a leitura de que o Reset "apaga a calibracao analogica" nao tem apoio em L240 e a justificativa da decisao 1 tem de ser corrigida — a guarda de tecla presa continua justificada porque o Reset apaga a calibracao **do usuario** e os setpoints de limite.
23. DESVIO DO MANUAL: se `cal_fab` estiver ausente ou reprovar no CRC, a UR **nao assume F = 450 em silencio**. As duas saidas analogicas ficam presas no codigo de falha **3932** (-11,00 V) permanentemente, e a tela principal exibe de forma permanente `SEM CALIBRACAO DE FABRICA` (nova tela; o posicionamento na tela principal e da decisao 12). Os reles continuam operando normalmente sobre o angulo real — a falta de calibracao invalida a saida analogica, nao a funcao de limite.

**F. Correcoes de firmware que esta decisao exige**

24. `src/drivers/xtr300.cpp:11`: `kZeroCode = 0x0000` passa a **0x8000**, e passa a significar apenas o ZERO ELETRICO (0,00 V). `zeroAll()` **deixa de ser o caminho de estado seguro**: `begin()`, `setMode()` e `enterSafeState()` passam a escrever `kAoFaultCode = 3932`, conforme a BASE COMUM. Onde houver calibracao carregada, o zero eletrico e `C0`, nao 32768.
25. `src/drivers/dac8562.cpp:21`: `kDataZero = 0x0000` passa a **3932** e e renomeado para `kDataBoot`, para que o nome deixe de mentir sobre o que o valor significa nesta placa.
26. `include/board_pins.h:79`: `kXtrRSetNominalOhms = 2500.0f` e **APAGADA** (nao corrigida para 1000). O no do pino SET tem exatamente tres conexoes e nenhuma delas e um R_SET; manter a constante com outro valor perpetuaria o modelo errado na fonte unica de verdade de pinos. Apagar apos a MEDICAO 1, como manda a base comum.
27. `src/tests/test_07_rset.cpp` e reescrito para o modelo bipolar **antes** de rodar a medicao 1: hoje ele imprime "R_SET implicado" e "corrente em codigo 0" com a equacao (1) do SBOS336C, que e do caso sem offset.
28. Toda a UI de fabrica que assume unipolar migra junto: `src/cmds/cmd_system.cpp:18` ("TENSAO (0-10 V)"), `src/sim/sim_commands.cpp:118-125` e `src/net/web_page.h:204`.

**G. Janela de POR**

29. A saida analogica e declarada **invalida** desde a energizacao ate o passo 5 da ordem de boot da BASE COMUM (~4 ms dentro do `setup()`, apos ~300 ms de bootloader). Nesta janela o DAC8562 esta em POR zero-scale **com a referencia interna desabilitada**, ou seja, os 2,5 V que fecham a malha de offset nao existem: o nivel da saida nessa janela **nao e -12,5 V, e indefinido pelo modelo** e so a medicao diz qual e. O rascunho errava ao chamar isso de -12,5 V e ao propor "DAC primeiro no setup()" — a ordem canonica de boot ja fixou o DAC como passo 5, atras do watchdog e dos reles, e essa mudanca de ordem nao altera nada de fisico.

### Por que

A leitura de TRIM e a unica que explica simultaneamente os tres fatos impressos: 4 digitos decimais para um conversor de 16 bits declarado em L42 (codigo absoluto nao cabe), as DUAS telas abrindo em `0000` (um codigo absoluto de +10,00 V jamais seria 0000, e o zero desta topologia e 32768), e o procedimento ser "acompanhando a leitura do voltímetro" (L173), que e ajuste incremental por realimentacao visual e nao digitacao de um valor conhecido. A unidade de 1 codigo do DAC e a menor correcao que o hardware consegue aplicar, e — decisivo depois da revisao — e a unica unidade que nao depende do ganho da cadeia, que ainda esta em disputa entre 2 e 5 ate a medicao 1. As faixas de ±999 e ±2621 saem de orcamento de erro (zero: offset do DAC, Vos do XTR300 e fugas, ~70 contagens, com a tolerancia do VREF cancelando por construcao em meia escala; ganho: 2 % de dois resistores mais 0,4 % de erro de ganho do DAC, ~630 contagens), com margens de 14x e 4,2x. A guarda de commit nao e um numero de ganho congelado: e a distancia minima em contagens que mantem o nivel de falha de -11,00 V distinguivel de qualquer saida legitima, propriedade que precisa valer num equipamento cujo unico canal de "estou morto" legivel por maquina e essa tensao e os contatos de rele.

### O que a revisao adversarial derrubou

1. **Cedido — a auto-verificacao dos 22,2 mV / 11,1 mV era circular.** As duas criticas estao certas: `10,00 V / 450` e `10,00 V / 900` sao reproduzidos por QUALQUER cadeia que entregue ±10,00 V no fundo de escala, independentemente de topologia, ganho, LSB e ate da largura do conversor. Apresentar isso como confirmacao do esquematico dentro de um argumento de seguranca era pior que nao verificar. O item foi **removido** como prova e substituido pela MEDICAO 1 (item 7 da proposta), que separa ganho 5 de ganho 4 de ganho 2 de forma inequivoca. Os 22,2/11,1 mV permanecem apenas como conferencia de consistencia da formula do item 9.
2. **Refutado com evidencia — o fator 5 nao esta errado, e a critica de fidelidade errou a aritmetica.** A critica afirma que "com os proprios rotulos da proposta o fator seria R_GAIN/R_OS = 10K/1K = 10". Nao seria: a equacao (2) do SBOS336C traz `R_GAIN/2`, e o trace da folha 2/2 (`lev_cadeia-analogica.md`) da `V_OUT = (R_GAIN/2)*(V_IN - V_REF)/R_OS = (10000/2)*(V_DAC-2,5)/1000 = 5*(V_DAC-2,5)`. O fator 2 esta no datasheet, nao foi esquecido. A critica esta certa em outra coisa, e nisso **cedo**: a nomenclatura. Chamar R12/R25 de `R_OS` e afirmar "nao existe R_SET" e a leitura correta da topologia tracada (o no do SET tem exatamente R12/R25 para o VREF, o Cc de 47 nF para o DRV e nada mais), mas colide com o vocabulario ja escrito em `src/tests/test_07_rset.cpp:2` e em `docs/perguntas_em_aberto.md` item 6. A reconciliacao esta no item 27: o teste e reescrito, para que exista **um** vocabulario. Note que o numero e o mesmo nos dois vocabularios — `R_GAIN/(2*R)` com R = 1K da 5 — entao esta disputa nunca foi sobre o ganho.
3. **Cedido — a guarda `S = 26214 ±10 %` era constante congelada sobre resistor nao medido.** A critica de seguranca esta certa: se o ganho real nao for 5, essa guarda recusaria TODA calibracao legitima com `Calibracao recusada!` e o produto ficaria impossivel de comissionar em campo, exatamente na funcao usada para comissionar o laco analogico — travamento nao diagnosticavel. A guarda foi **retirada** (item 12). No lugar ficou uma unica condicao, `(2*C0 - CFS) >= 5243`, que nao contem nenhum ganho: ela so exige que o extremo negativo calibrado fique 1311 contagens acima do codigo de falha. E a monotonicidade que a critica pedia (`S > 0`) ja e garantida por construcao pelas faixas dos campos (item 8), sem constante nenhuma.
4. **Cedido, e o achado mais importante da revisao — `0x8000` como estado seguro era mentira em faixa.** A critica de seguranca esta inteiramente certa: 0,00 V e a leitura legitima mais provavel de uma estrutura nivelada E a assinatura fisica da queda de energia. O rascunho trocava um valor detectavel por um valor plausivel. A BASE COMUM ja fixou o desfecho e esta decisao o adota sem reserva: o estado seguro, o valor de boot e o valor de dado invalido sao o **codigo 3932 (-11,00 V)**, nunca 0x8000. O que o rascunho acertou e permanece: `0x0000` **nao e zero nesta placa**, vale -12,5 V saturado, e `kZeroCode`/`kDataZero` tem de deixar de ser 0x0000 (itens 24 e 25). A confusao do rascunho foi tratar "codigo do zero eletrico" e "codigo do estado seguro" como a mesma coisa; sao dois valores diferentes e agora estao separados.
5. **Cedido — POR do DAC8562 com referencia interna desabilitada.** O item 7 da critica de seguranca esta certo: sem os 2,5 V do VREF a malha de offset esta aberta e o nivel da saida na janela de POR nao e previsto pelo modelo. "DAC primeiro no `setup()`" encurtava a janela e nao a eliminava — e a ordem canonica de boot da base comum ja tirou o DAC do primeiro lugar por outras razoes. Ver item 29 e a extensao da medicao 3.
6. **Cedido — `cal_fab` ausente nao pode virar F = 450 silencioso.** O item (d) da critica de seguranca esta certo: 3 s de mensagem e depois operar como se estivesse calibrado e uma escapada de fabrica virando falha silenciosa em campo. Ver item 23: anunciacao permanente e saida presa no nivel de falha, com os reles preservados.
7. **Cedido — telas inventadas nao marcadas e fim de procedimento ignorado.** A critica de fidelidade esta certa nos dois pontos. `Calibracao recusada!` e `SEM CALIBRACAO DE FABRICA` estao agora marcadas como DESVIO DO MANUAL (itens 11 e 23), e o fim do procedimento cumpre L183 e L184 explicitamente (item 17), o que o rascunho tinha simplesmente omitido.
8. **Cedido — citacoes deslocadas.** A critica de fidelidade esta certa em que o rascunho citava linhas erradas; mas as linhas que ela propos (L155-156, L175, L178, L231) tambem nao batem com `docs/manual-cliente-sui-2026.txt`. Todas as citacoes desta versao foram reconferidas neste arquivo, uma a uma. Em particular: L185 traz DUAS coisas (faixa do display fixa em ±90,0° E saturacao em ±10,00 Vcc) e "o ajuste do zero deve ser sempre realizado antes do ajuste do ganho" e L187, nao L178.
9. **Refutado — `board_pins.h:79` nao vira 1000, vira nada.** A critica de fidelidade pediu `kXtrRSetNominalOhms 2500 -> 1000`. Refuto: o trace mostra que nao existe resistor do SET para 0 V; trocar o valor manteria na fonte unica de verdade de pinos um componente inexistente, e a formula que o consome (`test_07_rset.cpp`) esta sendo reescrita de qualquer modo. A base comum ja manda apagar a constante apos a medicao 1. A critica esta certa, e **cedo**, em que deixar a constante como esta e defeito de seguranca e nao editorial, e em que `test_07_rset.cpp` faltava na lista de artefatos a corrigir.
10. **Refutado — o item 5 da critica de seguranca (EFOT/EFLD/EFCM so acendem LEDs locais) e verdadeiro mas nao e desta decisao.** Nao ha readback em nenhum ponto do caminho de atuacao, e isso ja esta registrado como risco nao enderecado na critica de completude. Nada nesta decisao afirma "saida calibrada" em servico: a Auto Calibracao e um procedimento assistido por operador com instrumento no borne, e a unica alegacao que ela faz e sobre o instante da calibracao, com um humano lendo o voltimetro. O que esta decisao acrescenta contra a malha aberta e o item 10: manter o codigo emitido dentro de `[5243, 61603]` garante que o XTR300 nunca entre em sobrecarga, que e a condicao que abriria a malha da IA sem ninguem saber.
11. **Refutado — sinal na tecla ▼ nesta tela contradiz o manual literal.** A decisao 6 propos por o sinal na ▼ e a decisao 10 propos preservar a grafia com offset de 5000. A ▼ e refutada por L173 e L181, que dizem literalmente "as teclas ▲ e ▼ para alterar seu valor". O offset de 5000 e refutado por L172 e L180: com offset, a correcao nula apareceria como `5000` e as duas telas **nao** abririam em `0000`, destruindo justamente o fato impresso que sustenta toda a leitura de trim.

### Precisa de decisao humana

1. **Sinal no campo de trim.** Opcoes: (A) quinta posicao de sinal, telas viram `Ajuste 0Vcc:+0000` e `Ajuste 10Vcc:+0000`, errata em L172 e L180; (B) manter 4 digitos literais com aritmetica de complemento de dez mil (0000 decresce para 9999 = -1), zero errata; (C) sinal na tecla ▼ (proposta da decisao 6); (D) offset de 5000 no campo (proposta da decisao 10). **Recomendacao: (A).** (C) contradiz L173/L181 palavra por palavra; (D) contradiz L172/L180 porque a tela deixaria de abrir em `0000`; (B) faz um tecnico ver `9987` no meio da calibracao de um equipamento de seguranca sem saber se aquilo e -13 ou um erro de digitacao. (A) custa um caractere de errata em duas linhas.
2. **Comportamento sem `cal_fab` valida.** Opcoes: (A) as duas saidas presas em -11,00 V com anunciacao permanente `SEM CALIBRACAO DE FABRICA`, reles normais; (B) carregar o default da Tabela 2 (F = 450, T0 = TG = 0) e operar. **Recomendacao: (A).** (B) e uma placa nao calibrada em servico se dizendo calibrada; a Tabela 2 descreve o resultado de um Reset Geral numa placa que TEM calibracao de fabrica, nao um substituto para a ausencia dela.
3. **Reset Geral e a calibracao do cliente.** Confirmar que `cal_fab` e imutavel e que o Reset Geral sobrescreve a calibracao feita pelo cliente em campo com a de fabrica, que e a leitura literal de L240. Consequencia comercial: uma UR recalibrada em campo perde essa calibracao num Reset Geral e volta ao ganho de fabrica. Opcoes: (A) seguir L240 como esta; (B) preservar `cal_usr` no Reset e reescrever L240. **Recomendacao: (A)**, e corrigir a justificativa da decisao 1, que afirma que o Reset apaga a calibracao analogica.
4. **Errata de manual em 5.7 sobre os reles e o laco do CLP.** Acrescentar a 5.7 que (i) os quatro reles continuam operando sobre o angulo real durante toda a Auto Calibracao e (ii) o laco analogico do eixo em calibracao deve ser colocado em manutencao no CLP antes de iniciar. **Recomendacao: aprovar as duas.** Sem (ii) o cliente tem uma saida mentindo por minutos sem aviso nenhum no manual.
5. **Dependencia herdada da BASE COMUM (nao se decide aqui, mas trava esta decisao):** o nivel de falha de -11,00 V (codigo 3932) precisa ser aceito pelo cartao de entrada analogica do CLP do cliente. Se o plano B (-10,00 V) for adotado, a guarda de commit do item 11 muda de numero e o item 10 perde a separacao de 1311 contagens. **Recomendacao: manter 3932.**

### Precisa de medicao de bancada

1. **MEDICAO 1 (base comum) - ganho da cadeia analogica.** Bloqueia TODA constante de tensao desta decisao: 32768/58982/6554, 381,47 uV, e o significado em volts de ±999 e ±2621. Aceitacao ja fixada: coeficiente angular dentro de ±2 % de 25 V/65536 e |V(32768)| <= 10 mV.
2. **Extensao da MEDICAO 1 - espalhamento real do trim no lote.** Nas mesmas tres placas, executar a Auto Calibracao completa e registrar o T0 e o TG efetivamente necessarios em cada eixo. **Aceitacao: |T0| <= 300 e |TG| <= 800 em todas as seis medidas** (isto e, dentro de um terco da faixa do campo). Se algum eixo exigir mais, a faixa do campo correspondente tem de ser reaberta antes do release, e a guarda do item 11 recalculada.
3. **MEDICAO 2 (base comum) - acoplamento cruzado X<->Y pelo VREF.** Decide se o roteiro de calibracao pode ser feito eixo a eixo com o outro em zero (como 5.7 assume) ou se o outro eixo tem de estar nos extremos durante a calibracao. Aceitacao ja fixada: desvio <= 5 mV.
4. **MEDICAO 3 (base comum) - swing real e viabilidade do nivel de falha de -11,00 V.** Confirma que 3932 da -11,00 ±0,05 V com EFOT/EFLD/EFCM apagados, e que os trilhos do A0515S-2WR3 sao ±15 V.
5. **Extensao da MEDICAO 3 - transitorio de energizacao no borne.** Osciloscopio em CN1L/CN1M com persistencia infinita, disparo na subida do +5 V, 20 ciclos de energizacao. Registrar (a) a excursao negativa de pico e (b) o tempo da energizacao ate a saida assumir -11,00 V. **Aceitacao: excursao de pico nao mais negativa que -13,0 V e tempo <= 500 ms.** Se reprovar, a correcao e hardware (grampo na saida, rele de saida, ou ligar o CLR# do DAC8562 ao reset do sistema), nao ordem de `setup()`.
6. **Confirmacao de BOM, nao de instrumento:** R17/R29 = 10K e R12/R25 = 1K com tolerancia declarada, valor de R27, e o part number do conversor DC/DC (A0515S-2WR3). Se o DC/DC nao for de entrada 5 V e saida ±15 V, os ±10 V sao inalcancaveis e toda esta decisao cai junto com a promessa de L42.

---

## Decisao 10 - Lado negativo da saida analogica: uma unica reta espelhada em aritmetica inteira, verificada em -FE

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto  
**REQ afetados:** MAN-2.1-L38, MAN-2.1-L42, MAN-5.7-L164, MAN-5.7-L165, MAN-5.7-L167, MAN-5.7-L172, MAN-5.7-L177, MAN-5.7-L180, MAN-5.7-L183, MAN-5.7-L184, MAN-5.7-L185, MAN-5.7-L186, MAN-5.7-L187, MAN-6.1-L277, MAN-8-L322..L325, Tabela 1 (Auto Calibracao 0,1 a 90,0 graus), Tabela 2 L254-L255 (calibracao de fabrica), src/drivers/xtr300.cpp:12, src/drivers/dac8562.cpp:21, include/board_pins.h:79, src/tests/test_07_rset.cpp:1-3, src/cmds/cmd_system.cpp:18, src/sim/sim_commands.cpp:118-125

### O que o manual diz

Arquivo /home/ubuntu/repos/SUI/docs/manual-cliente-sui-2026.txt:

- 2.1, L38: "Precisão, linearidade e repetibilidade: ±0,1% da escala máxima;"
- 2.1, L42: "Duas saídas analógicas isoladas, uma para cada eixo monitorado, com sinal bipolar de –10 a +10 Vcc, simétrico em relação ao zero (0,0° = 0,00 Vcc), com ângulo de fundo de escala programável por eixo e ajuste integral por software (sem trimpots), através de conversor D/A de 16 bits;"
- 5.7, L164: "A função Auto Calibração ajusta a saída analógica de cada eixo em dois pontos: o zero, em que 0,0° corresponde a 0,00 Vcc, e o ganho, em que o ângulo de fundo de escala informado pelo operador corresponde a +10,00 Vcc. Durante o procedimento, a Unidade Remota simula internamente a inclinação informada, permitindo calibrar a saída sem movimentar o equipamento monitorado."
- 5.7, L165: "A saída analógica é bipolar e simétrica em relação ao zero. Por exemplo, ajustando-se o fundo de escala em 45,0° para +10,00 Vcc, a inclinação de –45,0° resultará em –10,00 Vcc e a posição de 0,0° em 0,00 Vcc."
- 5.7, L167: "Importante: Antes de iniciar o procedimento, conecte um voltímetro de precisão aos terminais da saída analógica do eixo a ser calibrado (consulte o item 8 – Conexões Elétricas)."
- 5.7, L172 e L180: telas literais "Ajuste 0Vcc:0000" e "Ajuste 10Vcc:0000"; L177: "Angulo fim de escala X(°):+045,0"; L183: "Alteracao bem sucedida!"
- 5.7, L184: "...cada 0,1° corresponde a aproximadamente 22,2 mV; com fundo de escala em 90,0°, a aproximadamente 11,1 mV."
- 5.7, L185: "Inclinações superiores ao fundo de escala mantêm a saída saturada em ±10,00 Vcc."
- 5.7, L186: "A calibração deve ser realizada com voltímetro devidamente calibrado e com resolução compatível com a precisão requerida pela aplicação."
- 5.7, L187: "O ajuste do zero deve ser sempre realizado antes do ajuste do ganho, pois a referência de 0,00 Vcc é utilizada no cálculo da proporção da saída analógica."
- 6.1, L277: "A faixa de operação da saída analógica de –10 a +10 Vcc é definida pela Auto Calibração de cada eixo..."
- 8, L322 e L324: "Saída analógica bipolar de –10 a +10 Vcc relativa ao eixo X (+)" / "...eixo Y (+)"
- Tabela 2, L254-L255: "Calibração de fábrica: 0,0° = 0,00 Vcc e fundo de escala de 45,0° = +10,00 Vcc"

### A lacuna

O manual AFIRMA a simetria (L165: -45,0 graus = -10,00 Vcc) mas nao a mede, nao a ajusta e nao a tolera: as duas unicas telas de ajuste (L172 e L180) estao ambas do lado nao negativo. Fica indefinido: (1) como o ramo negativo e gerado — extrapolacao da reta de dois pontos, espelhamento do modulo, ou segunda reta; (2) qual o criterio de aceitacao em -FE, ja que os +/-0,1 %FE de L38 nunca sao aplicados a saida analogica; (3) o que fazer quando -FE nao bate; (4) o que os 4 digitos de "Ajuste 0Vcc:0000" e "Ajuste 10Vcc:0000" significam num D/A de 16 bits (L42), qual a faixa, qual o passo, e por que abrem em 0000 e nao no valor corrente; (5) qual codigo de DAC vale 0,00 V — e, portanto, qual e o codigo de qualquer escrita que nao venha de um angulo valido; (6) qual angulo alimenta a saida: o bruto do sensor ou a leitura exibida (com Preset e Sentido aplicados). O firmware de teste responde (5) errado: src/drivers/xtr300.cpp:12 (kZeroCode = 0x0000) e src/drivers/dac8562.cpp:21 (kDataZero = 0x0000) escrevem, nesta placa, -12,5 V pedidos e ~-12 V saturados.

### Proposta

1) FONTE DO ANGULO. A saida analogica de cada eixo acompanha a MESMA leitura exibida no display, ja com Preset e Sentido do Sensor aplicados (int16 em decimos de grau), nunca o angulo bruto do sensor. Uma unica grandeza comanda display, reles e saida analogica. Fora do assistente de Auto Calibracao, os dois eixos seguem a leitura real em qualquer modo, inclusive no Modo Programacao.

2) MODELO PERSISTIDO, por eixo, tudo inteiro: `calZeroCode` u16 (codigo para 0,0 grau), `calFsCode` u16 (codigo para +fundo de escala), `calFsAngleDeci` int16 (1..900, exatamente a faixa da Tabela 1). Nominais de fabrica (Tabela 2 L254-L255): 32768, 58982, 450. Base: `urbase::kDacZeroCode`, `kDacPlus10VCode`.

3) CONVERSAO, int32 do inicio ao fim, sem float:
```
int32_t delta = (int32_t)calFsCode - (int32_t)calZeroCode;      // nominal 26214
int32_t num   = delta * (int32_t)anguloDeci;                    // |num| <= 36213*900 = 32,6 M, cabe em int32
int32_t half  = (int32_t)calFsAngleDeci / 2;
int32_t quo   = (num >= 0) ? (num + half) : (num - half);
int32_t code  = (int32_t)calZeroCode + quo / (int32_t)calFsAngleDeci;   // arredondamento simetrico
```
O ramo negativo usa o MESMO `delta` e o MESMO divisor: o erro de espelhamento e identicamente zero e o de arredondamento e <= 0,5 LSB = 191 uV = 0,0019 %FE. Com FE = 45,0 graus, delta/450 = 58,25 codigos por 0,1 grau = 22,22 mV (L184: 22,2 mV); com FE = 90,0 graus, 29,13 codigos = 11,11 mV (L184: 11,1 mV).

4) SATURACAO (L185), em dominio de codigo e em int32: `code` limitado a [calZeroCode - delta, calZeroCode + delta] — nominal [6554, 58982] = -10,000 a +10,000 V exatos. O grampo e em +/-10,00 V, nao no limite eletrico do XTR300.

5) GRAMPO ABSOLUTO DE HARDWARE, aplicado depois do item 4 e antes do estreitamento para uint16: `code` em [3277, 62259] = -11,2498 a +11,2495 V, dentro do swing do XTR300 ((V-)+3 = -12 V, (V+)-3 = +12 V) e fora da faixa util. Nenhum caminho de codigo — calibracao, teste, console, estado seguro — escreve fora dessa janela. O nivel de falha da base (3932) esta dentro dela por construcao. O grampo e reconfirmado pela MEDICAO 3 da base (swing real medido).

6) VALIDACAO NA CONFIRMACAO, incluindo a COMBINACAO zero+ganho. Recusa (Err::Range) se: `calFsAngleDeci` fora de 1..900; `calZeroCode` fora de [27768, 37767]; `calFsCode` fora de [53982, 63981]; `delta` < 13107 (span menor que 5,00 V, ganho errado por mais de 2x); ou, o teste que faltava, `calZeroCode - delta` < 3277 OU `calZeroCode + delta` > 62259. Exemplo que a validacao antiga deixava passar e esta pega: zero = 27768 com ganho = 63981 da delta = 36213 e limite inferior -8445, negativo.

7) DESVIO DO MANUAL: os 4 digitos das telas "Ajuste 0Vcc:0000" e "Ajuste 10Vcc:0000" (L172, L180) sao trim com offset 5000: `calZeroCode = 32768 + (NNNN - 5000)`, faixa 27768..37767; `calFsCode = 58982 + (NNNN - 5000)`, faixa 53982..63981. Um digito = 1 LSB = 381,47 uV na saida; a faixa e +/-5000 LSB = +/-1,907 V (+/-19 %FE). As telas abrem no valor CORRENTE (5000 numa placa virgem), nao em 0000 como mostram as figuras L172 e L180 — abrir em 0000 saltaria a saida em -1,907 V no instante em que a tela aparece, na frente do voltimetro. A saida acompanha os digitos a cada tecla. A grafia de 4 digitos sem sinal do manual e preservada.

8) DESVIO DO MANUAL: tela de aviso na ENTRADA do assistente (antes do passo 2 de L171), duas linhas literais "SAIDA SIMULADA" / "Bloqueie o CLP", confirmada por MENU mantido por 3 s (mesmo gesto de L173 e L182). Motivo: durante todo o assistente a saida do eixo em calibracao esta desconectada do angulo real (L164) e pode estar indo para um CLP em servico. O eixo NAO calibrado continua seguindo a leitura real.

9) DESVIO DO MANUAL: passo 10, verificacao obrigatoria do lado negativo, entre o passo 9 (L182) e o retorno automatico ao Modo Normal (L184). A UR simula -`calFsAngleDeci`, escreve o codigo espelhado e exibe "Verifique -10Vcc" ate MENU, com teto ABSOLUTO de 30 s (nao os 120 s de inatividade de L127, porque aqui a saida esta comandando fundo de escala negativo; 30 s bastam para a leitura de um DMM que integra em menos de 1 s). Criterio de aceitacao, unico, valido em fabrica e em campo: |V_medido + 10,000 V| <= 10,0 mV, isto e, os mesmos +/-0,1 %FE de L38. Reprovou = falha de HARDWARE, placa reprovada e registrada — NAO se compensa em firmware, porque uma correcao so do lado negativo quebra o espelhamento exato e esconde componente fora de tolerancia.

10) DESVIO DO MANUAL: tela "Calibracao recusada!" quando o item 6 reprova, exibida por 2000 ms, com retorno ao passo do assistente que gerou a recusa e SEM gravar nada. O 5.7 nao tem caminho de recusa hoje.

11) ORCAMENTO DE ERRO QUE SUSTENTA OS 10,0 mV. As duas fontes dominantes sao absorvidas pela propria calibracao de dois pontos: a tolerancia da razao R_GAIN/R_OS e erro de GANHO (absorvido no ponto +10 V e rigorosamente simetrico), e os offsets do OPA/IA sao erro de ZERO (absorvido no ponto 0 V). Sobra: (a) acoplamento cruzado pelo VREF, ~2,7 mV em -FE apos a calibracao em +FE; (b) INL do DAC8562, +/-8 LSB = 3,05 mV; (c) arredondamento, 0,19 mV. Soma de piores casos 5,9 mV = 0,059 %FE. O instrumento exigido (5,5 digitos, item 12) contribui ~0,4 mV. Restam 3,7 mV de margem contra o portao de 10,0 mV. A nao linearidade propria do XTR300 nao entra com numero de catalogo: e exatamente o que o passo 10 mede. Se uma placa boa reprovar de forma repetitiva em tres unidades, quem esta errado e o requisito de L38 e o manual muda — nao o portao.

12) DESVIO DO MANUAL: L186 ("resolução compatível com a precisão requerida") ganha numero: voltimetro de 5,5 digitos, resolucao 0,1 mV na escala de 20 V, calibrado. O mesmo instrumento ja exigido pela MEDICAO 1 da base. Com 3,5 digitos (resolucao 10 mV) o criterio de 10,0 mV nao fecha e o procedimento fica sem valor.

13) NADA DE 0,00 V COMO ESTADO SEGURO — a decisao 10 CEDE integralmente a base. Toda escrita que nao venha de um angulo valido usa `urbase::kDacFaultCode` = 3932 = -11,00 V, escrito CRU, fora da reta de calibracao: no boot (passo 5 da ordem de boot), em toda troca de modo do XTR300, apos 3 transacoes Modbus invalidas (150 ms), com quadro integro mas status reprovado pelo sensor, e em qualquer estado seguro. Consequencia de codigo: apagar `kZeroCode = 0x0000` de src/drivers/xtr300.cpp:12 e `kDataZero = 0x0000` de src/drivers/dac8562.cpp:21; `zeroAll()` vira `faultAll()` escrevendo 3932; 32768 passa a existir apenas como valor nominal de fabrica de `calZeroCode`, nunca como valor de fallback.

14) REFRESCO PERIODICO DO DAC, 50 ms. A tarefa ctrl reescreve os DOIS canais a cada tick, mesmo sem mudanca de codigo. Custo: 2 canais x 3 bytes a 1 MHz = 48 us por tick de 50 ms (0,1 %). Ganha tres coisas que nenhum readback pode dar nesta placa: (a) um quadro SPI corrompido (o DAC8562 nao tem SDO e o VIH minimo de 3,5 V nao e atendido pelos 3,3 V do ESP32, ver pendencias) se auto-corrige em <= 50 ms em vez de ficar preso ate a proxima mudanca de angulo; (b) um DAC que sofra glitch de alimentacao sem reset do ESP32 volta ao valor correto; (c) o registro do DAC, que NAO e apagado pelo reset do ESP32, e reescrito 50 ms depois do primeiro tick da tarefa ctrl.

15) LIMITACAO DECLARADA, SEM MAQUIAGEM: a UR nao detecta cabo analogico aberto ou em curto, nem XTR300 em shutdown termico. EFOT (19), EFLD (18) e EFCM (17) so acendem LD1..LD6 (src/drivers/xtr300.h:132) e nao chegam a GPIO nenhum; o DAC8562 nao tem readback e board::kDacMiso = 36 e pino fantasma. Isso tem de estar escrito no manual, item 8, junto com a exigencia de que o CLP configure deteccao de fora de faixa. Sem essa deteccao configurada, o canal analogico NAO e canal de seguranca.

16) FE MENOR QUE 5,0 GRAUS: aceito sem tela extra, faixa integral 1..900 da Tabela 1 preservada. Um erro de digitacao (0,4 no lugar de 45,0) satura a saida em +/-10,00 V com meio grau de inclinacao, isto e, produz FALSO ALARME permanente, na direcao segura, e e apanhado no proprio passo 10, onde a sensibilidade medida nao sera de 22,2 mV por 0,1 grau. Nao ha caso em que FE pequeno esconda inclinacao.

### Por que

O hardware tem UM caminho afim: V_OUT = (R_GAIN/2)*(V_DAC - V_REF)/R_OS = 5*(V_DAC - 2,5 V) = 25*D/65536 - 12,5 V (folha 2/2; R12/R25 = 1K do pino SET ao VREF, R17/R29 = 10K entre RG1/RG2). Dois pontos definem a reta inteira, inclusive o ramo negativo — nao existe fisicamente um "ganho negativo" separado, e por isso os dois pontos que o manual ja publicou bastam; o que faltava era escrever o espelhamento. Fazendo-o em dominio de CODIGO, com o mesmo `delta` e o mesmo divisor, a simetria de L165 vira exata por construcao, e nao uma promessa: nao ha coeficiente calculado duas vezes, nao ha float, nao ha arredondamento diferente de um lado e do outro.

Ha ainda um argumento estrutural que ninguem tinha explicitado e que reforca o zero em 32768: a mesma VREF de 2,5 V alimenta o fundo de escala do DAC (ref interna com ganho 2, V_DAC = 5,0*D/65536) e a ponta fria do resistor de offset. Logo V_OUT = 10*V_REF*(D/65536 - 1/2): o ponto de zero e RATIOMETRICO e independe do valor real da VREF. Deriva, carga e tolerancia da referencia deslocam o GANHO, nunca o zero — por isso o trim de zero precisa cobrir apenas offsets de amplificador (poucos mV) e a faixa de +/-1,907 V do item 7 e folgada por larga margem.

E a verificacao em -FE e barata (30 s por eixo, com o voltimetro que L167 ja exige) e e a UNICA evidencia possivel, num sistema sem nenhuma leitura de volta analogica, de que a promessa de simetria vendida em L165 e verdadeira naquela placa.

### O que a revisao adversarial derrubou

CEDIDO — a critica estava certa:

1. Estado seguro em 0,00 V (item 9 do rascunho) era regressao de seguranca: transformava falha revelada em falha oculta, porque 0,00 V e simultaneamente "estrutura nivelada" e a assinatura fisica da queda de energia. A base ja fixou -11,00 V / codigo 3932, e o item 13 acima adota isso integralmente, com o adicional de que 3932 e escrito CRU, fora da reta de calibracao. O rascunho estava errado; a critica de seguranca esta certa.
2. Defeito aritmetico no clamp (critica 6): validacoes independentes permitiam zero = 27768 com ganho = 63981, limite inferior -8445, que em uint16 vira lixo no instante do angulo mais negativo — o instante do trip. Corrigido no item 6 com validacao da COMBINACAO contra o grampo absoluto, e no item 3/4/5 com todo o caminho em int32 antes do estreitamento para uint16.
3. Portao de fabrica em 20 mV era o DOBRO dos +/-0,1 %FE de L38 (critica de fidelidade, item a): inconsistencia interna do proprio rascunho, que no item 8 argumentava conformidade com 0,1 % e no item 7 aprovava com 0,2 %. Apertado para 10,0 mV, com o orcamento refeito no item 11 e a exigencia de 5,5 digitos no item 12 para que o portao seja mensuravel.
4. Tela "Calibracao recusada" nao declarada (critica de fidelidade, item b): declarada agora como acrescimo ao 5.7, com texto e duracao (item 10).
5. Auto Calibracao manda dado falso para um CLP possivelmente em servico (critica 7): aceito, com a tela de aviso obrigatoria do item 8 e teto absoluto de 30 s no passo 10.
6. EFOT/EFLD/EFCM ignorados e ausencia total de readback (critica 4): aceito. Item 15 declara a indetectabilidade por escrito e a ECO de rotear EFLD para IO36/IO39 vai para as pendencias.
7. board_pins.h:79 e test_07_rset.cpp nao listados nos REQ afetados (critica de fidelidade, item c): incluidos.
8. Nao conformidade de VIH do DAC8562 (critica de fidelidade, item d, e item 4 de docs/perguntas_em_aberto.md): aceita como real — AVDD = +5 V, VIH minimo 3,5 V contra 3,3 V do ESP32. Mitigacao de firmware no item 14 (refresco de 50 ms), medicao e ECO nas pendencias.
9. Comportamento no desligamento nunca medido (critica 10): vira MEDICAO 17.

REFUTADO — a critica estava errada:

10. "R20 de 1K em IAOUT e o R_SET, logo o ganho 5 cai" (critica de seguranca, item 1). O R_SET do SBOS336C liga-se ao pino SET (4), nao ao IAOUT (6); um resistor no IAOUT nao entra em nenhuma das equacoes (1), (2) ou (3) do datasheet. O trace por pixel da folha 2/2 registra que o no do SET tem exatamente tres conexoes — R12/R25 de 1K ao net VREF, C5/C24 de 47 nF ao DRV, e o proprio pino — e nada mais. Independentemente disso, a disputa ganho 2 x ganho 5 nao se resolve por argumento: a BASE COMUM ja a converteu em MEDICAO 1, e nenhuma constante desta decisao entra em release antes dela. A critica esta errada no diagnostico e certa na conclusao pratica (medir antes de fixar), que ja e obrigacao da base.
11. "Nao ha 2 mA por eixo drenados do VREF, o SET e entrada de alta impedancia, logo os 20 mV nao tem orcamento" (critica de seguranca, item 2). Errado: na Figura 2 do SBOS336C o pino SET e o no somador da malha, mantido pelo laco no potencial de VIN — e por isso que a corrente por R_OS e (V_IN - V_REF)/R_OS = +/-2 mA por eixo em +/-10 V. Nao e corrente de bias. A base ja reconhece o efeito e o transformou na MEDICAO 2, com previsao teorica de ate 3,7 mV e aceitacao de 5 mV. Uma correcao secundaria em cima da critica: como o efeito e ratiometrico (V_OUT = 10*V_REF*(D/65536 - 1/2)), a variacao da VREF e erro de GANHO, nao de offset — o que explica por que sobram apenas ~2,7 mV em -FE apos a calibracao em +FE, e nao os 3,7 mV brutos.
12. "O transitorio de energizacao e um perigo nao mitigado" (critica de seguranca, item 5). Parcialmente errado ao dimensionar a gravidade DEPOIS da base: com o nivel de falha em -11,00 V, o transitorio de POR (~-12 V) e o nivel de falha (-11,00 V) estao AMBOS fora da faixa publicada de -10 a +10 V. Para um CLP com deteccao de fora de faixa configurada — que o item 15 torna condicao de instalacao — a energizacao inteira le-se como "invalido" de forma continua, do POR ate o primeiro quadro Modbus valido, sem nenhum instante em que a saida minta um angulo plausivel. O que resta e o CLP sem essa deteccao, e para esse a base ja diz que o canal analogico nao e canal de seguranca. A ECO de OD/DAC8563 continua recomendada, mas nao e mais bloqueante.
13. "Recusar ou avisar FE absurdo" (critica de seguranca, item 9). Recusado: a Tabela 1 publica 0,1 a 90,0 graus, o erro de digitacao satura a saida na direcao de FALSO ALARME, e o passo 10 do item 9 revela a sensibilidade errada na hora. Uma tela extra de confirmacao para um valor que o proprio manual autoriza custa manual e nao compra seguranca.
14. "Gravar na NVS a tensao lida em -FE por eixo" (critica de seguranca, item 8). Recusado como redacao: a UR nao tem readback analogico, entao o numero teria de ser DIGITADO pelo tecnico — transcricao, nao evidencia, ao custo de mais uma tela de 4 digitos e de um campo de NVS que ninguem pode auditar. Cedido no que importa: grava-se `calCount` (contador de calibracoes concluidas, u8) e `calFsAngleDeci` por eixo, imprimiveis pelo console; a tensao medida vai para o relatorio de fabrica em papel/CSV que o jig ja gera; e a verificacao vira reteste periodico pelo console (`ao verify <x|y>`), sem passar pela NVS e sem tela nova.

### Precisa de decisao humana

1. **Passo 10 (verificacao em -FE) existe?** Opcoes: (a) sim, como no item 9 — acrescenta um passo a um procedimento ja publicado e obriga errata do 5.7; (b) nao, mantendo a simetria como promessa nao verificada. RECOMENDACAO: (a). E o unico ponto do produto em que metade da faixa de saida deixa de ser fe.
2. **Textos de tela acrescentados ao 5.7.** "SAIDA SIMULADA" / "Bloqueie o CLP" (item 8), "Verifique -10Vcc" (item 9), "Calibracao recusada!" (item 10). Todos sem acentuacao, seguindo a grafia de "Alteracao bem sucedida!" (L183) e "RESET DE FABRICA". RECOMENDACAO: aprovar os tres textos como estao, e imprimi-los na errata do manual.
3. **Semantica dos 4 digitos.** Opcoes: (a) offset 5000, tela abrindo no valor CORRENTE (item 7) — preserva a grafia de 4 digitos sem sinal de L172/L180 e nao salta a saida ao entrar na tela; (b) abrir em 0000 como a figura mostra, aceitando o salto de -1,907 V; (c) acrescentar sinal ao campo, o que muda a figura. RECOMENDACAO: (a).
4. **Estado dos reles durante o assistente de Auto Calibracao.** Opcoes: (a) os quatro reles continuam seguindo a leitura REAL do sensor durante todo o assistente — a simulacao de L164 e interna ao caminho analogico; (b) os quatro reles vao a ALARME durante o assistente, para que o CLP veja intervencao. RECOMENDACAO: (a). O tecnico nao esta movendo a estrutura, e (b) deixa o guindaste sem supervisao de limite durante minutos, trocando um risco de dado por um risco de cegueira. Trava com as decisoes 7, 8 e 11.
5. **Portao de 10,0 mV em -FE.** Opcoes: (a) 10,0 mV = os +/-0,1 %FE de L38, com voltimetro de 5,5 digitos obrigatorio (item 12) — RECOMENDADA; (b) afrouxar para 20 mV e declarar desvio de L38 no manual. Se as tres primeiras placas reprovarem em (a) de forma repetitiva, o que muda e o texto de L38, nao o portao.
6. **Errata do manual, itens exatos:** L38 (declarar que o +/-0,1 %FE se aplica tambem a saida analogica, verificado em -FE), L186 (voltimetro de 5,5 digitos, 0,1 mV na escala de 20 V), L42/L277/L322-L325 (acrescentar o nivel de falha de -11,00 Vcc, fora da faixa de medicao), item 8 (exigir do integrador deteccao de fora de faixa no cartao analogico do CLP e declarar que abertura/curto do cabo analogico e indetectavel pela UR). RECOMENDACAO: aprovar em bloco; sem o texto do item 8 o canal analogico nao pode ser chamado de canal de seguranca.
7. **ECO de nao conformidade de VIH do DAC8562** (AVDD = +5 V, VIH min 3,5 V x 3,3 V do ESP32; item 4 de docs/perguntas_em_aberto.md). Opcoes: (a) 74LVC1T45 ou 74AHCT125 em SYNC, SCLK e DIN na proxima revisao, mantendo o refresco de 50 ms do item 14 como paliativo nas placas ja montadas — RECOMENDADA; (b) alimentar AVDD com 3V3, que e INVIAVEL: perde o ganho 2, o FE do DAC cai para 2,5 V e a saida passa a -12,5..0,00 V, unipolar negativa; (c) aceitar como esta com base na MEDICAO 14.
8. **ECO de rotear EFLD dos dois eixos para IO36 e IO39** (pinos livres e fantasmas). RECOMENDACAO: aprovar para a proxima revisao da DE-PURI-DI261924; enquanto nao houver, vale a declaracao do item 15.
9. **ECO de OD (pino 20) para GPIO, hoje amarrado em +5 V.** RECOMENDACAO: aprovar como melhoria, prioridade abaixo dos itens 7 e 8, porque com o nivel de falha em -11,00 V o transitorio de POR ja e lido como invalido por um CLP corretamente configurado.
10. **Grampo absoluto [3277, 62259] = +/-11,25 V.** RECOMENDACAO: manter como constante de compilacao e reconfirmar contra o swing MEDIDO na MEDICAO 3 da base; se o swing medido for menor que +/-11,5 V, estreitar o grampo e, junto, reavaliar o `kAoFaultCode` de 3932.

### Precisa de medicao de bancada

Da BASE COMUM, sem renumerar e sem alterar criterio:
- **MEDICAO 1** (ganho da cadeia analogica, tres placas, minimos quadrados). BLOQUEIA todas as constantes desta decisao: 32768, 58982, 26214, 381,47 uV/LSB, [27768, 37767], [53982, 63981], [3277, 62259], 13107 e 3932. Ao aprovar, apagar include/board_pins.h:79 (kXtrRSetNominalOhms = 2500) e reescrever src/tests/test_07_rset.cpp para o modelo bipolar (hoje usa a equacao (1) do SBOS336C e acusaria -60 % do nominal em toda placa boa).
- **MEDICAO 2** (acoplamento cruzado X<->Y pelo VREF, aceitacao <= 5 mV). Alimenta diretamente o termo (a) do orcamento de erro do item 11. Se reprovar, o passo 10 passa a ser executado com o outro eixo em +FE, que e o pior caso.
- **MEDICAO 3** (swing real do XTR300 e viabilidade de -11,00 V no codigo 3932, aceitacao |V(3932) + 11,00| <= 50 mV e EFOT/EFLD/EFCM apagados). Reconfirma o grampo do item 5.
- **MEDICAO 13** (inspecao dos jumpers J3/J4, J5/J6, J13/J14). Se J5/J6 nao estiverem na posicao 'uC', o firmware deixa de chamar setMode() e o modo passa a ser fixo por montagem; o item 13 desta decisao (escrita de 3932 antes e depois da troca de modo) fica sem efeito e a troca de modo sai do produto.

Novas, especificas desta decisao:
- **MEDICAO 14 - INTEGRIDADE DAS ESCRITAS SPI DO DAC8562 COM LOGICA DE 3,3 V.** Procedimento: com o osciloscopio na saida do eixo X, alternar `ao raw x 6554` e `ao raw x 58982` 10.000 vezes, primeiro com o clock padrao de 1 MHz (`dac spi 1000000`) e depois em 100 kHz e 10 MHz; a cada escrita, esperar 5 ms e amostrar; contar qualquer patamar intermediario, que so pode vir de quadro corrompido. Repetir a 25 C e a 60 C de ambiente, com a caixa fechada. ACEITACAO: zero patamares intermediarios em 30.000 escritas por clock. Se reprovar em qualquer clock, a ECO de nivel logico (pendencia 7) deixa de ser recomendacao e vira bloqueio de release.
- **MEDICAO 15 - LINEARIDADE E SIMETRIA DA CADEIA COMPLETA, 11 PONTOS.** Procedimento: apos calibrar zero e ganho pelo assistente, medir com DMM de 5,5 digitos os codigos correspondentes a -10, -8, -6, -4, -2, 0, +2, +4, +6, +8 e +10 V, esperando 5 ms apos cada escrita; calcular o desvio de cada ponto contra a reta ideal e, em particular, |V(-FE) + 10,000|. Tres placas, dois eixos. ACEITACAO: desvio maximo <= 10,0 mV em qualquer ponto (0,1 %FE, L38) e |V(-FE) + 10,000| <= 10,0 mV. E esta medicao que valida ou derruba o orcamento de 5,9 mV do item 11 e, com ele, o portao do passo 10.
- **MEDICAO 16 - FAIXA DE TRIM REALMENTE NECESSARIA.** Procedimento: nas mesmas tres placas, registrar os valores de `calZeroCode` e `calFsCode` a que o assistente chega em cada eixo, e a distancia deles aos nominais 32768 e 58982. ACEITACAO: |calZeroCode - 32768| <= 1000 LSB e |calFsCode - 58982| <= 2500 LSB, ou seja, menos de metade da faixa de +/-5000 do item 7. Se qualquer eixo passar de 4000 LSB, a faixa de trim esta no fim do campo e o passo do digito sobe para 2 LSB (faixa +/-3,81 V, resolucao 763 uV, ainda 13x melhor que o portao de 10 mV).
- **MEDICAO 17 - EXCURSAO DA SAIDA NO DESLIGAMENTO E NO AFUNDAMENTO.** Procedimento: osciloscopio de quatro canais com disparo unico — canais na saida analogica do eixo X, na saida do eixo Y, no trilho +5 V e no +15 V — capturando (a) corte de alimentacao AC em CN2 e (b) afundamento de rede que dispare o brownout do ESP32, com carga representativa do cartao do CLP em CN1L/CN1M. 20 repeticoes de cada. Registrar a excursao maxima e sua duracao. ACEITACAO: nenhuma excursao para fora de [-12,0 V, +12,0 V] e nenhum trecho maior que 50 ms dentro da faixa util de -10 a +10 V durante o colapso (isto e, a saida nao pode parar num valor plausivel no caminho para 0 V). Se reprovar, o manual tem de especificar o tempo de filtro que o CLP precisa, ou a ECO passa a incluir retencao no +5 V suficiente para escrever 3932 antes do colapso.

---

## Decisao 11 - SCL3300 em MODE 4, anti-alias na sensora e tres bandas de faixa angular
**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto  
**REQ afetados:** MEA-01 (faixa +/-90,0 graus por eixo), MEA-02 (resolucao 0,1 grau), MEA-03 (precisao/linearidade/repetibilidade +/-0,1 % de FE), MEA-04 (TEMP como diagnostico), MEA-05 (WHO_AM_I como diagnostico), NRM-02 (indicacao na tela principal), DIR-01/DIR-02 (Sentido do Sensor), DSP-03/DSP-04 (o que a tela mostra antes do primeiro quadro valido e em falha)

### O que o manual diz
Item 2.1, manual-cliente-sui-2026.txt:34: "Escala de medição digital programável de ±90° nos eixos X e Y, em relação ao plano de referência (outras faixas de medição disponíveis sob consulta);"
Item 2.1, :37: "Resolução da indicação digital: 0,1°;"
Item 2.1, :38: "Precisão, linearidade e repetibilidade: ±0,1% da escala máxima;"
Item 2.1, :46: "Cálculo do ângulo realizado no próprio Sensor de Inclinação, por meio de chip acelerômetro digital com saída angular, transmitido à Unidade Remota já convertido em graus;"
Item 5.5, :139: "O Sensor de Inclinação calcula o ângulo internamente, por meio de um chip acelerômetro digital com saída angular, e transmite à Unidade Remota o valor já convertido em graus (°), através da interface RS485. A faixa de medição é de ±90,0° em cada eixo, com resolução de 0,1°."
Item 5.5, :140: "...são sempre expressos em graus, no formato ±XXX,X, com uma casa decimal fixa."
Item 5.7, :185: "Obs.: O ângulo de fundo de escala define apenas a proporção da saída analógica e não altera a faixa de indicação do display, que permanece em ±90,0°. Inclinações superiores ao fundo de escala mantêm a saída saturada em ±10,00 Vcc."
Item 5.8, :199: "Observação: A alteração do sentido do sensor inverte o sinal da leitura. Após alterar este parâmetro, recomenda-se refazer o Preset e conferir os valores programados nos Limites 1 a 4."
Item 6, :272: "O local de montagem do Sensor de Inclinação deve ser selecionado de forma a minimizar a exposição a vibrações mecânicas excessivas, impactos e deformações estruturais que possam comprometer a precisão da medição. O equipamento dispõe de filtros capazes de reduzir os efeitos de pequenas oscilações e vibrações presentes na aplicação. Contudo, esses recursos não substituem a necessidade de uma instalação mecânica adequada."
Item 6.1, :274: "...A faixa nominal de medição do sensor é de ±90° em cada eixo monitorado. Para que essa faixa seja plenamente utilizada, o sensor deve ser montado na posição especificada para a aplicação, observando-se a orientação indicada pela seta de referência existente no corpo do equipamento."
Item 6.1, :276: "Importante: Instalações realizadas com desalinhamento mecânico ou orientação incorreta do sensor podem reduzir a faixa útil de medição e comprometer a exatidão dos valores indicados."
Tabela 1, :117 e :122: "Ajusta Preset X | –90,0 a +90,0° (passo de 0,1°)" e "Limite 1 (X1) | –90,0 a +90,0° (passo de 0,1°)".

### A lacuna
O manual exige +/-90,0 graus por eixo com 0,1 grau e +/-0,1 % de fundo de escala, mas nao nomeia o sensor, nem seu modo, nem sua faixa dinamica em g, nem sua banda passante. Nao diz o que acontece quando o front-end satura por impacto, quando a leitura crua passa de 90,0 graus (o SCL3300 entrega ate +/-180,0), quando o sensor e montado invertido, nem quando o SCL3300 perde alimentacao no cabo de 500 m. Nao reparte o orcamento de erro entre quantizacao, sensor, temperatura e montagem, e nao diz se os +/-0,1 % sao especificacao estatica ou valem com a maquina em movimento. Do lado do codigo: o MODE esta preso em 1 pelo default do construtor (sensor/src/drivers/scl3300.h:25, nunca sobrescrito em sensor/src/main.cpp:37); a rajada periodica nao le ERR_FLAG1/ERR_FLAG2 (sensor/src/drivers/scl3300.cpp:220-223, kBurstFrames = 6 em scl3300.h:23); a leitura reprovada faz `|=` no status e deixa o angulo velho no registrador (sensor/src/main.cpp:151-158); `ready_` so e setado em `begin()` e o laco nunca rechama `begin()`; e a mascara de falha grave engloba os bits reservados 10..15 do STATUS (scl3300.cpp:12-13, kStatusHardMask = 0xFFAD). Nada disso tem contrato.

### Proposta

**1) MODE 4 fixo em compilacao.** `Scl3300 g_tilt(g_sclBus, board::kSclCs, Scl3300::kSpiDefaultHz, 4)` em sensor/src/main.cpp:37. Comando 0xB4000338 (`kCmdMode4`, scl3300_math.h:19); FE do acelerometro +/-1,2 g, sensibilidade 12000 LSB/g, LPF de 10 Hz, settle de 100 ms ja parametrizado (`kSettleMsMode34`, scl3300_math.h:53, tratado por `modeSettleMs()`, scl3300_math.cpp:96-106). Alteracao de uma linha; o driver ja e agnostico de modo. O MODE nao e selecionavel por menu nem por console: altera faixa dinamica, banda e ruido, ou seja altera a especificacao publicada, e e parametro de projeto, nao de operacao.

**2) Cobertura de faixa provada, e independe do modo.** Os registradores ANG_X/Y/Z tem escala fixa 90/2^14 em qualquer modo. A conversao ja e inteira, em decimos de grau, com arredondamento simetrico: `deci = (int16)raw * 900 / 16384` (scl3300_math.cpp:69-73, kAngleNumerator = 900, kAngleDenominator = 16384). Varredura dos 65536 codigos: LSB = 90/16384 = 0,0054932 grau = 18,20 LSB por digito de 0,1 grau; raw 0x4000 -> +900 decimos exatos; 0xC000 -> -900 exatos; extremos 0x7FFF/0x8000 -> +/-1800 decimos, que cabe em int16 sem wrap. O ramo COM SINAL (scl3300_math.cpp:70, `static_cast<int16_t>`) e obrigatorio: lido sem sinal, -45,0 graus viraria 315,0 e quebraria o formato "±XXX,X" de :140 e a faixa de Tabela 1 :117/:122.

**3) ANTI-ALIAS NA SENSORA: media movel de 5 amostras a 100 Hz.** O laco da sensora le a cada 10 ms (`kTiltPeriodMs = 10`, sensor/src/main.cpp:26) e hoje publica a amostra crua; a UR amostra a 20 Hz (kPollPeriodMs = 50 ms da BASE). A sensora passa a publicar a media movel das 5 ultimas amostras validas, em int32 e com arredondamento simetrico: `pub = (soma >= 0) ? (soma + 2) / 5 : -((-soma + 2) / 5)`. Uma media movel de 5 taps a 100 Hz tem zeros exatos em 20, 40, 60 e 80 Hz, ou seja no proprio ponto de amostragem da UR e em todos os seus harmonicos — e o unico anti-alias que fecha o par 100 Hz/20 Hz. Custo: atraso de grupo de (5-1)/2 * 10 = 20 ms, somado aos ~80 ms de resposta ao degrau do LPF de 10 Hz do MODE 4, total ~100 ms na sensora, que entra no orcamento de latencia da decisao 4. Uma amostra reprovada por `read()` NAO entra na janela; se a janela nao tiver 5 amostras validas dentro dos ultimos 100 ms, `kStsDataValid` fica limpo.

**4) CORRECAO DE SEGURANCA NO LACO DA SENSORA: atribuir o status, nunca `|=`.** sensor/src/main.cpp:151-158 hoje faz `g_registers[kRegStatus] |= kStsSclNotResponding` no ramo de falha, o que preserva o `kStsDataValid` da leitura boa anterior: o registrador 3 pode valer 0x0011 (DATA_VALID + NOT_RESPONDING) enquanto os registradores 0..2 exibem angulo velho indefinidamente. Uma UR que decida rele por `status & kStsDataValid` aceita dado velho e o rele NAO ATUA. Regra: em toda leitura, valida ou nao, o registrador 3 recebe `tilt.status` INTEIRO por atribuicao; em leitura invalida os registradores 0..2 nao sao atualizados (o dado velho fica marcado como invalido pelo proprio status, nunca mascarado por ele). O LED de status da sensora (main.cpp:161-165) passa a ler o mesmo `tilt.status`.

**5) ERR_FLAG1/ERR_FLAG2 ENTRAM NA RAJADA PERIODICA.** `kBurstFrames` de 6 para 8 (scl3300.h:23); sequencia ANG_X, ANG_Y, ANG_Z, TEMP, STATUS, ERR_FLAG1, ERR_FLAG2, ERR_FLAG2 (a resposta do quadro N pertence ao comando N-1, entao 8 comandos entregam 7 cargas uteis). `kErrFlag1SatMask = kErr1AfeSat | kErr1AdcSat = 0x07FE | 0x0800 = 0x0FFE` (scl3300_math.h:115-116); qualquer bit desta mascara em 1 seta `kStsSaturated` na amostra corrente, ao lado do STATUS.SAT (0x0040). Custo a 2 MHz: 2 quadros de 32 bits mais 12 us de CS alto = 56 us, de 168 us para 224 us num periodo de 10 ms (2,2 % do laco). Como a leitura de ERR_FLAG1/2 os LIMPA, `lastErrFlag1_`/`lastErrFlag2_` passam a ser palavras pegajosas (OR acumulado), zeradas so por `begin()`, `selfTest()` e pelo comando `diag` do console — senao a leitura periodica engoliria o historico que hoje o `begin()` reporta.

**6) SATURACAO NAO E FALHA IMEDIATA: BALDE FURADO NA UR, +20/-2, TETO 200.** A UR mantem um contador uint16 por eixo: a cada tick de 50 ms com `kStsSaturated` setado soma 20; a cada tick sem, subtrai 2 (piso 0); teto 200. Consequencias: saturacao continua declara FALHA em 10 ticks = 500 ms; ciclo de trabalho de saturacao sustentado acima de 2/22 = 9,1 % tambem declara falha; recuperacao do balde cheio leva 100 ticks limpos = 5,0 s. Enquanto o balde nao estoura, um tick saturado NAO comanda rele nem DAC: os quatro reles e as duas saidas analogicas ficam CONGELADOS no ultimo estado comandado, por no maximo 500 ms consecutivos (limite imposto pelo proprio balde). Ao estourar, aplica-se o estado de falha ja fixado pela BASE: reles no nivel de alarme (`kRelayAlarmLevel`) e DAC em `kDacFaultCode` = 3932 = -11,00 V nos dois eixos, com a permanencia minima de `kFaultMinDwellMs` = 2000 ms e saida por `kGoodsToRecover` = 5 transacoes boas. A UR conta as ocorrencias de saturacao num contador de 32 bits acessivel por console e por tela de diagnostico — sem tocar no contrato de fio de 8 registradores.
**EXCECAO EXPLICITA A BASE COMUM:** a BASE fixa `kDataMaxAgeMs` = 72 ms como idade maxima do dado que comanda rele. O congelamento do item 6 estende para 500 ms a idade do dado que SUSTENTA um rele ja comandado. Nao ha comando novo sobre dado velho — o estado e apenas mantido — e o limite e duro por construcao. Justificativa: a BASE ja exclui `kStsSaturated` da lista de status que levam a -11,00 V imediato ("kStsSclNotResponding, kStsSclCrcError, kStsSclSelfTestFail"), ou seja ela mesma trata saturacao como classe separada; e a alternativa (falha a cada impacto) e a indisponibilidade cronica que faz o operador pontear o contato.

**7) DESVIO DO MANUAL: tres bandas de faixa angular sobre o valor CRU do sensor.** Aplicado a `|deci|` de X e de Y, antes de Sentido e antes de Preset:
- 0 a 900 (0,0 a 90,0 graus): operacao normal.
- 901 a 950 (90,1 a 95,0 graus): BANDA DE TOLERANCIA. Sem falha, sem alteracao de rele. A indicacao e grampeada em +/-90,0 conforme :185, um bit de diagnostico "faixa util reduzida" e setado e contado. Justificativa do 950: 5,0 graus acima da faixa vendida sao 910 LSB do sensor e 100x a quantizacao de 0,05 grau — nenhum ruido, deriva ou erro de calibracao fecha essa distancia — e cobrem exatamente o desalinhamento mecanico que :276 ja admite como redutor da faixa util.
- acima de 950 por 20 ticks consecutivos de 50 ms (1000 ms): FALHA MECANICA DE FAIXA. Reles em `kRelayAlarmLevel`, DAC em 3932 (-11,00 V), tela de falha. NAO e latch permanente: sai da falha com `|deci| <= 950` por `kGoodsToRecover` = 5 ticks e `kFaultMinDwellMs` = 2000 ms de permanencia minima — os mesmos numeros da BASE, sem inventar um segundo criterio. Pega sensor invertido (le tipicamente 180,0 graus), sensor solto e sensor fora da orientacao da seta de :274.

**8) INDICACAO E LIMITES GRAMPEADOS EM +/-90,0, EM FIDELIDADE A :185.** O valor exibido e o valor avaliado contra os quatro limites e o mesmo: `exib = clamp(sentido * cru - preset, -900, +900)`, aritmetica int16 exata em decimos. Consequencia provada, e nao afirmada: nenhum rele deixa de atuar por causa do grampo. Para operacao ">=" com limite L em [-900, +900], se o valor real passa de 900 entao 900 >= L e o rele atua. Para "+" (modulo), |900| >= L e o rele atua. Para "<=", o valor real acima de 900 nao deveria atuar e nao atua, exceto no unico ponto L = +900, em que o grampo produz atuacao — atuacao a mais, no sentido conservador. Com isso desaparece a regiao de leitura exibida sem limite programavel: toda leitura exibida esta dentro de [-90,0, +90,0], que e exatamente a faixa que a Tabela 1 :117/:122 permite programar.

**9) CORRECAO DA MASCARA DE FALHA GRAVE DO STATUS.** Hoje `kStatusFault = 0xFFFF & ~(PWR|MODE_CHANGE) = 0xFFED` (scl3300_math.h:47) e `kStatusHardMask = 0xFFAD` (scl3300.cpp:12-13), enquanto `describeStatus()` so nomeia os bits 0..9 (scl3300_math.cpp:129-142). Um bit reservado 10..15 devolvido em 1 pelo silicio, ou preso por ruido num quadro que passe no CRC de 8 bits, declara `hardFault` e derruba o equipamento sem causa nomeavel. Passa a: `kStatusKnownMask = 0x03FF`; `kStatusFault = 0x03FF & ~0x0012 = 0x03ED`; `kStatusHardMask = 0x03ED & ~0x0040 = 0x03AD`. Bits reservados nao-zero sao contados em diagnostico e nunca declaram falha.

**10) REINICIALIZACAO AUTOMATICA DO SCL3300 NA SENSORA.** `ready_` so e setado em `begin()` (scl3300.cpp:207,215-218) e o laco nunca rechama `begin()`: hoje um afundamento do +5 Vcc no cabo de 500 m deixa a UR em FALHA permanente ate alguem desligar e religar, porque a unica saida e o comando de console `reinit` (console.cpp:292,596), inacessivel no topo de um portico. Regra: 50 leituras consecutivas reprovadas (500 ms a 10 ms) disparam `reinit()`, com backoff de 1000 ms, 2000 ms, 5000 ms e 5000 ms daí em diante; o contador de reinicializacoes e mantido em uint16 e exposto no console `diag`; durante os 3 ms de reset mais os 100 ms de settle do MODE 4, `kStsDataValid` fica limpo e `kStsSclStartup` (0x0004) setado. A janela de invalidez do reinit (103 ms) e irrelevante para a UR porque o reinit so ocorre depois de 500 ms de falhas, quando a UR ja declarou falha pelo criterio de 3 transacoes (150 ms) da BASE.

**11) DESVIO DO MANUAL (errata): orcamento de erro declarado como "+/-0,1 % da escala maxima +/-1 digito", especificacao estatica.** Reparticao dos +/-0,09 grau (0,1 % de 90 graus): sensor e deriva termica +/-0,05 grau, montagem e alinhamento +/-0,03 grau, folga +/-0,01 grau. A quantizacao de +/-0,05 grau NAO e cobrada deste orcamento — ela e o termo de formato ja publicado separadamente em :37 ("Resolução da indicação digital: 0,1°"), e :37 e :38 sao duas especificacoes distintas; mas o manual tem de dizer "+/-1 dígito" em :38 para que essa leitura deixe de depender de convencao. A aritmetica da UR contribui ZERO: Sentido e Preset sao troca de sinal e subtracao exatas em int16 de decimos, nunca ganho fracionario; a Auto Calibracao so escala a saida analogica (DAC de 16 bits) e nao entra no caminho de leitura. O vies de retificacao de vibracao (oscilacao simetrica de 0,5 a 3 Hz produz vies DC de angulo, nao ruido de media zero, e o LPF de 10 Hz nao o remove) NAO esta coberto por este orcamento: os +/-0,1 % sao especificacao estatica, medida com a maquina parada, e o manual tem de dizer isso.

**12) A RESTRICAO DE VIBRACAO DE 0,2 g NAO E PUBLICADA.** Os 0,2 g de folga dinamica em 90,0 graus (1,2 g de FE menos 1,0 g de gravidade) sao fato de projeto e criterio de ensaio, nao condicao de operacao imposta ao cliente. Nada e acrescentado a :272, que promete filtros de robustez. O que o produto entrega no lugar da restricao e o item 6: impacto vira evento tolerado e contado, nao indisponibilidade.

### Por que
A pergunta original ("o MODE 1 cobre +/-90,0 graus?") esta mal posta: a faixa angular do SCL3300 nao e funcao do modo — a escala 90/2^14 dos registradores ANG e fixa, e todos os quatro modos cobrem +/-90,0 com folga de 18x. O modo escolhe fundo de escala do acelerometro (+/-1,2 g nos modos 1, 3 e 4; +/-2,4 g no modo 2) e o LPF (40/70/10/10 Hz). Como faixa nao e problema, a decisao correta e por ruido e por anti-aliasing, e ai o MODE 4 ganha em tudo o que importa: mesma faixa, mesma resolucao, menor densidade de ruido especificada e um LPF de 10 Hz. O argumento que sozinho sustenta a escolha e o anti-aliasing: com LPF de 40 Hz do MODE 1 contra amostragem util de 20 Hz na UR, tudo entre 10 e 40 Hz (motor, redutor, ressonancia de estrutura) dobra para dentro da banda e aparece como deriva lenta do zero — que e exatamente o defeito que faz um rele de seguranca atuar fora do ponto. O LPF de 10 Hz e a media movel de 5 taps do item 3 fecham essa janela por dois caminhos independentes, um analogico no chip e um digital com zero exato em 20 Hz. Os custos sao 75 ms a mais de settle no boot (irrelevante diante do splash nao bloqueante de 1200 ms da BASE) e ~100 ms de resposta ao degrau na sensora, absorvidos pelo orcamento da decisao 4. As correcoes 4, 5, 9 e 10 nao sao melhorias: cada uma e um caminho verificado no repo pelo qual o equipamento hoje ou publica dado velho como valido, ou afirma detectar saturacao que nao le, ou converte ruido em parada, ou exige visita ao topo de um portico depois de um afundamento de tensao.

### O que a revisao adversarial derrubou
**Derrubou e a proposta cedeu:**
- *"0,2 g de folga nao sobrevive a um guindaste portuario e o item 4 do rascunho transforma isso em indisponibilidade cronica"* (lente seguranca 1) e *"publicar limite de 0,2 g restringe o que :272 ja promete"* (lente fidelidade 4). Certas as duas. O rascunho publicava "vibracao no ponto de fixacao <= 0,2 g de pico; acima disso o equipamento sinaliza falha em vez de medir", o que em portico com grab significa reles caindo varias vezes por hora — e alarme falso repetido treina o operador a pontear o contato, que e falha de seguranca, nao conservadorismo. O item 12 apaga a restricao publicada e o item 6 substitui a falha imediata por balde furado com teto de 500 ms de congelamento.
- *"o caminho de deteccao citado nao existe em tempo de execucao: a rajada le so ANG_X/Y/Z, TEMP e STATUS duas vezes; ERR_FLAG1/2 so em begin() e selfTest()"* (lente seguranca 3). Certa e verificada em scl3300.cpp:220-223 com `kBurstFrames = 6` (scl3300.h:23). A afirmacao do rascunho era falsa no codigo. Item 5 poe os dois registradores na rajada, com a mascara concreta 0x0FFE tirada de scl3300_math.h:115-116, e trata o efeito colateral de que a leitura os limpa.
- *"falha declarada na borda exata da faixa publicada: com o sensor a 89,9 graus e ruido, o equipamento oscila entre medir e latchar falha, derrubando os 4 reles no topo da propria faixa"* (lente seguranca 4). Certa. O equipamento nao pode reprovar a si mesmo dentro da especificacao que vende. Item 7 institui a banda de tolerancia ate 95,0 graus, com falha so acima disso e por 1000 ms.
- *"o item 5 resolve o display e deixa o rele em aberto: com Preset diferente de zero existe regiao de leitura exibida sem limite programavel"* (lente seguranca 5) e *"o item 5 contradiz :185 e apresenta a contradicao como conformidade; o formato ±XXX,X e mascara de digitos, nao autorizacao de faixa"* (lente fidelidade 1). Certas as duas, e apontam para a mesma correcao. O rascunho deixava a leitura com Preset passar de 90,0. O item 8 grampeia em +/-90,0 conforme :185, o que elimina de uma vez a contradicao de fidelidade e a regiao sem limite programavel, e acrescenta a prova de que o grampo nao impede nenhum rele de atuar.
- *"o escape para instalacao invertida nao funciona: :199 diz que o Sentido do Sensor inverte o sinal, e inverter sinal nao traz 180 graus para dentro de +/-90"* (lente fidelidade 2). Certa, e o rascunho estava errado ao oferecer esse remedio. O item 7 nao oferece mais remedio de menu para montagem invertida: montagem invertida e defeito de instalacao, detectado e sinalizado, e corrigido com chave de fenda. E a deteccao continua existindo porque 180,0 graus fica muito acima dos 95,0 da banda de tolerancia.
- *"nao ha recuperacao automatica de brown-out do SCL3300; a unica saida e o console reinit"* (lente seguranca 6). Certa e verificada (scl3300.cpp:207,215-218 contra sensor/src/main.cpp:151-158). Item 10.
- *"a retificacao de vibracao nao esta no orcamento de erro e desloca o ponto de atuacao de forma sistematica"* (lente seguranca 7). Certa: vies DC nao e ruido de media zero e o LPF de 10 Hz nao remove balanco de 0,5 a 3 Hz. Item 11 declara os +/-0,1 % como especificacao estatica e a medicao 16 mede o vies.
- *"-11,50 V e codigo inventado e 'estado sinalizado' do rele nao esta decidido"* (lente fidelidade 3). Certa quanto ao numero: a BASE COMUM ja fixou -11,00 V / codigo 3932 e ja registrou a polaridade do rele como decisao do bigboss. Os itens 6 e 7 usam so os simbolos da BASE (`kDacFaultCode`, `kRelayAlarmLevel`), sem numero proprio.

**Derrubou parcialmente, com refutacao:**
- *"a quantizacao de 0,05 grau consome mais da metade dos +/-0,09 grau e a promessa fica sem plano de fechamento"* (lente seguranca implicita, lente fidelidade 5). A aritmetica esta certa mas a conclusao nao segue: :37 e :38 sao duas especificacoes distintas no mesmo item 2.1, e a convencao de instrumentacao e que exatidao se declara sobre a leitura, com o digito menos significativo somado a parte. O que procede da critica e que o manual nao diz isso. Item 11 fecha por escrito, com "+/-1 dígito", e reparte o resto em numeros; nao ha plano de aumentar resolucao, porque 0,1 grau esta fixado em :37, :139, :140 e na Tabela 1 inteira.
- *"nao congelar o MODE antes de medir 24 h com a maquina em operacao"* (lente seguranca, correcao a). O diagnostico e bom, a conclusao nao: adiar a escolha nao e decisao, e o firmware precisa de um valor para existir. Alem disso o argumento decisivo do MODE 4 nao depende da medicao — e o anti-aliasing contra 20 Hz, que o MODE 2 (LPF de 70 Hz) piora, nao melhora. O MODE 4 fica fixado agora e a medicao 14 e o criterio objetivo unico que pode reverte-lo; e mesmo revertendo para MODE 2 a media movel do item 3 permanece, porque e ela e nao o LPF do chip que zera 20 Hz.
- *"tratar saturacao como amostra invalida com hold do ultimo valor, falha so acima de 1 s"* (lente seguranca, correcao b). Cedido no principio, com numero diferente: 500 ms e nao 1 s. 500 ms sao 5x a duracao mais longa esperada de saturacao por impacto (10 a 100 ms) e limitam a 0,5 s a janela em que um rele de seguranca sustenta estado sobre dado congelado. E, ao contrario da proposta da critica, um unico mecanismo (o balde) produz tanto o limite de 500 ms continuos quanto a tolerancia de ciclo de trabalho de 9,1 % — a critica pedia dois criterios ("1 s" mais "balde furado"), o que seria um segundo numero comandando rele.

**Nao derrubou:**
- A correcao da mascara de status (item 9) foi confirmada pelas duas lentes contra o codigo (scl3300_math.h:47, scl3300.cpp:12-13, describeStatus so nomeia bits 0..9). Mantida sem alteracao: 0x03FF / 0x03ED / 0x03AD.
- A leitura do angulo pelo ramo com sinal, a conta 90/2^14 e a existencia do `kCmdMode4` = 0xB4000338 com settle de 100 ms ja implementado foram verificadas pelas duas lentes. Mantidas.

### Precisa de decisao humana
1. **Errata de :38 para "Precisão, linearidade e repetibilidade: ±0,1% da escala máxima ±1 dígito", e qualificacao como especificacao estatica.** Opcoes: (a) publicar a errata e declarar que os +/-0,1 % valem com a maquina parada, com o vies de vibracao tratado por instalacao mecanica conforme :272; (b) manter o texto atual e aceitar que a promessa nao e demonstravel em campo. RECOMENDACAO: (a). Sem ela, a medicao 16 pode reprovar um equipamento que esta correto.
2. **Texto literal da tela de falha mecanica de faixa (item 7).** O manual nao tem tela literal nenhuma para falha (o lev_manual-ihm.md registra a ausencia inclusive para falha de comunicacao). Proposta concreta, na convencao sem acento das mensagens do manual ("Alteracao bem sucedida!", "RESET DE FABRICA"): linha 1 "SENSOR FORA DE FAIXA", linha 2 o eixo culpado e o valor cru grampeado em 3 digitos. Opcoes: (a) aprovar este texto e acrescenta-lo ao manual; (b) escolher outro texto. RECOMENDACAO: (a) — mas nenhuma tela pode ser inventada em silencio, e esta so existe se for para o manual.
3. **Publicar ou nao a banda de tolerancia de 95,0 graus no manual.** Opcoes: (a) publicar em 6.1, junto de :276, como "leituras entre 90,1 e 95,0 graus sao indicadas como 90,0 graus e registradas como faixa util reduzida"; (b) manter como comportamento interno nao documentado. RECOMENDACAO: (a): o cliente que instalar com 3 graus de desalinhamento precisa saber por que perdeu faixa, e :276 ja prepara o terreno.
4. **Aceitar a excecao ao `kDataMaxAgeMs` da BASE: rele congelado por ate 500 ms em saturacao.** Opcoes: (a) aprovar o congelamento de 500 ms (item 6); (b) exigir falha imediata em qualquer amostra saturada, com reles ao estado de alarme a cada impacto. RECOMENDACAO: (a). (b) e correto no papel e insustentavel em porto — e o equipamento que sera ponteado.
5. **Ordem de aplicacao de Sentido e Preset, que o item 8 assume.** A proposta usa `exib = clamp(sentido * cru - preset, -900, +900)`, ou seja cru -> sentido -> preset. Opcoes: (a) esta ordem, em que trocar o Sentido inverte tambem o significado do Preset ja gravado e por isso o firmware DEVE avisar e pedir refazer o Preset, como :199 ja recomenda; (b) cru -> preset -> sentido, em que o Preset sobrevive a troca de Sentido mas o sinal do offset deixa de bater com a leitura. RECOMENDACAO: (a), com invalidacao explicita do Preset e aviso na tela ao confirmar troca de Sentido — e a unica que casa com o texto de :199. Nota: este sub-item pertence formalmente a DIR-01/DIR-02, que hoje nao tem dono; o item 8 nao pode ser implementado sem ele.
6. **MODE 2 como escape.** Opcoes: (a) MODE 4 fixo, revertido para MODE 2 apenas se a medicao 14 reprovar; (b) MODE 2 preventivo. RECOMENDACAO: (a). MODE 2 dobra a faixa dinamica mas leva o LPF a 70 Hz, piorando o aliasing contra 20 Hz, e corta a sensibilidade bruta pela metade — nunca preventivamente.

### Precisa de medicao de bancada
- **MEDICAO 14 - PICO DE |ACC| POR EIXO E CONTAGEM DE SATURACAO EM 24 h NA MAQUINA REAL (BLOQUEIA MODE 4 x MODE 2).** Sensor montado no ponto de fixacao definitivo, na atitude estatica de instalacao. Registrar por 24 h de operacao real: pico e percentil 99,9 de |ACC_X|, |ACC_Y| e |ACC_Z| (comandos ja existentes: `kCmdReadAccX/Y/Z` = 0x040000F7 / 0x080000FD / 0x0C0000FB), a contagem de ticks com `kStsSaturated` e o valor maximo atingido pelo balde do item 6. ACEITACAO: pico por eixo <= 1,10 g (8 % abaixo do FE de 1,2 g do MODE 4) e balde nunca acima de 100 (metade do teto). Se reprovar, MODE 4 esta fora e a reversao e para MODE 2, mantendo a media movel de 5 taps do item 3, que e quem faz o anti-alias.
- **MEDICAO 15 - RUIDO ANGULAR MODE 1 x MODE 4.** Sensor parado sobre bancada de granito, 10 minutos de registro contínuo a 20 Hz pela UR, em MODE 1 e em MODE 4. Registrar desvio padrao em decimos de grau e pico-a-pico do ultimo digito do display. ACEITACAO para MODE 4: sigma <= 0,02 grau e ultimo digito estavel (sem alternancia) em pelo menos 95 % das amostras. Confere o numero de ruido do doc 4921 rev.4 contra a bancada, em vez de cita-lo de catalogo.
- **MEDICAO 16 - VIES DE RETIFICACAO DE VIBRACAO (BLOQUEIA A ERRATA DE :38).** Mesmo angulo mecanico, sem tocar na montagem: media de 10 minutos com a maquina PARADA contra media de 10 minutos com a maquina em operacao normal. ACEITACAO: diferenca das medias <= 0,03 grau. Se passar, os +/-0,1 % de :38 nao se sustentam em campo e a errata do item 11 (especificacao estatica) deixa de ser opcional.
- **MEDICAO 17 - FAIXA E BANDAS, EM MESA DIVISORA OU NIVEL DE PRECISAO.** Um eixo por vez (com um unico vetor gravidade X e Y nao podem valer 90 simultaneos, e o roteiro de aceitacao nao pode exigir 90/90). Pontos: -90,0 / -45,0 / 0,0 / +45,0 / +90,0 graus mecanicos, mais 92,5 graus (banda de tolerancia) e 100,0 graus (falha). ACEITACAO: erro <= 0,09 grau +/-1 digito nos cinco pontos da faixa; em 92,5 graus o display indica 90,0 e nenhum rele muda de estado; em 100,0 graus a falha e declarada entre 1000 e 1100 ms e some entre 2000 e 2300 ms depois do retorno (permanencia minima de 2000 ms mais 5 ticks). Repetir com o sensor montado invertido (180 graus) e confirmar falha.
- **MEDICAO 18 - RECUPERACAO DE BROWN-OUT DO SCL3300 (VALIDA O ITEM 10).** Com o cabo real de 500 m, aplicar 20 afundamentos do +5 Vcc do sensor (queda a 0 V por 50 ms, por 200 ms e por 2 s, com fonte programavel em CN1 terminais 4/5). Medir o tempo do fim do afundamento ate a UR voltar a receber `status == 0x0001`. ACEITACAO: recuperacao automatica em 100 % dos 20 eventos, com tempo maximo <= 1700 ms (500 ms de deteccao + 1000 ms do primeiro backoff + 103 ms de reset e settle + 2 ticks), sem nenhuma intervencao de console. Registrar o contador de reinicializacoes.
- **MEDICAO 19 - CUSTO DA RAJADA DE 8 QUADROS E DA MEDIA MOVEL NO LACO DA SENSORA.** GPIO livre da sensora levantado antes e baixado depois do bloco `read()` + publicacao, osciloscopio, 1000 ciclos. Previsto 224 us de SPI mais a media movel. ACEITACAO: pior caso do bloco <= 1,0 ms, ou seja <= 10 % do periodo de 10 ms, para que a latencia do escravo derivada na BASE (2,05 a 4,55 ms) continue valendo.

---

## Decisao 12 - Controlador do display, autoteste, tela principal e LED LIG

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto
**REQ afetados:** MAN-2.1-L35, MAN-4-L72, MAN-4-L73, MAN-5-L75, MAN-5-L76, MAN-5-L77, MAN-5.2-L84, MAN-5.2-L85, MAN-5.2-L86, MAN-5.2-L92, MAN-5.2-L94, MAN-7-L306, DSP-01, DSP-02, DSP-03, DSP-04, NRM-02; src/drivers/display_u8g2.h/.cpp, include/board_pins.h:23/25/42/74, src/main.cpp (passos 8/9/10 e 14 da ordem de boot), BOM do modulo de display.

Justificativa do grau "alto", e nao "baixo" como no rascunho: o trafego de refresh especificado aqui compartilha o unico condutor de retorno do painel (CN3-4) com as tres linhas de botao, e IO34/IO35 nao tem pull-up na placa mae (lev_display-teclado.md). Um toque falso em ▲ efetiva o Preset, e por MAN-5.9-L214 os quatro Limites se referem a leitura ja com Preset — ou seja, ruido de refresh e um caminho fisico ate o deslocamento silencioso dos quatro pontos de atuacao de rele. O caminho e criado por esta decisao e por isso ela e dona do piso de debounce e do limite de acoplamento.

### O que o manual diz
- 2.1, L35: "Display OLED branco de 3,2", resolução 256 × 64 pixels;"
- 4, L72-L73: "Display" / "Em condições normais, o display indica o valor da medição, inclusive para valores negativos. Quando em modo de programação, mostra o novo valor a ser programado e seus parâmetros."
- 5, L75: "Ao energizar a Unidade Remota, o equipamento executa automaticamente uma rotina de inicialização. Durante esse processo, é realizado um autoteste do display, permitindo verificar seu correto funcionamento."
- 5, L76: "Simultaneamente, o LED LIG é acionado, indicando que a Unidade Remota está devidamente alimentada e em operação."
- 5, L77: "Após a conclusão do autoteste, a logomarca da Di-Elétrons é exibida temporariamente no display. Em seguida, o equipamento conclui a sequência de inicialização e passa a operar normalmente, apresentando a tela principal de medição."
- 5.2, L84-L86: "No Modo Normal, a Unidade Remota exibe continuamente no display o valor da inclinação medida. Como o equipamento monitora dois eixos independentes, a leitura é acompanhada da identificação do eixo correspondente:" / "X: Inclinação do eixo X;" / "Y: Inclinação do eixo Y."
- 5.2, L92 e L94: "Alterna a indicação do display entre os eixos X e Y." (tecla ▼) / "Não possui função neste modo de operação." (tecla ▲)
- 5.5, L136: "A indicação do display e todos os parâmetros angulares — Preset, Limites 1 a 4 e pontos de Auto Calibração — são sempre expressos em graus, no formato ±XXX,X, com uma casa decimal fixa."
- 7, L306: "...será exibida no display a mensagem de falha de comunicação."

### A lacuna
O manual nunca nomeia o controlador, a interface, o clock, o conteudo do autoteste, seu criterio de aprovacao, sua duracao, a duracao da logomarca, o layout literal da tela principal, o texto literal da mensagem de falha de comunicacao (L306 promete "a mensagem" e nao a escreve), a politica de acentuacao das strings, nem como o operador distingue um painel congelado de um painel vivo. O esquematico nao ajuda: o CN4 tem 7 pinos sem designador, sem part number, sem GND (o retorno e o CN3-4) e sem MISO — o controlador e eletronicamente inobservavel. E o "LED LIG" da L76 nao existe como LED discreto em nenhuma das duas folhas; o unico net possivel e o CN4-1 / IO2, acionado por firmware.

### Proposta

1) CONTROLADOR. SSD1322 (Solomon Systech), painel OLED branco 3,2", 256x64, modulo classe NHD-3.2-25664, interface 4-wire hardware SPI, modo SPI 0, CS ativo baixo, D/C alto = dado, RES ativo baixo, column start 28 (0x1C), 128 bytes por linha (2 px/byte). Sao exatamente os parametros do driver ssd1322_nhd_256x64 do U8g2 ja em uso. O modulo chega strapped em 4SPI, porque BS0/BS1 nao chegam ao CN4.

2) FONTE UNICA. Criar include/display_panel.h com: kPanelWidth=256, kPanelHeight=64, kPanelColStart=28, kPanelSpiHz=4000000, kPanelSpiMode=0, kPanelResetLowMs=10, kPanelResetSettleMs=120, kPanelContrast=255, kFrameBufferBytes=2048. Mais duas notas normativas que hoje so existem em levantamento: CN4 NAO tem GND (retorno pelo CN3-4; display sozinho no CN4 nunca acende) e CN4-5, serigrafado "DATA CLEAR", e o D/C# do SSD1322. A mensagem de falha do teste de display cita o CN3-4.

3) CLOCK DE 4 MHz TORNADO REAL. U8g2Display::begin() passa a chamar `u8g2_.setBusClock(4000000)` ANTES de `u8g2_.begin()`. Sem essa chamada, u8x8->bus_clock fica 0 e e preenchido com display_info->sck_clock_hz = 10000000UL (U8x8lib.cpp:786-787 e u8x8_d_ssd1322.c:253): o painel roda hoje a 10 MHz, e board::kDisplaySpiHz = 4 MHz e ficcao neste caminho. Escolho 4 MHz, nao 10 MHz: 10 MHz e o teto do proprio SSD1322 em serial de 4 fios (tcycle 100 ns) e o comentario do U8g2 registra que esse valor foi alcancado por escalada empirica ("increased to 8MHz (issue 215), 10 MHz (issue 301)"), ou seja, margem zero — sobre uma cablagem de fio soldado, sem conector, sem GND proprio e com retorno compartilhado com os botoes. Consequencia aritmetica unica: 8192 bytes por quadro = 16,384 ms de relogio por sendBuffer().

4) DRIVE STRENGTH REDUZIDO. IO18 (SCLK), IO23 (MOSI), IO4 (DC) e IO5 (CS) configurados com gpio_set_drive_capability(..., GPIO_DRIVE_CAP_0) logo apos o passo 8 da ordem de boot. Reduz di/dt na fonte, que e o parametro que governa o degrau de terra no CN3-4 — baixar o clock reduz o numero de bordas por segundo, mas so o drive reduz a amplitude de cada borda.

5) PISO DE DEBOUNCE IMPOSTO POR ESTA DECISAO. Nenhuma tecla pode ser aceita com menos de 50 ms de nivel estavel. 50 ms / 16,384 ms = 3,05x o maior quadro: um pulso acoplado, mesmo que durasse a rajada inteira de um sendBuffer, nao atravessa o filtro. Se a medicao 10 revisada devolver quadro acima de 16,7 ms, o piso de debounce sobe junto para manter os 3,0x. Este e um piso, nao o gesto: a semantica das teclas continua sendo das decisoes 1, 2 e 3.

6) SPLASH, CONFORME A BASE COMUM. Autoteste 600 ms + logomarca Di-Eletrons 600 ms, NAO BLOQUEANTES, executados como maquina de estados no loop() depois do setup(), com a tarefa ctrl ja polando, avaliando limites e comandando reles. Os 2000 ms + 1500 ms do rascunho estao mortos. Contraste 255 durante todo o splash e durante toda a operacao.

7) CONTEUDO DO AUTOTESTE (e este ensaio E o autoteste da L75). Um unico quadro contendo: (a) moldura de 1 px percorrendo (0,0)-(255,63); (b) regua horizontal com tique a cada 16 px e marca alta em x = 0, 64, 128, 192 e 255; (c) duas colunas isoladas de 1 px em x=0 e x=255; (d) a linha de texto "AUTOTESTE  FW <maj>.<min>.<pat>". Criterio de aprovacao, em uma frase para o operador: as QUATRO bordas fechadas, as cinco marcas altas visiveis e o texto legivel. O que ele prova: painel vivo, 256 colunas e 64 linhas efetivamente enderecadas com o driver SSD1322, column start correto (0 em vez de 28 desloca a imagem em 56 px e corta a moldura) e ausencia de linha/coluna morta. O que ele NAO prova: a identidade do controlador — ver item 8.

8) AUTOTESTE SOB DEMANDA. DESVIO DO MANUAL: o mesmo padrao e reexecutavel em Modo Normal mantendo a tecla ▼ pressionada por 3000 ms, sem senha (o padrao nao altera parametro nenhum) e sem desligar o equipamento; sai ao primeiro toque em qualquer tecla ou por timeout de 30000 ms, voltando a tela principal. O toque curto em ▼ continua alternando X/Y (L92). Isso da cobertura em servico ao autoteste na inspecao periodica do item 6.3 — a energizacao unica de uma UR portuaria daria cobertura praticamente nula. Exige uma linha em 5.2 e uma em 6.3 do manual.

9) VEREDITO DE CONTROLADOR VEM DA ETIQUETA, NAO DA TELA. O part number do modulo passa a ser item CONTROLADO da BOM, com controle de alteracao de engenharia, lido na etiqueta e registrado unidade a unidade no relatorio de fabrica. O build flag alternativo `-D UR_DISPLAY_PANEL=SSD1362` permanece como opcao de compilacao, nao como procedimento de campo. Nao ha deteccao automatica: sem MISO nao ha o que ler.

10) TELA PRINCIPAL, UM EIXO POR VEZ (NRM-02, L84-L86 e L92). Layout literal, 256x64:
- linha de cabecalho, y=0..13, fonte u8g2_font_helvB12_tr, texto "EIXO X" ou "EIXO Y";
- campo de valor, y=18..57, fonte u8g2_font_logisoso32_tn, formato exato "±XXX,X" conforme L136 (sinal sempre presente, virgula decimal, uma casa fixa), seguido do simbolo de grau desenhado por primitiva (drawCircle raio 2 px), para nao depender de codepage;
- batimento, item 11, no canto inferior direito.
Eixo exibido na energizacao: X. A selecao X/Y e volatil e NAO e persistida em NVS (evita escrita de flash por toque de tecla e da um estado inicial conhecido a cada boot). ▼ continua alternando X/Y tambem durante a falha de comunicacao.

11) BATIMENTO DE DADO FRESCO, NAO DE LACO VIVO. DESVIO DO MANUAL: acrescimo a secao Display (L73). Marcador rotativo de 4 posicoes numa caixa de 8x8 px em (247,55)-(254,62); o indice da posicao e `(contadorDeTransacoesValidas / 5) % 4`, publicado pela tarefa ctrl. Com o enlace saudavel a 50 ms por transacao (base comum), o marcador avanca um passo a cada 250 ms. Marcador parado = nao ha dado novo, seja porque o enlace parou, seja porque o firmware parou. Escolhi marcador rotativo, e nao bloco piscante, porque o refresh e de 250 ms e um piscante de periodo 500 ms fica na fronteira de Nyquist da propria taxa de desenho; a rotacao de 4 estados nao alias em nenhuma fase.

12) OBSOLESCENCIA DO DADO, CONTRATO UNICO COM A BASE. Tres estados, um criterio, sem numero novo:
- antes do primeiro quadro Modbus valido: cabecalho "AGUARDANDO SENSOR", campo de valor "---,-", marcador parado;
- enlace saudavel: numero, conforme item 10;
- falha declarada (3 transacoes invalidas consecutivas = 150 ms, criterio unico da tarefa ctrl): DESVIO DO MANUAL na letra, nao no espirito — cabecalho substituido por "FALHA DE COMUNICACAO" e campo de valor substituido por "---,-". L306 exige a mensagem e nao define o texto; este e o texto. A saida de falha e a mesma da base: 5 transacoes boas consecutivas e permanencia minima de 2000 ms.
NAO exibo a idade do ultimo quadro em decimos de segundo, como pediu a critica: em operacao normal a idade e limitada a 72 ms por construcao (kDataMaxAgeMs) e a falha e declarada em 150 ms, entao o campo mostraria "0,0" ou "0,1" para sempre e nao informaria nada ao operador. A frescura e binaria: numero ou "---,-".

13) O DISPLAY NAO COMANDA NADA. Sem MISO, U8g2Display::begin() nao prova que o painel respondeu; o retorno so diz que a biblioteca inicializou. Nenhuma decisao de rele e nenhuma escrita de DAC depende do estado do display, e a ausencia de imagem nunca inibe a atuacao dos reles. O refresh roda exclusivamente no loop() (core 1, prioridade 1); a tarefa ctrl (core 0, prioridade 5) nunca desenha. Cadencia de tela fixa em 250 ms (4 Hz): 16,384 ms de 250 ms = 6,6 % do loopTask.

14) LED LIG, RESOLVIDO E NAO ANOTADO (L76). O unico net capaz de acender um LED de "LIG" no painel e o CN4-1 / IO2; nao existe LED discreto ligado a fonte em nenhuma das duas folhas. DESVIO DO MANUAL: o LED LIG passa a ser pulsado, nao continuo — 900 ms aceso / 100 ms apagado, periodo de 1000 ms, gerado pela MESMA ISR de timer de hardware em IRAM a 1 kHz que chuta o WDI, e portanto travado pelo MESMO token de liveness de 800 ms da tarefa ctrl. Consequencia: LED aceso com uma piscada curta por segundo = alimentada E firmware vivo; LED apagado = sem alimentacao, firmware travado ou placa em modo download. Um LED de alimentacao acionado por firmware e continuo mente exatamente quando importa: fica aceso no travamento. A relacao 900/100 preserva a leitura visual de "aceso" exigida pela L76. IO2 permanece em nivel BAIXO do passo 3 ate o passo 14 da ordem de boot, por ser pino de strapping.

15) IO0 SAI DO FIRMWARE DE APLICACAO. `board::kFreeTestpoint = IO0` (include/board_pins.h:42) e removido da aplicacao: IO0 e o pino de boot do header PROG (CN5F) com pull-up R36 de 10K, nao um testpoint. A aplicacao nunca configura IO0. Com o item 14, "energizada e morta em modo download" deixa de ser silenciosa: sem pulso de WDI, o STWD100 assere RST# em 1,12 a 2,24 s e a placa entra em ciclo de reset visivel, com o LED LIG apagado.

16) POLITICA DE ACENTUACAO, FECHADA AQUI PORQUE E ESCOLHA DE FONTE. DESVIO DO MANUAL: toda a IHM usa fontes "_tr"/"_tn" do U8g2 (ASCII), SEM acentuacao. Isso reproduz literalmente as strings que o manual ja imprime sem acento ("Alteracao bem sucedida!", "Angulo fim de escala X(°):+045,0", "RESET DE FABRICA") e obriga a errata dos itens de menu que o manual imprime acentuados: "Auto Calibracao", "Operacao Limite", "Horario", "Anti-horario". O simbolo de grau nao vem da fonte: e desenhado por primitiva. Motivo: um unico conjunto de glifos, sem risco de codepage, e nenhuma string da IHM podendo cair para um glifo vazio no campo que o operador le.

17) VIDA UTIL. Contraste fixo em 255 (maximo) em toda a operacao, incluindo o autoteste. Contra queima de layout estatico em 24/7, deslocamento da tela inteira em 1 px, alternando entre (0,0), (1,0), (1,1) e (0,1) a cada 900000 ms (15 min). O deslocamento carrega junto o cabecalho, o valor e o marcador de batimento.

18) ORDEM DE BOOT: NADA A ALTERAR. Vale a base comum. O passo 8 pre-reserva o VSPI com SPI.begin(18, -1, 23, -1), o begin() interno do U8g2 vira no-op e IO19 (WDI) nunca e reivindicado; o passo 10 (rearmPin) fica como cinto-e-suspensorio. A lacuna de WDI atribuivel ao display passa a ser ZERO, e o chute e por ISR em IRAM, com teto duro de 250 ms independentemente do que o display faca.

19) RECURSOS. Framebuffer U8g2 full-buffer monocromatico 256x64 = 2048 bytes (u8g2_m_32_8_f), estatico dentro do objeto, instanciado em escopo de arquivo no composition root. Nenhuma alocacao de heap depois do setup().

### Por que
O que o firmware faz com o painel (column start 28, 2 px por byte, mapeamento de linha, comandos de contraste) e especifico do SSD1322: se o modulo for outro, nada disso funciona e nao ha erro reportavel, so tela errada. Sem MISO a unica prova possivel e visual, entao a prova visual tem de ser projetada para discriminar largura e offset, nao apenas para acender pixels — e a identidade do controlador tem de vir da etiqueta, que e o unico dado observavel de verdade. Os 4 MHz deixam de ser numero de papel e passam a ser numero de fio, com 2,5x de margem sobre o teto do controlador, sobre uma cablagem soldada sem terra propria. E o LED LIG, que o manual promete como indicador de alimentacao, so nao mente se for pulsado pela mesma ISR que mantem o cachorro vivo: e o unico jeito de um indicador acionado por GPIO significar "em operacao" e nao "estava em operacao quando travou".

### O que a revisao adversarial derrubou

CEDI, e a proposta corrigiu:
- **Boot loop (seguranca 1).** A critica esta certa: 3500 ms de splash contra tWD minimo de 1,12 s, sem posicao definida em relacao ao rearmPin, e falha determinante. Corrigido pela base comum e reproduzido no item 6: 600 + 600 ms NAO BLOQUEANTES depois do setup(), e no item 18: SPI.begin(18,-1,23,-1) antes do U8g2 fecha a lacuna de WDI do display em zero, e o chute e por ISR em IRAM.
- **Dado velho exibido com vivacidade (seguranca 2).** Certa. O batimento sozinho e confianca visual num display que pode mentir. Corrigido pelos itens 11 e 12: o marcador e comandado por TRANSACAO VALIDA, nao por volta de laco, e a falha substitui o numero por "---,-" e escreve "FALHA DE COMUNICACAO". Recusei so o campo de idade em decimos de segundo, pelo motivo aritmetico do item 12.
- **Display como unico anunciador (seguranca 3).** Certa no contexto do rascunho. Deixou de ser verdade: a base comum leva os reles ao estado de alarme e a saida analogica a -11,00 V (codigo 3932) na falha de enlace. O display e agora o anunciador legivel por humano; os canais legiveis por maquina existem.
- **Cobertura nula do autoteste em servico (seguranca 4).** Certa. Item 8: autoteste sob demanda por hold de 3000 ms em ▼, sem senha e sem desligar.
- **Injecao nos botoes pelo retorno comum (seguranca 5).** Certa, e era o achado mais serio. Itens 4, 5 e a medicao nova 14: drive reduzido, 4 MHz, piso de debounce de 50 ms com 3,05x sobre o quadro, e osciloscopio nas tres linhas de botao durante rajada de refresh, na placa real com o cabo real.
- **Orcamento de latencia de laco cooperativo (seguranca 6).** Certa. Resolvido pela base: a tarefa ctrl e dona exclusiva de rele e DAC, e o quadro do display nao entra no orcamento de seguranca. Os "36,4 ms" do rascunho estao mortos.
- **Contraste reduzido sem ensaio (seguranca 7).** Certa. Item 17: 255 fixo. Reduzir so depois da medicao 15.
- **LED LIG continua mentindo (seguranca 8) e fidelidade (a).** Ambas certas, e a de fidelidade tem razao dupla: o rascunho listou o REQ da L76 e nao o resolveu, nem com implementacao, nem com desvio. Item 14 resolve: IO2 pulsado pela ISR do WDI, travado pelo token de liveness.
- **IO0/IO2 como caminho de falha do equipamento, nao nota de conector (seguranca 9).** Certa. Item 15: IO0 sai do firmware de aplicacao; e com o item 14 a placa em modo download deixa de ser silenciosa (LED apagado + ciclo de reset do STWD100).
- **Part number como item controlado (seguranca 10).** Certa. Item 9.
- **kPanelSpiHz = 4 MHz e ficcao (fidelidade b, e a critica de completude).** Certa, e verificada no repo: U8x8lib.cpp:786-787 preenche bus_clock com sck_clock_hz do driver quando ninguem chama setBusClock, e u8x8_d_ssd1322.c:253 traz 10000000UL. Escolhi tornar os 4 MHz reais (item 3) em vez de adotar os 10 MHz, pelo motivo de margem do item 3, e assumo o custo: 16,384 ms por quadro em vez de 6,55 ms.
- **Poder discriminante superestimado (fidelidade c).** Certa. Um SSD1325/SSD1327 recebendo comandos de SSD1322 nao desenha "uma moldura de 128 colunas": desenha lixo ou nada. O item 7 foi reescrito para reivindicar apenas o que o padrao prova (painel vivo, 256x64 enderecado, column start, linha/coluna morta) e o item 9 move o veredito de controlador para a etiqueta.

REFUTEI:
- **Idade do quadro em permanencia na tela (seguranca 2, parte final).** A base fixa idade maxima do dado que comanda rele em 72 ms e declaracao de falha em 150 ms. Um campo de idade mostraria "0,0"/"0,1" durante toda a vida do equipamento e "---" na falha: nao acrescenta informacao ao operador e ocupa area do unico campo que ele precisa ler a distancia. A informacao util e binaria e ja esta no item 12.
- **"Transferir o quadro so depois de N amostras estaveis" / blanking explicito de amostragem (seguranca 5a).** Desnecessario neste desenho: o refresh e a amostragem de botao rodam na MESMA tarefa (loopTask), entao nao existe amostragem concorrente com o sendBuffer — o blanking e estrutural. O que resta e o nivel latchado logo apos a rajada, e isso e coberto pelo piso de 50 ms do item 5, que e 3,05x o quadro.
- **Chute de WDI por LEDC/RMT (seguranca 1b).** A direcao esta certa mas o mecanismo foi superado: a base comum ja fixa ISR de timer de hardware em IRAM a 1 kHz com GPIO.out_w1ts/out_w1tc e token de liveness de 800 ms, que e melhor que LEDC porque continua morrendo quando o firmware trava — um periferico em loop livre chutaria o cachorro para sempre, inclusive com o firmware morto.

### Precisa de decisao humana
1. **Texto literal da mensagem de falha de comunicacao (L306 nao o define).** Opcoes: (a) "FALHA DE COMUNICACAO"; (b) "FALHA DE COMUNICACAO COM O SENSOR"; (c) texto com acentuacao. RECOMENDACAO: (a) — cabe folgado na largura do cabecalho, casa com a grafia sem acento que o manual ja usa em "RESET DE FABRICA", e o detalhamento de causas ja esta na L306 do manual, nao na tela.
2. **Politica de acentuacao da IHM (item 16).** Opcoes: (a) toda a IHM sem acento, com errata do manual em "Auto Calibracao", "Operacao Limite", "Horario", "Anti-horario"; (b) fonte com acentuacao e strings fieis ao manual, que hoje e internamente inconsistente (menus acentuados, mensagens sem acento). RECOMENDACAO: (a).
3. **Autoteste sob demanda por hold de 3000 ms em ▼ (item 8).** Opcoes: (a) o gesto proposto, com uma linha nova em 5.2 e uma em 6.3; (b) item novo no menu de programacao, o que acrescenta um 17o parametro a Tabela 1; (c) sem autoteste sob demanda, cobertura so na energizacao. RECOMENDACAO: (a) — nao toca na Tabela 1, nao exige senha para um padrao que nao altera parametro, e nao colide com o toque curto de ▼ (L92) nem com o duplo toque de ▲.
4. **LED LIG pulsado 900/100 ms em IO2 (item 14).** Opcoes: (a) pulsado pela ISR do WDI, com errata da L76 descrevendo a piscada; (b) continuo em IO2, fiel a letra da L76 e mentindo no travamento; (c) ECO na placa frontal alimentando o LED LIG direto do trilho 3V3, cumprindo "alimentada" por hardware, e IO2 fica com um segundo indicador de "firmware vivo". RECOMENDACAO: (a) agora, (c) na proxima revisao de placa — (c) e a unica que separa de verdade "alimentada" de "em operacao", mas exige desenho da placa frontal, que nao existe no repo.
5. **Batimento rotativo na tela principal (item 11).** Opcoes: (a) implementar, com acrescimo de uma frase a secao Display (L73); (b) nao implementar, e aceitar que uma tela plausivel e imovel seja indistinguivel de operacao normal. RECOMENDACAO: (a); custa 64 px e cobre o modo de falha mais perigoso de uma IHM de seguranca.
6. **Part number do modulo de display como item controlado (item 9).** Opcoes: (a) item controlado da BOM, com ECO e leitura de etiqueta registrada unidade a unidade; (b) item de BOM comum. RECOMENDACAO: (a) — sem MISO, a etiqueta e o unico dado observavel; uma troca de fornecedor por outro 256x64 com column start diferente passa despercebida em (b).
7. **Manter o build flag `-D UR_DISPLAY_PANEL=SSD1362`.** Opcoes: (a) manter como opcao de compilacao de fabrica; (b) remover e travar o produto em SSD1322. RECOMENDACAO: (a) enquanto a medicao 16 nao fechar o part number em tres lotes; depois disso, (b).

### Precisa de medicao de bancada
- **MEDICAO 10 (REVISADA — CONTRADIZ A BASE COMUM NUM PONTO, DECLARADAMENTE).** Tempo de quadro e de init com o clock TORNADO REAL em 4 MHz (item 3): GPIO livre levantado antes e baixado depois de u8g2_.sendBuffer(), osciloscopio, 100 quadros, registrar maximo e mediana; medir tambem u8g2_.begin() inteiro. A base comum fixa aceitacao de sendBuffer <= 10 ms; esse numero so e alcancavel a 10 MHz (6,55 ms de relogio) e e ARITMETICAMENTE IMPOSSIVEL a 4 MHz, cujo piso teorico e 16,384 ms. NOVA ACEITACAO: sendBuffer <= 25 ms e begin() <= 150 ms. A contradicao e assumida porque a razao de existir do teto de 10 ms na base era o orcamento de latencia de rele, e esse orcamento deixou de depender do display quando a base criou a tarefa ctrl: com a ctrl dona de rele e DAC, um quadro de 16,4 ms custa 6,6 % do loopTask e ZERO no ciclo de seguranca.
- **MEDICAO 11 (EXISTENTE, BLOQUEIA ESTA DECISAO).** Nivel de repouso dos botoes com o painel conectado. Sem ela, o piso de debounce de 50 ms do item 5 nao tem significado, porque nao existe limiar conhecido a defender.
- **MEDICAO 12 (EXISTENTE, AMPLIADA).** Mapeamento e polaridade dos LEDs do painel, mais: confirmar que o LED de IO2 acende com GPIO em nivel ALTO, medir a corrente do LED LIG com o ciclo 900/100 ms em regime, e confirmar que a placa ainda entra em modo download com IO0 baixo e o painel conectado. ACEITACAO: LED ativo em nivel alto, corrente <= 8 mA, modo download preservado.
- **MEDICAO 14 (NOVA — ACOPLAMENTO DO REFRESH NAS LINHAS DE BOTAO).** Osciloscopio de 4 canais na placa REAL com o cabo REAL de painel soldado: canais em IO15, IO34, IO35 e um canal de gatilho no IO18 (SCLK). Disparar rajadas continuas de sendBuffer a 4 Hz por 1 hora (10 000 quadros minimo) com o firmware nao aceitando tecla, e registrar a maior excursao de cada linha de botao a partir do nivel de repouso medido na medicao 11. Repetir em duas condicoes: com drive padrao e com GPIO_DRIVE_CAP_0, e com 4 MHz e com 10 MHz, para quantificar o efeito de cada um dos dois. ACEITACAO: com 4 MHz e drive reduzido, excursao <= 0,8 V e ZERO cruzamentos do limiar logico nas tres linhas em 10 000 quadros; e ZERO eventos de tecla registrados pelo contador de botoes no mesmo periodo. Se reprovar, a correcao e ECO no painel (pull-up <= 4,7K e retorno dedicado para os botoes), nao mais firmware.
- **MEDICAO 15 (NOVA — LEGIBILIDADE E QUEIMA COM CONTRASTE 255).** Legibilidade do campo de valor com contraste 255, a 1,0 m de distancia, 30 graus fora do eixo, sob 100 klx de iluminancia incidente (condicao de sol direto, coerente com a recomendacao de instalacao protegida da radiacao solar do item 6 do manual). ACEITACAO: sinal e os quatro digitos lidos sem erro por tres observadores. Registrar tambem fotografia de referencia da tela principal em t = 0 h e em t = 2000 h de operacao continua com o deslocamento de 1 px ativo, para linha de base de queima. Sem esta medicao, contraste continua fixo em 255 e nao ha autorizacao para reduzi-lo.
- **MEDICAO 16 (NOVA — IDENTIDADE DO MODULO E OFFSET).** Ler e fotografar a etiqueta do modulo de display em TRES unidades de lotes diferentes, registrar o part number no relatorio de fabrica e executar o padrao do item 7 em cada uma. ACEITACAO: part number identico nas tres, moldura fechada nos quatro lados, cinco marcas altas visiveis e texto legivel, sem deslocamento horizontal. Divergencia de part number entre lotes bloqueia a liberacao ate ECO.



---

# Parte 4 — Decisoes novas levantadas pela analise de cobertura

A revisao de completude achou familias inteiras de REQ sem contrato. Estas decisoes nao
estavam nas 12 do briefing e tambem dependem de aprovacao.

# Decisoes novas (D13 em diante)

Antes das decisoes: **tres das sete familias apontadas pela critica de completude JA TEM DONO** e nao recebem decisao nova, para nao duplicar contrato.

- **O que o display mostra antes do primeiro quadro valido (DSP-03/04) — RESOLVIDO, nao criar decisao.** A base comum (ORDEM DE BOOT, passo 16) fixa `AGUARDANDO SENSOR` ate a primeira transacao valida e mata os "5,0 s" e os "3500 ms"; a Decisao 7 item 6 define o estado AGUARDANDO e a Decisao 12 item 12 define o campo de valor `---,-`. O que resta e **contradicao de texto**, listada em `contradicoesResiduais` (D7 x D8 x D12), e se resolve escolhendo uma tabela de strings, nao escrevendo uma decisao nova.
- **LED LIG (manual L76) — RESOLVIDO pela Decisao 12 item 14, nao criar decisao.** D12 identifica que nao existe LED discreto de alimentacao em nenhuma das duas folhas, que o unico net possivel e CN4-1 / IO2, e institui o pulso de 900 ms aceso / 100 ms apagado gerado pela mesma ISR de timer em IRAM que chuta o WDI, travada pelo mesmo token de liveness de 800 ms. Isso e um contrato completo, com desvio declarado e pendencia humana propria (D12, decisao humana 4). Falta apenas o bigboss assinar e a base comum deixar de descrever IO2 como "heartbeat" sem dono no passo 14.
- **Reinicializacao automatica do SCL3300 na sensora — RESOLVIDA pela Decisao 11 item 10, nao criar decisao.** D11 fixa 50 leituras consecutivas reprovadas (500 ms) para disparar `reinit()`, backoff de 1000/2000/5000/5000 ms, contador de reinicializacoes em uint16 exposto no console, `kStsDataValid` limpo e `kStsSclStartup` setado durante os 103 ms de reset+settle, e a MEDICAO 18 que prova a recuperacao com o cabo real. O que falta e ECO de firmware da sensora autorizado (mesma autorizacao que D8 pendencia 7 ja pede), nao decisao nova.

Ficam **quatro** decisoes novas: D13 (senha), D14 (sentido do sensor), D15 (escopo de software do produto / Wi-Fi), D16 (ausencia de readback).

---

## Decisao 13 - Senha, tela de login e gate de acesso ao Modo Programacao

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto
**REQ afetados:** PWD-01, PWD-02, PWD-03, PWD-04, PWD-05, NRM-03, PRG-01, RST-01; MAN-5.1-L82, MAN-5.2-L90, MAN-5.3-L95..L106, MAN-5.10-L224..L237, MAN-5.11-L249; D2 item 2 (campo `password`, off 40 do ParamRecord), D2 item 10 (falha latchada), D3 item 8 (gesto de confirmacao), D3 item 13 (composicao de digitos), src/drivers/buttons.h:13, include/board_pins.h:27..29

### O que o manual diz
- 5.1, L82: "Para acessar o Modo Programação, é necessário informar a senha de acesso: 1234, conforme descrito no item 5.3."
- 5.2, L90 (tecla MENU): "Mantida pressionada por aproximadamente 3 segundos, permite o acesso ao Modo Programação, mediante a digitação da senha: 1234."
- 5.3, L97: "Segure o botão Menu até que a tela de login seja exibida:"
- 5.3, L98 (tela literal): `Senha de acesso:0000`
- 5.3, L99: "A senha 1234 vem configurada de fábrica." ; L100 (tela literal): `Senha de acesso:1234`
- 5.3, L101: "Use os botões ▲ e ▼ para editar o valor do dígito piscando e pressione Menu para que o dígito à esquerda fique editável (piscando);"
- 5.3, L102: "Depois de inserir a senha, segure o botão Menu;"
- 5.3, L103: "Se a senha estiver correta, o Menu de Opções será exibido. Caso contrário, a mensagem \"Senha incorreta!\" aparecerá por alguns segundos antes de desaparecer e permitir uma nova tentativa."
- 5.3, L104 (tela literal): `Senha incorreta!`
- 5.3, L105: "Caso a senha não seja digitada após a solicitação de acesso, a Unidade Remota cancelará automaticamente a operação e retornará ao Modo Normal após aproximadamente 2 minutos de inatividade."
- 5.3, L106: "Observação: A senha padrão de fábrica é 1234. Esse valor também será restaurado após a execução do reset geral do equipamento."
- 5.10, L231 (tela literal): `Edita senha:1234` ; L235: hold de MENU de ~3 s grava ; L236: retorna automaticamente ao Modo Programacao ; L237: "Observação: A nova senha somente passará a ser utilizada nos próximos acessos ao Modo Programação."

### A lacuna
O manual e mais completo aqui do que em qualquer outro ponto: define a tela, a edicao digito a digito, a submissao por hold, a mensagem de erro, o timeout e o default. **PWD-05 esta integralmente resolvido por L237 e nao e lacuna.** O que falta: (a) quantos segundos sao "alguns segundos" de L103; (b) numero de tentativas e existencia de bloqueio — o manual permite tentativas infinitas; (c) qual digito comeca piscando (L101 diz que MENU move para o digito **a esquerda**, o que implica comecar pelo da direita, mas nao o diz); (d) o que acontece ao chegar no digito mais a esquerda; (e) se a senha e exibida em claro (as figuras de L98 e L100 mostram `1234` na tela, o que sugere que sim); (f) o que fazer quando o campo de senha da NVS reprova no CRC — a Decisao 2 restaura os padroes de fabrica em varios caminhos e nenhuma decisao autoriza que isso apague o controle de acesso; (g) se a senha protege alguma coisa alem do Modo Programacao — ela **nao** protege o PSET (5.6, Modo Normal) nem o Reset Geral (5.11, energizacao), que sao as duas operacoes que mais mexem em setpoint de rele; (h) o que os reles fazem durante o login.

### Proposta
1. **GESTO DE ENTRADA (NRM-03).** Hold de MENU de `kHoldEnterMs = 3000 ms` no Modo Normal abre a tela de login. Mesmo numero e mesmo gesto do hold de confirmacao de 5.4 (L110), para nao ensinar dois tempos. Consumo do pressionamento conforme D2 item 14: exige soltura antes de aceitar o proximo evento de MENU, senao o mesmo dedo entra no menu e confirma o primeiro parametro.
2. **TELA LITERAL.** `Senha de acesso:0000` (byte a byte de L98), quatro digitos, sem sinal, **em claro** — fidelidade as figuras de L98 e L100. O digito ativo pisca a 500 ms aceso / 500 ms apagado.
3. **ORDEM DE EDICAO, FIXADA AQUI PARA TODOS OS EDITORES DE DIGITO DO PRODUTO.** O campo abre com o digito **mais a direita** ativo; clique curto em MENU move o cursor **para a esquerda**; do digito mais a esquerda, volta ao mais a direita (rolagem circular **dentro do campo**, ao contrario das listas de menu, que D3 item 7 declara nao circulares). ▲ incrementa e ▼ decrementa o digito ativo, com rolagem 9->0 e 0->9. Isto e a leitura literal de L101 e vale identicamente para `Edita senha` (5.10), para os campos de limite (5.9) e para os campos de trim (5.7), fechando a omissao que D3 item 8 e D9 item 2 deixaram aberta (nenhuma das duas diz a direcao do cursor).
4. **SUBMISSAO.** Hold de MENU de 3000 ms (L102, "segure o botão Menu"). Clique curto **nunca** submete.
5. **ACERTO.** Vai direto ao menu de nivel 1 de D3 (L103). Sem tela intermediaria, sem mensagem de boas-vindas.
6. **ERRO.** `Senha incorreta!` (byte a byte de L104) por `kWrongPwdMs = 2000 ms`, campo volta a `0000` com o cursor no digito da direita, nova tentativa. Os 2000 ms sao o numero que da conteudo aos "alguns segundos" de L103; e o mesmo valor das outras telas de recusa que as decisoes ja usam.
7. **DESVIO DO MANUAL: bloqueio temporario apos 5 tentativas.** `kMaxAttempts = 5` tentativas erradas consecutivas armam um bloqueio de `kLockoutMs = 60000 ms`, com a tela nova `Bloqueado 060s` contando de 060 a 000 em passos de 1 s, sem aceitar tecla. Ao fim, volta ao Modo Normal. O contador de tentativas e **volatil** (RAM), zerado por acerto e por energizacao, e **nao** e persistido: persistir tentativas gastaria escrita de flash para contar erros de digitacao e criaria um estado de bloqueio que sobrevive ao religamento. Nao existe bloqueio permanente: num painel de cais, bloqueio permanente transforma erro de digitacao em visita de manutencao, e o unico caminho de saida seria o Reset Geral, que apaga os quatro setpoints (Tabela 2 leva dois limites a Off). Exige acrescimo de um paragrafo a 5.3.
8. **TIMEOUT.** `kLoginIdleMs = 120000 ms` sem borda de tecla na tela de login devolve ao Modo Normal, sem mensagem — literal de L105. Qualquer borda rearma. Vale tambem durante o bloqueio do item 7 (o bloqueio corre ate o fim independentemente de tecla, e o retorno ao Modo Normal e imediato ao terminar).
9. **RELES E SAIDA ANALOGICA DURANTE O LOGIN: nada muda.** A tarefa ctrl continua polando a 50 ms, filtrando, avaliando os quatro limites e escrevendo os quatro GPIOs e os dois canais do DAC, exatamente como em Modo Normal. O gate de senha e controle de acesso a EDICAO, nunca suspensao de funcao. Isto e a mesma regra ja escrita em D3 item 28, D5 item 8 e D6 item 2, repetida aqui por ser o unico ponto do produto em que um humano fica parado na frente do painel por ate 2 minutos.
10. **PWD-05, SEM INVENCAO.** L237 ja resolve: a nova senha so vale nos proximos acessos. Implementacao literal: a senha vigente e copiada para a RAM da sessao na entrada do Modo Programacao; o commit de `Senha` (5.10) grava a nova e **nao** derruba a sessao corrente nem reexige login. Nao ha desvio e nao ha decisao a tomar.
11. **SENHA INVALIDA NA NVS — corrige a lacuna que a critica de completude apontou.** Nao existe, em nenhum caminho, restauracao silenciosa de `1234`. Se o registro de parametros reprovar no CRC, vale integralmente a falha latchada de D2 item 10 (quatro reles no nivel de alarme, saida analogica em 3932, tela travada em `CONFIG PERDIDA - REPROGRAMAR`), e a **unica** saida e o Reset Geral de 5.11, que restaura `1234` conforme L106 e L249 — um ato fisico deliberado, com acesso ao equipamento e ciclo de energia. A senha so volta ao valor de fabrica junto com todos os outros parametros, nunca sozinha e nunca em silencio.
12. **A SENHA NAO PROTEGE O PSET NEM O RESET GERAL, e isso e declarado.** L161 poe o PSET no Modo Normal e L243-L247 poe o Reset Geral na energizacao; nenhum dos dois passa pelo gate. Consequencia escrita por extenso no manual: **quem alcanca fisicamente o painel move os quatro pontos de atuacao sem digitar senha.** A barreira contra isso nao e a senha, sao as guardas de D1 (armamento, dado valido, estabilidade, confirmacao acima de 5,0 graus, hold de 3000 ms e soltura no Reset). O manual tem de dizer que o painel frontal e area controlada.
13. **SEM BACKDOOR.** No firmware de produto nao existe comando de console que leia, altere ou contorne a senha, e nao existe senha mestra. O comando de fabrica que grava a senha inicial vive no build de fabrica, atras do mesmo flag do item 3 da Decisao 15.
14. **PERSISTENCIA.** Campo `password` (uint16, 0..9999) do ParamRecord de D2 item 2, off 40, sob o mesmo CRC-16/MODBUS e o mesmo banco duplo. A senha e um parametro como os outros: **DESVIO** em relacao a D2 item 5 apenas neste ponto — a senha e o unico parametro cujo commit **nao** espera a efetivacao no SAIR, porque L236 manda retornar ao Modo Programacao imediatamente apos a confirmacao e L237 ja define quando ela passa a valer. Grava-se no hold de 3 s, como todos, e vale no proximo login.

### Por que
A senha e o unico gate do modo que altera setpoint de rele, e hoje ela nao tem uma linha de contrato em nenhuma das 12 decisoes — a Decisao 2 a lista nos REQ afetados e trata apenas do campo de 16 bits. O manual entrega quase tudo pronto; o que falta e exatamente o que um equipamento de seguranca nao pode deixar indefinido: tentativas infinitas num painel de cais, e um caminho de codigo que reponha `1234` sozinho depois de um CRC ruim. O item 11 fecha esse caminho ligando a senha ao mesmo estado latchado dos outros parametros, e o item 12 para de fingir que a senha protege o que ela nao protege.

### O que a revisao adversarial derrubou
- **Contra a Decisao 2:** a critica de completude estava certa ao dizer que "a decisao 2 restaura 1234 automaticamente, apagando o controle de acesso do site, sem que nenhuma decisao autorize isso". Verificado: D2 item 12 so carrega a Tabela 2 no Reset Geral e D2 item 10 tranca o equipamento quando nao ha registro valido, entao **o defeito nao esta em D2 como ela ficou** — esta na ausencia de qualquer decisao que dissesse isso por escrito para a senha. O item 11 diz.
- **Refutado: bloqueio permanente com desbloqueio por Reset Geral.** Seria o unico gate realmente forte, e e inaceitavel: o Reset Geral leva `Operacao Limite 2` e `Operacao Limite 4` a Off (Tabela 2, L258-L263), ou seja, punir tres erros de digitacao com a desativacao de dois dos quatro reles de seguranca. Bloqueio temporario de 60 s custa 60 s.
- **Refutado: esconder a senha digitada com asteriscos.** As duas figuras do manual (L98 e L100) mostram o campo em claro, e o operador edita digito a digito olhando qual pisca; mascarar quebraria as duas figuras e a propria mecanica de L101 sem ganho real num painel que exige presenca fisica.
- **Cedido a D3:** a direcao do cursor (item 3) e propriedade global do produto e nao podia ficar so aqui; D3 item 8 e D9 item 2 tem de referenciar este item em vez de reabrir a questao.

### Precisa de decisao humana
1. **Bloqueio apos 5 tentativas (item 7).** (a) 5 tentativas + 60 s de bloqueio, com tela nova `Bloqueado 060s` e paragrafo novo em 5.3; (b) tentativas infinitas, fidelidade literal a L103. **RECOMENDACAO: (a).**
2. **Duracao de `Senha incorreta!` (item 6).** 2000 ms proposto, contra os "alguns segundos" de L103. **RECOMENDACAO: 2000 ms, e escrever o numero na errata.**
3. **Direcao do cursor nos editores de digito (item 3):** da direita para a esquerda, com rolagem circular dentro do campo, valendo para senha, limites, preset, fundo de escala e trim. **RECOMENDACAO: aprovar, e propagar para D3 item 8, D6 item 9, D9 item 2 e D10 item 7, que hoje nao dizem a direcao.**
4. **Senha exibida em claro (item 2).** **RECOMENDACAO: em claro, por fidelidade a L98/L100.**
5. **Declarar no manual que PSET e Reset Geral nao passam pela senha (item 12), e que o painel frontal e area controlada.** **RECOMENDACAO: aprovar; e a unica forma honesta de descrever o produto.**
6. **Texto da tela nova `Bloqueado 060s`** (sem acento, no padrao de `RESET DE FABRICA`). **RECOMENDACAO: aprovar como escrito.**

### Precisa de medicao de bancada
- **MEDICAO 11 da base comum (nivel de repouso dos botoes), BLOQUEANTE.** IO34 e IO35 sao input-only sem pull interno e nao ha pull-up na placa mae; sem nivel de repouso medido com o painel conectado, os 3000 ms de entrada, os 3000 ms de submissao, os 120000 ms de timeout e as 5 tentativas nao tem significado.
- **MEDICAO 3 da Decisao 2 (granularidade real do hold de 3 s), reaproveitada.** Acrescentar 50 gestos de submissao de senha, com o display redesenhando. ACEITACAO: submissao entre 3000 e 3250 ms em 50 de 50.
- **MEDICAO 14 da Decisao 12 (acoplamento do refresh nas linhas de botao), BLOQUEANTE em conjunto com a MEDICAO 11.** Se um pulso de refresh puder ser lido como toque, uma senha errada pode ser composta sozinha e o bloqueio do item 7 dispara sem operador.
- **MEDICAO 23 (NOVA) — TENTATIVAS E BLOQUEIO.** Injetar por gerador de pulsos, no pino de MENU, 1000 ciclos de "senha errada + hold de submissao" e confirmar: contador incrementa exatamente uma vez por submissao, bloqueio arma na 5a, dura 60000 +/- 500 ms, o contador zera no primeiro acerto apos o bloqueio e ZERO escritas na NVS ocorrem em todo o ensaio (verificado pelo contador de apagamentos de setor da MEDICAO 5 da Decisao 2). ACEITACAO: 1000/1000 e zero escritas.

---

## Decisao 14 - Sentido do Sensor X e Y: ordem canonica, efeito sobre o Preset e aviso obrigatorio

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto
**REQ afetados:** DIR-01, DIR-02, PST-04, LIM-01..LIM-08 (deslocam junto), MEA-01, NRM-02; MAN-2.1-L46, MAN-5.5-L139, MAN-5.8-L188..L199, MAN-6.1-L274, MAN-6.3-L303..L304 (Figura 4, etiqueta com o sentido), MAN-Tab1 (Sentido Sensor X e Y), MAN-Tab2-L264..L265 (default Horario); D1 item 13 e item 20, D2 item 2 (campo `sensorDir[2]`, off 46), D3 item 3 e pendencia 10, D4 item 7, D5 item 3 e pendencia 9, D11 item 8 e pendencia 5

### O que o manual diz
- 5.8, L189: "Este parâmetro define o sentido em que a leitura angular é incrementada, permitindo adequar a indicação ao sentido de movimento do equipamento monitorado. A configuração é independente para cada eixo."
- 5.8, L191: "Horário: o valor do ângulo aumenta quando o sensor gira no sentido horário e diminui no sentido anti-horário;"
- 5.8, L192: "Anti-horário: o valor do ângulo aumenta quando o sensor gira no sentido anti-horário e diminui no sentido horário."
- 5.8, L194..L198: procedimento — selecionar `Sentido Sensor X` ou `Sentido Sensor Y` com ▲/▼, clique em MENU para habilitar, ▲/▼ para escolher a opcao, hold de MENU de ~3 s para gravar.
- 5.8, L199: "Observação: A alteração do sentido do sensor inverte o sinal da leitura. Após alterar este parâmetro, recomenda-se refazer o Preset e conferir os valores programados nos Limites 1 a 4."
- 5.9, L223: os limites "referem-se à leitura apresentada no display, ou seja, já consideram o Preset e o Sentido do Sensor configurados para o respectivo eixo."
- 6.1, L274 e a Figura 4 (L303-L304): a orientacao e dada pela seta de referencia no corpo do sensor.
- Tabela 2, L264-L265: default de fabrica `Horário` nos dois eixos.

### A lacuna
O manual define as duas opcoes e avisa que o sinal inverte, mas nunca diz: (a) a ORDEM de aplicacao em relacao ao Preset e ao grampo — e as decisoes existentes se contradizem no sinal (D1 soma o offset, D11 subtrai o preset); (b) o que acontece com o offset de Preset **ja gravado** quando o sentido inverte — ele foi calculado contra o `dir` antigo, e mante-lo desloca os quatro pontos de atuacao em ate 180,0 graus sem nenhum indicio na tela; (c) se o firmware faz alguma coisa alem de "recomendar" (L199 fala com o operador, nao com o firmware); (d) se o Sentido afeta tambem a saida analogica, o eixo Z e a temperatura; (e) o que significa fisicamente "Horario" — o manual amarra a seta da etiqueta, e ninguem verificou contra o sinal que o SCL3300 entrega.

### Proposta
1. **ORDEM CANONICA UNICA, ESCRITA NO CABECALHO DE `src/app/measurement_chain.h`,** ratificando D4 item 7, D5 item 3 e D11 item 8:
   `bruto (ja media de N na sensora) -> mediana/EMA -> SENTIDO -> PRESET -> grampo +/-900 -> unico int16 publicado no tick`.
   O filtro fica **a montante** do Sentido e do Preset porque os dois sao lineares (troca de sinal e soma de constante) e comutam com o EMA: assim o commit de qualquer um dos dois produz um degrau exato, com transitorio de filtro igual a zero e nenhum rele atuado por artefato.
2. **FORMULA UNICA, EM INTEIRO, QUE DESEMPATA D1 x D11:**
   `leitura = clamp(dir * bruto + offset, -900, +900)`, com `dir` em {+1 = Horario, -1 = Anti-horario} e `offset = presetOffsetDeci[eixo]` (int16, decimos de grau).
   No aceite do PSET: `offset := P - dir * bruto` (D1 item 13).
   **A formula de D11 item 8 (`sentido * cru - preset`) esta ERRADA como escrita** e tem de ser corrigida: o que entra na soma e o OFFSET produzido pelo gesto de PSET, nao o valor programado `P`, e ele entra SOMANDO. Com `P = 0`, as duas coincidem por acidente; com `P` diferente de zero, D11 desloca a leitura em `2*P`.
3. **UM UNICO `dir` POR EIXO, APLICADO UMA UNICA VEZ.** Nenhum outro ponto do firmware pode inverter sinal: nem o escalador analogico, nem o comparador de limites, nem o display. O comparador recebe a `leitura` do item 2 e mais nada (fronteira ja declarada em D4 item 15 e D5 item 15).
4. **ESCOPO.** `dir` afeta os registradores 0 (ANG_X) e 1 (ANG_Y), cada um pelo seu proprio parametro. **NAO** afeta o registrador 2 (ANG_Z, diagnostico), o registrador 4 (TEMP), nem a leitura crua usada pelas guardas de faixa de D11 item 7 — a banda de tolerancia e a falha mecanica sao avaliadas sobre `|bruto|`, antes de `dir` e antes de `offset`, porque sao propriedades da MONTAGEM, nao da convencao de sinal.
5. **A SAIDA ANALOGICA SEGUE A LEITURA, NAO O BRUTO.** O escalador de D6/D9/D10 recebe a mesma `leitura` do item 2, ja com `dir` e `offset`. Isto e a leitura literal de L223 estendida a saida analogica pelo mesmo argumento (um unico numero comanda display, rele e analogica) e ja e o que D10 item 1 diz.
6. **DESVIO DO MANUAL (L199 recomenda; aqui o firmware age): ao gravar uma mudanca de `Sentido Sensor X` ou `Sentido Sensor Y`, o offset de Preset daquele eixo e ZERADO.** Justificativa: o offset foi definido contra o `dir` anterior; mante-lo desloca os dois pontos de atuacao daquele eixo em `2*offset`, ate 180,0 graus, sem nenhum indicio visivel. Zerar e deterministico e VISIVEL, porque o marcador permanente `PSET X:-012,0` de D1 item 17 desaparece da tela principal no mesmo instante. Ratifica D1 item 20, que ja pedia isso e nao tinha dono.
7. **DESVIO DO MANUAL: aviso obrigatorio na confirmacao.** Ao completar o hold de 3000 ms sobre `Sentido Sensor X`, o display exibe por `kDirWarnMs = 3000 ms`, e so entao volta ao submenu:
   - linha 1: `Sentido X alterado!`
   - linha 2: `Preset zerado - confira X1 X2`
   (analogas para Y, com `Y1 Y2`). Telas novas, sem acentuacao, no padrao de `Alteracao bem sucedida!` (L183). Exige paragrafo novo em 5.8. **Resolve a pendencia 10 da Decisao 3, na opcao (b) que ela recomendava.**
8. **OS LIMITES NAO SAO ALTERADOS NEM FORCADOS A `Off`.** Rejeitada a opcao (c) da pendencia 10 de D3: forcar `Off` nos dois limites do eixo desativa dois dos quatro reles de seguranca por causa de uma mudanca de convencao de sinal, e a reativacao dependeria de o operador lembrar — e o mesmo modo de falha que a Tabela 2 do Reset Geral ja cria e que D1 item 27 usa como justificativa da guarda de tecla presa. O limite programado continua valendo **sobre a leitura**, conforme L223 e L219; o que muda e a leitura, e o item 7 avisa.
9. **EFETIVACAO ATOMICA.** `dir` novo, `offset` zerado, recalculo das duas leituras, reavaliacao dos quatro limites e escrita dos quatro GPIOs acontecem **dentro do mesmo tick de 50 ms** da tarefa ctrl, sob portMUX, pelo mesmo mecanismo de fila de D1 item 18 e D3 item 19. Nao existe tick com `dir` novo e `offset` velho. Os contadores de confirmacao de ataque e liberacao sao ZERADOS no tick do commit (D5 item 8), e a temporizacao de liberacao nao se aplica a transicao causada por commit — acao deterministica do operador, nao medida ruidosa.
10. **O FILTRO NAO E REINICIALIZADO.** Ratifica D4 item 9: trocar `dir` nao invalida o estado do EMA nem a janela da mediana. Como `dir` e aplicado a JUSANTE do filtro, o valor filtrado apenas troca de sinal no mesmo tick, exatamente como o valor cru. Nenhum pulso de rele por artefato de filtro.
11. **PERSISTENCIA.** `sensorDir[2]` (uint8, 0 = Horario, 1 = Anti-horario) no ParamRecord de D2 item 2, off 46. Default de fabrica `Horario` nos dois eixos (Tabela 2, L264-L265). O Reset Geral repoe `Horario` **e zera os dois offsets**, pela mesma regra do item 6.
12. **DEFINICAO FISICA DE "HORARIO", A_MEDIR.** `Horario` = `dir = +1` = o sinal do registrador ANG do SCL3300 **sem inversao**, com o sensor montado na orientacao da seta de referencia da Figura 4 (L303-L304) e do item 6.1 (L274). Esta amarracao **nao pode ser afirmada por leitura de esquematico**: depende da orientacao fisica com que o SCL3300 foi montado na placa sensora e de como a seta foi serigrafada na caixa. Enquanto a MEDICAO 24 nao fechar, `dir = +1` e uma hipotese, e o manual nao pode ser impresso.

### Por que
Cinco decisoes (1, 3, 4, 5 e 11) tocam neste parametro e todas as cinco declaram, por escrito, que ele nao tem dono. O resultado e que a unica formula do produto que combina sentido, preset e grampo aparece com **sinais diferentes** em D1 e D11, e que o efeito mais perigoso do parametro — inverter o sinal sem tocar num offset de PSET gravado — esta apenas "recomendado" ao operador pela L199. Num supervisor portuario isso significa: o tecnico inverte o sentido para acertar a leitura, os quatro pontos de atuacao deslocam ate 180,0 graus, o display mostra numeros plausiveis e nada avisa. O item 6 troca esse modo de falha silencioso por um efeito visivel (o marcador de PSET some) e o item 7 o anuncia. O item 8 recusa a "correcao" que desligaria dois reles.

### O que a revisao adversarial derrubou
- **Contra D11:** a formula do item 8 de D11 (`sentido*cru - preset`) esta incorreta e foi substituida pelo item 2. D11 continua certa no resto (grampo em +/-900 como ultima operacao, prova de que o grampo nao suprime atuacao).
- **Contra D3 pendencia 10:** as tres opcoes eram (a) nada, (b) aviso, (c) forcar `Off`. Adotada (b) e refutada (c) pelo item 8, com o argumento de que desativar rele de seguranca para punir mudanca de convencao e criar um perigo maior que o coberto.
- **Contra D5 pendencia 9:** D5 fixa `bruto -> sentido -> preset` e manda ratificar "na decisao dona do Preset". A decisao dona e esta, e ela ratifica — mas corrige o alcance: a ordem tambem governa a saida analogica (item 5), o que D5 nao dizia.
- **Refutado: manter o offset e apenas avisar** (a leitura literal de L199, que so "recomenda"). Um aviso de 3000 ms desaparece; um deslocamento de 180,0 graus nos quatro pontos de atuacao permanece ate alguem notar. Zerar e a unica acao cujo efeito o operador ve na tela principal em permanencia.
- **Refutado: aplicar `dir` na sensora, no proprio quadro Modbus.** Quebraria o contrato de fio (o registrador passaria a depender de um parametro da UR), tiraria o eixo Z e a faixa da mesma referencia, e impediria o diagnostico bruto que a MEDICAO 24 exige.

### Precisa de decisao humana
1. **Zerar o offset de Preset ao trocar o Sentido (item 6).** (a) zerar, com o marcador de PSET sumindo da tela; (b) manter e apenas avisar, fidelidade literal a L199. **RECOMENDACAO: (a).** Tem de ser assinada uma unica vez e repetida identica em D1 item 20.
2. **Aviso obrigatorio e textos das duas telas novas (item 7):** `Sentido X alterado!` / `Preset zerado - confira X1 X2`. **RECOMENDACAO: aprovar como escrito, sem acentuacao, e acrescentar o paragrafo a 5.8.**
3. **Nao forcar `Off` nos limites do eixo (item 8).** **RECOMENDACAO: aprovar a recusa.**
4. **Amarracao fisica de `Horario` (item 12).** Depende da MEDICAO 24. **RECOMENDACAO: nao imprimir manual nem etiqueta antes do ensaio.**
5. **Ratificar a formula do item 2 como a UNICA do produto** e corrigir D11 item 8 no documento. **RECOMENDACAO: aprovar; sem isso o implementador escolhe entre duas formulas com sinais opostos.**

### Precisa de medicao de bancada
- **MEDICAO 24 (NOVA) — AMARRACAO DO SENTIDO FISICO A ETIQUETA (BLOQUEIA O MANUAL E A ETIQUETA).** Sensora montada em mesa divisora, com a seta de referencia da Figura 4 na orientacao especificada. Girar o eixo X em +5,0 graus no sentido HORARIO visto pelo observador posicionado conforme a figura, e registrar o sinal do registrador 0 com `Sentido Sensor X = Horario`. Repetir para o eixo Y e para os dois sentidos, e repetir em TRES sensoras de lotes diferentes. ACEITACAO: com `Horario`, giro horario produz leitura CRESCENTE nos dois eixos, em 3 de 3 unidades, conforme L191. Se reprovar, `dir = +1` passa a ser `Anti-horario` no firmware **ou** a serigrafia da seta muda — e essa escolha e ECO, nao firmware.
- **MEDICAO 25 (NOVA) — TRANSICAO DE COMMIT DE SENTIDO, SEM PULSO DE RELE.** Com um limite programado em `+` a 5,0 graus e a estrutura estatica a 3,0 graus (abaixo do limite nos dois sentidos), gravar 100 vezes a troca de Sentido do eixo, com osciloscopio no GPIO do rele correspondente e no GPIO do outro rele do mesmo eixo. ACEITACAO: ZERO comutacoes de rele em 100 commits, e o valor exibido troca de sinal em <= 50 ms (um tick da ctrl), sem valores intermediarios. Reprovacao aqui invalida os itens 9 e 10.
- **MEDICAO 11 da base comum (nivel de repouso dos botoes), herdada e bloqueante** para o hold de 3000 ms do commit.
- **MEDICAO 4 da base comum (lacuna de WDI na escrita de NVS), herdada:** o commit de Sentido grava o ParamRecord, e vale o mesmo orcamento de commit que a reconciliacao D2 x D3 fixar.

---

## Decisao 15 - Escopo de software do firmware de aplicacao: Wi-Fi, portal web, simulador e comandos de atuacao do console fora do produto

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto
**REQ afetados:** nenhum REQ de manual (e esse e o ponto); MAN-2.1-L27..L46 (a lista de caracteristicas do produto **nao** menciona nenhuma interface sem fio), MAN-8 (Conexoes Eletricas: nao existe antena, conector de rede ou porta de servico), base comum secao 7 (`#define UR_WIFI_ENABLED 0`, A_APROVAR), base comum secao 2 (tarefa ctrl no core 0, prio 5); codigo: `src/net/wifi_portal.cpp`, `src/net/wifi_portal.h`, `src/net/web_page.h`, `src/sim/sim_commands.cpp`, `src/cmds/cmd_relay.cpp:67`, `src/cmds/cmd_analog.cpp:85` e `:312`, `src/cmds/cmd_system.cpp:226` e `:248`, `src/tests/*`, `sensor/src/core/console.cpp:300` (comando `proto`)

### O que o manual diz
Nada. Uma varredura do manual inteiro (`grep -n "Wi-Fi\|WiFi\|wireless"` em `docs/manual-cliente-sui-2026.txt`) devolve **zero ocorrencias**. A lista de caracteristicas de 2.1 (L27-L46) enumera display, teclado, RS-485 com o sensor, duas saidas analogicas isoladas, quatro saidas a rele e retencao em memoria nao volatil. A Tabela 3 e a Tabela 4 (item 8) descrevem CN1 e CN2 terminal a terminal e nao ha um unico terminal de rede. **O manual nao promete e nao admite nenhuma interface sem fio.**

### A lacuna
O repositorio tem `src/net/wifi_portal.cpp`, `src/net/web_page.h` e um simulador em `src/sim/`, herdados do firmware de teste de fabrica, e a base comum deixou a questao como `#define UR_WIFI_ENABLED 0 // A_APROVAR` — ou seja, ninguem decidiu. Nenhuma das 12 decisoes e dona disto. Junto vem uma lacuna maior e nao vista: o console de produto herda comandos que **escrevem em rele, no DAC e no modo do XTR300** (`relay all on`, `ao raw`, `ao mode`, `test 8`, `cal erase`), e nao ha nenhuma regra que os separe do build de produto. A Decisao 8 item 17 ja identificou o mesmo buraco do lado da sensora (`proto [jig|modbus]` derruba o link de seguranca por comando de console) e o fechou la; do lado da UR ele continua aberto.

### Proposta
1. **`UR_WIFI_ENABLED = 0` no build de produto, e a exclusao e de LINKAGEM, nao de flag.** `src/net/` sai do `build_src_filter` do ambiente de producao no `platformio.ini`; nenhuma chamada a `WiFi.*` existe no binario; o ambiente de fabrica (`env:factory`) continua compilando tudo. Verificacao objetiva de release: `nm` sobre o `.elf` de producao nao pode conter nenhum simbolo de `esp_wifi` nem de `WiFiClass`.
2. **Motivo tecnico, nao estetico.** A tarefa ctrl vive no core 0 com prioridade 5 (base comum); as tarefas do stack Wi-Fi do ESP-IDF rodam em prioridades 22 e 23 no mesmo core e **preemptam a ctrl incondicionalmente**. Todo o orcamento de 50 ms da base — round-trip de 21,3 ms, filtro, avaliacao dos quatro limites, escrita de rele e DAC — foi calculado num core cujo unico ocupante de alta prioridade e o `esp_timer`. Com Wi-Fi ligado, o pior caso do ciclo de seguranca deixa de ser uma propriedade verificavel. Some-se: dezenas de KB de RAM, e um portal web num equipamento cujo unico gate de acesso e uma senha de 4 digitos publicada no manual (L82, L106).
3. **CONSOLE SERIAL PERMANECE, EM DOIS NIVEIS.** O console de 115200 do passo 6 da ordem de boot e o unico diagnostico quando tudo o mais falha e **nao sai do produto**. Mas os comandos passam a ser classificados, e a classificacao e de compilacao:
   - **Nivel LEITURA, sempre disponivel:** `status`, `rs485 stats`, `rs485 sniff`, `diag`, `btn`, `wdt status`, e os contadores das decisoes 5, 7, 8 e 11.
   - **Nivel ATUACAO, ausente do binario de producao:** `relay <n> <on|off>`, `relay all on`, `ao raw`, `ao mode`, `test <n>`, `cal erase`, `wifi on`, `sim *`, e qualquer comando que escreva GPIO de rele, codigo de DAC ou pino OP_MODE. Vivem no `env:factory`.
   Motivo: `relay all on` num console acessivel no campo energiza os quatro contatos de intertravamento sem passar por senha, por limite ou pela tarefa ctrl.
4. **CONSEQUENCIA PARA O ENSAIO FUNCIONAL PERIODICO (Decisao 16).** Como os comandos de atuacao somem do produto, o ensaio de prova dos quatro limites **nao pode** depender de `relay <n> <on|off>`: passa a ser feito pelo Modo Programacao, programando o limite abaixo da leitura corrente e restaurando, como D5 item 14 ja descreve. Isto e um requisito que D5 e D7 assumiram sem verificar.
5. **O `env:factory` E RASTREAVEL.** O binario de fabrica imprime, no banner do console e no comando `status`, a string `BUILD=FACTORY` e o `FW_VERSION` com sufixo `-f`. Uma UR que saia de linha com binario de fabrica e detectavel por inspecao, e o relatorio de fabrica registra qual binario foi gravado por numero de serie.
6. **REFLEXO NA SENSORA.** Identico ao que D8 item 17 ja exige: build de producao sobe em Modbus RTU, o quadro do jig fica atras de flag do ambiente de fabrica e o comando `proto` some do produto. Esta decisao apenas declara que a regra e a mesma nas duas placas e que o criterio de release e o mesmo (`nm` sobre o `.elf`).
7. **MEDICAO 9 DA BASE MUDA DE STATUS.** A condicao (iv) daquela medicao ("tudo o acima com Wi-Fi ligado") deixa de ser condicao de release do produto e passa a ser caracterizacao do `env:factory`. A aceitacao de release passa a ser a condicao (iv) **sem** Wi-Fi, como a propria base ja escreve.
8. **SE O CLIENTE PEDIR ACESSO REMOTO,** isso e requisito novo de produto, com secao nova de manual, superficie de ataque declarada, autenticacao que nao seja uma senha de 4 digitos publicada, e uma decisao propria. Nao se resolve reativando um portal herdado do firmware de teste.

### Por que
Este e o unico item do conjunto em que o manual e o esquematico concordam em silencio absoluto: nao ha antena, nao ha terminal, nao ha uma linha de texto. Um radio que ninguem pediu, rodando em prioridade 23 no mesmo core que decide se quatro reles de seguranca atuam, e a definicao de risco nao justificado — e, ao contrario dos outros riscos deste projeto, custa exatamente zero para remover, porque nada no produto depende dele. A parte que realmente muda o produto e o item 3: hoje um cabo USB e um `relay all on` fecham os quatro contatos de intertravamento de um portico, sem senha e sem registro.

### O que a revisao adversarial derrubou
- **Refutado: "manter o Wi-Fi desligado por flag de runtime, para o cliente poder habilitar em campo".** Um flag de runtime deixa o codigo no binario, o stack inicializavel e a superficie de ataque presente; e a garantia do item 2 (nenhuma tarefa de prioridade 22/23 no core 0) deixa de ser demonstravel por inspecao do `.elf`.
- **Refutado: "o console tambem deveria sair do produto".** E o unico caminho de diagnostico quando o display esta morto e o enlace caiu, e a base comum ja o poe no passo 6 do boot por esse motivo. O que sai sao os comandos de ATUACAO, nao a porta.
- **Cedido a Decisao 8:** a regra do `proto` na sensora ja estava certa e apenas nao tinha generalizacao; o item 6 generaliza.
- **Cedido a Decisao 5 e a Decisao 7:** as duas descrevem o ensaio de prova usando `relay <n> <on|off>`, comando que esta decisao remove do produto. O item 4 corrige as duas.

### Precisa de decisao humana
1. **Wi-Fi e portal web fora do binario de producao (itens 1 e 2).** (a) fora, por `build_src_filter`, com verificacao por `nm` no release; (b) presente e desligado por flag de runtime; (c) presente e disponivel. **RECOMENDACAO: (a).**
2. **Comandos de atuacao do console fora do binario de producao (item 3).** (a) fora, vivendo so no `env:factory`; (b) presentes, atras de um comando de destravamento com a senha do equipamento; (c) presentes como hoje. **RECOMENDACAO: (a);** (b) e aceitavel se a assistencia tecnica precisar atuar em campo, e nesse caso a senha tem de ser a do item 14 da Decisao 13 e cada uso tem de ir para o console com carimbo de tempo.
3. **Marcacao `BUILD=FACTORY` e sufixo de versao (item 5).** **RECOMENDACAO: aprovar; sem isso nao ha como provar que uma UR de campo nao esta com binario de fabrica.**
4. **Se ha requisito de acesso remoto (item 8).** **RECOMENDACAO: declarar formalmente que NAO ha; se houver, abrir decisao propria antes de qualquer codigo.**

### Precisa de medicao de bancada
- **MEDICAO 9 da base comum (consumo total), com a mudanca de status do item 7.** ACEITACAO de release: <= 4,0 W na condicao (iv) **sem** Wi-Fi. A condicao com Wi-Fi permanece no roteiro apenas como caracterizacao.
- **MEDICAO 26 (NOVA) — JITTER DA TAREFA ctrl COM E SEM Wi-Fi (quantifica o custo, caso o bigboss queira manter).** GPIO livre levantado na entrada e baixado na saida do tick de 50 ms da tarefa ctrl; osciloscopio em modo de largura de intervalo; 100.000 ticks em cada condicao: (i) `env:production`, (ii) `env:factory` com `wifi on` e um cliente conectado ao portal fazendo requisicoes continuas. Registrar o maximo e o percentil 99,99 do periodo entre ticks e o numero de ticks perdidos. ACEITACAO para o produto: periodo entre ticks <= 55 ms em 100.000 de 100.000 e ZERO transacoes Modbus perdidas por atraso. Espera-se que a condicao (ii) REPROVE — e essa reprovacao medida que fecha o item 2 por numero em vez de por argumento.
- **MEDICAO 27 (NOVA) — INVENTARIO DO BINARIO DE PRODUCAO.** Sobre o `.elf` de release: `nm` sem simbolos de `esp_wifi`/`WiFiClass`, sem os manipuladores dos comandos de atuacao do item 3, e `idf.py size` registrando RAM estatica livre. ACEITACAO: zero simbolos proibidos e >= 80 KB de heap livre apos o `setup()`, medidos por `esp_get_free_heap_size()` impresso no `status`.

---

## Decisao 16 - Ausencia de readback no caminho de atuacao: declaracao, ensaio funcional periodico unico e ECOs de diagnostico

**Status:** PENDENTE DE APROVACAO
**Impacto de seguranca:** alto
**REQ afetados:** LIM-01..LIM-08 (o caminho de atuacao dos quatro limites), CAL-01..CAL-08 (o caminho da saida analogica), COM-01, DSP-01/02; MAN-5.9-L202 (contato NA/NF e LED de sinalizacao), MAN-6.3-L292..L294 (inspecao **antes de energizar** — nao ha inspecao periodica no manual), MAN-7-L305..L309, MAN-8-Tabela-4-L326..L345; codigo: `src/drivers/relays.cpp` (`RelayBank::get()` devolve cache de escrita), `src/drivers/xtr300.h:132` (EFOT/EFLD/EFCM so acendem LD1..LD6), `include/board_pins.h` (`kDacMiso = 36`, pino fantasma; o DAC8562 nao tem SDO), `src/drivers/display_u8g2.*` (CN4 sem MISO), `include/board_pins.h:59..64` (serigrafia cruzada do CN3)

### O que o manual diz
- 5.9, L202: "Cada limite dispõe de uma saída a relé independente, com contato NA/NF para 5 A / 250 Vca máx., e de um LED de sinalização no painel frontal da Unidade Remota."
- 6.3, L292-L294: "Inspeção da Instalação — **Antes de energizar** o Supervisor de Inclinação, recomenda-se verificar cuidadosamente todos os itens relacionados à instalação mecânica, elétrica e hidráulica do sistema." A lista de itens que segue e toda de instalacao (fixacao, conexoes, refrigeracao). **Nao existe, em nenhum ponto do manual, ensaio funcional periodico do caminho de atuacao.**
- 7, L306-L307: a unica falha que o manual manda o equipamento anunciar e a de comunicacao com o sensor.
- Tabela 4, L345: "A configuração dos contatos NA/NF de cada limite é feita por jumper na placa da Unidade Remota, sendo o padrão de fábrica NF (normalmente fechado)."

### A lacuna
O manual descreve o rele como se a sua atuacao fosse certa. Nao existe, em ponto nenhum do caminho de atuacao, um unico sinal de volta:
- **Rele:** `RelayBank::get()` devolve o cache da ultima escrita, nao o estado do contato. Contato colado, bobina aberta, BC337 em curto ou aberto, trilho de +5 V caido e conector CN2 solto sao **todos indetectaveis pelo firmware**, e o firmware nem pode saber que nao sabe.
- **LED do painel:** nao e canal independente — o LED pendura no mesmo net do GPIO, antes do resistor de base (Decisao 6 item 4, `include/board_pins.h:59-64`), e a serigrafia esta CRUZADA (LIM1 acende o LED rotulado "LED LIM3"). Um LED aceso prova que o GPIO subiu, nao que a bobina puxou nem que o contato mudou.
- **Saida analogica:** o DAC8562 nao tem SDO e `kDacMiso = 36` e pino fantasma; as tres flags de erro do XTR300 (EFOT termico, EFLD carga aberta, EFCM modo corrente) so acendem LEDs locais dentro da caixa e nao chegam a GPIO nenhum. Cabo analogico rompido ou em curto e shutdown termico do XTR300 sao indetectaveis.
- **Display:** sem MISO, `U8g2Display::begin()` nao prova que o painel respondeu (Decisao 12 item 13).
Quatro decisoes tocaram nisso e nenhuma e dona: D5 item 14 propoe ensaio de 6 meses, D7 pendencia 5 propoe 12 meses, D6 item 4 retira o LED da lista de canais confiaveis, D10 item 15 declara a indetectabilidade. Resultado: dois intervalos, nenhuma redacao de manual e nenhuma ECO priorizada.

### Proposta
1. **DECLARACAO NO MANUAL, EM SECAO PROPRIA, COM A LISTA FECHADA DO QUE A UR NAO DETECTA.** Nova secao (proposta: 6.4, "Limitacoes de Diagnostico e Ensaio Funcional"), contendo por extenso: contato de rele colado ou aberto; bobina aberta; transistor de acionamento em curto ou aberto; conector CN2 solto; cabo da saida analogica aberto ou em curto; XTR300 em protecao termica; display congelado exibindo imagem valida; **e a queda de alimentacao da propria UR**, cuja sinalizacao depende inteiramente da decisao de polaridade do rele (base comum, secao POLARIDADE). Consequencia contratual escrita no manual: **o intertravamento do cliente tem de usar os dois canais (contato de rele E saida analogica com deteccao de fora de faixa configurada) e, se a polaridade aprovada for a do manual (Opcao A), tem de monitorar a alimentacao da UR por canal externo.**
2. **ENSAIO FUNCIONAL PERIODICO, INTERVALO UNICO DE 6 MESES.** Resolve a contradicao D5 (6 meses) x D7 (12 meses) escolhendo 6 meses, pelo motivo simples de que nao ha nenhum diagnostico entre um ensaio e o outro: o intervalo **e** a janela de falha latente. Procedimento, com o intertravamento daquele equipamento em manutencao e registrado no relatorio:
   a. anotar a leitura corrente dos dois eixos;
   b. para cada um dos quatro limites: programar o valor 1,0 grau abaixo (para `>=` e `+`) ou acima (para `<=`) da leitura corrente, com a operacao ja programada; confirmar por hold de 3 s; sair do submenu; **verificar com multimetro no borne CN2** que o contato mudou de estado e que o LED do painel acompanhou; restaurar o valor original e confirmar;
   c. para cada eixo: verificar com voltimetro nos bornes CN1L/CN1M e CN1N/CN1O que a saida analogica corresponde a leitura exibida, com erro <= 10,0 mV (o mesmo portao de D10 item 9);
   d. registrar os quatro contatos, os quatro LEDs, as duas tensoes e o contador de comutacoes de cada rele.
   **O procedimento NAO usa `relay <n> <on|off>`:** esse comando sai do binario de producao pela Decisao 15 item 3. As redacoes de D5 item 14 e D7 pendencia 5 tem de ser corrigidas nesse ponto.
3. **UM MODO DE ENSAIO GUIADO NA IHM, OPCIONAL, QUE NAO ATUA SOZINHO.** DESVIO DO MANUAL: item novo `Ensaio` no submenu de diagnostico, que **apenas conduz** o operador pelos passos do item 2 e registra o resultado que ele confirma tecla a tecla. Nao existe modo que force os quatro reles: forcar contato sem que a estrutura tenha mudado e criar, num cais, exatamente o evento perigoso que o rele existe para sinalizar.
4. **CONTADOR DE VIDA DO CONTATO, COM A POLITICA DE ESCRITA RECONCILIADA.** Contagem total de atuacoes por rele em uint32, mantida em RAM pela tarefa ctrl. **Persistida a cada 1000 comutacoes, nao a cada 100** como D5 item 7 propoe: com o teto de comutacao adaptativo da propria D5 (~1400 comutacoes/dia em balanco pendular), gravar a cada 100 daria ~56 escritas/dia nos quatro reles e furaria em ~5x o orcamento de vida util da flash de D2 item 17 (que conta 12 gravacoes/dia). A cada 1000, sao ~5,6 escritas/dia, que cabem no orcamento. Alerta de fim de vida em 100000 comutacoes (D5 item 7), visivel no console e no relatorio do item 2.
5. **ECOs DE DIAGNOSTICO, PRIORIZADAS E COM O PINO NOMEADO.** Nenhuma e implementavel em firmware; todas exigem revisao de placa.
   - **ECO-A (prioridade 1): readback de contato auxiliar dos quatro reles.** Nao ha quatro entradas livres; a implementacao viavel e uma rede resistiva somando os quatro contatos auxiliares numa unica entrada ADC (IO36 ou IO39, hoje livres), com quatro pesos binarios distinguiveis. Custo: quatro contatos auxiliares (o AX1RC-5V tem), cinco resistores e uma entrada. Beneficio: e a **unica** ECO que transforma "contato colado" de indetectavel em detectavel, e a unica que permite ao produto reivindicar diagnostico no canal de rele.
   - **ECO-B (prioridade 2): EFLD/EFCM dos dois eixos do XTR300 para GPIO** (o pino restante de IO36/IO39, com multiplexacao, ou um expansor). Detecta cabo analogico aberto e shutdown termico.
   - **ECO-C (prioridade 3): CLR# do DAC8562 ao reset do sistema.** Elimina os ate 3,30 s de angulo velho na saida apos travamento (D7 item 16).
   - **ECO-D (prioridade 4): adequacao de nivel logico 3,3 V -> 5 V no SPI do DAC8562** (VIH minimo 3,5 V contra 3,3 V do ESP32; D10 pendencia 7). Sobe de prioridade para 1 se a MEDICAO 14 de D10 reprovar.
6. **ENQUANTO ECO-A E ECO-B NAO EXISTIREM, O PRODUTO NAO PODE REIVINDICAR NENHUMA CATEGORIA OU NIVEL DE DESEMPENHO DE SEGURANCA.** O canal de rele e de canal unico, sem diagnostico e sem teste automatico. Isto tem de estar escrito na folha de dados e na proposta comercial, nao apenas no manual do usuario. E o ponto em que o projeto tem de parar de descrever o equipamento como mais capaz do que ele e.
7. **O QUE O FIRMWARE PODE FAZER E FAZ, sem readback:** manter os contadores por causa da Decisao 7 item 18, o contador de vida do item 4, o marcador de batimento da tela principal (D12 item 11, que denuncia dado velho mas nao contato colado), e a escrita periodica do DAC a cada 50 ms (D10 item 14, que corrige um quadro SPI corrompido em <= 50 ms sem precisar le-lo de volta). Nenhum desses substitui o ensaio do item 2, e o manual tem de dizer isso.

### Por que
Um supervisor de inclinacao portuario cujo unico canal de seguranca legivel por maquina e um contato seco sem realimentacao esta, do ponto de vista de diagnostico, no mesmo lugar de uma chave fim de curso mecanica dos anos 70 — com o agravante de que o firmware, o display e o console **parecem** dizer que tudo esta bem, porque todos leem o cache da propria escrita. A falha perigosa aqui nao e o contato colar; e o painel afirmar por seis meses que o limite esta armado. As quatro decisoes que tocaram nisso acertaram o diagnostico e nenhuma virou obrigacao com prazo, texto de manual e ECO nomeada. Esta decisao existe para que a limitacao pare de ser uma observacao dispersa em quatro documentos e vire um item de contrato com o cliente e uma fila de ECO com prioridade.

### O que a revisao adversarial derrubou
- **Cedido a D5 item 14, D6 item 4, D7 pendencia 5 e D10 item 15:** as quatro estao certas no diagnostico e sao a base desta decisao. O que faltava era um dono, um intervalo unico e uma redacao de manual.
- **Contra D5 e D7:** o intervalo passa a ser 6 meses (o de D5), e o procedimento de D5 e D7 e corrigido no item 2 — nao pode depender de `relay <n> <on|off>`, que a Decisao 15 remove do produto.
- **Contra D5 item 7:** a politica de escrita do contador de vida (a cada 100 comutacoes) fura o orcamento de flash de D2 item 17; corrigida para 1000 no item 4.
- **Refutado: modo de ensaio automatico que forca os quatro reles ciclicamente.** Comutar quatro saidas de intertravamento sem que a estrutura tenha mudado e, num cais, capaz de derrubar carga ou disparar parada de emergencia — o mesmo argumento com que a Decisao 1 refutou "forcar os reles a alarme durante o PSET". O ensaio e guiado e confirmado por humano (item 3).
- **Refutado: usar as flags EFOT/EFLD/EFCM lidas pelo LED como diagnostico "por inspecao visual dentro da caixa".** A caixa e de aluminio e fica no topo de um portico; um LED que so e visto quando alguem abre o equipamento nao e canal de diagnostico, e chama-lo de canal foi o erro que D6 item 4 corrigiu para o LED do painel.

### Precisa de decisao humana
1. **Intervalo do ensaio funcional periodico.** (a) 6 meses (proposto, e o de D5); (b) 12 meses (o de D7). **RECOMENDACAO: (a)** — o intervalo e literalmente a janela de falha latente, porque nao ha diagnostico nenhum entre dois ensaios.
2. **Secao nova 6.4 do manual com a lista fechada do que a UR nao detecta (item 1) e a obrigacao de intertravamento em dois canais.** **RECOMENDACAO: aprovar; sem ela o cliente cabeia um canal unico achando que tem diagnostico.**
3. **Aprovar ECO-A (readback de contato por rede resistiva numa entrada ADC) para a proxima revisao da DE-PURI-DI261924.** **RECOMENDACAO: aprovar como prioridade 1;** e a unica ECO que muda a classe de seguranca do produto.
4. **Aprovar ECO-B, ECO-C e ECO-D nas prioridades 2, 3 e 4** (ECO-D sobe para 1 se a MEDICAO 14 da Decisao 10 reprovar). **RECOMENDACAO: aprovar a fila.**
5. **Declaracao comercial (item 6): o produto, na revisao atual, nao reivindica categoria nem PL.** **RECOMENDACAO: assinar;** e a unica das decisoes deste conjunto que precisa de aprovacao fora da engenharia.
6. **Item `Ensaio` na IHM (item 3):** (a) existe, como assistente guiado que nao atua; (b) nao existe, e o ensaio e feito so pelo Modo Programacao. **RECOMENDACAO: (b) na primeira versao,** para nao acrescentar item ao menu que a Decisao 3 fecha em 16 folhas; (a) na versao seguinte, dentro de um submenu de diagnostico que ja tera outros itens.

### Precisa de medicao de bancada
- **MEDICAO 7 e MEDICAO 8 da base comum**, herdadas: definem se a bobina energiza e se o BC337 sustenta conducao, que sao as duas falhas que o ensaio do item 2 tem de conseguir revelar.
- **MEDICAO 12 da base comum (mapeamento e polaridade dos LEDs)**, herdada e bloqueante para o passo (b) do item 2: sem o mapa serigrafia/net assinado, o operador confere o LED errado e o ensaio assina um rele que nao foi testado.
- **MEDICAO 28 (NOVA) — ENSAIO DE PROVA CRONOMETRADO E EFETIVO.** Executar o procedimento do item 2 em TRES URs completas, com o painel conectado e um multimetro no borne CN2 de cada limite. Cronometrar. Injetar, em uma das tres, falhas plantadas: um contato colado (rele substituido por um com o contato ponteado), uma bobina aberta e um BC337 removido. ACEITACAO: o procedimento cabe em <= 20 min por UR, e as TRES falhas plantadas sao reveladas em 3 de 3 execucoes, por operadores diferentes, seguindo apenas o texto do manual. Se qualquer falha plantada passar, o texto do manual esta errado, nao o operador.
- **MEDICAO 29 (NOVA) — VIABILIDADE DA ECO-A EM BANCADA (protótipo).** Montar a rede resistiva de quatro contatos auxiliares numa entrada ADC do ESP32 (IO36) e medir as 16 combinacoes possiveis, a 25 C e a 60 C, com a fonte em 4,75 V e em 5,25 V. ACEITACAO: as 16 combinacoes separadas por >= 150 mV de margem em todas as condicoes, com deteccao correta em 1000 de 1000 leituras. Reprovando, ECO-A muda de topologia (expansor I2C ou optoacoplador por canal) antes de entrar em desenho.


---

# Parte 5 — Contradicoes residuais entre decisoes

O que a revisao final de coerencia ainda encontrou depois da reconciliacao. Cada item
precisa ser resolvido antes que a etapa de TDD correspondente comece.

- FILTRO, forma e constante de tempo — Decisao 4 x Decisao 5. D4 item 8 institui parametro de menu "Filtro" com 4 degraus (k=2/4/5/6), default 0,8 s (tau real 0,775 s), nova linha na Tabela 1 e na Tabela 2, tela `Filtro(s):0,8`. D5 item 12 declara o filtro FIXO em kEmaAlphaQ8 = 57 (tau 198,5 ms), SEM item de menu, ajustavel apenas por comando de console `filtro <50..2000>`, e afirma que a Tabela 1 nao passara a ter o parametro. Os dois documentos estao PENDENTES ao mesmo tempo: o implementador escolhe entre 775 ms e 198,5 ms, e entre mexer e nao mexer no manual.
- HISTERESE DE LIBERACAO — Decisao 4 x Decisao 3/Decisao 5. D3 item 25 e D5 item 5 fixam h = 3 decimos (0,3 grau). D4 item 15 afirma que "a histerese de 3 decimos ... e substituida pelos 5 decimos ... da decisao 5" e usa 0,5 grau em toda a justificativa do default do filtro ("pico a pico 0,68 grau, MAIOR que a histerese de 0,5 grau da decisao 5"). A D5 real usa 0,3 grau. O default do filtro de D4 foi escolhido contra um numero que a D5 nao tem.
- TEMPORIZACAO DO COMPARADOR — Decisao 3 x Decisao 5. D3 item 26: ataque em 1 tick (50 ms), liberacao em 10 ticks (500 ms), permanencia minima em alarme de 20 ticks (1000 ms). D5 item 7: ataque em 2 ciclos (100 ms), liberacao em 60 ciclos (3000 ms), NENHUMA permanencia minima separada, mais um teto de comutacao adaptativo que estende a liberacao para 60000 ms. Sao tres numeros diferentes para os mesmos tres eventos no mesmo rele.
- ANTI-ALIAS NA SENSORA, TAMANHO DA JANELA — Decisao 4/Decisao 11 x Decisao 5. D4 item 2 e D11 item 3 especificam media movel de 5 amostras a 100 Hz (janela 50 ms, atraso de grupo 20 ms). D5 item 1b especifica media movel de N = 10 (janela 100 ms, atraso de grupo 45 ms). As tres decisoes exigem ECO no firmware da sensora com contas de rejeicao diferentes.
- RECARGA POR SALTO — Decisao 4 x Decisao 5/Decisao 11. D4 item 6 institui recarga do estado do EMA quando |x - y| >= 2,0 graus, o que produz atuacao em 0,43 s em qualquer ajuste. D5 (lista fechada do item 4) e D11 nao tem recarga por salto: D5 recarrega apenas no boot e na saida de falha, e rejeita explicitamente qualquer segundo caminho rapido. Com D5, uma excursao de 6,0 graus pode levar 3,56 s; com D4, 0,43 s.
- FORMULA DE SENTIDO E PRESET — Decisao 1 x Decisao 11. D1 item 13: `leitura = clamp(dir * bruto + offset, -900, +900)` com `offset := P - dir*bruto`. D11 item 8: `exib = clamp(sentido * cru - preset, -900, +900)`. O termo de preset entra SOMANDO numa e SUBTRAINDO na outra, e D11 chama de "preset" o que D1 chama de "offset". Se as duas virarem codigo como escritas, a referencia sai com sinal trocado e os quatro pontos de atuacao deslocam em 2*offset.
- LEITURA FORA DE +/-90,0 GRAUS — Decisao 8 x Decisao 11 x Decisao 7. D8 item 7.7 declara INVALIDO todo quadro com |reg0| ou |reg1| > 900 decimos, o que leva a falha em 150 ms e aos quatro reles em alarme. D11 item 7 declara 90,1 a 95,0 graus como BANDA DE TOLERANCIA em operacao normal, sem falha, e falha mecanica so acima de 95,0 graus por 1000 ms. D7 item 12 aceita ate +/-1800 decimos, grampeia em +/-900 e nao declara falha nenhuma. Tres comportamentos incompativeis para a mesma amostra de 92,0 graus.
- CRITERIO DE ACEITACAO DA AMOSTRA (registrador 3) — Decisao 7/Decisao 4/Decisao 1 x Decisao 8 x Decisao 3 x Decisao 5. D7 item 4, D4 item 3 e D1 item 9: `reg3 == 0x0001` EXATO. D8 item 7.4: `(reg3 & ~0x0040) == 0x0001`, tolerando WDT_RESET, MAIS `reg5 == 0x00C1` e `reg6 >= 0x0002`. D3 item 29: aceita com DATA_VALID setado e nenhum de {CRC, STARTUP, SELFTEST, NOT_RESPONDING}, ou seja TOLERA kStsSaturated (0x0020). D5 item 4: exige DATA_VALID e nenhum de {CRC, SELFTEST, NOT_RESPONDING, SATURADO}, ou seja TOLERA kStsSclStartup (0x0004). Quatro portas de entrada diferentes para o mesmo filtro.
- O QUE FAZER COM kStsSaturated — Decisao 7 x Decisao 11 x Decisao 5. D7 item 7: estado INSTAVEL, retem o ultimo angulo comandando rele por ate 1000 ms (idade maxima declarada de 1072 ms). D11 item 6: balde furado +20/-2, reles CONGELADOS por ate 500 ms, falha declarada aos 500 ms continuos. D5 item 4: amostra saturada e invalida, logo 3 ciclos = 150 ms ate a falha. 150 ms x 500 ms x 1000 ms.
- REGISTRADOR 7 — Decisao 8 x Decisao 7 x Decisao 4. D8 item 5 REDEFINE o registrador 7 como HEARTBEAT incrementado a cada tick de 10 ms da sensora, e o item 7.5 exige que ele MUDE a cada transacao. D7 item 11 trata o mesmo registrador como uptime em segundos e declara falha de sensor quando `delta == 0` por mais de 5000 ms. D4 item 3 usa `kRegUptimeS` e so detecta reset quando ele DIMINUI. Com o heartbeat de D8, o criterio de D7 nunca dispara e o de D4 dispara a cada 655,36 s.
- TEXTO DA TELA DE FALHA DE COMUNICACAO — Decisao 7 x Decisao 8 x Decisao 12. D7 item 6: quatro estados com quatro textos (`AGUARDANDO SENSOR`, `FALHA DE COMUNICACAO` + `Verifique cabo RS485 e +5V`, `FALHA DO SENSOR` + `Sensor de inclinacao em falha`, `MEDICAO INSTAVEL`). D8 item 9: duas linhas, `FALHA DE COMUNICACAO` + `SENSOR SI-DI141389XY`. D12 item 12: apenas o cabecalho `FALHA DE COMUNICACAO` com o campo de valor em `---,-`, sem segunda linha e sem os estados FALHA DO SENSOR e MEDICAO INSTAVEL. Tres layouts e tres conjuntos de strings.
- MARCADOR DE LEITURA GRAMPEADA — Decisao 7 x Decisao 12. D7 item 12 poe o caractere `>` imediatamente antes do sinal no campo de medicao (ex.: `>+090,0`). D12 item 10 especifica o layout literal da tela principal (cabecalho helvB12 + campo logisoso32 no formato `±XXX,X` + marcador de batimento) e nao tem esse caractere nem espaco reservado para ele.
- CAMPO DE TRIM DE 4 DIGITOS DA AUTO CALIBRACAO — Decisao 6 x Decisao 9 x Decisao 10. D6 item 9: neutro em 5000, telas viram `Ajuste 0Vcc:5000`. D9 item 2: quinta posicao de SINAL, telas viram `Ajuste 0Vcc:+0000`. D10 item 7: offset 5000 MAS a tela abre no VALOR CORRENTE (5000 numa placa virgem), e D10 refuta explicitamente abrir em 0000. Tres grafias diferentes para a mesma figura do manual (L172/L180).
- FAIXA DO TRIM — Decisao 9 x Decisao 6 x Decisao 10. D9 item 3: T0 em [-999, +999] e TG em [-2621, +2621]. D6 item 9: trim unico em [-5000, +4999] para os dois campos. D10 item 7: +/-5000 para os dois campos. A guarda de commit de cada uma foi calculada sobre a sua propria faixa.
- GATE DE PLAUSIBILIDADE DO COMMIT DA CALIBRACAO — Decisao 6 x Decisao 9 x Decisao 10. D6 item 12: recusa se (codeFullScale - codeZero) sair de 20971..31457 (+/-20 %). D9 itens 11 e 12: RETIRA explicitamente a guarda de span e adota `(2*C0 - CFS) >= 5243` como unica condicao. D10 item 6: recusa se delta < 13107 E se calZeroCode sair de [27768, 37767] E se calFsCode sair de [53982, 63981]. Uma calibracao legitima aprovada por uma e recusada por outra.
- TEXTO E DURACAO DA RECUSA DE CALIBRACAO — Decisao 6 x Decisao 9 x Decisao 10. D6 item 12: `CALIBRACAO REJEITADA` por 2000 ms. D9 item 11: `Calibracao recusada!` por 3000 ms. D10 item 10: `Calibracao recusada!` por 2000 ms.
- EXISTE OU NAO O PASSO 10 (VERIFICACAO EM -FE) — Decisao 10 x Decisao 6/Decisao 9. D10 item 9 acrescenta um passo OBRIGATORIO entre o passo 9 e o retorno ao Modo Normal, com tela `Verifique -10Vcc`, teto absoluto de 30 s e portao de |V + 10,000| <= 10,0 mV. D6 item 7 e D9 item 17 encerram o wizard em `Alteracao bem sucedida!` e retorno automatico, sem passo nenhum de verificacao do ramo negativo.
- COMO SE ANUNCIA O OVERRIDE DA SAIDA — Decisao 6 x Decisao 10 x Decisao 9. D6 itens 6 e 7: marcadores de 1000 ms em -11,00 V na entrada e na saida do wizard, e recusa explicita da tela de aviso. D10 item 8: tela `SAIDA SIMULADA` / `Bloqueie o CLP` confirmada por hold de MENU de 3 s, sem marcadores. D9: nem marcador nem tela.
- GATE DE ENTRADA DO WIZARD — Decisao 6 x Decisao 9/Decisao 10. D6 item 13 recusa a Auto Calibracao enquanto QUALQUER limite estiver sinalizado, com tela `CALIBRACAO BLOQUEADA` por 2000 ms. D9 e D10 nao tem gate de entrada nenhum.
- CALIBRACAO AUSENTE OU COM CRC REPROVADO NO BOOT — Decisao 6 x Decisao 9 x Decisao 2. D6 item 17: assume o par de fabrica (32768 / 58982 / 450) e OPERA NORMALMENTE, reportando so no console. D9 item 23: as duas saidas ficam PRESAS em 3932 (-11,00 V) permanentemente com `SEM CALIBRACAO DE FABRICA` na tela. D2 item 10: se o bloco de parametros (que em D2 CONTEM a calibracao) reprovar, o equipamento entra em falha latchada com os quatro reles em alarme e a tela travada em `CONFIG PERDIDA - REPROGRAMAR`.
- GRAMPO DE CODIGO DO DAC — Decisao 6 x Decisao 10 x Decisao 9 x base comum. D6 item 7: 6554..61342 (-10,00 a +10,90 V). D10 item 5: 3277..62259 (+/-11,25 V). D9 item 10: o codigo emitido em servico fica em [5243, 61603]. Base comum: faixa util grampeada em 6554..58982. Quatro janelas, e o codigo de falha 3932 esta FORA de duas delas (D6 e base) e DENTRO das outras duas.
- ONDE VIVE A CALIBRACAO NA NVS — Decisao 2 x Decisao 9 x Decisao 6. D2 item 2 poe calFsAngleDeci[2], calZeroCode[2] e calFsCode[2] DENTRO do ParamRecord unico de 52 bytes, com banco duplo par_a/par_b e efetivacao no SAIR. D9 item 21 exige DOIS registros separados (`cal_fab` imutavel e `cal_usr`), gravados fora do fluxo de parametros. D6 item 11 exige um put unico por EIXO no passo 9 do wizard. Com D2, gravar a calibracao de um eixo obriga a reescrever o registro inteiro de parametros e a passar pela tela de revisao do SAIR.
- MOMENTO DA EFETIVACAO DOS PARAMETROS — Decisao 2 x Decisao 3. D2 itens 1 e 5: durabilidade no hold de 3 s por parametro (chave `par_e`), EFETIVACAO num instante unico ao selecionar SAIR, com tela de revisao `NOVA CONFIG - CONFIRMA?` + `MENU 3s CONFIRMA  BAIXO 3s DESCARTA` e novo hold de 3000 ms. D3 itens 17 a 21: efetivacao e gravacao na saida do SUBMENU `Limite N>` (item Voltar), sem tela de revisao, e o conjunto ativo troca par a par. As duas se descrevem como "a" correcao do rascunho e sao mutuamente exclusivas.
- SAIDA POR TIMEOUT DE 120 s — Decisao 2 x Decisao 3. D2 item 8: o timeout NAO efetiva e NAO descarta; a edicao fica persistida como pendente e a tela principal passa a piscar `CONFIG PENDENTE - REVISAR` ate um tecnico resolver. D3 item 14: o timeout DESCARTA cfg_edit e exibe `Alteracao descartada!` por 3000 ms. Comportamentos opostos para o mesmo evento.
- TEXTO DA FALHA DE GRAVACAO — Decisao 2 x Decisao 3. D2 item 4: `FALHA DE GRAVACAO` por 3000 ms. D3 item 10: `Falha de gravacao!` por 3000 ms. Duas strings distintas para a mesma condicao.
- DURACAO DE `Alteracao bem sucedida!` — Decisao 2 x Decisao 3 x Decisao 6 x Decisao 9. D2 item 4: 1500 ms. D3 item 12: 3000 ms. D6 pendencia 11: 1000 ms. D9 item 17: 3000 ms.
- ORCAMENTO DE BLOQUEIO DO COMMIT EM NVS — Decisao 2 x Decisao 3 x base comum. D2 item 16: kNvsCommitBudgetMs = 500 ms, com um contrato especial (`nvsCommitInProgress`) que manda a tarefa ctrl NAO contar os ticks perdidos. D3 item 16: 100 ms, argumentando que 2 ticks perdidos ficam abaixo dos 3 que declaram falha. A base comum diz que os 3 ciclos cobrem uma janela de ate ~100 ms. Com os 500 ms de D2 e sem a flag, toda gravacao de parametro declara falha de enlace e leva os quatro reles a alarme.
- IDADE MAXIMA DO DADO QUE SUSTENTA UM RELE — Decisao 3 x Decisao 11 x Decisao 7 x base comum. Base: kDataMaxAgeMs = 72 ms. D3 item 30: guarda DURA — idade acima de 250 ms leva os quatro reles a alarme, independentemente do contador de falhas. D11 item 6: congelamento declarado de ate 500 ms em saturacao. D7 item 7: retencao declarada de ate 1072 ms no estado INSTAVEL. As duas excecoes (500 ms e 1072 ms) violam a guarda de 250 ms de D3, que dispararia no meio de ambas.
- DEBOUNCE E GESTO MINIMO DE TECLA — Decisao 1/Decisao 3 x Decisao 12. D1 item 7 aceita prensagens de 30 ms e a medicao 15 de D1 exige ZERO prensagens de 30 ms perdidas; D1 item 5 e D3 item 8 usam kBtnDebounceMs = 20 ms (src/drivers/buttons.h:13). D12 item 5 impoe PISO DE DEBOUNCE de 50 ms ("nenhuma tecla pode ser aceita com menos de 50 ms de nivel estavel") para vencer o acoplamento do refresh no retorno comum CN3-4. Com o piso de D12, o gesto de PSET de D1 e inexecutavel por construcao.
- QUEM AMOSTRA OS BOTOES — base comum x Decisao 1 x Decisao 2 x Decisao 12. Base (DONO DO CICLO): botoes no loop(). D1 item 5 (emenda declarada): tarefa FreeRTOS "btn", core 1, prio 3, cadencia de 5 ms. D2 item 15: hold avaliado "a cada passagem do loop()", com granularidade declarada de 250 ms. D12 item 13 e a refutacao do item 5: "o refresh e a amostragem de botao rodam na MESMA tarefa (loopTask), entao nao existe amostragem concorrente ... o blanking e estrutural" — argumento que a tarefa "btn" de D1 destroi.
- PSET APLICA UM EIXO OU OS DOIS — Decisao 1 x Decisao 2. D1 item 8: o duplo toque aplica P_X e P_Y ao mesmo tempo, atomicamente. D2 item 9 (i): "so altera presetOffsetDeci[eixo]" — um eixo por gesto.
- PSET, EFETIVACAO IMEDIATA OU CONFIRMADA — Decisao 2 x Decisao 1. D2 item 9 e a sua propria secao de refutacoes: o duplo acionamento "efetiva a referencia imediatamente" e D2 recusa qualquer tela de confirmacao por contrariar L161. D1 item 11: acima de 5,0 graus de deslocamento o PSET NAO e aplicado pelo duplo toque e exige hold de MENU de 3000 ms, com cancelamento em 10000 ms. D2 acrescenta ainda intervalo minimo de 10000 ms entre gravacoes de PSET (item 9 iii), que D1 nao conhece.
- INTERVALO DO ENSAIO FUNCIONAL PERIODICO — Decisao 5 x Decisao 7. D5 item 14: a cada 6 meses, obrigatorio no manual. D7 pendencia 5: intervalo de 12 meses declarado no manual. Mesmo ensaio, mesma justificativa (ausencia de readback), dois intervalos.
- ESCRITA DO CONTADOR DE VIDA DO RELE NA NVS — Decisao 5 x Decisao 2. D5 item 7 persiste o contador de comutacoes a cada 100 comutacoes por rele; com o proprio teto de D5 (~1400 comutacoes/dia em balanco pendular) isso da ~14 escritas/dia por rele, ~56/dia nos quatro. O orcamento de vida util de D2 item 17 conta 12 gravacoes/dia (so PSET) e conclui margem de 89x. Com D5, o orcamento de D2 e furado por um fator ~5.
- DECLARACAO DE FALHA DE ENLACE — base comum/Decisao 7 x Decisao 8. Base e D7 item 5: 3 transacoes invalidas CONSECUTIVAS. D8 item 8: balde +3/-1 com teto 30 e disparo em 9, que declara falha tambem em padroes NAO consecutivos (invalida/valida alternadas disparam na 4a invalida, em 7 ciclos) — comportamento que a base nao tem, apesar de D8 afirmar que reproduz a base "sem numero novo".
- LATCH POR FLAPPING — Decisao 7 x Decisao 8/base comum. D7 item 15: 5 entradas em falha dentro de 60 s TRAVAM o estado de falha ate ciclo de energia ou comando `link clear`, com segunda linha `Falha travada - religue a UR`. D8 item 8 e a base definem recuperacao sempre automatica (5 transacoes boas + 2000 ms), sem latch nem comando de liberacao.
- MODO CORRENTE DO XTR300 — Decisao 6 x Decisao 10. D6 item 15 PROIBE o modo corrente no firmware de aplicacao: setMode() deixa de existir e OP_MODE fica fixo em nivel baixo. D10 item 13 continua especificando que o codigo de falha 3932 e escrito "em toda troca de modo do XTR300", ou seja, pressupoe que trocas de modo existem em servico.
- ACEITACAO DO TEMPO DE QUADRO DO DISPLAY — base comum x Decisao 12. Base, MEDICAO 10: sendBuffer <= 10 ms. D12, MEDICAO 10 revisada: sendBuffer <= 25 ms, com a contradicao declarada e justificada (a 4 MHz o piso teorico e 16,384 ms). Enquanto as duas coexistirem, o mesmo ensaio tem dois criterios de aprovacao.
- NUMERACAO DAS MEDICOES — base comum internamente inconsistente, e nove decisoes colidindo no mesmo numero. Dentro da propria base: a secao DONO DO CICLO chama a lacuna de WDI da NVS de "medicao 3" e a lista a chama de MEDICAO 4; a ORDEM DE BOOT chama o tempo de bootloader de "medicao 8" e a lista o chama de MEDICAO 5; a ORDEM DE BOOT chama o tempo de quadro do display de "medicao 9" e a lista o chama de MEDICAO 10. Alem disso, "MEDICAO 14" designa nove ensaios diferentes (chatter do comparador em D3; espectro de vibracao em D4; latencia angulo-contato em D5; excursao de trim em D6; censo de flags saturado em D7; excursao de trim em D9; integridade SPI do DAC em D10; pico de aceleracao em D11; acoplamento do refresh nos botoes em D12). O plano de bancada nao e rastreavel como esta.
- TETO ABSOLUTO DE PERMANENCIA — Decisao 3 x Decisao 6. D3 item 15: teto absoluto de 600000 ms no Modo Programacao, independente de tecla. D6 item 8: teto absoluto de 300000 ms no wizard de Auto Calibracao, tambem independente de tecla. Nenhuma das duas declara a hierarquia entre os dois tetos nem o que a tela mostra quando o de 300 s estoura dentro dos 600 s.


---

# Parte 6 — REQ ainda sem contrato

- ADVERTENCIA DE METODO (vale para toda esta secao): a matriz de REQ nao existe publicada no repositorio. Um `grep` por esses identificadores encontra apenas DOIS deles definidos, ambos em docs/protocolo-rs485.md:629 (REQ-COM-01) e :630 (REQ-MEA-02). Todo o resto do mapeamento abaixo e INFERIDO das secoes do manual pela convencao dos prefixos. Antes de qualquer TDD, o bigboss tem de publicar a matriz REQ->secao do manual; sem ela, "cobertura" e opiniao.
- NRM-03 (entrada no Modo Programacao por MENU mantida ~3 s, manual L90) — NAO aparece em nenhuma decisao nem na base comum. Deve entrar na DECISAO 13 (nova, Senha e gate de acesso), que e a dona do gesto de entrada e do login.
- NRM-04 (tecla ▲ sem funcao no Modo Normal, manual L94) — o codigo nao aparece em lugar nenhum, embora a Decisao 1 trate o assunto por extenso e o declare DESVIO DO MANUAL (▲ passa a ser a tecla de PSET). Basta ETIQUETAR: acrescentar NRM-04 aos REQ afetados da Decisao 1. Nao precisa de decisao nova.
- PWD-02, PWD-03, PWD-04 (tela de login `Senha de acesso:0000`, edicao digito a digito com o digito piscando, submissao por hold de MENU, mensagem `Senha incorreta!` e nova tentativa, timeout de ~2 min sem digitacao — manual L95..L106) — NAO aparecem em nenhuma decisao. Precisam da DECISAO 13 (nova).
- PWD-01 e PWD-05 aparecem apenas como etiqueta nos REQ afetados da Decisao 2, cujo corpo trata a senha somente como campo `password` do ParamRecord (off 40) e nao especifica uma unica tela, gesto ou regra de tentativa. Cobertura nominal, nao substantiva: passam para a DECISAO 13 (nova), que herda de D2 apenas a persistencia.
- PRG-01 (acesso ao Modo Programacao, manual 5.3/5.4 L107-L109) — nao aparece como codigo. Deve entrar na DECISAO 13 (nova), junto com NRM-03.
- PRG-02 (navegacao e edicao dos parametros, manual 5.4 L108-L110 e Tabela 1) — nao aparece como codigo, embora a Decisao 3 seja substantivamente a dona (menu de dois niveis, 16 folhas, gesto de confirmacao). Basta ETIQUETAR na Decisao 3.
- PRG-04 (saida do Modo Programacao por SAIR e por timeout de ~2 min, manual L134-L136) — nao aparece como codigo e, pior, esta em contradicao aberta entre Decisao 2 item 8 e Decisao 3 item 14. Deve ser etiquetado na decisao que sobreviver a reconciliacao D2 x D3 (recomendacao: Decisao 2, que e a dona do momento da efetivacao).
- PRG-03 aparece so nos REQ afetados da Decisao 6 (reles durante o Modo Programacao). Substantivamente coberto por D3 item 28, D5 item 8 e D6 item 2. Etiquetar tambem em D3.
- PST-04 (a leitura passa a ser apresentada em relacao ao valor programado e o Preset e gravado na EEPROM, manual L161-L162) — nao aparece como codigo. Substantivamente coberto por Decisao 1 itens 17 e 19 e Decisao 2 item 9. Basta ETIQUETAR na Decisao 1.
- CAL-06, CAL-07, CAL-08 (angulo de fundo de escala como proporcao que nao altera a faixa do display, saturacao em +/-10,00 Vcc acima do fundo de escala, e o zero antes do ganho — manual L184, L185, L187) — nao aparecem como codigo. Substantivamente tratados de forma DIVERGENTE por D6, D9 e D10. Recomendacao: nao criar decisao nova; FUNDIR D6 + D9 + D10 numa unica decisao de cadeia analogica e calibracao (proposta: renumerar como Decisao 6 consolidada) e etiquetar CAL-01..08 nela. Enquanto as tres coexistirem, CAL-01..08 esta triplicado e nao coberto.
- DIR-01 e DIR-02 (Sentido do Sensor X e Y, manual 5.8 L188-L199) — APARECEM citados em D1, D3, D4, D5 e D11, e todas as cinco dizem explicitamente que o parametro NAO TEM DONO ("hoje sem dono", "ratificar na decisao dona do Preset", "pertence formalmente a DIR-01/DIR-02"). Cobertura zero na pratica: nenhuma fixa a ordem de aplicacao, o efeito sobre o offset de Preset ja gravado, nem o aviso ao operador. Precisa da DECISAO 14 (nova).
- LIM-01 a LIM-08 (os quatro pares Valor + Operacao dos Limites 1 a 4, manual 5.9 L200-L223 e Tabela 1 L121-L128) — NENHUM dos oito codigos aparece em nenhuma das 12 decisoes nem na base comum, apesar de D3 e D5 serem substantivamente as donas. Nao precisa de decisao nova: etiquetar LIM-01..04 (valores) e LIM-05..08 (operacoes) na Decisao 3 (menu, edicao, commit do par) e na Decisao 5 (comparador, histerese, temporizacao) DEPOIS de reconciliar as duas. Sem a etiqueta, nao ha como provar em revisao que os oito parametros da Tabela 1 tem contrato.
- PER-01 (falta de energia: parametros permanecem gravados sem bateria e o valor real da inclinacao e restabelecido automaticamente na religacao — manual 7, L308) — NAO aparece em nenhuma decisao nem na base comum. Substantivamente ja resolvido: a retencao vem da Decisao 2 (ParamRecord + banco duplo + CRC) e o restabelecimento automatico e propriedade do proprio sensor gravitacional, que nao guarda estado, mais o offset de PSET restaurado da NVS. NAO precisa de decisao nova: etiquetar PER-01 na Decisao 2 e acrescentar um unico item la declarando que a leitura relativa (offset de PSET) tambem e restabelecida, que e a unica parte da promessa de L308 que depende de firmware. O que PERMANECE sem dono na queda de energia (a UR nao consegue sinalizar a propria morte por nenhum canal legivel por maquina) ja esta explicito na base comum, secao POLARIDADE DO RELE, e e decisao do bigboss, nao lacuna de REQ.
- COBERTOS, sem acao: MEA-01..05 (Decisao 11 em cheio; MEA-04/05 tambem em D8 item 10, que fecha a pendencia "registrador lido e jogado fora" com uma acao unica de console acima de 70,0 C); DSP-01 e DSP-02 (Decisao 12 itens 6 e 7); DSP-03 e DSP-04 (base comum passo 16 + D7 item 6 + D12 item 12 — cobertos, mas com os tres textos divergentes listados nas contradicoes); NRM-01 e NRM-02 (D1, D11, D12 item 10); COM-01 (D7 e D8, sobre a base de tempo unica); RST-01 (D1 itens 21-29); RST-02 (D1 item 27, D2 item 12 e D9 item 22, agora convergentes em "restaura, nao apaga" — a ambiguidade apontada pela critica de completude esta RESOLVIDA e so falta o bigboss assinar).


---

# Parte 7 — Ordem de entrega dos ciclos de TDD

## Ordem recomendada dos ciclos de TDD

**Regra que governa toda a ordem:** so entra em ciclo de TDD o que tem **contrato numerico assinado**. Onde duas decisoes dao dois numeros para a mesma coisa (ver `contradicoesResiduais`), o ciclo **nao comeca** — nao porque falte codigo, mas porque o teste que se escreveria seria uma escolha do implementador disfarcada de especificacao. Onde o numero e A_MEDIR mas a **forma** e unanime, o ciclo comeca com o numero injetado como parametro e nunca literal.

Todo o dominio (`src/app/*`) e compilavel e testavel em `env:native`, no molde de `src/drivers/calibration.cpp`. Nenhum ciclo de dominio depende de Arduino, de FreeRTOS ou de hardware.

---

### ETAPA 0 — Correcoes factuais e travas de seguranca. **COMECA JA, sem nenhuma decisao aprovada.**

Nada aqui depende de escolha de produto; sao erros verificados no repositorio e unanimidades das 12 decisoes.

| Ciclo | O que | Depende de |
|---|---|---|
| 0.1 | `src/proto/modbus_rtu.h`: `kRegisterCount` 2->8, `kDataBytes` 4->16, `kResponseLen` 9->21, `kRxCap` 16->32, `kDefaultPollTimeoutMs` 50->35. Teste: resposta de 21 bytes cabe e e parseada. | Nada (base comum item 6; D7 item 2; D8 item 12) |
| 0.2 | Extinguir os literais `0x0000` de `src/drivers/xtr300.cpp:12` e `src/drivers/dac8562.cpp:21`, substituindo por `kDacBootCode`/`kDacFaultCode` **injetados**. Teste: nenhum caminho de `begin()`, `setMode()` ou estado seguro escreve 0x0000. | Nada. O **valor** depende da MEDICAO 1; a **remocao do 0x0000** nao depende de nada: -12,5 V saturado e errado nas duas hipoteses de ganho |
| 0.3 | Sensora: ramo de falha de `sensor/src/main.cpp:157` passa a ATRIBUIR o status inteiro e a nao atualizar os angulos. Teste nativo: leitura reprovada nunca publica `kStsDataValid`. | Nada — unanimidade de D3 item 32, D5 item 1c, D8 item 12 e D11 item 4. Precisa apenas da autorizacao de ECO da sensora (D8 pendencia 7) |
| 0.4 | `crc16Modbus` + serializacao de registro de tamanho fixo com `magic`/`version`/`crc` e `static_assert`. Teste: round-trip, deteccao de bit invertido, rejeicao de versao. | Nada — mecanismo ja validado no repo (`CalRecord`) |
| 0.5 | Chute do WDI por ISR de timer de hardware em IRAM (1 kHz, 250 ms, pulso de 1 ms, token de liveness de 800 ms) e a ordem de boot canonica de 16 passos, com `SPI.begin(18,-1,23,-1)` antes do U8g2. | **Base comum apenas** — ja fixada, nenhuma das 12 decisoes a contradiz. E pre-requisito das MEDICOES 4 e 5, que por sua vez destravam varias decisoes |

### ETAPA 1 — `angle` e `measurement_chain`. **COMECA PARCIALMENTE JA.**

| Ciclo | O que | Depende de |
|---|---|---|
| 1.1 | `angle`: `int16` em decimos, conversao `raw*900/16384` com sinal e arredondamento simetrico, faixa, `clamp(+/-900)`. | Nada — unanime (D11 item 2) |
| 1.2 | `ema_q8`: EMA de 1 polo em Q8 com estado `int32`, passo minimo (sem zona morta), arredondamento simetrico unico, **coeficiente injetado**. | Nada — a FORMA e unanime (D4 item 5, D5 item 2). O VALOR (k=4 / tau 775 ms x alfa 57 / tau 198,5 ms) fica fora do dominio |
| 1.3 | `median3` + porta de validade da amostra. | **BLOQUEADO** ate reconciliar o criterio de aceitacao do registrador 3 (D7/D4/D1 x D8 x D3 x D5) |
| 1.4 | `measurement_chain` completa (ordem filtro -> sentido -> preset -> grampo, recarga, retencao de ordem zero). | **BLOQUEADO** ate D4 x D5 reconciliadas (forma do filtro, recarga por salto, N da media na sensora) |

### ETAPA 2 — `sensor_direction`. **Depende de D14 aprovada.**

Nao comeca antes de o bigboss assinar a formula unica `dir*bruto + offset` (D14 item 2, que corrige D11 item 8) e a regra de zerar o offset na troca (D14 item 6). E um ciclo pequeno (troca de sinal e um flag), mas e ele que fixa o sinal de tudo o que vem depois: comecar com a formula errada custa reescrever `preset`, `limit_evaluator` e `analog_scaler`.

### ETAPA 3 — `preset`. **Depende de D14 e de D1 aprovadas.**

- 3.1 `offset := P - dir*bruto` e a aplicacao `dir*bruto + offset`: **pode comecar junto da Etapa 2**, com D14 assinada — a aritmetica e exata e nao depende do gesto.
- 3.2 Guardas do gesto (armamento de 120 s, `reg3 == 0x0001`, buffer de 8 amostras / 400 ms com pico-a-pico <= 5 decimos, confirmacao acima de 5,0 graus, um ou dois eixos): **BLOQUEADO** ate D1 aprovada **e** ate D1 x D2 reconciliadas (um eixo x dois eixos; efetivacao imediata x confirmada; intervalo minimo de 10 s).

### ETAPA 4 — `limit_rule` e `limit_evaluator`. **Depende de D3 x D5 reconciliadas.**

- 4.1 `limit_rule` puro (Off / `>=` / `<=` / `+` com histerese assimetrica so na liberacao, degenerado `|L| < h`, estado LOGICO de alarme sem nenhuma nocao de polaridade): **pode comecar com a polaridade AINDA PENDENTE** — D3 item 24 ja garante que nenhuma linha do comparador conhece polaridade, e uma constante da base inverte tudo. Mas **nao pode comecar** com histerese em disputa (0,3 x 0,5).
- 4.2 `limit_evaluator` (temporizacao): **BLOQUEADO** ate D3 item 26 x D5 item 7 reconciliadas — 1 tick x 2 ciclos de ataque, 500 ms x 3000 ms de liberacao, 1000 ms x nenhuma permanencia minima, teto de comutacao adaptativo existe ou nao.
- 4.3 Guarda dura de idade e comportamento em falha (quatro reles ao alarme, inclusive `Off`): **BLOQUEADO** ate reconciliar 250 ms (D3) x 500 ms (D11) x 1072 ms (D7). O ponto **`Off` vai a alarme na falha** ja e unanime (D3 item 31, D5 item 10, D7 item 9, D8 item 9) e pode ser testado desde ja.

### ETAPA 5 — `analog_scaler` e `analog_calibration`. **Depende da MEDICAO 1 e da fusao D6+D9+D10.**

- 5.1 `analog_scaler` com o modelo `{calZeroCode, calFsCode, calFsAngleDeci}` e conversao inteira em `int32` com arredondamento simetrico e grampo: **pode comecar ja**, porque o MODELO e comum as tres decisoes; os codigos entram injetados. Testes de faixa, monotonicidade, simetria exata em `-FE` e ausencia de estouro de `int32` nao dependem do ganho real.
- 5.2 Grampos, codigo de falha e codigo de boot: **BLOQUEADO** pela MEDICAO 1 (ganho 2 x 5) e pela reconciliacao dos quatro grampos concorrentes (D6 x D9 x D10 x base).
- 5.3 Wizard de Auto Calibracao (campo de trim, gates, marcadores, passo 10): **BLOQUEADO** ate D6, D9 e D10 serem FUNDIDAS numa unica decisao. E o bloco mais contaminado do conjunto: tres grafias de tela, tres faixas de trim, tres gates e a existencia disputada do passo de verificacao em `-FE`.

### ETAPA 6 — `parameters`. **Depende de D2 x D3 reconciliadas e de D9 item 21.**

- 6.1 Regra de banco duplo (validade ANTES de sequencia, `(int16_t)(seqA-seqB) < 0`, proibicao de ler `seq` de banco reprovado) e recuperacao de corte de energia: **pode comecar ja** — a regra e propria de D2 e nao conflita com nada. E o ciclo de maior valor por linha de teste do projeto.
- 6.2 Layout do `ParamRecord` e o que vive dentro dele: **BLOQUEADO** ate decidir se a calibracao mora no ParamRecord (D2) ou em `cal_fab`/`cal_usr` separados (D9).
- 6.3 Momento da efetivacao, tela de revisao, timeout de 120 s: **BLOQUEADO** ate D2 x D3 (SAIR com revisao x saida do submenu; persistir pendente x descartar).

### ETAPA 7 — `digit_editor` e `password`. **Depende de D13 e da MEDICAO 11.**

- 7.1 `digit_editor` puro (composicao de digitos, direcao do cursor da direita para a esquerda com rolagem no campo, recusa de composicao fora de faixa sem clamp silencioso, digito de centenas fixo em `0`): **pode comecar assim que D13 item 3 for assinado**; nao depende de hardware.
- 7.2 `password` (tentativas, bloqueio de 60 s, timeout, `Senha incorreta!`, senha nova so no proximo acesso): depende de **D13 aprovada**. A parte de gesto (hold de 3000 ms) depende da **MEDICAO 11**, que e bloqueante para toda a IHM.

### ETAPA 8 — `ui`. **Ultima do dominio. Depende de tudo acima mais D12 e a tabela de strings assinada.**

Nao comecar antes de existir **uma unica tabela de strings aprovada byte a byte**. Hoje ha, so para falha de comunicacao, tres layouts (D7, D8, D12); para `Alteracao bem sucedida!`, quatro duracoes; para a recusa de calibracao, tres textos. Escrever a UI antes disso e escrever tres vezes.

### ETAPA 9 — Adaptadores. **Parcialmente ja.**

- 9.1 Mestre Modbus (transacao FC03 start 0 count 8, enquadramento, CRC, timeout de 35 ms, contadores por causa): **pode comecar ja** — a transacao e unanime e a base comum a fixou. So a **regra de aceitacao** (Etapa 1.3) fica de fora.
- 9.2 `RelayBank`, `Xtr300`/`DAC8562`, `Buttons`, `Display`, `NvsStore`: seguem os contratos das etapas correspondentes; o `RelayBank` so precisa da polaridade no ultimo instante, porque recebe estado logico.

### ETAPA 10 — `app` + `main` (setup, tarefa ctrl, splash nao bloqueante). **Esqueleto COMECA JA; recheio por ultimo.**

O esqueleto — ordem de boot de 16 passos, criacao da tarefa ctrl (core 0, prio 5, stack 4096, `vTaskDelayUntil` de 50 ms), fila/portMUX para os pedidos da IHM, splash de 600+600 ms como maquina de estados no `loop()` — depende **apenas da base comum**, que ja esta fixada, e por isso pode ser construido em paralelo com a Etapa 1. O recheio (o que a ctrl chama dentro do tick) entra a medida que as etapas 1 a 5 forem liberadas.

---

## Resumo: o que pode comecar HOJE, com decisoes pendentes

1. **Etapa 0 inteira** (0.1 a 0.5) — correcoes verificadas, unanimidades e a base comum.
2. **Etapa 1.1 e 1.2** — `angle` e `ema_q8` com coeficiente injetado.
3. **Etapa 5.1** — `analog_scaler` com o modelo parametrizado.
4. **Etapa 6.1** — regra de banco duplo e recuperacao de corte de energia.
5. **Etapa 7.1** — `digit_editor`, assim que D13 item 3 for assinado (e um item, nao a decisao inteira).
6. **Etapa 9.1** — adaptador Modbus sem a regra de aceitacao.
7. **Etapa 10 (esqueleto)** — ordem de boot, ISR/IRAM do WDI e tarefa ctrl vazia. **E o item de maior urgencia do projeto**, porque destrava as MEDICOES 4 e 5, e as medicoes destravam decisoes.

## O que NAO pode comecar, e o que exatamente falta assinar

| Bloqueio | Falta |
|---|---|
| `measurement_chain`, `limit_evaluator` | Reconciliar D3 x D4 x D5: forma e constante do filtro, histerese (0,3 x 0,5), ataque, liberacao, permanencia, recarga por salto, N da media na sensora |
| Regra de aceitacao da amostra | Reconciliar D1/D4/D7 x D8 x D3 x D5 (quatro criterios de `reg3`) e o contrato do registrador 7 (heartbeat x uptime) |
| Faixa e falha mecanica acima de 90 graus | Reconciliar D7 x D8 x D11 |
| Wizard de calibracao e grampos do DAC | FUNDIR D6 + D9 + D10 numa decisao so, e a MEDICAO 1 |
| `parameters` (layout e efetivacao) | Reconciliar D2 x D3, e decidir onde mora a calibracao (D2 x D9) |
| `preset` (gesto) | D1 aprovada e reconciliada com D2 (um eixo x dois; confirmacao) |
| `sensor_direction` | **D14** (nova) aprovada |
| `password` | **D13** (nova) aprovada + MEDICAO 11 |
| `ui` inteira | Tabela unica de strings assinada + MEDICAO 11 + MEDICAO 14 de D12 (acoplamento do refresh nos botoes) + reconciliar debounce 20 ms x piso de 50 ms |
| Escrita de rele em hardware | Polaridade (`kRelayFailSafePolarity`) assinada pelo bigboss, sustentada pelas MEDICOES 7, 8 e 9 |
| Escrita do DAC com valor fisico | MEDICAO 1 (ganho 2 x 5) e MEDICAO 3 (swing e -11,00 V) |