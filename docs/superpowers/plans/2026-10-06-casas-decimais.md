# Casas decimais da indicacao de angulo - plano de implementacao

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** opcao `Casas Decimais` no menu da UR (0 ou 1 casa) que muda toda indicacao de angulo, menos o campo em edicao.

**Architecture:** um enum `AngleDecimals` e uma funcao unica de texto (`formatDeciText`) em `angle.h`; `Angle::format` e `PresetWizard::formatDeci` passam a receber a opcao explicitamente. A opcao mora em `Parameters` (bloco v3, le v1/v2), e editada no menu no molde de `Sentido Sensor`, e chega as telas por `NormalInput::decimals` e por parametro nas telas de Preset.

**Tech Stack:** C++17 (gnu++17), PlatformIO, Unity, ESP32 (`env:esp32dev`) e host (`env:native`).

**Spec:** `docs/superpowers/specs/2026-10-06-casas-decimais-design.md`

## Global Constraints

- Opcoes: so `0` e `1` casa. Padrao de fabrica: `1`.
- Arredondamento com 0 casas: inteiro mais proximo, meio para longe do zero. Zero nunca sai `-000`.
- Largura constante: sinal + 3 digitos (`+045`), ou sinal + 3 digitos + `,` + 1 digito (`+045,0`).
- Sem leitura: `---` com 0 casas, `---,-` com 1 casa.
- Campo em edicao (`DigitEditor`) sempre com 1 casa. Nao mexer em `digit_editor.*`.
- Reles, limites, Preset, saida analogica: continuam em decimo inteiro. A opcao e so apresentacao.
- Bloco de parametros v3 (36 bytes); v1 (32) e v2 (34) continuam carregando com `1` casa.
- Nome do item: `Casas Decimais`, posicao 13 de 14, antes de `Sair`.
- Textos da IHM sem acento (decisao 12 item 16).
- Comentarios no estilo do repo: portugues sem acento, explicando o POR QUE.
- Testes: `cd ur && pio test -e native`. Se aparecer falha estranha em teste que nao foi tocado, apagar `ur/.pio/build/native` e rodar de novo (cache de header ja enganou uma vez em 2026-10-06).

## Review Focus

1. Arredondamento negativo no meio-grau: `-5` decimos com 0 casas tem de dar `-001`, nao `+000` nem `-000`. Teste na Task 1.
2. Bloco v2 gravado em campo com atraso diferente do padrao (ex.: 35) tem de carregar o atraso E receber 1 casa - um erro de offset troca os dois. Teste na Task 2.
3. Offset de Preset de +/-1800 decimos com 0 casas: `+180`/`-180`, e nao traco. Teste na Task 3.
4. Abrir o editor de limite com 0 casas ativas: o campo continua `+005,0`. Teste na Task 4.
5. Leitura sem credito (Emenda 2) e valor de limite na tela de detalhe tambem seguem a opcao - sao os caminhos de texto que a tela principal nao exercita. Teste na Task 5.

---

### Task 1: texto de angulo com 0 ou 1 casa (`angle.h`)

**Files:**
- Modify: `ur/src/domain/angle.h` (comentario do topo, novo enum, nova funcao livre, `Angle::format`)
- Test: `ur/test/native/test_angle/test_angle.cpp`

**Interfaces:**
- Produces:
  - `enum class domain::AngleDecimals : uint8_t { Zero = 0, One = 1 };`
  - `constexpr uint8_t domain::kDeciTextCap = 7;`
  - `bool domain::formatDeciText(int32_t deci, AngleDecimals decimals, char* out, uint8_t cap);` - false se `out == nullptr`, `cap < kDeciTextCap` ou parte inteira (ja arredondada) > 999.
  - `bool Angle::format(char* out, uint8_t cap, AngleDecimals decimals) const;` (o parametro e obrigatorio, sem default)

- [ ] **Step 1: Atualizar as 8 chamadas existentes e escrever os testes novos**

Em `test_angle.cpp`, acrescentar `using domain::AngleDecimals;` e `using domain::formatDeciText;` e trocar toda chamada `.format(texto, sizeof(texto))` por `.format(texto, sizeof(texto), AngleDecimals::One)` (linhas 50, 53, 56, 59, 62, 71, 80, 86). Depois acrescentar:

```cpp
// --- DECISAO 18: CASAS DECIMAIS (2026-10-06) ---------------------------------------------------
//
// Sem casa: inteiro mais proximo, meio para longe do zero. Mesma convencao da conversao da
// sensora (DECISIONS.md, Decisao 11 item 2), e erro maximo de 0,5 grau, simetrico nos sinais.

static void test_D18_sem_casa_arredonda_para_o_inteiro_mais_proximo(void) {
    struct Caso { int16_t deci; const char* texto; };
    const Caso casos[] = {
        {453, "+045"}, {445, "+045"}, {444, "+044"}, {-445, "-045"}, {-444, "-044"},
        {0, "+000"},   {900, "+090"}, {-900, "-090"}, {5, "+001"},   {-5, "-001"},
    };
    for (const Caso& c : casos) {
        char texto[Angle::kTextCap];
        TEST_ASSERT_TRUE(Angle::fromDeciDegrees(c.deci).format(texto, sizeof(texto),
                                                                AngleDecimals::Zero));
        TEST_ASSERT_EQUAL_STRING(c.texto, texto);
    }
}

// "-000" diria que a leitura e negativa quando o numero mostrado e zero.
static void test_D18_sem_casa_zero_nunca_sai_negativo(void) {
    char texto[Angle::kTextCap];
    for (int16_t v = -4; v <= 0; ++v) {
        TEST_ASSERT_TRUE(Angle::fromDeciDegrees(v).format(texto, sizeof(texto),
                                                          AngleDecimals::Zero));
        TEST_ASSERT_EQUAL_STRING("+000", texto);
    }
}

// Largura fixa: sem ela o numero danca ao cruzar o zero e o 10.
static void test_D18_sem_casa_largura_constante_na_faixa_inteira(void) {
    char texto[Angle::kTextCap];
    for (int16_t v = Angle::kMinDeciDeg; v <= Angle::kMaxDeciDeg; ++v) {
        TEST_ASSERT_TRUE(Angle::fromDeciDegrees(v).format(texto, sizeof(texto),
                                                          AngleDecimals::Zero));
        TEST_ASSERT_EQUAL_size_t(4u, strlen(texto));
    }
}

static void test_D18_sem_leitura_sem_casa_e_traco_sem_virgula(void) {
    char texto[Angle::kTextCap];
    TEST_ASSERT_TRUE(Angle::invalid().format(texto, sizeof(texto), AngleDecimals::Zero));
    TEST_ASSERT_EQUAL_STRING("---", texto);
    TEST_ASSERT_TRUE(Angle::invalid().format(texto, sizeof(texto), AngleDecimals::One));
    TEST_ASSERT_EQUAL_STRING("---,-", texto);
}

// O offset de Preset vai a +/-1800 (A9) e usa a mesma funcao. Acima de 999 graus nao cabe em
// tres digitos e a funcao recusa em vez de imprimir lixo.
static void test_D18_texto_de_decimos_cobre_o_offset_e_recusa_o_que_nao_cabe(void) {
    char texto[kDeciTextCap];
    TEST_ASSERT_TRUE(formatDeciText(1800, AngleDecimals::Zero, texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("+180", texto);
    TEST_ASSERT_TRUE(formatDeciText(-1800, AngleDecimals::One, texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-180,0", texto);
    TEST_ASSERT_TRUE(formatDeciText(9994, AngleDecimals::Zero, texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("+999", texto);
    TEST_ASSERT_FALSE(formatDeciText(9995, AngleDecimals::Zero, texto, sizeof(texto)));
    TEST_ASSERT_FALSE(formatDeciText(10000, AngleDecimals::One, texto, sizeof(texto)));
    TEST_ASSERT_FALSE(formatDeciText(0, AngleDecimals::One, nullptr, kDeciTextCap));
    TEST_ASSERT_FALSE(formatDeciText(0, AngleDecimals::One, texto, kDeciTextCap - 1));
}
```

