// Testes de dominio das regras de limite e do comparador dos quatro reles.
// REQ-LIM-01: quatro limites independentes, programados em angulo na faixa +/-90,0 graus com
//             resolucao de 0,1 grau; Limites 1 e 2 no eixo X, 3 e 4 no eixo Y (manual L200 a L202).
// REQ-LIM-02: Off - o rele permanece em repouso, independentemente do angulo medido (L204).
// REQ-LIM-03: >= - o rele e acionado sempre que o angulo medido for maior ou igual ao valor
//             programado (L205).
// REQ-LIM-04: <= - o rele e acionado sempre que o angulo medido for menor ou igual ao valor
//             programado (L206).
// REQ-LIM-05: + - o rele e acionado sempre que o MODULO do angulo medido for maior ou igual ao
//             MODULO do valor programado, nos dois sentidos (L207 e L208).
// REQ-LIM-06: o valor comparado e a leitura apresentada no display, ja com Preset e Sentido do
//             Sensor aplicados (L223); o comparador so recebe leitura, nunca valor cru.
// REQ-LIM-07: o ponto de atuacao do rele fica exatamente no angulo programado (L142), e o
//             ponto de liberacao existe para TODO ajuste legal de L200, inclusive nas bordas.
// REQ-LIM-08: falha de comunicacao com a sensora e sinalizada nas saidas (secao 7, L296 a L306).
// Decisoes: A1 (polaridade por parametro), A3 (histerese de 0,3 grau so na liberacao, ataque
//           100 ms, liberacao 3000 ms, teto anti-chatter de 60000 ms apos 20 ataques em 600 s,
//           alarme angular sem latch), A4 (amostra invalida; 3 consecutivas levam ao alarme;
//           estado de boot e o estado seguro), A5 (limite em Off vai ao alarme na falha de
//           enlace), A9 (commit de parametro nao pulsa rele - criterio da medicao M9).
// Manual: docs/manual-cliente-sui-2026.txt, secoes 5.5 (L142), 5.9 (L200 a L223) e 7.
//
// O relogio e o test::FakeClock CANONICO de test/fakes/fake_clock.h, que parte de 0xFFFF0000:
// toda esta suite atravessa o wrap de 2^32 ms enquanto roda, que e como o defeito de janela do
// teto anti-chatter aparece sem precisar de um teste dedicado. Nenhum relogio falso local.
//
// Os prazos sao verificados em TEMPO DECORRIDO, com ticks de passo irregular: uma implementacao
// que contasse ciclos passa em qualquer suite de tick uniforme e falha aqui.
#include <unity.h>

#include "domain/angle.h"
#include "domain/limit_evaluator.h"
#include "domain/limit_rule.h"
#include "fakes/fake_clock.h"
#include "ports/i_clock.h"
#include "ports/i_relay_bank.h"

using domain::Angle;
using domain::coilLevel;
using domain::LimitEvaluator;
using domain::LimitInput;
using domain::LimitOp;
using domain::LimitRule;
using test::FakeClock;

namespace ops = domain::ops;

void setUp(void) {}
void tearDown(void) {}

// --- Bancada: ciclo nominal de 50 ms (base comum, poll de 50 ms), com passo ajustavel ---

static const uint32_t kTickMs = 50;

static RelayMask tick(LimitEvaluator& evaluator, FakeClock& fakeClock, int16_t xDeci,
                      int16_t yDeci, uint32_t stepMs = kTickMs) {
    fakeClock.advanceMs(stepMs);
    const LimitInput in{Angle::fromDeciDegrees(xDeci), Angle::fromDeciDegrees(yDeci), true};
    return evaluator.update(in);
}

static RelayMask tickBlind(LimitEvaluator& evaluator, FakeClock& fakeClock,
                           uint32_t stepMs = kTickMs) {
    fakeClock.advanceMs(stepMs);
    const LimitInput in{Angle::invalid(), Angle::invalid(), false};
    return evaluator.update(in);
}

static RelayMask run(LimitEvaluator& evaluator, FakeClock& fakeClock, int16_t xDeci, int16_t yDeci,
                     uint32_t spanMs, uint32_t stepMs = kTickMs) {
    RelayMask result = evaluator.mask();
    for (uint32_t elapsed = 0; elapsed < spanMs; elapsed += stepMs) {
        result = tick(evaluator, fakeClock, xDeci, yDeci, stepMs);
    }
    return result;
}

