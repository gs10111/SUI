# Casas decimais da indicacao de angulo - design

Data: 2026-10-06. Branch: `feat/casas-decimais`. Escopo: so a UR (`ur/`).

## Pedido

O cliente quer escolher no menu como os numeros de angulo aparecem. Hoje e fixo em uma casa
(`+045,0`).

> **EMENDA 1 (2026-10-06, mesmo dia):** a decisao 1 abaixo foi revista a pedido do responsavel. A
> opcao `2 (+045,00)` existe, com o centesimo tirado do estado do filtro da UR - sem mudar sensora
> nem protocolo. Detalhes e limites na Decisao 18, Emenda 1, em `DECISIONS.md`.

## Decisoes tomadas na conversa de 2026-10-06

| # | Pergunta | Decidido | Por que |
|---|----------|----------|---------|
| 1 | Quais opcoes | **0 ou 1 casa.** Duas casas fica de fora | A sensora entrega decimo de grau (`docs/protocolo-rs485.md:281`), a UR guarda decimo (`ur/src/domain/angle.h`) e a exatidao declarada e +/-0,09 grau (`DECISIONS.md:2328`). A segunda casa seria ruido ou zero inventado |
| 2 | Tirar o quadrado de batimento para abrir espaco | **Nao. Fica.** | Com 0 ou 1 casa o texto nao cresce, entao o motivo de espaco sumiu. O batimento e Decisao 12 item 11, o unico campo que denuncia painel congelado |
| 3 | Onde o formato vale | **Em toda indicacao de angulo**: leitura ao vivo, valor de limite, offset de Preset, leitura crua do assistente de Preset | Escolha do cliente. Consequencia aceita: com 0 casas um limite gravado em 45,3 aparece como `+045` |
| 4 | Campo em edicao | **Sempre com uma casa**, nos dois modos | Ninguem altera o ponto de atuacao de um rele sem ver o decimo. O `DigitEditor` nao muda |
| 5 | Arredondamento com 0 casas | **Inteiro mais proximo, meio para longe do zero** | Erro maximo de 0,5 grau, simetrico nos dois sinais, mesma convencao da conversao da sensora (`DECISIONS.md:2306`) |

## Comportamento

### Menu

- Item novo `Casas Decimais`, posicao 13 de 14 (logo antes de `Sair`), atras da senha do Modo
  Programacao como todo o menu.
- Escolha entre duas opcoes, no mesmo molde de `Sentido Sensor`: `0 (+045)` e `1 (+045,0)`.
  Confirmar grava; cancelar sai sem gravar.
- Padrao de fabrica: **1**. Placa atualizada se comporta exatamente como antes ate alguem
  mexer.

### Formato

| Valor (decimos) | 1 casa | 0 casas |
|-----------------|--------|---------|
| +453 | `+045,3` | `+045` |
| +445 | `+044,5` | `+045` |
| +444 | `+044,4` | `+044` |
| -445 | `-044,5` | `-045` |
| -4 | `-000,4` | `+000` |
| 0 | `+000,0` | `+000` |
| +900 | `+090,0` | `+090` |
| sem leitura | `---,-` | `---` |

- Largura constante nos dois modos: sinal sempre presente, tres digitos inteiros. O numero nao
  danca ao cruzar o zero nem o 10.
- Zero nunca sai com sinal negativo: `-000` e proibido, o arredondamento que da zero sai `+000`.
- Offset de Preset vai a +/-180,0 (A9) e segue a mesma regra: `+180`, `-180`.

### O que nao muda

- Avaliacao de limites, reles, saida analogica e Preset continuam em decimo de grau inteiro. A
  opcao e so de APRESENTACAO.
- O quadrado de batimento, a porcentagem `SAI:`, os textos de falha.
- O campo em edicao (`DigitEditor::format`).

## Arquitetura

### `domain::Angle` (`ur/src/domain/angle.h`)

