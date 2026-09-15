# Atualizacao de firmware por WiFi (OTA) - SUI-DI141388XY

Contrato da atualizacao de firmware das **duas** placas do produto: a Unidade Remota
`DE-PURI-DI261924` (supervisora) e a placa sensora `PUSI-DI261930`.

**Regra de autoria deste arquivo:** o que esta descrito como "implementado" foi lido no
codigo-fonte ou medido, nao presumido. O que nao foi medido esta na secao
[9. O que ainda nao foi medido](#9-o-que-ainda-nao-foi-medido), e essa secao e a parte mais
importante do documento.

---

## 1. Em uma frase

O radio das duas placas fica **desligado**. No menu da supervisora, o item **Atualizar** pede um
codigo de quatro digitos - **1976** - e liga o ponto de acesso WPA2 **das duas**: o dela e o da
sensora, este por RS-485. Quem precisa atualizar liga o celular naquela rede, abre a pagina,
escolhe o arquivo `.ota` e envia. A supervisora exige ainda uma confirmacao no painel, com aviso
do que vai acontecer com as saidas; a sensora, que nao tem painel, aceita direto.

O radio **cai sozinho** depois de **20 minutos sem nada chegar**, ou de 60 minutos no ar.

---

## 2. O erro que o formato existe para impedir

Nao e "arquivo corrompido". E **a imagem da outra placa**.

As duas placas sobem o mesmo tipo de ponto de acesso, com a mesma pagina, e quem atualiza esta no
patio com o celular na mao. Uma imagem da supervisora gravada na sensora e uma imagem
**valida**: ela sobe, passa em toda verificacao que a IDF faz, e nao tem SCL3300, nao tem RS-485
escravo e nao tem como ser atualizada de novo. Vira caminhonete com cabo USB.

Por isso todo pacote comeca com 64 bytes de cabecalho, conferidos **antes de o primeiro byte ir
para a flash**:

| desl. | tam. | campo |
|---|---|---|
| 0 | 8 | magica `DEPURIOT` |
| 8 | 2 | versao do cabecalho (1) |
| 10 | 2 | **alvo**: 1 = supervisora, 2 = sensora |
| 12 | 4 | tamanho da imagem, sem contar estes 64 |
| 16 | 4 | CRC-32 da imagem |
| 20 | 32 | SHA-256 da imagem |
| 52 | 3 | versao maior.menor.correcao |
| 55 | 5 | reservado (tem de ser zero) |
| 60 | 4 | CRC-32 deste cabecalho, sobre os bytes 0..59 |

A ordem das conferencias e deliberada e a mensagem na tela depende dela: tamanho do buffer,
magica, **CRC do cabecalho**, versao do formato, alvo, tamanho da imagem, reservados. O CRC vem
antes de qualquer campo porque, sem ele, um bit trocado no campo `alvo` seria lido como um alvo
valido.

Layout e regras em `lib_shared/depuri_ota/include/ota_package.h`.

---

## 3. Isto NAO e autenticacao

CRC-32 responde "o arquivo chegou inteiro?". SHA-256 responde "este arquivo e *aquele* arquivo?".
**Nenhum dos dois responde "quem mandou este arquivo?"** - nao ha assinatura. Quem produz o
arquivo produz o resumo.

O unico controle de acesso e a senha WPA2 do ponto de acesso, e ela tem dois caminhos, nesta
ordem de precedencia:

1. **NVS `ota`/`pw`** - senha **sorteada na producao**, gravada pelo jig, impressa na etiqueta da
   placa. Nao esta em firmware nenhum. Se existir, **ganha**. E o que a Decisao 15 item 8 exige.
2. **Senha padrao de fabrica** - `dieletrons-2025`. Vale enquanto o jig nao sortear nada, que e
   **hoje, em toda a frota**.

> **A senha padrao esta no firmware, e o firmware e o arquivo que entregamos ao cliente.** Um
> unico `.ota` que vaze abre a frota inteira, para sempre, e nao ha como trocar sem regravar todas
> as placas. Isso foi escolhido conscientemente em 2026-09-14, com o custo declarado - nao e um
> descuido a ser descoberto depois.
>
> **O que isso NAO piorou:** a alternativa anterior derivava a senha do MAC, e o MAC vai no ar em
> texto claro em toda baliza; qualquer um com o firmware calculava a senha de qualquer
> equipamento do patio. As duas sao publicas. A fixa so e mais honesta sobre isso.
>
> **O caminho para a senha de verdade continua aberto e nao custa firmware:** quando a producao
> passar a sortear e gravar em NVS, nenhuma linha muda. Placas novas saem com senha propria; as
> antigas seguem com a padrao ate serem regravadas.

O que continua distinguindo um equipamento do outro e o **SSID**, e so ele.

### Sao TRES segredos, com tres propositos

| segredo | responde | quem troca |
|---|---|---|
| `1234` (Modo Programacao) | posso mexer na configuracao? | o cliente, pelo menu |
| **`1976`** (codigo OTA) | posso **ligar o radio**? | ninguem: e fixo |
| `dieletrons-2025` (WPA2) | posso falar com o radio? | ninguem: e fixo, vai na etiqueta |

O codigo `1976` ser fixo e deliberado: um cliente que troque a senha do Modo Programacao e a
esqueca nao pode, com isso, ficar sem caminho de atualizacao. E saber `1234` nao basta para ligar
o radio de um equipamento de seguranca.

**E por isso que o radio deixou de ficar permanentemente no ar.** Com a senha WPA2 fixa e
publicada, um ponto de acesso permanente e um equipamento gravavel por quem passar perto do patio,
o ano inteiro. Sob comando, a janela encolhe para os minutos em que um tecnico esta na frente do
painel. Este e o unico ganho de seguranca real que o produto teve depois que a senha virou fixa.

Detalhes em `lib_shared/depuri_ota/include/ota_credentials.h`.

### Como descobrir a senha de uma placa

A senha e `dieletrons-2025`, igual em toda placa que nao tenha senha propria gravada na producao.

O que muda de placa para placa e o **SSID**, e a propria placa imprime os dois no console de
115200 assim que liga:

```
ota: ponto de acesso NO AR SUI-UR-123456 senha dieletrons-2025  (PADRAO DE FABRICA - ver docs/ota.md)
```

O sufixo entre parenteses diz em qual dos dois caminhos aquela placa esta:

| sufixo | significa |
|---|---|
| `(PADRAO DE FABRICA ...)` | sem senha em NVS; vale `dieletrons-2025` |
| `(gravada na producao)` | tem senha propria; esta na etiqueta, nao aqui |

---

## 4. O que acontece com as saidas

Escolha do operador, registrada: **alarme declarado, com aviso na tela antes**.

Do instante da confirmacao ate a placa reiniciar:

- os **quatro reles** vao a `Signalled`;
- as **duas saidas analogicas** vao ao codigo de falha 3932 (-11,00 V, fora de banda).

Isto nao e uma falha detectada - e uma declaracao, feita **antes da primeira escrita na flash**.
Apagar um setor trava os dois nucleos por dezenas de milissegundos; quatro reles seguindo leitura
velha sao piores que quatro reles em alarme anunciado.

**Nao e latch.** Sai sozinho quando a sessao termina ou morre. Exigir rearme humano a cada
gravacao poria alguem na frente do painel toda vez.

**Nao se confunde com A8.** "Configuracao perdida" diz *ninguem sabe com que limites este
equipamento opera*; o alarme de atualizacao diz *estou me regravando, de proposito, por um
minuto*. Um operador que veja "configuracao perdida" em toda atualizacao aprende a ignorar a
mensagem que um dia vai ser verdadeira.

Na **sensora** nao ha rele: o alarme sai porque o enlace para de responder e a supervisora declara
falha por conta propria (decisao A5).

---

## 5. Os relogios que impedem a maquina de ficar parada

O modo de falha de campo de verdade nao e o arquivo corrompido. E: o operador confirma, comeca a
subir, o celular bloqueia a tela, ele vai almocar - e a maquina fica travada em alarme sem ninguem
entender por que.

| relogio | valor | o que mata |
|---|---|---|
| **inatividade do radio** | **20 min** | ponto de acesso no ar e **nada chegando** |
| teto do radio | 60 min | alguem usando de verdade, e continuando a usar |
| confirmacao | 60 s | pacote aceito e ninguem confirmou no painel |
| estagnacao | 60 s | parou de chegar byte |
| teto da gravacao | 300 s | fluxo lento porem continuo, que nunca estagna |
| mensagem na tela | 10 s | recusa ou erro sai sozinho e a placa volta a aceitar |

Os dois da gravacao existem juntos porque **um fluxo lento porem continuo nunca estagna e um
fluxo morto nunca estoura o teto**. Vencido qualquer um, a sessao morre e as saidas voltam.

### O relogio do radio conta ATIVIDADE, nao cliente conectado

E a diferenca que importa em campo: **um celular no bolso continua associado a rede do
equipamento por horas sem pedir nada**. Se associacao contasse como vida, o radio ficaria ligado
exatamente no caso que o prazo existe para cobrir.

O que renova os 20 minutos e uma requisicao chegar - abrir a pagina, a pagina consultar o estado,
um pedaco de imagem subir. O teto de 60 minutos existe porque a propria pagina consulta o estado
a cada 700 ms enquanto estiver aberta: uma aba esquecida aberta renovaria o prazo para sempre.

Nenhum dos dois derruba o radio no meio de uma gravacao.

Ha um caso que nenhum destes relogios cobre e que resolve sozinho: se o cliente parar de enviar
**sem desconectar**, o `WebServer` do core fica em `while(!client.available() && client.connected())
delay(2)` (`Parsing.cpp:339`), que nao tem prazo. Nenhum pedaco chega, o gancho de keep-alive nao
e chamado, e o watchdog externo reseta a placa. **Isso e aceitavel e e o comportamento desejado:**
nada foi ativado, a `otadata` nao foi tocada e a particao que esta rodando esta intacta - a placa
volta no firmware antigo.

---

## 6. Por que nao `Update.h`

`Updater.cpp:213-217` do core Arduino apaga blocos de **64 KiB numa unica chamada** - ate 2000 ms
parado. Isso estoura o token de liveness da supervisora (800 ms) e o `tWD` minimo do STWD100 da
sensora (1120 ms): a placa reseta no meio da gravacao.

O produto usa `esp_ota_begin(..., OTA_WITH_SEQUENTIAL_WRITES)`, que faz a IDF apagar **setor a
setor (4 KiB)** durante a escrita, e fatia cada `write()` em 4 KiB para que a chamada volte rapido.

`maiorEscritaMs()` e **medido, nao estimado**: `esp_ota_ops.c` vem pre-compilado no framework e o
tempo de apagamento depende do chip de flash soldado na placa. A placa mede e informa.

### A diferenca entre as placas que custa caro

`WebServer::handleClient()` le o corpo inteiro do POST **dentro de uma unica chamada**
(`Parsing.cpp:429-471`) e so devolve quando o envio termina. O laco principal nao roda nesse
intervalo.

- **Supervisora:** o batimento do watchdog e da tarefa `ctrl`, que vive no **core 0** e continua
  rodando. O watchdog nao depende do gancho. O gancho serve para mexer o painel - sem ele a barra
  de progresso congela em 0% e quem esta na frente da maquina conclui que travou.
- **Sensora:** nao ha segunda tarefa. O laco e um so, e e ele que renova o token de liveness.
  **Sem batimento no gancho a placa reseta no meio de todo envio.**

---

## 7. Rollback: a placa decide se a imagem nova presta

Sem isto o primeiro OTA seria irrecuperavel.

Com `CONFIG_APP_ROLLBACK_ENABLE=y`, `initArduino()` chama
`esp_ota_mark_app_valid_cancel_rollback()` **antes do `setup()`** (`esp32-hal-misc.c:203-238`),
guardado so por dois simbolos **fracos**. De fabrica, portanto, qualquer imagem que apenas chegue a
rodar e dada por boa - inclusive uma sensora que sobe e nao fala com o SCL3300.

O produto define `verifyRollbackLater()` **forte**, devolvendo `true`
(`{ur,sensor}/src/ota_rollback_hook.cpp`), o que faz o core pular o bloco inteiro, e passa a
decidir por criterio explicito:

- **5 ciclos bons CONTINUOS em ate 30 s.** Ciclo ruim zera a contagem - cinco ciclos bons
  intercalados com ruins descrevem um equipamento intermitente.
- **Ciclo bom** = enlace `Ok` e nao `stale` na supervisora; leitura valida do inclinometro na
  sensora. "O firmware subiu" nao serve: e o que o core ja fazia.
- **Reprovado nao retorna:** `esp_ota_mark_app_invalid_rollback_and_reboot()` reinicia e o
  bootloader sobe a particao anterior.

> **O gancho so serve na imagem que JA ESTA na placa.** Nao adianta ele estar na imagem que vai
> subir. Por isso ele foi a primeira peca implementada, e por isso a **primeira** gravacao de cada
> placa da frota tem de ser **por cabo**.

`check_rollback_hook.py` roda como pos-script do build nas duas placas e exige ` T
verifyRollbackLater` no ELF via `nm`: um gancho que voltasse a ser simbolo fraco passaria
despercebido sem isso.

---

## 8. Procedimento

### 8.1 Gerar o pacote

O `.ota` sai **automaticamente** junto do `firmware.bin`, com alvo e versao ja preenchidos:

```
cd ur     && pio run -e esp32dev   # -> .pio/build/esp32dev/firmware-supervisora-0.1.0.ota
cd sensor && pio run -e pusi       # -> .pio/build/pusi/firmware-sensora-0.2.0.ota
```

Isto e passo de build e nao comando que alguem lembra de rodar, porque o campo que importa e o
**alvo**, e um empacotamento manual erra o alvo exatamente no dia corrido.

A mao, quando necessario:

```
python3 scripts/empacota_ota.py <firmware.bin> -a supervisora|sensora -v 0.2.0
```

### 8.2 Conferir antes de ir ao patio

Com o **mesmo parser que roda na placa**:

```
g++ -std=gnu++17 -I lib_shared/depuri_ota/include -o /tmp/verifica_ota scripts/verifica_ota.cpp
/tmp/verifica_ota ur/.pio/build/esp32dev/firmware-supervisora-0.1.0.ota 1
```

Vale mais a pena rodar com o alvo **errado** de proposito: tem de dar veredito 5 (`AlvoErrado`).

### 8.3 Atualizar

0. **Ligue o radio**, no painel da supervisora: `Menu` > `Atualizar` > digite **1976** > segure
   MENU 3 s. A tela confirma `WIFI LIGADO - VER CONSOLE`. Isso liga o ponto de acesso **das
   duas** placas - o da sensora vai por RS-485, em broadcast.
1. No celular, ligue na rede `SUI-UR-XXXXXX` (supervisora) ou `SUI-SEN-XXXXXX` (sensora), onde
   `XXXXXX` sao os tres ultimos bytes do MAC. A senha esta na etiqueta da placa.
2. O celular abre a tela de atualizacao sozinho, como faz em rede de hotel (ha um servidor DNS na
   placa que manda toda consulta para ela mesma). Se o seu celular nao abrir, digite o endereco a
   mao - **a propria placa imprime qual e**, no console, no instante em que o radio sobe:

   ```
   ota: ponto de acesso NO AR SUI-SEN-184710  pagina http://192.168.4.1
   ```

   O endereco e perguntado a pilha de rede e nao assumido: o padrao do softAP vem de uma
   biblioteca pre-compilada do ESP-IDF e nao esta em cabecalho nenhum deste repositorio. Na
   sensora, o comando `wifi` no console tambem mostra.
3. Escolha o `.ota` e toque em **Enviar**.
4. **Supervisora:** o painel mostra `ATUALIZAR FIRMWARE?` com o aviso `SAIDAS VAO PARA ALARME` e a
   versao. Confirme **segurando MENU por 3 s**; **DOWN** cancela. (Mesmo gesto do commit de
   Preset: um toque solto perto de um painel nao e decisao.)
   **Sensora:** comeca direto.
5. A barra anda; ao fim a placa reinicia sozinha.
6. A imagem nova entra **em prova**: 5 ciclos bons em ate 30 s ou a placa volta para a anterior.

### 8.3.1 Como a sensora entra em OTA

Ela nao tem painel nem teclado: quem liga o radio dela e **a supervisora**, pelo item `Atualizar`
do menu. O comando vai pelo RS-485 em **broadcast** (funcao 0x06, registrador 8, valor 1976), tres
vezes em ciclos diferentes.

**Broadcast nao tem resposta** - e de proposito: esperar confirmacao dentro do tick de 50 ms do
ciclo de seguranca custaria o dobro do orcamento por um comando administrativo. O preco e que
**nao ha confirmacao no fio**. A confirmacao e a rede `SUI-SEN-XXXXXX` aparecer na lista do
celular. Se nao aparecer, repita o passo 0.

Na bancada, sem supervisora nenhuma ligada, o console da sensora (115200) resolve:

```
wifi on             # liga o ponto de acesso agora
wifi                # SSID, modo e quantos clientes estao ligados
wifi off            # derruba
```

Durante a atualizacao a sensora **para de responder ao RS-485** por alguns segundos. Isso e
esperado: a supervisora declara falha de enlace e leva os quatro reles a alarme por conta propria
(decisao A5). Nao ha nada a fazer no painel da supervisora.

### 8.4 Conferir o que ficou gravado

O console de bancada registra o SHA-256 do pacote no instante em que ele e aceito. Confira contra
o que o `empacota_ota.py` imprimiu. E o unico jeito de afirmar **qual** arquivo ficou na placa sem
abrir o modulo.

---

## 8.5 Em que estados a placa aceita atualizacao

| estado da supervisora | ponto de acesso | aceita pacote? |
|---|---|---|
| operacao normal | no ar | sim |
| Modo Programacao / assistentes | no ar | sim - a tela de confirmacao toma o painel |
| falha de enlace, latch de A7 | no ar | sim |
| **CONFIG PERDIDA (A8)** | no ar | **sim** |
| logomarca e autoteste do boot | no ar | **nao** - a pagina fica muda |

**CONFIG PERDIDA merece a linha em negrito.** Aquele estado devolve o laco antes da IHM e fica no
ar por semanas, ate alguem ir ao painel. Se a atualizacao vivesse atras daquele desvio, a placa
que mais precisa de firmware novo - a que esta travada em falha - seria a unica que nao
conseguiria receber. E e seguro: em CONFIG PERDIDA os quatro reles **ja** estao em alarme, entao
nao ha o que declarar.

Durante o **splash de boot** a pagina nao responde, de proposito: aceitar um pacote ali poria a
tela de confirmacao atras da logomarca, o operador nao veria a pergunta e a sessao morreria por
prazo sem ele entender por que. Sao poucos segundos.

---

## 9. O que ainda nao foi medido

Esta secao existe porque a atualizacao por WiFi foi implementada **antes** das medicoes que a
qualificam. As duas abaixo podem reabrir a decisao.

### MEDICAO 26 - jitter da tarefa `ctrl` com o radio ligado (supervisora)

As tarefas do stack WiFi do ESP-IDF rodam em prioridades 22 e 23 **no core 0**, que e onde vive a
tarefa `ctrl`. Todo o orcamento de 50 ms do ciclo de seguranca foi calculado num core cujo unico
ocupante de alta prioridade e o `esp_timer`.

**A DECISIONS.md espera que esta medicao REPROVE.** Ela nunca foi feita. Ate que exista, o ponto de
acesso permanentemente no ar e uma **escolha assumida, nao uma propriedade verificada** - e esta
registrada como tal no proprio `setup()` de `ur/src/main.cpp`.

Aceitacao: periodo entre ticks <= 55 ms em 100.000 de 100.000 e zero transacoes Modbus perdidas
por atraso.

### MEDICAO 12 - ruido de RF do radio no SCL3300 (sensora)

`WiFi.mode(WIFI_OFF)` estava na sensora **desde o inicio por este motivo**. O inclinometro e a
funcao inteira do produto, e o radio agora fica ligado 100% do tempo, nao so durante a
atualizacao.

Caminho de volta, imediato e sem regravar nada:

```
wifi off        # no console de bancada da sensora, 115200
```

Desligar **nao persiste** no boot seguinte, de proposito: e ferramenta de medicao, nao
configuracao. Uma placa que se lembrasse de estar sem radio seria uma placa que ninguem atualiza
mais sem cabo.

### MEDICAO NOVA - alcance util do ponto de acesso

**Nao ha um unico dado de RF neste repositorio.** O produto preve ate 500 m entre a supervisora e
a sensora, e as duas ficam em modulos separados. O ponto de acesso foi configurado em 11 dBm e 2
clientes justamente porque o alcance util pretendido e *o lado da maquina* - quem atualiza precisa
estar perto o bastante para ler a etiqueta. Se na pratica nao der para chegar perto de uma das
placas, isso e requisito novo, nao ajuste de potencia.

---

## 10. Relacao com a Decisao 15

A Decisao 15 declarava WiFi **fora** do binario de producao, por exclusao de linkagem, com
verificacao por `nm` no release. A **Decisao 17** a substitui nos itens 1, 2, 7 e 8 - e so neles.

Continua valendo, sem alteracao:

- **Item 3** - comandos de **atuacao** do console (`relay`, `ao raw`, `ao mode`, `test`,
  `cal erase`) fora do binario de producao. Um ponto de acesso para atualizar firmware nao
  autoriza escrever em rele por console.
- **Itens 4, 5 e 6** - ensaio funcional sem comandos de atuacao, marcacao `BUILD=FACTORY`, e a
  mesma regra nas duas placas.

O item 8 previa que acesso remoto, se pedido, exigiria "secao nova de manual, superficie de ataque
declarada, autenticacao que nao seja uma senha de 4 digitos publicada, e uma decisao propria".
**A secao de manual ainda nao existe** - ver a pendencia no fim da Decisao 17.