static bool signalled(const LimitEvaluator& evaluator, LimitChannel channel) {
    return evaluator.state(channel) == RelayState::Signalled;
}

static void driveUntil(LimitEvaluator& evaluator, FakeClock& fakeClock, int16_t xDeci,
                       RelayState wanted, uint32_t budgetMs) {
    for (uint32_t elapsed = 0;
         elapsed < budgetMs && evaluator.state(LimitChannel::Limit1) != wanted;
         elapsed += kTickMs) {
        tick(evaluator, fakeClock, xDeci, 0);
    }
    TEST_ASSERT_TRUE(evaluator.state(LimitChannel::Limit1) == wanted);
}

// O avaliador nasce em ALARME (estado de boot = estado seguro). Todo teste que precise partir
// do repouso tem de CONQUISTAR o repouso, com leitura valida durante os 3000 ms de liberacao.
static void bootClear(LimitEvaluator& evaluator, FakeClock& fakeClock, int16_t xDeci,
                      int16_t yDeci) {
    run(evaluator, fakeClock, xDeci, yDeci, domain::kReleaseConfirmMs + kTickMs);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, evaluator.mask());
}

// --- A4 / A5: o estado de boot e o estado seguro, nao o estado limpo ---

static void test_A4_boot_comeca_em_alarme_ate_a_primeira_leitura_valida(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kOff, 0));

    // ANTES do primeiro update(): a mascara que a tarefa ctrl aplicaria no passo 13 do boot ja
    // e a de alarme, e nao 0x00. Aplicar 0x00 aqui energizaria as quatro bobinas e anunciaria
    // "estrutura nivelada" ao CLP antes de existir leitura.
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, evaluator.mask());
    TEST_ASSERT_TRUE(evaluator.linkFaulted());
    TEST_ASSERT_TRUE(evaluator.alarmActive());
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        const RelayState relayState = evaluator.state(static_cast<LimitChannel>(i));
        TEST_ASSERT_FALSE(coilLevel(relayState, true));  // fail-safe: bobina desenergizada
    }

    // A saida do alarme de boot e regida pela liberacao normal de A3, contada a partir da
    // PRIMEIRA leitura valida - nao pelos 3 ciclos de A4, que so governam a queda em falha.
    tick(evaluator, fakeClock, 0, 0);
    TEST_ASSERT_FALSE(evaluator.linkFaulted());
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, evaluator.mask());

    run(evaluator, fakeClock, 0, 0, 2950);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, evaluator.mask());

    tick(evaluator, fakeClock, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, evaluator.mask());
    TEST_ASSERT_FALSE(evaluator.alarmActive());
}

// --- REQ-LIM-03 / REQ-LIM-07: ">=" ataca no valor programado, exatamente ---

static void test_REQ_LIM_03_maior_ou_igual_ataca_no_valor_exato(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, 49, 0, 1000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, 50, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
}

// --- REQ-LIM-04 / REQ-LIM-07: "<=" ataca no valor programado, exatamente ---

static void test_REQ_LIM_04_menor_ou_igual_ataca_no_valor_exato(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtMost, -50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, -49, 0, 1000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, -50, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
}

// --- REQ-LIM-07: nas bordas da faixa o ponto de liberacao continua alcancavel ---

static void test_REQ_LIM_07_maior_igual_no_extremo_da_faixa_ainda_libera(void) {
    // L = -89,8 graus e ajuste LEGAL (L200 publica -90,0 a +90,0 com 0,1 grau de resolucao).
    // Sem o grampo, o ponto de liberacao seria -90,1 grau, que Angle nao representa: o rele
    // ficaria atacado para sempre em -90,0 grau, onde a condicao de L205 e FALSA - latch, que
    // a decisao A3 proibe ("alarme angular SEM latch").
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, -898));

    run(evaluator, fakeClock, -900, 0, domain::kReleaseConfirmMs + kTickMs);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, -898, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, -900, 0, domain::kReleaseConfirmMs + kTickMs);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
}

static void test_REQ_LIM_07_menor_igual_no_extremo_da_faixa_ainda_libera(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtMost, 898));

    run(evaluator, fakeClock, 900, 0, domain::kReleaseConfirmMs + kTickMs);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, 898, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, 900, 0, domain::kReleaseConfirmMs + kTickMs);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
}

// --- REQ-LIM-05 / REQ-LIM-07: "+" ataca no modulo exato, nos dois sentidos ---