Usar `using domain::kDeciTextCap;`. Registrar os 5 testes no `main` do arquivo com `RUN_TEST(...)`. Se `strlen` nao estiver incluido, `<string.h>` ja esta no topo.

- [ ] **Step 2: Rodar e ver falhar**

Run: `cd ur && pio test -e native -f native/test_angle`
Expected: erro de compilacao (`AngleDecimals` nao declarado).

- [ ] **Step 3: Implementar em `angle.h`**

Trocar a linha 4 do comentario do topo ("indicacao sempre em graus no formato +XXX,X com uma casa decimal fixa") por:

```cpp
// 0,1 grau. A INDICACAO sai com uma casa (+XXX,X, o formato do manual) ou sem casa (+XXX),
// escolhido no menu - Decisao 18, desvio declarado do manual. O valor guardado e sempre decimo.
```

Antes de `class Angle`, dentro de `namespace domain`:

```cpp
// Quantas casas a INDICACAO mostra (Decisao 18). So apresentacao: reles, limites, Preset e saida
// analogica continuam em decimo inteiro. Duas casas ficaram de fora de proposito - a sensora
// entrega decimo e a exatidao declarada e +/-0,09 grau, entao a segunda casa seria ruido.
enum class AngleDecimals : uint8_t {
    Zero = 0,  // "+045"
    One = 1,   // "+045,0" - o formato do manual, padrao de fabrica
};

// "+045,0" mais o terminador; o texto sem casa e mais curto e cabe no mesmo buffer.
constexpr uint8_t kDeciTextCap = 7;

// DONO UNICO do texto de um valor em decimos de grau: a leitura (Angle::format) e o offset de
// Preset (PresetWizard::formatDeci, faixa +/-1800) passam por aqui, para que os dois nunca
// arredondem diferente. Largura constante - sinal sempre presente e tres digitos inteiros - para
// o numero nao dancar ao cruzar o zero e o 10.
//
// Sem casa: inteiro mais proximo com o meio indo para longe do zero, a mesma convencao da
// conversao da sensora. O sinal acompanha o NUMERO MOSTRADO: -0,4 vira "+000", porque "-000"
// diria que ha leitura negativa onde a tela mostra zero.
inline bool formatDeciText(int32_t deci, AngleDecimals decimals, char* out, uint8_t cap) {
    if (out == nullptr || cap < kDeciTextCap) {
        return false;
    }
    const int32_t magnitude = (deci < 0) ? -deci : deci;
    const bool semCasa = (decimals == AngleDecimals::Zero);
    const int32_t inteiro = semCasa ? (magnitude + 5) / 10 : magnitude / 10;
    if (inteiro > 999) {
        return false;
    }
    const bool negativo = semCasa ? (deci < 0 && inteiro > 0) : (deci < 0);
    out[0] = negativo ? '-' : '+';
    out[1] = static_cast<char>('0' + (inteiro / 100));
    out[2] = static_cast<char>('0' + ((inteiro / 10) % 10));
    out[3] = static_cast<char>('0' + (inteiro % 10));
    if (semCasa) {
        out[4] = '\0';
        return true;
    }
    out[4] = ',';
    out[5] = static_cast<char>('0' + (magnitude % 10));
    out[6] = '\0';
    return true;
}
```

Em `class Angle`: `kTextCap = kTextLen + 1;` continua; acrescentar logo abaixo
`static_assert(kTextCap == kDeciTextCap, "Angle e formatDeciText tem de concordar no buffer");`.

Substituir `format` inteiro por:

```cpp
    // Escreve a leitura com a quantidade de casas pedida, ou o traco quando nao ha leitura -
    // "---,-" ou "---", na mesma largura do numero. Devolve false sem tocar no buffer se ele
    // nao couber.
    bool format(char* out, uint8_t cap, AngleDecimals decimals) const {
        if (out == nullptr || cap < kTextCap) {
            return false;
        }
        if (!valid_) {
            out[0] = '-';
            out[1] = '-';
            out[2] = '-';
            if (decimals == AngleDecimals::Zero) {
                out[3] = '\0';
                return true;
            }
            out[3] = ',';
            out[4] = '-';
            out[5] = '\0';
            return true;
        }
        return formatDeciText(deci_, decimals, out, cap);
    }
```

- [ ] **Step 4: Rodar e ver passar**

Run: `cd ur && pio test -e native -f native/test_angle`
Expected: PASS. (Os outros suites ainda nao compilam - `normal_screen.cpp` e `application.cpp` chamam `format` com 2 argumentos; isso fecha nas Tasks 3 e 5.)

Para nao deixar o build quebrado entre commits, nesta mesma task fazer a troca MECANICA nos chamadores de producao, passando `AngleDecimals::One` (comportamento de hoje):
- `ur/src/domain/ui/normal_screen.cpp:69`: `angle.format(texto, Angle::kTextCap, AngleDecimals::One)` (com `using domain::AngleDecimals;` ou qualificado).
- `ur/src/app/application.cpp:776`: `preset.lastRaw(eixos[i]).format(campo, domain::Angle::kTextCap, domain::AngleDecimals::One);`

Run: `cd ur && pio test -e native`
Expected: tudo PASS.

- [ ] **Step 5: Commit**

```bash
git add ur/src/domain/angle.h ur/test/native/test_angle/test_angle.cpp ur/src/domain/ui/normal_screen.cpp ur/src/app/application.cpp
git commit -m "feat(ur): texto de angulo com 0 ou 1 casa - dono unico em formatDeciText"
```

---

### Task 2: `Parameters` guarda a opcao (bloco v3)

**Files:**
- Modify: `ur/src/domain/parameters.h` (constantes de versao/tamanho, getter/setter/validador, `RelayGroup`)
- Modify: `ur/src/domain/parameters.cpp` (offset novo, construtor, `serializeParams`, `loadParams`)
- Test: `ur/test/native/test_parameters/test_parameters.cpp`