`format()` ganha o numero de casas como parametro explicito. Continua sendo o unico dono do
texto de angulo. Nao existe estado global de formato: quem desenha recebe a opcao e a repassa.

Um tipo pequeno nomeia a opcao (ex.: `enum class AngleDecimals : uint8_t { Zero = 0, One = 1 }`)
para que um `uint8_t` solto nao seja confundido com largura ou indice.

O arredondamento fica numa funcao so, usada por `Angle::format` e por `PresetWizard::formatDeci`,
para que leitura e offset nunca arredondem diferente.

### `PresetWizard::formatDeci` (`ur/src/domain/ui/preset_wizard.cpp:411`)

Mesmo parametro. E dono do texto do offset (+/-1800 decimos, fora da faixa de `Angle`).

### `Parameters` (`ur/src/domain/parameters.h/.cpp`)

- Campo novo `displayDecimals`, validado (so 0 ou 1; qualquer outro valor e `Err::Range`).
- Bloco de parametros sobe da **v2 para a v3**: o campo novo entra onde fica o CRC da v2 e o CRC
  anda dois bytes para o fim, como a v2 fez com a v1.
- `loadParams` aceita **v1, v2 e v3**. v1 e v2 carregam `displayDecimals = 1`. Recusar um
  formato antigo levaria a frota a CONFIG PERDIDA na atualizacao - os quatro reles em alarme.
  A proxima gravacao promove o bloco a v3; nenhuma escrita no boot.

### `MenuMachine` (`ur/src/domain/ui/menu_machine.*`)

- `MenuItem::CasasDecimais = 12`, `Sair` passa a 13.
- Estado novo `EditDecimais`, no molde de `EditSentido`.

### Quem desenha

- `NormalScreen`: `NormalInput` ganha a opcao; `Line::add(Angle)` e `addPresetOffset` a repassam.
  Cobre leitura (tela principal e detalhe) e valor de limite (detalhe).
- `PresetWizard`: telas que mostram offset (`preset_wizard.cpp:462`, `:522`).
- `application.cpp:776`: leitura crua no assistente de Preset.
- `application.cpp:62` ja entrega o valor de limite como `Angle`; so precisa preencher a opcao
  no `NormalInput`.

Dimensionamento: o texto de 0 casas e mais curto que o de 1 casa, entao nenhum calculo de largura
(`detailValueX`, `detailFont`, `statusFont`) piora. Os piores casos literais continuam os de 1
casa.

## Testes (antes do codigo, um por regra)

1. `Angle::format` com 0 casas: cada linha da tabela de formato, inclusive `-000` virando `+000`
   e o traco `---`.
2. `Angle::format` com 1 casa: saida identica a de hoje (regressao).
3. `formatDeci` com 0 casas nos extremos +/-1800 e no meio-grau negativo.
4. `Parameters`: padrao 1; 0 e 1 aceitos; 2 e 255 recusados sem alterar o valor; ida e volta
   v3; bloco v1 e bloco v2 carregam com 1; v3 com campo invalido e recusado.
5. Menu: o item aparece na posicao 13; abrir mostra o valor corrente; trocar e confirmar grava;
   cancelar nao grava.
6. Edicao com o modo 0 ativo: campo de limite continua `+045,3`.
7. `NormalScreen`: tela principal e detalhe nos dois modos, com assercao literal das strings.
8. Aplicacao: valor gravado chega a tela depois de reiniciar (blob ida e volta).

## Documentacao

- `docs/ihm-estados.md`: tela do item novo.
- `DECISIONS.md`: Decisao 18, registrando o desvio do manual (secao 5.5 diz "uma casa decimal
  fixa") e as cinco decisoes acima.
- Comentario do topo de `angle.h`, que hoje cita o formato fixo.

## Fora de escopo

- Duas casas decimais (exigiria protocolo, firmware da sensora e tipo `Angle` novos).
- Mudar o batimento.
- Editar em graus inteiros.