static void test_REQ_LIM_05_modulo_ataca_no_valor_exato_nos_dois_sentidos(void) {
    // Limite 1 no eixo X e Limite 3 no eixo Y, com a mesma regra: um eixo entra pelo lado
    // positivo e o outro pelo negativo no mesmo ciclo (L207 e o exemplo de L208).
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kModulus, 50));
    evaluator.setRule(LimitChannel::Limit3, LimitRule(ops::kModulus, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, 49, -49, 1000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit3));

    run(evaluator, fakeClock, 50, -50, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit3));
}

static void test_REQ_LIM_05_modulo_com_valor_programado_negativo(void) {
    // "+" compara MODULO com MODULO: um limite gravado em -5,0 graus atua igual a um gravado
    // em +5,0 graus, nos dois sentidos.
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kModulus, -50));
    evaluator.setRule(LimitChannel::Limit3, LimitRule(ops::kModulus, -50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, 49, -49, 1000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit3));

    run(evaluator, fakeClock, 50, -50, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit3));
}

// --- REQ-LIM-05 / A3: caso degenerado de "+" com |L| <= 2 decimos ---

static void test_A3_modulo_degenerado_fica_permanentemente_atacado(void) {
    // |a| >= 0 e sempre verdadeiro e o ponto de liberacao |a| <= |L| - 3 nao existe: leitura
    // literal de L207. Especificado, nao defeito - a Tabela 2 entrega os limites em +000,0
    // grau com Operacao Off, nunca com "+". Aqui o Limite 1 nunca sai do alarme de boot, e o
    // Limite 3 conquista o repouso em Off antes de receber o "+" degenerado.
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kModulus, 0));
    evaluator.setRule(LimitChannel::Limit3, LimitRule(ops::kOff, 0));

    run(evaluator, fakeClock, 0, 0, domain::kReleaseConfirmMs + kTickMs);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit3));

    evaluator.setRule(LimitChannel::Limit3, LimitRule(ops::kModulus, 2));
    run(evaluator, fakeClock, 0, 0, 1000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit3));

    run(evaluator, fakeClock, 0, 5, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit3));

    run(evaluator, fakeClock, 0, 0, 10000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit3));
}

// --- REQ-LIM-02 / A1: "Off" ignora o angulo e fica no estado de repouso ---

static void test_REQ_LIM_02_off_ignora_angulo_muito_alem_do_valor(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kOff, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, 900, 0, 10000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, evaluator.mask());

    // A1: o mesmo estado de repouso, nas duas polaridades possiveis.
    TEST_ASSERT_TRUE(coilLevel(evaluator.state(LimitChannel::Limit1), true));
    TEST_ASSERT_FALSE(coilLevel(evaluator.state(LimitChannel::Limit1), false));
}

// --- A3: histerese de 3 decimos, so na liberacao ---

static void test_A3_histerese_so_atrasa_a_liberacao_nunca_o_ataque(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    // Ataque exato em L, e nao em L + banda.
    run(evaluator, fakeClock, 50, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    // Dentro da banda (L-1 e L-2): a condicao de L205 ja e falsa e o rele CONTINUA acionado.
    run(evaluator, fakeClock, 49, 0, 10000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
    run(evaluator, fakeClock, 48, 0, 10000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    // No fundo da banda (L-3) libera.
    run(evaluator, fakeClock, 47, 0, 5000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    // E o ataque volta a ser exato em L: a banda nao deslocou o ponto de atuacao.
    run(evaluator, fakeClock, 50, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
}

static void test_A3_histerese_no_sentido_oposto_com_menor_ou_igual(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtMost, -50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, -50, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, -48, 0, 10000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, -47, 0, 5000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
}

static void test_A3_histerese_do_modulo_vale_nos_dois_sentidos(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kModulus, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, -50, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, -48, 0, 10000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, -47, 0, 5000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
}

// --- A3: prazos de ataque e de liberacao, contados pelo IClock injetado ---

static void test_A3_ataque_confirma_em_100ms_e_nao_antes(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    tick(evaluator, fakeClock, 0, 0);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 60, 0);  // primeira observacao da condicao
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 60, 0);  // 50 ms de condicao continua
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 60, 0);  // 100 ms: ataca
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
}

static void test_A3_ataque_conta_tempo_decorrido_e_nao_ciclos(void) {
    // Um UNICO ciclo atrasado de 120 ms ja completa os 100 ms de kAttackConfirmMs. Qualquer
    // implementacao que contasse ciclos (2 ou 3 ticks) ainda estaria esperando o proximo tick.
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    tick(evaluator, fakeClock, 60, 0);  // primeira observacao da condicao
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 60, 0, 120);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
}

static void test_A3_ciclo_atrasado_nao_encurta_o_ataque(void) {
    // O simetrico: com ticks de 40 ms, DOIS ciclos de condicao continua sao 80 ms e nao podem
    // atacar. Junto com o teste anterior, nenhuma contagem de ciclos satisfaz os dois - um
    // exige comutar no 2o tick, o outro proibe comutar no 3o. So o tempo decorrido satisfaz.
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    tick(evaluator, fakeClock, 60, 0, 40);  // primeira observacao
    tick(evaluator, fakeClock, 60, 0, 40);  // 40 ms
    tick(evaluator, fakeClock, 60, 0, 40);  // 80 ms
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 60, 0, 40);  // 120 ms: ataca
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
}