**Interfaces:**
- Consumes: `domain::AngleDecimals` (Task 1).
- Produces:
  - `static constexpr uint16_t Parameters::kParamVersion = 3;`
  - `static constexpr uint16_t Parameters::kParamVersionV2 = 2;` / `kParamBlobSizeV2 = 34;`
  - `static constexpr uint16_t Parameters::kParamBlobSize = 36;`
  - `static constexpr AngleDecimals Parameters::kDefaultDisplayDecimals = AngleDecimals::One;`
  - `AngleDecimals Parameters::displayDecimals() const;`
  - `Status Parameters::setDisplayDecimals(AngleDecimals decimals);` - `Err::Range` fora de 0/1, sem alterar o valor.
  - `static constexpr bool Parameters::displayDecimalsValid(uint16_t raw);`

- [ ] **Step 1: Testes que falham**

No teste existente `test_bloco_da_versao_1_continua_carregando` (por volta da linha 970), depois da assercao do atraso, acrescentar:

```cpp
    TEST_ASSERT_TRUE_MESSAGE(p.displayDecimals() == AngleDecimals::One,
                             "bloco v1 tem de herdar uma casa, o formato que a placa sempre teve");
```

Acrescentar `using domain::AngleDecimals;` no topo e os testes:

```cpp
// --- DECISAO 18: CASAS DECIMAIS (2026-10-06) ---------------------------------------------------

static void test_D18_padrao_de_fabrica_e_uma_casa(void) {
    TEST_ASSERT_TRUE(Parameters::factoryDefaults().displayDecimals() == AngleDecimals::One);
    TEST_ASSERT_TRUE(Parameters().displayDecimals() == AngleDecimals::One);
}

static void test_D18_aceita_zero_e_uma_casa_e_recusa_o_resto(void) {
    Parameters p;
    TEST_ASSERT_TRUE(p.setDisplayDecimals(AngleDecimals::Zero).ok());
    TEST_ASSERT_TRUE(p.displayDecimals() == AngleDecimals::Zero);
    TEST_ASSERT_TRUE(p.setDisplayDecimals(static_cast<AngleDecimals>(2)).failed());
    TEST_ASSERT_TRUE(p.setDisplayDecimals(static_cast<AngleDecimals>(255)).failed());
    TEST_ASSERT_TRUE_MESSAGE(p.displayDecimals() == AngleDecimals::Zero,
                             "recusa nao pode mexer no valor que estava");
    TEST_ASSERT_TRUE(p.setDisplayDecimals(AngleDecimals::One).ok());
    TEST_ASSERT_TRUE(p.displayDecimals() == AngleDecimals::One);
}

static void test_D18_casas_sobrevivem_a_gravacao_e_a_leitura(void) {
    Parameters origem;
    TEST_ASSERT_TRUE(origem.setDisplayDecimals(AngleDecimals::Zero).ok());
    TEST_ASSERT_TRUE(origem.setAlarmDelayDeciS(35).ok());

    uint8_t blob[Parameters::kParamBlobSize];
    uint16_t n = 0;
    TEST_ASSERT_TRUE(origem.serializeParams(blob, sizeof(blob), n).ok());
    TEST_ASSERT_EQUAL_UINT16(36u, n);
    TEST_ASSERT_EQUAL_UINT8(3u, blob[4]);

    Parameters destino;
    TEST_ASSERT_TRUE(destino.loadParams(blob, n).ok());
    TEST_ASSERT_TRUE(destino.displayDecimals() == AngleDecimals::Zero);
    TEST_ASSERT_EQUAL_UINT16(35u, destino.alarmDelayDeciS());
}

// Toda placa gravada pelo firmware de 2026-09-17 tem um bloco v2 de 34 bytes. Recusa-lo levaria a
// frota a CONFIG PERDIDA. O atraso diferente do padrao pega erro de offset entre os dois campos.
static void test_D18_bloco_v2_carrega_o_atraso_e_recebe_uma_casa(void) {
    Parameters origem;
    TEST_ASSERT_TRUE(origem.setAlarmDelayDeciS(35).ok());
    uint8_t v3[Parameters::kParamBlobSize];
    uint16_t n = 0;
    TEST_ASSERT_TRUE(origem.serializeParams(v3, sizeof(v3), n).ok());

    // v2 = os mesmos 32 primeiros bytes, versao 2, CRC sobre 0..31 nos bytes 32..33.
    uint8_t v2[34];
    memcpy(v2, v3, 32);
    v2[4] = 2;
    v2[5] = 0;
    const uint16_t crc = crc16Modbus(v2, 32);
    v2[32] = static_cast<uint8_t>(crc & 0xFFu);
    v2[33] = static_cast<uint8_t>((crc >> 8) & 0xFFu);

    Parameters p;
    TEST_ASSERT_TRUE(p.setDisplayDecimals(AngleDecimals::Zero).ok());   // prova que o load escreve
    TEST_ASSERT_TRUE_MESSAGE(p.loadParams(v2, sizeof(v2)).ok(), "bloco v2 recusado");
    TEST_ASSERT_EQUAL_UINT16(35u, p.alarmDelayDeciS());
    TEST_ASSERT_TRUE(p.displayDecimals() == AngleDecimals::One);
}

// v3 com o campo fora de 0/1 e defeito de gravacao, nao formato novo: recusa sem carga parcial.
static void test_D18_bloco_v3_com_casas_invalidas_e_recusado_inteiro(void) {
    Parameters origem;
    TEST_ASSERT_TRUE(origem.setAlarmDelayDeciS(35).ok());
    uint8_t blob[Parameters::kParamBlobSize];
    uint16_t n = 0;
    TEST_ASSERT_TRUE(origem.serializeParams(blob, sizeof(blob), n).ok());
    blob[32] = 2;
    blob[33] = 0;
    const uint16_t crc = crc16Modbus(blob, 34);
    blob[34] = static_cast<uint8_t>(crc & 0xFFu);
    blob[35] = static_cast<uint8_t>((crc >> 8) & 0xFFu);

    Parameters p;
    TEST_ASSERT_TRUE(p.loadParams(blob, n).failed());
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(Parameters::kDefaultAlarmDelayDeciS, p.alarmDelayDeciS(),
                                     "recusa nao pode deixar o atraso do bloco no agregado");
    TEST_ASSERT_TRUE(p.displayDecimals() == AngleDecimals::One);
}
```

Registrar os 5 com `RUN_TEST`.

- [ ] **Step 2: Rodar e ver falhar**

Run: `cd ur && pio test -e native -f native/test_parameters`
Expected: erro de compilacao (`displayDecimals` nao existe).

- [ ] **Step 3: Implementar**

`parameters.h`: incluir `"domain/angle.h"` se ainda nao estiver (ja esta, `Angle` e usado). Substituir o bloco de versoes (linhas ~86-91) por:

```cpp
    static constexpr uint16_t kParamVersion = 3;
    static constexpr uint16_t kParamVersionLegado = 1;
    static constexpr uint16_t kParamBlobSizeLegado = 32;
    // v2 (2026-09-17, atraso de alarme). Continua carregando pelo mesmo motivo da v1.
    static constexpr uint16_t kParamVersionV2 = 2;
    static constexpr uint16_t kParamBlobSizeV2 = 34;
    static constexpr uint16_t kCalVersion = 1;
    static constexpr uint16_t kParamBlobSize = 36;
    static constexpr uint16_t kCalBlobSize = 20;
```

Atualizar o comentario logo acima ("Um bloco v1 e carregado...") para dizer que v1 e v2 carregam, com os campos que nao tinham no valor de fabrica.

Junto das constantes do atraso:

```cpp
    // CASAS DECIMAIS DA INDICACAO (Decisao 18, 2026-10-06). So apresentacao. O padrao e uma
    // casa, o formato do manual: placa atualizada nao muda nada ate alguem mexer.
    static constexpr AngleDecimals kDefaultDisplayDecimals = AngleDecimals::One;
```

Junto de `alarmDelayValid`:

```cpp
    AngleDecimals displayDecimals() const { return static_cast<AngleDecimals>(rel_.displayDecimals); }
    Status setDisplayDecimals(AngleDecimals decimals);

    static constexpr bool displayDecimalsValid(uint16_t raw) {
        return raw <= static_cast<uint16_t>(AngleDecimals::One);
    }
```

Em `RelayGroup`, depois de `alarmDelayDeciS`: `uint16_t displayDecimals;`

`parameters.cpp`:
- Depois de `kOffAlarmDelay`: 
  ```cpp
  // Campo da versao 3. Mesmo movimento da v2: ocupa o lugar do CRC antigo e o CRC anda dois bytes.
  constexpr uint16_t kOffDisplayDecimals = 32;
  ```
- Construtor: `rel_.displayDecimals = static_cast<uint16_t>(kDefaultDisplayDecimals);`
- `serializeParams`, antes de `sign`: `put16(dst + kOffDisplayDecimals, rel_.displayDecimals);`
- Setter, junto de `setAlarmDelayDeciS`:
  ```cpp
  Status Parameters::setDisplayDecimals(AngleDecimals decimals) {
      const uint16_t raw = static_cast<uint16_t>(decimals);
      if (!displayDecimalsValid(raw)) {
          return Err::Range;
      }
      rel_.displayDecimals = raw;
      return kOk;
  }
  ```
- `loadParams`: trocar o comentario do topo ("ACEITA OS DOIS FORMATOS") para "ACEITA OS TRES FORMATOS" mantendo o argumento, e trocar a escolha de tamanho/versao por:
  ```cpp
      const uint16_t versaoLida = get16(src + kOffVersion);
      uint16_t tamanho = kParamBlobSize;
      if (versaoLida == kParamVersionLegado) {
          tamanho = kParamBlobSizeLegado;
      } else if (versaoLida == kParamVersionV2) {
          tamanho = kParamBlobSizeV2;
      }
      // Versao desconhecida cai no envelope da atual e e recusada la (Crc ou Unsupported).
      const uint16_t versao = (tamanho == kParamBlobSize) ? kParamVersion : versaoLida;
  ```
  e, no fim, no lugar do bloco do atraso:
  ```cpp
      // Campo que o bloco nao tem assume o valor de fabrica - o comportamento que aquela placa
      // ja tinha.
      lido.alarmDelayDeciS = (versaoLida == kParamVersionLegado) ? kDefaultAlarmDelayDeciS
                                                                 : get16(src + kOffAlarmDelay);
      if (!alarmDelayValid(lido.alarmDelayDeciS)) {
          return Err::Range;
      }
      lido.displayDecimals = (tamanho == kParamBlobSize)
                                 ? get16(src + kOffDisplayDecimals)
                                 : static_cast<uint16_t>(kDefaultDisplayDecimals);
      if (!displayDecimalsValid(lido.displayDecimals)) {
          return Err::Range;
      }
  ```
- Atualizar o mapa de bytes no comentario do topo do arquivo (registro de parametros) com `off 32 uint16 displayDecimals` e o CRC em 34.

- [ ] **Step 4: Rodar e ver passar**

Run: `cd ur && pio test -e native`
Expected: tudo PASS (inclusive `test_persist`, que grava e le blobs pelo fake de 48 bytes).

- [ ] **Step 5: Commit**

```bash
git add ur/src/domain/parameters.h ur/src/domain/parameters.cpp ur/test/native/test_parameters/test_parameters.cpp
git commit -m "feat(ur): parametro de casas decimais - bloco v3, v1 e v2 continuam carregando"
```

---

### Task 3: telas e textos do Preset seguem a opcao

**Files:**
- Modify: `ur/src/domain/ui/preset_wizard.h:300-316`, `ur/src/domain/ui/preset_wizard.cpp:411-528`
- Modify: `ur/src/app/application.h:390-395`, `ur/src/app/application.cpp:738-790`
- Modify: `ur/src/main.cpp:688` e `:776`
- Modify: `ur/src/domain/ui/normal_screen.cpp:76-85` (chamada de `formatDeci`)
- Test: `ur/test/native/test_preset/test_preset.cpp`, `ur/test/native/test_appscreens/test_appscreens.cpp`

**Interfaces:**
- Consumes: `formatDeciText`, `AngleDecimals` (Task 1); `Parameters::displayDecimals()` (Task 2).
- Produces:
  - `static bool PresetWizard::formatDeci(int16_t deci, AngleDecimals decimals, char* out, uint8_t cap);`
  - `bool PresetWizard::formatPendingConfirm(Axis axis, AngleDecimals decimals, char* out, uint8_t cap) const;`
  - `static bool PresetWizard::formatIndicator(Axis axis, const Parameters& params, char* out, uint8_t cap);` - assinatura igual, passa a usar `params.displayDecimals()`.
  - `void app::renderPresetCapture(IDisplay&, const PresetWizard&, Axis, domain::AngleDecimals);`
  - `void app::renderPresetConfirm(IDisplay&, const PresetWizard&, Axis, domain::AngleDecimals);`

- [ ] **Step 1: Ajustar chamadas e escrever os testes**

Em `test_preset.cpp`: `formatPendingConfirm(Axis::X, tela, sizeof(tela))` vira `formatPendingConfirm(Axis::X, AngleDecimals::One, tela, sizeof(tela))` (linhas 497, 521, 529, 538); `app::renderPresetCapture(..., eixo)` ganha `, AngleDecimals::One` (linhas 810, 930, 946, 958). Em `test_appscreens.cpp:121`: `app::renderPresetConfirm(tela, preset, Axis::X, domain::AngleDecimals::One);`.

Testes novos em `test_preset.cpp` (com `using domain::AngleDecimals;`):