static void test_A3_condicao_interrompida_zera_a_confirmacao_de_ataque(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    tick(evaluator, fakeClock, 60, 0);
    tick(evaluator, fakeClock, 60, 0);
    tick(evaluator, fakeClock, 0, 0);   // a condicao caiu: a contagem recomeca do zero
    tick(evaluator, fakeClock, 60, 0);
    tick(evaluator, fakeClock, 60, 0);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 60, 0);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
}

static void test_A3_liberacao_confirma_em_3000ms_e_nao_antes(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, 60, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 0, 0);            // primeira observacao da liberacao
    run(evaluator, fakeClock, 0, 0, 2950);       // 2950 ms de condicao continua
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 0, 0);            // 3000 ms: libera
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
}

static void test_A3_liberacao_conta_tempo_decorrido_com_ticks_irregulares(void) {
    // Os mesmos 3000 ms, servidos em ticks de 700, 1300, 900, 99 e 1 ms. O criterio de
    // aceitacao da medicao M9 admite periodo entre ticks de ate 55 ms, e nenhuma contagem de
    // ciclos reproduz esta fronteira.
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, 60, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 0, 0);              // primeira observacao da liberacao
    tick(evaluator, fakeClock, 0, 0, 700);         // 700 ms
    tick(evaluator, fakeClock, 0, 0, 1300);        // 2000 ms
    tick(evaluator, fakeClock, 0, 0, 900);         // 2900 ms
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 0, 0, 99);          // 2999 ms
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 0, 0, 1);           // 3000 ms exatos: libera
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
}

// --- A3: teto anti-chatter no vigesimo ataque dentro de 600 s, nao no decimo nono ---