```cpp
// --- DECISAO 18 ---------------------------------------------------------------------------------

// A9 deixa o offset ir a +/-1800 decimos. Sem casa, os extremos continuam numero, nao traco.
static void test_D18_offset_sem_casa_nos_extremos_e_no_meio_grau_negativo(void) {
    char texto[PresetWizard::kValueTextCap];
    TEST_ASSERT_TRUE(PresetWizard::formatDeci(1800, AngleDecimals::Zero, texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("+180", texto);
    TEST_ASSERT_TRUE(PresetWizard::formatDeci(-1800, AngleDecimals::Zero, texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-180", texto);
    TEST_ASSERT_TRUE(PresetWizard::formatDeci(-15, AngleDecimals::Zero, texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-002", texto);
    TEST_ASSERT_TRUE(PresetWizard::formatDeci(-15, AngleDecimals::One, texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-001,5", texto);
}

static void test_D18_indicador_de_pset_segue_a_opcao_gravada(void) {
    Parameters params = Parameters::factoryDefaults();
    TEST_ASSERT_TRUE(params.setPresetOffset(Axis::X, 123).ok());
    char tela[PresetWizard::kIndicatorTextCap];

    TEST_ASSERT_TRUE(PresetWizard::formatIndicator(Axis::X, params, tela, sizeof(tela)));
    TEST_ASSERT_EQUAL_STRING("PSET X:+012,3", tela);

    TEST_ASSERT_TRUE(params.setDisplayDecimals(AngleDecimals::Zero).ok());
    TEST_ASSERT_TRUE(PresetWizard::formatIndicator(Axis::X, params, tela, sizeof(tela)));
    TEST_ASSERT_EQUAL_STRING("PSET X:+012", tela);
}

static void test_D18_tela_de_captura_sem_casa(void) {
    TelaPreset t;
    TEST_ASSERT_TRUE(t.preset.beginCapture(Axis::X));
    t.preset.sample(Angle::fromDeciDegrees(37), Angle::fromDeciDegrees(-12));
    app::renderPresetCapture(t.tela, t.preset, Axis::X, AngleDecimals::Zero);

    TEST_ASSERT_TRUE(t.tela.showsExactly("X:+004 Y:-001"));
    verificarQuadroPreset(t.tela);
}

static void test_D18_tela_de_captura_sem_leitura_e_sem_casa_mostra_traco_curto(void) {
    TelaPreset t;
    TEST_ASSERT_TRUE(t.preset.beginCapture(Axis::X));
    t.preset.sample(Angle::invalid(), Angle::invalid());
    app::renderPresetCapture(t.tela, t.preset, Axis::X, AngleDecimals::Zero);

    TEST_ASSERT_TRUE(t.tela.showsExactly("X:--- Y:---"));
}
```

Para a confirmacao, copiar o preparo de `test_appscreens.cpp:100-119` (captura de 300 decimos ate `NeedsConfirm`) num teste novo em `test_appscreens.cpp`:

```cpp
static void test_D18_confirmacao_de_pset_sem_casa_nao_tem_virgula(void) {
    FakeClock relogio;
    FakeDisplay tela;
    PresetWizard preset(relogio);
    Parameters params = Parameters::factoryDefaults();
    TEST_ASSERT_TRUE(preset.beginCapture(Axis::X));
    preset.cancelCapture();
    preset.onProgrammingExit();
    const uint32_t passo = 50;
    for (uint32_t t = 0; t <= PresetWizard::kStaticHoldMs + passo; t += passo) {
        preset.sample(domain::Angle::fromDeciDegrees(300), domain::Angle::fromDeciDegrees(0));
        relogio.advanceMs(passo);
    }
    TEST_ASSERT_TRUE(preset.requestPset(params) == domain::ui::PsetOutcome::NeedsConfirm);

    char linha[PresetWizard::kConfirmTextCap];
    TEST_ASSERT_TRUE(preset.formatPendingConfirm(Axis::X, domain::AngleDecimals::Zero, linha,
                                                 sizeof(linha)));
    TEST_ASSERT_NULL(strchr(linha, ','));
    TEST_ASSERT_TRUE(strstr(linha, "030") != nullptr);

    app::renderPresetConfirm(tela, preset, Axis::X, domain::AngleDecimals::Zero);
    TEST_ASSERT_TRUE(tela.showsExactly(linha));
    conferirQuadro(tela);
}
```

(`#include <string.h>` se o arquivo ainda nao tiver.) Registrar todos com `RUN_TEST`.

- [ ] **Step 2: Rodar e ver falhar**

Run: `cd ur && pio test -e native -f native/test_preset -f native/test_appscreens`
Expected: erro de compilacao nas assinaturas novas.

- [ ] **Step 3: Implementar**

`preset_wizard.h`: trocar as declaracoes de `formatDeci` e `formatPendingConfirm` pelas da secao Interfaces; acrescentar `static_assert(kValueTextCap == kDeciTextCap, "...")` junto de `kValueTextCap`.

`preset_wizard.cpp`:

```cpp
bool PresetWizard::formatDeci(int16_t deci, AngleDecimals decimals, char* out, uint8_t cap) {
    if (out == nullptr || cap < kValueTextCap) {
        return false;
    }
    return formatDeciText(deci, decimals, out, cap);
}
```

Em `formatIndicator`: `formatDeci(offset, params.displayDecimals(), out + pos, ...)`.
Em `formatPendingConfirm`: novo parametro `AngleDecimals decimals`, repassado a `formatDeci`.

`normal_screen.cpp:80`: `ui::PresetWizard::formatDeci(deci, AngleDecimals::One, texto, ...)` por enquanto (a Task 5 troca por `in.decimals`).

`application.h/.cpp`: `renderPresetCapture` e `renderPresetConfirm` ganham `domain::AngleDecimals decimals` como ultimo parametro; `renderPresetConfirm` repassa a `formatPendingConfirm(axis, decimals, linha, sizeof(linha))`; `renderPresetCapture` usa `.format(campo, domain::Angle::kTextCap, decimals)` na linha 776 (troca o `AngleDecimals::One` da Task 1).

`main.cpp:688`: `app::renderPresetCapture(g_display, g_preset, g_presetAxis, g_params.displayDecimals());`
`main.cpp:776`: `app::renderPresetConfirm(g_display, g_preset, g_presetAxis, g_params.displayDecimals());`

- [ ] **Step 4: Rodar e ver passar**

Run: `cd ur && pio test -e native`
Expected: tudo PASS.

Run: `cd ur && pio run -e esp32dev`
Expected: SUCCESS (main.cpp so compila no alvo).

- [ ] **Step 5: Commit**

```bash
git add ur/src/domain/ui/preset_wizard.h ur/src/domain/ui/preset_wizard.cpp ur/src/app/application.h ur/src/app/application.cpp ur/src/main.cpp ur/src/domain/ui/normal_screen.cpp ur/test/native/test_preset/test_preset.cpp ur/test/native/test_appscreens/test_appscreens.cpp
git commit -m "feat(ur): textos e telas do Preset seguem as casas decimais gravadas"
```

---

### Task 4: item `Casas Decimais` no menu

**Files:**
- Modify: `ur/src/domain/ui/menu_machine.h` (`MenuItem`, `MenuState`, `kItemCount`, constantes de texto, `onEditDecimais`, `openDecimais`, `decSel_`)
- Modify: `ur/src/domain/ui/menu_machine.cpp` (`kNomeItem`, despacho de gesto, `openItem`, render, construtor)
- Test: `ur/test/native/test_menu/test_menu.cpp`

**Interfaces:**
- Consumes: `Parameters::displayDecimals()`, `Parameters::setDisplayDecimals()` (Task 2).
- Produces:
  - `MenuItem::CasasDecimais = 12`, `MenuItem::Sair = 13`, `MenuMachine::kItemCount = 14`
  - `MenuState::EditDecimais`
  - `static constexpr const char* MenuMachine::kRotuloDecimais = "Casas Decimais:";`
  - `static constexpr const char* MenuMachine::kOpcaoSemCasa = "0 (+045)";`
  - `static constexpr const char* MenuMachine::kOpcaoUmaCasa = "1 (+045,0)";`

- [ ] **Step 1: Testes que falham**

Em `kOrdemDoManual` (linha ~84), inserir `"Casas Decimais",` entre `"Atraso Alarme"` e `"Sair"`, e acrescentar ao comentario acima: "E o DECIMO QUARTO, "Casas Decimais", entrou em 2026-10-06 pelo mesmo criterio."

Testes novos (com `using domain::AngleDecimals;`):

```cpp
// --- CASAS DECIMAIS (Decisao 18, 2026-10-06) ----------------------------------------------------

static void test_D18_casas_decimais_abre_no_valor_corrente(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::CasasDecimais);
    TEST_ASSERT_EQUAL_STRING("Casas Decimais", selecionado(b));

    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::EditDecimais), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly(MenuMachine::kRotuloDecimais));
    TEST_ASSERT_TRUE(b.tela.showsExactly(MenuMachine::kOpcaoUmaCasa));
}

static void test_D18_trocar_para_sem_casa_so_vale_na_saida_confirmada(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::CasasDecimais);
    toque(b, Key::Menu);

    toque(b, Key::Up);
    TEST_ASSERT_TRUE(b.tela.showsExactly(MenuMachine::kOpcaoSemCasa));
    hold(b);
    esperar(b, MenuMachine::kGravOkMs);
    TEST_ASSERT_TRUE_MESSAGE(b.menu.pendingConfig(), "trocar tem de marcar configuracao pendente");
    TEST_ASSERT_TRUE_MESSAGE(b.ativo.displayDecimals() == AngleDecimals::One,
                             "o agregado ativo so muda na confirmacao da saida");

    descerAte(b, MenuItem::Sair);
    toque(b, Key::Menu);
    hold(b);   // NOVA CONFIG - CONFIRMA?
    TEST_ASSERT_TRUE(b.ativo.displayDecimals() == AngleDecimals::Zero);
}

static void test_D18_confirmar_o_mesmo_valor_nao_cria_pendencia(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::CasasDecimais);
    toque(b, Key::Menu);
    hold(b);
    esperar(b, MenuMachine::kGravOkMs);
    TEST_ASSERT_FALSE(b.menu.pendingConfig());
}

static void test_D18_sair_por_inatividade_descarta_a_troca(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::CasasDecimais);
    toque(b, Key::Menu);
    toque(b, Key::Up);
    hold(b);
    esperar(b, MenuMachine::kGravOkMs);
    esperar(b, MenuMachine::kTimeoutMs + 1000u);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.ativo.displayDecimals() == AngleDecimals::One);
}

// Decisao 18 item 4: o campo em edicao SEMPRE mostra a casa. Ninguem altera o ponto de atuacao de
// um rele sem ver o decimo.
static void test_D18_editor_de_limite_continua_com_uma_casa_no_modo_sem_casa(void) {
    Bancada b;
    TEST_ASSERT_TRUE(b.ativo.setDisplayDecimals(AngleDecimals::Zero).ok());
    entrarNoMenu(b);
    descerAte(b, MenuItem::Limite1);
    toque(b, Key::Menu);
    toque(b, Key::Down);
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::EditValor), code(b.menu.state()));
    TEST_ASSERT_TRUE(mostraTelaDeValor(b, "Valor Limite X1(graus):+005,0"));
}
```

Registrar os 5 com `RUN_TEST`. Se o teste de inatividade nao chegar a `Normal` (o timeout pode morar em `Password` e precisar de `bombear`), copiar o padrao exato de `test_REQ_PRG_03_timeout_de_120_s_sem_tecla_volta_ao_modo_normal` (linha ~507) - o que importa e a ultima assercao.

- [ ] **Step 2: Rodar e ver falhar**

Run: `cd ur && pio test -e native -f native/test_menu`
Expected: erro de compilacao (`MenuItem::CasasDecimais`).

- [ ] **Step 3: Implementar**

`menu_machine.h`:
- `MenuItem`: depois de `AtrasoAlarme = 11,`:
  ```cpp
      // ITEM 13 DE 14, acrescentado em 2026-10-06 (Decisao 18). Quantas casas a INDICACAO de
      // angulo mostra: 0 ou 1. So apresentacao - reles, limites e Preset continuam em decimo, e o
      // campo em edicao continua com a casa para ninguem mexer num ponto de atuacao sem ve-lo.
      CasasDecimais = 12,
      Sair = 13,
  ```
- `MenuState`: depois de `EditAtraso,`: `EditDecimais,    // Decisao 18: escolha entre "0 (+045)" e "1 (+045,0)"`
- `kItemCount = 14;`
- Constantes, junto de `kPrefixoAtraso`:
  ```cpp
      static constexpr const char* kRotuloDecimais = "Casas Decimais:";
      static constexpr const char* kOpcaoSemCasa = "0 (+045)";
      static constexpr const char* kOpcaoUmaCasa = "1 (+045,0)";
  ```
- Privados: `void onEditDecimais(const Gesture& gesture);`, `void openDecimais();`, membro `uint8_t decSel_;` junto de `dirSel_`.

`menu_machine.cpp`:
- `kNomeItem`: inserir `"Casas Decimais",` antes de `"Sair"`.
- Construtor: inicializar `decSel_(0)` na mesma posicao relativa em que `dirSel_` e inicializado.
- Despacho (linha ~241): `case MenuState::EditDecimais: onEditDecimais(gesture); break;`
- `openItem` (linha ~659): `case MenuItem::CasasDecimais: openDecimais(); break;`
- Novo handler, depois de `onEditAtraso`:
  ```cpp
  // Mesmo molde de Sentido do Sensor: duas opcoes, UP e DOWN escolhem, hold de MENU grava no
  // rascunho. Sem aviso temporizado - trocar o formato nao desloca ponto de atuacao nenhum.
  void MenuMachine::onEditDecimais(const Gesture& gesture) {
      if (gesture.key == Key::Menu && gesture.kind == GestureKind::Hold) {
          const AngleDecimals escolhido = static_cast<AngleDecimals>(decSel_);
          if (escolhido != draft_.displayDecimals()) {
              if (!gravacaoAceita(draft_.setDisplayDecimals(escolhido))) {
                  return;
              }
              pending_ = true;
          }
          gravado();
          return;
      }
      if (gesture.kind != GestureKind::ShortTap) {
          return;
      }
      if (gesture.key == Key::Up && decSel_ > 0) {
          decSel_ = 0;
          dirty_ = true;
      } else if (gesture.key == Key::Down && decSel_ == 0) {
          decSel_ = 1;
          dirty_ = true;
      }
  }
  ```