static void test_A3_teto_engata_com_20_ataques_dentro_da_janela(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    for (int cycle = 1; cycle <= 19; ++cycle) {
        driveUntil(evaluator, fakeClock, 60, RelayState::Signalled, 1000);
        TEST_ASSERT_EQUAL_UINT32(domain::kReleaseConfirmMs,
                                 evaluator.releaseConfirmMs(LimitChannel::Limit1));
        driveUntil(evaluator, fakeClock, 0, RelayState::Clear, 5000);
    }

    driveUntil(evaluator, fakeClock, 60, RelayState::Signalled, 1000);
    TEST_ASSERT_EQUAL_UINT32(domain::kReleaseCeilingMs,
                             evaluator.releaseConfirmMs(LimitChannel::Limit1));

    // O teto so PROLONGA o alarme: aos 3000 ms o rele ainda esta acionado.
    run(evaluator, fakeClock, 0, 0, 10000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    // E aos 60000 ms libera - o alarme angular nao tem latch.
    driveUntil(evaluator, fakeClock, 0, RelayState::Clear, 70000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
}

static void test_A3_ataque_fora_da_janela_de_600_s_nao_conta(void) {
    // A metade "em 600 s" da decisao A3. Com a janela removida (contagem desde o boot), os 19
    // primeiros ataques ainda contariam e o teto engataria no vigesimo: 60 s de parada de
    // producao por causa de comutacoes espalhadas por semanas.
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    for (int cycle = 1; cycle <= 19; ++cycle) {
        driveUntil(evaluator, fakeClock, 60, RelayState::Signalled, 1000);
        driveUntil(evaluator, fakeClock, 0, RelayState::Clear, 5000);
    }
    TEST_ASSERT_EQUAL_UINT32(domain::kReleaseConfirmMs,
                             evaluator.releaseConfirmMs(LimitChannel::Limit1));

    // A estrutura fica parada por mais de 600 s: os 19 ataques saem da janela.
    run(evaluator, fakeClock, 0, 0, domain::kChatterWindowMs + 10000, 1000);

    driveUntil(evaluator, fakeClock, 60, RelayState::Signalled, 1000);
    TEST_ASSERT_EQUAL_UINT32(domain::kReleaseConfirmMs,
                             evaluator.releaseConfirmMs(LimitChannel::Limit1));
}

static void test_A3_teto_desengata_quando_a_comutacao_cai(void) {
    // Um teto que engatasse e nunca mais desengatasse deixaria o rele com 60 s de liberacao
    // para o resto da vida da UR. O limiar exato de saida (kChatterCeilingExit) nao esta
    // registrado em DECISIONS.md e por isso NAO e afirmado aqui: o teste exige apenas que a
    // janela esvaziada desengate o teto, que e o que a decisao A3 aprovada sustenta.
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    for (int cycle = 1; cycle <= 19; ++cycle) {
        driveUntil(evaluator, fakeClock, 60, RelayState::Signalled, 1000);
        driveUntil(evaluator, fakeClock, 0, RelayState::Clear, 5000);
    }
    driveUntil(evaluator, fakeClock, 60, RelayState::Signalled, 1000);
    TEST_ASSERT_EQUAL_UINT32(domain::kReleaseCeilingMs,
                             evaluator.releaseConfirmMs(LimitChannel::Limit1));
    driveUntil(evaluator, fakeClock, 0, RelayState::Clear, 70000);

    // Com os 20 ataques ainda dentro da janela, o teto continua engatado.
    run(evaluator, fakeClock, 0, 0, 400000, 1000);
    TEST_ASSERT_EQUAL_UINT32(domain::kReleaseCeilingMs,
                             evaluator.releaseConfirmMs(LimitChannel::Limit1));

    // Esvaziada a janela de 600 s, desengata.
    run(evaluator, fakeClock, 0, 0, 300000, 1000);
    TEST_ASSERT_EQUAL_UINT32(domain::kReleaseConfirmMs,
                             evaluator.releaseConfirmMs(LimitChannel::Limit1));

    // E na pratica: o ataque seguinte libera em 3000 ms, nao em 60000 ms.
    driveUntil(evaluator, fakeClock, 60, RelayState::Signalled, 1000);
    tick(evaluator, fakeClock, 0, 0);
    run(evaluator, fakeClock, 0, 0, 2950);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
    tick(evaluator, fakeClock, 0, 0);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
}

static void test_A3_teto_anti_chatter_sobrevive_ao_wrap_do_contador(void) {
    // Relogio a 30 s do wrap de 2^32 ms: a janela de 600 s do teto atravessa o wrap no meio
    // da sequencia de 20 ataques. Com o indice de balde derivado de nowMs / 50000, o historico
    // zerava no wrap e a liberacao caia de 60000 para 3000 ms sem nenhum indicio - uma vez a
    // cada 49,7 dias de operacao continua.
    FakeClock fakeClock(0xFFFF8AD0u);
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    for (int cycle = 1; cycle <= 19; ++cycle) {
        driveUntil(evaluator, fakeClock, 60, RelayState::Signalled, 1000);
        driveUntil(evaluator, fakeClock, 0, RelayState::Clear, 5000);
    }
    driveUntil(evaluator, fakeClock, 60, RelayState::Signalled, 1000);
    TEST_ASSERT_EQUAL_UINT32(domain::kReleaseCeilingMs,
                             evaluator.releaseConfirmMs(LimitChannel::Limit1));

    run(evaluator, fakeClock, 0, 0, 10000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    driveUntil(evaluator, fakeClock, 0, RelayState::Clear, 70000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
}

// --- REQ-LIM-08 / A5: falha de enlace leva os quatro reles ao alarme ---

static void test_A5_falha_de_enlace_leva_os_quatro_reles_ao_alarme(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kOff, 0));
    evaluator.setRule(LimitChannel::Limit2, LimitRule(ops::kAtLeast, 800));
    evaluator.setRule(LimitChannel::Limit3, LimitRule(ops::kAtMost, -800));
    evaluator.setRule(LimitChannel::Limit4, LimitRule(ops::kModulus, 500));
    bootClear(evaluator, fakeClock, 0, 0);

    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, tickBlind(evaluator, fakeClock));
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, tickBlind(evaluator, fakeClock));
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, tickBlind(evaluator, fakeClock));
    TEST_ASSERT_TRUE(evaluator.linkFaulted());

    // A5, desvio declarado de L204: o canal programado em Off tambem denuncia a UR cega.
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
}