- Nova transicao, depois de `openAtraso`:
  ```cpp
  void MenuMachine::openDecimais() {
      editReturn_ = MenuState::Menu;
      decSel_ = static_cast<uint8_t>(draft_.displayDecimals());
      state_ = MenuState::EditDecimais;
      dirty_ = true;
  }
  ```
- Render, depois do `case MenuState::EditAtraso`:
  ```cpp
          case MenuState::EditDecimais: {
              const char* opcao = (decSel_ == 0u) ? kOpcaoSemCasa : kOpcaoUmaCasa;
              drawLine(kRotuloY, kRotuloDecimais, contentFont(kRotuloDecimais));
              drawLine(kConteudoY, opcao, contentFont(opcao));
              break;
          }
  ```
- Rodar `grep -n "EditAtraso\|EditSentido" ur/src/domain/ui/menu_machine.cpp` e conferir se ha outro `switch`/lista de estados (timeout, `isEditing`, etc.) onde `EditDecimais` tambem precisa entrar; acrescentar onde `EditSentido` estiver.

- [ ] **Step 4: Rodar e ver passar**

Run: `cd ur && pio test -e native`
Expected: tudo PASS (inclusive `test_REQ_PRG_01_os_itens_na_ordem_do_manual_mais_o_rearme`, que confere a lista inteira).

- [ ] **Step 5: Commit**

```bash
git add ur/src/domain/ui/menu_machine.h ur/src/domain/ui/menu_machine.cpp ur/test/native/test_menu/test_menu.cpp
git commit -m "feat(ur): item Casas Decimais no menu, 0 ou 1, no molde de Sentido Sensor"
```

---

### Task 5: tela principal e de detalhe seguem a opcao

**Files:**
- Modify: `ur/src/domain/ui/normal_screen.h` (`NormalInput::decimals`)
- Modify: `ur/src/domain/ui/normal_screen.cpp` (`Line::add(Angle)`, `addPresetOffset`, todos os chamadores)
- Modify: `ur/src/app/application.cpp:33-75` (`buildNormalInput`)
- Modify: `ur/src/main.cpp:168` (static_assert)
- Test: `ur/test/native/test_normal/test_normal.cpp`, `ur/test/native/test_application/test_application.cpp`

**Interfaces:**
- Consumes: `AngleDecimals`, `Angle::format(..., AngleDecimals)` (Task 1); `PresetWizard::formatDeci(..., AngleDecimals, ...)` (Task 3); `Parameters::displayDecimals()` (Task 2).
- Produces: `AngleDecimals NormalInput::decimals = AngleDecimals::One;` (default member initializer - `NormalInput entrada{}` nos testes continua em uma casa).

- [ ] **Step 1: Testes que falham**

Em `test_normal.cpp` (com `using domain::AngleDecimals;`):

```cpp
// --- DECISAO 18: CASAS DECIMAIS ----------------------------------------------------------------

static void test_D18_tela_principal_sem_casa(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);
    entrada.decimals = AngleDecimals::Zero;

    bancada.ciclo(entrada);

    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X:+046"));   // 45,5: meio vai para longe do zero
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("Y:-012"));
    TEST_ASSERT_FALSE(bancada.painel.shows(","));
    verificarQuadro(bancada.painel);
}

// A tela de detalhe tem tres caminhos de angulo: a leitura, o valor dos dois limites e o offset
// de Preset. Os tres seguem a opcao.
static void test_D18_detalhe_sem_casa_leitura_limites_e_pset(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);
    entrada.decimals = AngleDecimals::Zero;
    entrada.presetActive[kNormalAxisX] = true;
    entrada.presetOffsetDeci[kNormalAxisX] = -120;

    tocar(bancada, Key::Down);
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailX);

    TEST_ASSERT_TRUE(bancada.painel.showsExactly("+046"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X1:-- +050"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X2:-- +050"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("PSET X:-012"));
    TEST_ASSERT_FALSE(bancada.painel.shows(","));
    verificarQuadro(bancada.painel);
}

// Emenda 2: a leitura sem credito tambem e indicacao de angulo.
static void test_D18_leitura_marcada_sem_casa(void) {
    Bancada bancada;
    NormalInput entrada = enlaceEmFalha(NormalLinkState::SensorFault);
    entrada.decimals = AngleDecimals::Zero;
    entrada.unqualified[kNormalAxisX] = Angle::fromDeciDegrees(495);
    entrada.unqualified[kNormalAxisY] = Angle::fromDeciDegrees(9);

    bancada.ciclo(entrada);

    TEST_ASSERT_TRUE(bancada.painel.shows("+050"));
    TEST_ASSERT_TRUE(bancada.painel.shows("+001"));
    TEST_ASSERT_FALSE(bancada.painel.shows("+049,5"));
    verificarQuadro(bancada.painel);
}

static void test_D18_sem_quadro_e_sem_casa_mostra_traco_curto(void) {
    Bancada bancada;
    NormalInput entrada = enlaceEmFalha(NormalLinkState::CommFault);
    entrada.decimals = AngleDecimals::Zero;

    bancada.ciclo(entrada);

    TEST_ASSERT_TRUE(bancada.painel.shows("---"));
    TEST_ASSERT_FALSE(bancada.painel.shows("---,-"));
    verificarQuadro(bancada.painel);
}
```

Se `showsExactly("X1:-- +050")` nao bater por causa do texto exato da linha de limite do detalhe (montada em `normal_screen.cpp:366-372`), rodar uma vez com 1 casa, ver o literal com `+050,0` e trocar so o sufixo - o formato da linha nao muda nesta task.

Em `test_application.cpp`, no fim de `test_buildNormalInput_leva_todo_campo_do_snapshot_para_a_tela`:

```cpp
    // Decisao 18: a opcao gravada tem de atravessar, senao o menu grava e a tela ignora.
    TEST_ASSERT_TRUE(in.decimals == domain::AngleDecimals::One);
    domain::Parameters semCasa = domain::Parameters::factoryDefaults();
    TEST_ASSERT_TRUE(semCasa.setDisplayDecimals(domain::AngleDecimals::Zero).ok());
    TEST_ASSERT_TRUE(app::buildNormalInput(snap, semCasa).decimals == domain::AngleDecimals::Zero);
```

Registrar os 4 testes novos de `test_normal.cpp` com `RUN_TEST`.

- [ ] **Step 2: Rodar e ver falhar**

Run: `cd ur && pio test -e native -f native/test_normal -f native/test_application`
Expected: erro de compilacao (`NormalInput::decimals`).

- [ ] **Step 3: Implementar**

`normal_screen.h`, no fim de `NormalInput` (depois de `heartbeatPhase`):

```cpp
    // Decisao 18: quantas casas a indicacao de angulo mostra. Vem de Parameters por
    // buildNormalInput. O default e o formato do manual, para que um NormalInput montado a mao
    // (testes) continue com uma casa.
    AngleDecimals decimals = AngleDecimals::One;
```

`normal_screen.cpp`:
- `Line::add(const Angle&)` vira `add(const Angle& angle, AngleDecimals decimals)` e chama `angle.format(texto, Angle::kTextCap, decimals)`.
- `addPresetOffset(int16_t deci)` vira `addPresetOffset(int16_t deci, AngleDecimals decimals)`; repassa a `formatDeci` e ao traco de fallback.
- `addPercent`: o traco de porcentagem fora de faixa NAO e angulo; manter `add(Angle::invalid(), AngleDecimals::One)` com o comentario "traco de porcentagem: largura da coluna de hoje, independente da opcao de angulo".
- Todo chamador de `add(<Angle>)` e `addPresetOffset(...)` passa `in.decimals` (linhas ~216, 221, 361, 372, 395 e as da tela de falha, ~471 e ~476). Rodar `grep -n "\.add(in\.\|add(Angle::invalid\|addPresetOffset" ur/src/domain/ui/normal_screen.cpp` e conferir que nenhum ficou sem o segundo argumento.

`application.cpp`, em `buildNormalInput`, antes do `return in;`:

```cpp
    in.decimals = params.displayDecimals();
```

`main.cpp`, logo depois de `constexpr uint16_t kBlobCap = NvsParameterStore::kCapacityBytes;`:

```cpp
// O bloco de parametros cresceu na v3 (Decisao 18). Se um dia passar da chave da NVS, a gravacao
// falharia em campo; aqui o build reprova antes.
static_assert(domain::Parameters::kParamBlobSize <= kBlobCap,
              "bloco de parametros nao cabe na chave da NVS");
```

- [ ] **Step 4: Rodar e ver passar**

Run: `cd ur && pio test -e native`
Expected: tudo PASS.

Run: `cd ur && pio run -e esp32dev`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add ur/src/domain/ui/normal_screen.h ur/src/domain/ui/normal_screen.cpp ur/src/app/application.cpp ur/src/main.cpp ur/test/native/test_normal/test_normal.cpp ur/test/native/test_application/test_application.cpp
git commit -m "feat(ur): telas principal e de detalhe seguem as casas decimais gravadas"
```

---

### Task 6: Decisao 18 e tela no catalogo da IHM

**Files:**
- Modify: `DECISIONS.md` (nova secao no fim)
- Modify: `docs/ihm-estados.md` (nova secao no fim, depois de "Atraso de armamento do alarme")

- [ ] **Step 1: `DECISIONS.md`**

Acrescentar no fim, no formato da Decisao 17:

```markdown
## Decisao 18 - Casas decimais da indicacao de angulo, escolhidas no menu

**Status:** IMPLEMENTADA (pedido do cliente, decidido com o responsavel em 2026-10-06)
**Impacto de seguranca:** baixo - so apresentacao; reles, limites, Preset e saida analogica nao mudam
**Desvio do manual:** secao 5.5 (L130 a L133) diz "uma casa decimal fixa". Entra na errata.
**Spec:** `docs/superpowers/specs/2026-10-06-casas-decimais-design.md`
**Codigo:** `ur/src/domain/angle.h`, `ur/src/domain/parameters.*`, `ur/src/domain/ui/menu_machine.*`,
`ur/src/domain/ui/normal_screen.*`, `ur/src/domain/ui/preset_wizard.*`, `ur/src/app/application.cpp`

### O que foi decidido

1. **Opcoes 0 ou 1 casa.** Duas casas ficaram de fora: a sensora entrega decimo
   (`docs/protocolo-rs485.md` secao de registradores) e a exatidao declarada e +/-0,09 grau
   (Decisao 11 item 11). A segunda casa seria ruido, ou um zero fixo fingindo precisao.
2. **O batimento (Decisao 12 item 11) fica.** O pedido original era tira-lo para abrir espaco;
   com 0 ou 1 casa o texto nao cresce e o motivo sumiu.
3. **A opcao vale em toda indicacao de angulo**: leitura ao vivo, leitura sem credito (Emenda 2),
   valor de limite, offset de Preset, leitura do assistente de Preset. Consequencia aceita: com 0
   casas um limite em 45,3 aparece como `+045`.
4. **O campo em edicao sempre mostra uma casa.** Ninguem altera ponto de atuacao sem ver o decimo.
5. **Arredondamento sem casa: inteiro mais proximo, meio para longe do zero.** Erro maximo de
   0,5 grau, simetrico. Zero nunca sai `-000`.

### Persistencia

Bloco de parametros v3 (36 bytes, campo em off 32). v1 e v2 continuam carregando com uma casa -
recusa-los levaria a frota a CONFIG PERDIDA na atualizacao.

### Precisa de medicao de bancada

- Conferir na placa real, nos dois modos, a tela principal, o detalhe de X e Y, a captura e a
  confirmacao do Preset, e o editor de limite (que tem de continuar com a casa).
```

- [ ] **Step 2: `docs/ihm-estados.md`**

Acrescentar no fim, no formato da secao do atraso:

```markdown
## Casas decimais da indicacao (item de menu, 2026-10-06)

Decisao 18. `Menu > Casas Decimais`, item 13 de 14, antes de `Sair`.

| Campo | Valor |
|---|---|
| Tela | rotulo `Casas Decimais:` e a opcao abaixo, `0 (+045)` ou `1 (+045,0)` |
| Gestos | UP escolhe `0`, DOWN escolhe `1`, hold de MENU grava no rascunho e mostra `Alteracao bem sucedida!` |
| Padrao de fabrica | `1` |
| Quando vale | na confirmacao da saida (`NOVA CONFIG - CONFIRMA?`), como todo item do menu |
| Sem casa | `+045`; sem leitura `---`; -0,4 vira `+000` |
| Nao muda | campo em edicao (sempre `+045,3`), porcentagem `SAI:`, batimento |
```

- [ ] **Step 3: Commit**

```bash
git add DECISIONS.md docs/ihm-estados.md
git commit -m "docs: Decisao 18 - casas decimais da indicacao, e a tela no catalogo da IHM"
```

- [ ] **Step 4: Verificacao final**

Run: `cd ur && rm -rf .pio/build/native && pio test -e native && pio run -e esp32dev`
Expected: todos os testes PASS (667 + os novos) e build do alvo SUCCESS.