static void test_A5_um_ciclo_valido_zera_a_contagem_de_falha(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 800));

    tickBlind(evaluator, fakeClock);
    tickBlind(evaluator, fakeClock);
    tick(evaluator, fakeClock, 0, 0);
    tickBlind(evaluator, fakeClock);
    tickBlind(evaluator, fakeClock);
    TEST_ASSERT_FALSE(evaluator.linkFaulted());

    // Os reles NAO acompanham o enlace de imediato: partiram do alarme de boot e ainda nao
    // cumpriram os 3000 ms de liberacao.
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, evaluator.mask());

    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, tickBlind(evaluator, fakeClock));
    TEST_ASSERT_TRUE(evaluator.linkFaulted());
}

static void test_A5_indicacao_de_falha_acompanha_os_reles_ate_a_liberacao(void) {
    // linkFaulted() cai no primeiro ciclo valido, mas os quatro reles ainda dizem alarme ao
    // CLP por 3000 ms. Quem escolhe o texto de tela e o codigo da saida analogica tem de usar
    // alarmActive(), senao display, analogica e reles contam tres historias diferentes.
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kOff, 0));
    bootClear(evaluator, fakeClock, 0, 0);

    tickBlind(evaluator, fakeClock);
    tickBlind(evaluator, fakeClock);
    tickBlind(evaluator, fakeClock);
    TEST_ASSERT_TRUE(evaluator.linkFaulted());
    TEST_ASSERT_TRUE(evaluator.alarmActive());
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, evaluator.mask());

    // Enlace recuperado: o diagnostico do enlace ja limpou, o alarme aplicado ainda nao.
    tick(evaluator, fakeClock, 0, 0);
    TEST_ASSERT_FALSE(evaluator.linkFaulted());
    TEST_ASSERT_TRUE(evaluator.alarmActive());
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, evaluator.mask());

    // Coerencia em TODOS os ciclos ate a liberacao: alarmActive() e exatamente "algum rele
    // sinalizado", nunca uma indicacao que se adianta aos contatos.
    for (uint32_t elapsed = 0; elapsed < 2950; elapsed += kTickMs) {
        tick(evaluator, fakeClock, 0, 0);
        TEST_ASSERT_TRUE(evaluator.alarmActive());
        TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, evaluator.mask());
    }

    tick(evaluator, fakeClock, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, evaluator.mask());
    TEST_ASSERT_FALSE(evaluator.alarmActive());
    TEST_ASSERT_FALSE(evaluator.linkFaulted());
}

// --- A4: amostra fora de +/-90,0 graus nao e leitura ---

static void test_A4_tres_amostras_fora_de_faixa_levam_ao_alarme(void) {
    // Angle::fromDeciDegrees recusa fora de +/-900: a amostra chega ao comparador invalida,
    // mesmo com o quadro integro no fio.
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kOff, 0));
    evaluator.setRule(LimitChannel::Limit2, LimitRule(ops::kAtLeast, 800));
    evaluator.setRule(LimitChannel::Limit3, LimitRule(ops::kAtMost, -800));
    evaluator.setRule(LimitChannel::Limit4, LimitRule(ops::kModulus, 500));
    bootClear(evaluator, fakeClock, 0, 0);

    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, tick(evaluator, fakeClock, 950, 0));
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, tick(evaluator, fakeClock, 950, 0));
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllSignalled, tick(evaluator, fakeClock, 950, 0));
    TEST_ASSERT_TRUE(evaluator.linkFaulted());
}

static void test_A4_amostra_invalida_congela_o_prazo_e_nao_o_completa(void) {
    // Sem leitura nova nao ha evidencia nem para atacar nem para liberar: o prazo para de
    // correr e recomeca de onde parou, em vez de vencer com dado velho.
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, 60, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 0, 0);        // primeira observacao da liberacao
    run(evaluator, fakeClock, 0, 0, 2850);   // 2850 ms de condicao continua

    tickBlind(evaluator, fakeClock);         // ciclo invalido: congela em 2850 ms
    tick(evaluator, fakeClock, 0, 0);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 0, 0);        // aqui teriam vencido 3000 ms sem o congelamento
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 0, 0);        // 3000 ms de leitura valida: libera
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
}

// --- A9 / M9: commit de regra nao pode pulsar o rele ---

static void test_A9_commit_de_regra_nao_pulsa_o_rele(void) {
    // Criterio de aceitacao da medicao M9: "cem commits de troca de Sentido [...] zero
    // comutacoes". Tempo contado sob o predicado ANTIGO nao pode ser creditado ao NOVO: sem
    // zerar a contagem, 2950 ms acumulados fazem o rele largar o alarme 50 ms depois do
    // commit, com a regra nova observada por 50 ms em vez dos 3000 ms de A3.
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, 60, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 0, 0);
    run(evaluator, fakeClock, 0, 0, 2900);   // 2900 ms de liberacao contados sob a regra antiga
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 40));

    tick(evaluator, fakeClock, 0, 0);        // primeira observacao sob a regra nova
    run(evaluator, fakeClock, 0, 0, 2950);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 0, 0);        // 3000 ms COMPLETOS sob a regra nova: libera
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
}

static void test_A3_commit_de_regra_reinicia_a_confirmacao_de_ataque(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    tick(evaluator, fakeClock, 60, 0);       // primeira observacao
    tick(evaluator, fakeClock, 60, 0);       // 50 ms sob a regra antiga

    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 55));

    tick(evaluator, fakeClock, 60, 0);       // sem o conserto, aqui venceriam os 100 ms
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
    tick(evaluator, fakeClock, 60, 0);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    tick(evaluator, fakeClock, 60, 0);       // 100 ms sob a regra nova: ataca
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
}

// --- A1: uma unica funcao traduz o estado em nivel de bobina, nas duas polaridades ---

static void test_A1_polaridade_e_parametro_e_vale_nos_dois_valores(void) {
    // kRelayFailSafePolarity = true (fail-safe): bobina energizada e o estado SAUDAVEL.
    TEST_ASSERT_TRUE(coilLevel(RelayState::Clear, true));
    TEST_ASSERT_FALSE(coilLevel(RelayState::Signalled, true));

    // kRelayFailSafePolarity = false (fidelidade ao manual): energizado = limite atingido.
    TEST_ASSERT_FALSE(coilLevel(RelayState::Clear, false));
    TEST_ASSERT_TRUE(coilLevel(RelayState::Signalled, false));
}

static void test_A1_falha_de_enlace_energiza_conforme_a_polaridade_escolhida(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kOff, 0));
    bootClear(evaluator, fakeClock, 0, 0);

    tickBlind(evaluator, fakeClock);
    tickBlind(evaluator, fakeClock);
    tickBlind(evaluator, fakeClock);

    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        const RelayState relayState = evaluator.state(static_cast<LimitChannel>(i));
        TEST_ASSERT_FALSE(coilLevel(relayState, true));
        TEST_ASSERT_TRUE(coilLevel(relayState, false));
    }
}

// --- OCP: uma operacao nova nao toca em LimitEvaluator nem em nenhum switch ---

static bool windowAttacks(const LimitRule& rule, int16_t angleDeci) {
    return angleDeci >= rule.valueDeci() && angleDeci <= rule.valueDeci() + 100;
}

static bool windowReleases(const LimitRule& rule, int16_t angleDeci) {
    return angleDeci <= rule.valueDeci() - rule.hysteresisDeci() ||
           angleDeci >= rule.valueDeci() + 100 + rule.hysteresisDeci();
}

static const LimitOp kWindow{windowAttacks, windowReleases};

static void test_OCP_operacao_nova_entra_sem_tocar_no_avaliador(void) {
    // Janela entre A e A+10,0 graus, definida INTEIRAMENTE aqui no teste: nenhuma linha de
    // limit_evaluator.cpp conhece esta operacao, e mesmo assim ela ataca, mantem e libera com
    // os mesmos prazos e a mesma banda das quatro operacoes do manual.
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(kWindow, 50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, 49, 0, 1000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, 50, 0, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, 150, 0, 5000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));

    run(evaluator, fakeClock, 153, 0, 5000);
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit1));
}

// --- REQ-LIM-01: os quatro canais sao independentes e cada par segue o seu eixo ---

static void test_REQ_LIM_01_quatro_canais_independentes_por_eixo(void) {
    FakeClock fakeClock;
    LimitEvaluator evaluator(fakeClock);
    evaluator.setRule(LimitChannel::Limit1, LimitRule(ops::kAtLeast, 50));
    evaluator.setRule(LimitChannel::Limit2, LimitRule(ops::kAtMost, -50));
    evaluator.setRule(LimitChannel::Limit3, LimitRule(ops::kAtLeast, 50));
    evaluator.setRule(LimitChannel::Limit4, LimitRule(ops::kAtMost, -50));
    bootClear(evaluator, fakeClock, 0, 0);

    run(evaluator, fakeClock, 60, -60, 1000);
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit1));
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit2));
    TEST_ASSERT_FALSE(signalled(evaluator, LimitChannel::Limit3));
    TEST_ASSERT_TRUE(signalled(evaluator, LimitChannel::Limit4));
    TEST_ASSERT_EQUAL_UINT8(0x09, evaluator.mask());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_A4_boot_comeca_em_alarme_ate_a_primeira_leitura_valida);
    RUN_TEST(test_REQ_LIM_01_quatro_canais_independentes_por_eixo);
    RUN_TEST(test_REQ_LIM_02_off_ignora_angulo_muito_alem_do_valor);
    RUN_TEST(test_REQ_LIM_03_maior_ou_igual_ataca_no_valor_exato);
    RUN_TEST(test_REQ_LIM_04_menor_ou_igual_ataca_no_valor_exato);
    RUN_TEST(test_REQ_LIM_05_modulo_ataca_no_valor_exato_nos_dois_sentidos);
    RUN_TEST(test_REQ_LIM_05_modulo_com_valor_programado_negativo);
    RUN_TEST(test_REQ_LIM_07_maior_igual_no_extremo_da_faixa_ainda_libera);
    RUN_TEST(test_REQ_LIM_07_menor_igual_no_extremo_da_faixa_ainda_libera);
    RUN_TEST(test_A3_modulo_degenerado_fica_permanentemente_atacado);
    RUN_TEST(test_A3_histerese_so_atrasa_a_liberacao_nunca_o_ataque);
    RUN_TEST(test_A3_histerese_no_sentido_oposto_com_menor_ou_igual);
    RUN_TEST(test_A3_histerese_do_modulo_vale_nos_dois_sentidos);
    RUN_TEST(test_A3_ataque_confirma_em_100ms_e_nao_antes);
    RUN_TEST(test_A3_ataque_conta_tempo_decorrido_e_nao_ciclos);
    RUN_TEST(test_A3_ciclo_atrasado_nao_encurta_o_ataque);
    RUN_TEST(test_A3_condicao_interrompida_zera_a_confirmacao_de_ataque);
    RUN_TEST(test_A3_liberacao_confirma_em_3000ms_e_nao_antes);
    RUN_TEST(test_A3_liberacao_conta_tempo_decorrido_com_ticks_irregulares);
    RUN_TEST(test_A3_teto_engata_com_20_ataques_dentro_da_janela);
    RUN_TEST(test_A3_ataque_fora_da_janela_de_600_s_nao_conta);
    RUN_TEST(test_A3_teto_desengata_quando_a_comutacao_cai);
    RUN_TEST(test_A3_teto_anti_chatter_sobrevive_ao_wrap_do_contador);
    RUN_TEST(test_A3_commit_de_regra_reinicia_a_confirmacao_de_ataque);
    RUN_TEST(test_A4_tres_amostras_fora_de_faixa_levam_ao_alarme);
    RUN_TEST(test_A4_amostra_invalida_congela_o_prazo_e_nao_o_completa);
    RUN_TEST(test_A5_falha_de_enlace_leva_os_quatro_reles_ao_alarme);
    RUN_TEST(test_A5_um_ciclo_valido_zera_a_contagem_de_falha);
    RUN_TEST(test_A5_indicacao_de_falha_acompanha_os_reles_ate_a_liberacao);
    RUN_TEST(test_A9_commit_de_regra_nao_pulsa_o_rele);
    RUN_TEST(test_A1_polaridade_e_parametro_e_vale_nos_dois_valores);
    RUN_TEST(test_A1_falha_de_enlace_energiza_conforme_a_polaridade_escolhida);
    RUN_TEST(test_OCP_operacao_nova_entra_sem_tocar_no_avaliador);
    return UNITY_END();
}
