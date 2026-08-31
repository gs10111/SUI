// Testes de dominio do filtro passa-baixa da UR: EMA Q8 sobre decimos de grau, com recarga por
// salto e retencao de ordem zero.
//
// REQ-MEA-04. Manual: docs/manual-cliente-sui-2026.txt L71, L142, L223 e L272.
// Decisao A6, aprovada (DECISIONS.md L67 e L249): constante fixa de 0,8 s, recarga por salto de
// 2,0 grau sempre ativa, quatro degraus 0,2 / 0,8 / 1,6 / 3,2 s so no comissionamento.
// BASE COMUM 2.1 item 4 e 2.5: o ciclo de controle e de 50 ms e a constante de tempo e dada em
// ms e convertida em coeficiente NESSE periodo.
//
// Nao existe relogio falso aqui, e isso e deliberado: o filtro e dirigido pelo TICK da tarefa
// ctrl, nao por um instante lido de um relogio. Um tick e uma chamada de update(); os prazos
// desta suite sao contados em ticks e convertidos em ms pelo periodo de amostragem que o
// proprio teste injeta. Injetar tempo por IClock aqui seria inventar uma dependencia que o
// modulo nao tem e esconder o defeito de "filtro dirigido por amostra", que e exatamente o que
// o teste do enlace com perda de quadros persegue.
//
// Os numeros esperados nao sao chute nem gravacao do comportamento observado: com Ts = 50 ms a
// constante realizavel vale Ts*(2^(k+1) - 1)/2, o que da 175 / 775 / 1575 / 3175 ms para
// k = 2 / 4 / 5 / 6. O tempo de subida a 63,2 % de um degrau tem de cair sobre esse tau, com
// tolerancia de um oitavo mais um tick - um oitavo porque o degrau e medido no decimo de grau
// exibido, que e grosso perto do joelho da exponencial, e um tick porque a medida so pode
// acontecer em multiplo do ciclo de controle.
//
// A varredura de zona morta e o teste que justifica o modulo existir, e ela mede o estado
// INTERNO, nao so o numero exibido. A razao esta num resultado de mutacao que vale registrar:
// removendo o passo minimo da implementacao, TODAS as assercoes sobre o valor exibido continuam
// passando. E logico - o congelamento de um EMA Q8 sem passo minimo fica abaixo de 2^k LSB, ou
// 0,006 grau no ajuste padrao, e o arredondamento para decimo de grau engole isso. O defeito
// nao some por ser invisivel no display: o resto do estado continua enviesado, e o vies desloca
// o unico ponto do modulo que le o estado com resolucao cheia - o limiar de recarga por salto.
// Com o estado 0,006 grau abaixo do alvo, um salto de exatamente 2,0 grau para baixo mede
// 1,994 grau, nao recarrega, e a excursao brusca que o manual promete seguir de imediato passa
// a ser filtrada com 0,8 s de atraso. Por isso stateIsBitExact() sonda o limiar de recarga nos
// dois sentidos depois de cada convergencia: e a forma de exigir y == x*256 bit a bit usando so
// a interface publica. A varredura cobre os 1801 alvos de -90,0 a +90,0 grau, nos dois sentidos
// de aproximacao, com a diferenca de 1 decimo (o pior caso da zona morta) e com a diferenca de
// 19 decimos (o maior degrau que ainda nao dispara a recarga).
#include <stdint.h>
#include <unity.h>

#include "domain/angle.h"
#include "domain/low_pass_filter.h"

using domain::Angle;
using domain::LowPassFilter;

void setUp(void) {}
void tearDown(void) {}

static constexpr uint16_t kTs = 50;
static constexpr uint16_t kTaus[4] = {LowPassFilter::kTauStep1Ms, LowPassFilter::kTauStep2Ms,
                                      LowPassFilter::kTauStep3Ms, LowPassFilter::kTauStep4Ms};
static constexpr uint8_t kShifts[4] = {2, 4, 5, 6};
static constexpr uint32_t kRealized[4] = {175, 775, 1575, 3175};

static Angle deg(int16_t deci) { return Angle::fromDeciDegrees(deci); }

static int16_t valueOf(const LowPassFilter& filter) { return filter.value().deciDegrees(); }

static int ticksToCross(LowPassFilter& filter, int16_t sampleDeci, int16_t thresholdDeci,
                        int limit) {
    for (int tick = 1; tick <= limit; ++tick) {
        const Angle out = filter.update(deg(sampleDeci));
        if (out.valid() && out.deciDegrees() >= thresholdDeci) {
            return tick;
        }
    }
    return -1;
}

static bool stateIsBitExact(const LowPassFilter& filter, int16_t settledDeci) {
    const int32_t saltos[2] = {LowPassFilter::kJumpReloadDeci, -LowPassFilter::kJumpReloadDeci};
    bool provado = false;
    for (int i = 0; i < 2; ++i) {
        const int32_t alvo = static_cast<int32_t>(settledDeci) + saltos[i];
        if (alvo < Angle::kMinDeciDeg || alvo > Angle::kMaxDeciDeg) {
            continue;
        }
        LowPassFilter probe = filter;
        const Angle out = probe.update(deg(static_cast<int16_t>(alvo)));
        if (!probe.reloaded() || out.deciDegrees() != alvo) {
            return false;
        }
        provado = true;
    }
    return provado;
}

static int ticksToSettleExactly(LowPassFilter& filter, int16_t sampleDeci, int limit) {
    for (int tick = 1; tick <= limit; ++tick) {
        filter.update(deg(sampleDeci));
        if (valueOf(filter) == sampleDeci && stateIsBitExact(filter, sampleDeci)) {
            LowPassFilter probe = filter;
            for (int extra = 0; extra < 8; ++extra) {
                probe.update(deg(sampleDeci));
                if (valueOf(probe) != sampleDeci) {
                    return -1;
                }
            }
            return tick;
        }
    }
    return -1;
}

static void test_A6_os_quatro_degraus_dao_os_coeficientes_do_projeto(void) {
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_UINT8(kShifts[i], LowPassFilter::shiftFor(kTaus[i], kTs));
        TEST_ASSERT_EQUAL_UINT32(kRealized[i],
                                 LowPassFilter::realizedTimeConstantMs(kShifts[i], kTs));
        const LowPassFilter filter(kTaus[i], kTs);
        TEST_ASSERT_EQUAL_UINT8(kShifts[i], filter.shift());
        TEST_ASSERT_EQUAL_UINT32(kRealized[i], filter.timeConstantMs());
    }
}

static void test_A6_default_de_fabrica_e_08_s(void) {
    TEST_ASSERT_EQUAL_UINT16(800, LowPassFilter::kTauDefaultMs);
    const LowPassFilter filter(LowPassFilter::kTauDefaultMs, kTs);
    TEST_ASSERT_EQUAL_UINT8(4, filter.shift());
    TEST_ASSERT_EQUAL_UINT32(775, filter.timeConstantMs());
}

static void test_REQ_MEA_04_convergencia_ao_degrau_no_ajuste_de_08_s(void) {
    LowPassFilter filter(LowPassFilter::kTauDefaultMs, kTs);
    filter.reload(deg(0));
    TEST_ASSERT_EQUAL_INT16(0, valueOf(filter));

    const int16_t degrau = 19;
    const int16_t joelho = 12;
    const int ticks = ticksToCross(filter, degrau, joelho, 400);
    TEST_ASSERT_GREATER_THAN_INT(0, ticks);

    const int32_t medido = static_cast<int32_t>(ticks) * kTs;
    const int32_t tau = 775;
    const int32_t folga = tau / 8 + kTs;
    TEST_ASSERT_INT32_WITHIN(folga, tau, medido);
}

static void test_REQ_MEA_04_os_quatro_degraus_convergem_cada_um_no_seu_tempo(void) {
    int32_t anterior = 0;
    for (int i = 0; i < 4; ++i) {
        LowPassFilter filter(kTaus[i], kTs);
        filter.reload(deg(0));
        const int ticks = ticksToCross(filter, 19, 12, 1000);
        TEST_ASSERT_GREATER_THAN_INT(0, ticks);

        const int32_t medido = static_cast<int32_t>(ticks) * kTs;
        const int32_t tau = static_cast<int32_t>(kRealized[i]);
        TEST_ASSERT_INT32_WITHIN(tau / 8 + kTs, tau, medido);
        TEST_ASSERT_GREATER_THAN_INT32(anterior, medido);
        anterior = medido;

        TEST_ASSERT_GREATER_THAN_INT(0, ticksToSettleExactly(filter, 19, 4000));
        TEST_ASSERT_EQUAL_INT16(19, valueOf(filter));
    }
}

static void test_REQ_MEA_04_sem_zona_morta_em_toda_a_faixa_no_ajuste_padrao(void) {
    const int16_t distancias[4] = {-19, -1, 1, 19};
    for (int16_t alvo = Angle::kMinDeciDeg; alvo <= Angle::kMaxDeciDeg; ++alvo) {
        for (int d = 0; d < 4; ++d) {
            const int32_t partida = static_cast<int32_t>(alvo) + distancias[d];
            if (partida < Angle::kMinDeciDeg || partida > Angle::kMaxDeciDeg) {
                continue;
            }
            LowPassFilter filter(LowPassFilter::kTauDefaultMs, kTs);
            filter.reload(deg(static_cast<int16_t>(partida)));
            const int ticks = ticksToSettleExactly(filter, alvo, 400);
            if (ticks < 0) {
                TEST_ASSERT_EQUAL_INT16_MESSAGE(alvo, valueOf(filter),
                                                "o filtro congelou antes do alvo");
            }
            TEST_ASSERT_GREATER_THAN_INT(0, ticks);
        }
    }
}

static void test_REQ_MEA_04_sem_zona_morta_no_degrau_mais_lento(void) {
    const int16_t distancias[2] = {-1, 1};
    for (int16_t alvo = Angle::kMinDeciDeg; alvo <= Angle::kMaxDeciDeg; ++alvo) {
        for (int d = 0; d < 2; ++d) {
            const int32_t partida = static_cast<int32_t>(alvo) + distancias[d];
            if (partida < Angle::kMinDeciDeg || partida > Angle::kMaxDeciDeg) {
                continue;
            }
            LowPassFilter filter(LowPassFilter::kTauStep4Ms, kTs);
            filter.reload(deg(static_cast<int16_t>(partida)));
            const int ticks = ticksToSettleExactly(filter, alvo, 400);
            TEST_ASSERT_GREATER_THAN_INT(0, ticks);
        }
    }
}

static void test_A6_recarga_dispara_em_20_grau_e_nao_em_19(void) {
    LowPassFilter perto(LowPassFilter::kTauDefaultMs, kTs);
    perto.reload(deg(0));
    perto.update(deg(19));
    TEST_ASSERT_FALSE(perto.reloaded());
    TEST_ASSERT_EQUAL_INT16(1, valueOf(perto));

    LowPassFilter salto(LowPassFilter::kTauDefaultMs, kTs);
    salto.reload(deg(0));
    salto.update(deg(20));
    TEST_ASSERT_TRUE(salto.reloaded());
    TEST_ASSERT_EQUAL_INT16(20, valueOf(salto));
}

static void test_A6_recarga_vale_nos_dois_sentidos_e_em_qualquer_ajuste(void) {
    for (int i = 0; i < 4; ++i) {
        LowPassFilter subindo(kTaus[i], kTs);
        subindo.reload(deg(-350));
        subindo.update(deg(-330));
        TEST_ASSERT_TRUE(subindo.reloaded());
        TEST_ASSERT_EQUAL_INT16(-330, valueOf(subindo));

        LowPassFilter descendo(kTaus[i], kTs);
        descendo.reload(deg(-330));
        descendo.update(deg(-350));
        TEST_ASSERT_TRUE(descendo.reloaded());
        TEST_ASSERT_EQUAL_INT16(-350, valueOf(descendo));

        LowPassFilter dentro(kTaus[i], kTs);
        dentro.reload(deg(-350));
        dentro.update(deg(-331));
        TEST_ASSERT_FALSE(dentro.reloaded());
    }
}

static void test_A6_recarga_mede_do_estado_filtrado_e_nao_da_amostra_anterior(void) {
    LowPassFilter filter(LowPassFilter::kTauDefaultMs, kTs);
    filter.reload(deg(0));
    filter.update(deg(19));
    TEST_ASSERT_FALSE(filter.reloaded());
    TEST_ASSERT_EQUAL_INT16(1, valueOf(filter));

    filter.update(deg(38));
    TEST_ASSERT_TRUE(filter.reloaded());
    TEST_ASSERT_EQUAL_INT16(38, valueOf(filter));
}

static void test_sem_amostra_valida_nao_existe_angulo(void) {
    LowPassFilter filter(LowPassFilter::kTauDefaultMs, kTs);
    TEST_ASSERT_FALSE(filter.primed());
    for (int tick = 0; tick < 10; ++tick) {
        TEST_ASSERT_FALSE(filter.update(Angle::invalid()).valid());
        TEST_ASSERT_FALSE(filter.primed());
        TEST_ASSERT_TRUE(filter.held());
    }
    const Angle primeira = filter.update(deg(-457));
    TEST_ASSERT_TRUE(primeira.valid());
    TEST_ASSERT_TRUE(filter.primed());
    TEST_ASSERT_TRUE(filter.reloaded());
    TEST_ASSERT_EQUAL_INT16(-457, primeira.deciDegrees());
}

static void test_amostra_invalida_nao_envenena_o_estado(void) {
    LowPassFilter filter(LowPassFilter::kTauDefaultMs, kTs);
    filter.reload(deg(0));
    for (int tick = 0; tick < 5; ++tick) {
        filter.update(deg(15));
    }
    const int16_t antes = valueOf(filter);
    TEST_ASSERT_GREATER_THAN_INT16(0, antes);

    int16_t anterior = antes;
    for (int tick = 0; tick < 20; ++tick) {
        const Angle out = filter.update(Angle::invalid());
        TEST_ASSERT_TRUE(out.valid());
        TEST_ASSERT_TRUE(filter.held());
        TEST_ASSERT_FALSE(filter.reloaded());
        TEST_ASSERT_GREATER_OR_EQUAL_INT16(anterior, out.deciDegrees());
        TEST_ASSERT_LESS_OR_EQUAL_INT16(15, out.deciDegrees());
        anterior = out.deciDegrees();
    }
    TEST_ASSERT_GREATER_THAN_INT16(antes, anterior);
    TEST_ASSERT_GREATER_THAN_INT(0, ticksToSettleExactly(filter, 15, 400));

    LowPassFilter negativo(LowPassFilter::kTauDefaultMs, kTs);
    negativo.reload(deg(0));
    for (int tick = 0; tick < 5; ++tick) {
        negativo.update(deg(-15));
    }
    int16_t tetoNegativo = valueOf(negativo);
    for (int tick = 0; tick < 20; ++tick) {
        const Angle out = negativo.update(Angle::invalid());
        TEST_ASSERT_TRUE(out.valid());
        TEST_ASSERT_LESS_OR_EQUAL_INT16(tetoNegativo, out.deciDegrees());
        tetoNegativo = out.deciDegrees();
    }
    TEST_ASSERT_GREATER_THAN_INT(0, ticksToSettleExactly(negativo, -15, 400));
}

static void test_enlace_com_perda_de_quadros_nao_estica_a_constante_de_tempo(void) {
    LowPassFilter completo(LowPassFilter::kTauDefaultMs, kTs);
    LowPassFilter perdido(LowPassFilter::kTauDefaultMs, kTs);
    completo.reload(deg(0));
    perdido.reload(deg(0));
    for (int tick = 0; tick < 60; ++tick) {
        completo.update(deg(18));
        perdido.update((tick % 3 == 0) ? deg(18) : Angle::invalid());
    }
    TEST_ASSERT_EQUAL_INT16(valueOf(completo), valueOf(perdido));
}

static void test_salto_maximo_de_menos_900_a_mais_900_nao_estoura(void) {
    LowPassFilter filter(LowPassFilter::kTauStep4Ms, kTs);
    filter.reload(deg(Angle::kMinDeciDeg));
    for (int ciclo = 0; ciclo < 200; ++ciclo) {
        const Angle subiu = filter.update(deg(Angle::kMaxDeciDeg));
        TEST_ASSERT_TRUE(subiu.valid());
        TEST_ASSERT_TRUE(filter.reloaded());
        TEST_ASSERT_EQUAL_INT16(Angle::kMaxDeciDeg, subiu.deciDegrees());

        const Angle desceu = filter.update(deg(Angle::kMinDeciDeg));
        TEST_ASSERT_TRUE(desceu.valid());
        TEST_ASSERT_TRUE(filter.reloaded());
        TEST_ASSERT_EQUAL_INT16(Angle::kMinDeciDeg, desceu.deciDegrees());
    }
    for (int tick = 0; tick < 50; ++tick) {
        TEST_ASSERT_EQUAL_INT16(Angle::kMinDeciDeg,
                                filter.update(deg(Angle::kMinDeciDeg)).deciDegrees());
    }
}

static void test_regime_permanente_e_exato_e_estavel(void) {
    const int16_t valores[7] = {-900, -457, -1, 0, 1, 457, 900};
    for (int i = 0; i < 4; ++i) {
        for (int v = 0; v < 7; ++v) {
            LowPassFilter filter(kTaus[i], kTs);
            filter.reload(deg(valores[v]));
            for (int tick = 0; tick < 500; ++tick) {
                const Angle out = filter.update(deg(valores[v]));
                TEST_ASSERT_EQUAL_INT16(valores[v], out.deciDegrees());
                TEST_ASSERT_FALSE(filter.reloaded());
            }
        }
    }
}

static void test_o_valor_filtrado_de_menos_x_espelha_o_de_mais_x(void) {
    for (int i = 0; i < 4; ++i) {
        LowPassFilter positivo(kTaus[i], kTs);
        LowPassFilter negativo(kTaus[i], kTs);
        positivo.reload(deg(0));
        negativo.reload(deg(0));
        for (int tick = 0; tick < 300; ++tick) {
            const int16_t p = positivo.update(deg(17)).deciDegrees();
            const int16_t n = negativo.update(deg(-17)).deciDegrees();
            TEST_ASSERT_EQUAL_INT16(p, static_cast<int16_t>(-n));
        }
        TEST_ASSERT_EQUAL_INT16(17, valueOf(positivo));
        TEST_ASSERT_EQUAL_INT16(-17, valueOf(negativo));
    }
}

static void test_A6_trocar_o_degrau_nao_reinicializa_o_estado(void) {
    LowPassFilter filter(LowPassFilter::kTauDefaultMs, kTs);
    filter.reload(deg(0));
    for (int tick = 0; tick < 20; ++tick) {
        filter.update(deg(19));
    }
    const int16_t antes = valueOf(filter);

    filter.setTimeConstant(LowPassFilter::kTauStep4Ms);
    TEST_ASSERT_EQUAL_UINT8(6, filter.shift());
    TEST_ASSERT_EQUAL_UINT32(3175, filter.timeConstantMs());
    TEST_ASSERT_EQUAL_INT16(antes, valueOf(filter));
    TEST_ASSERT_TRUE(filter.primed());

    filter.setTimeConstant(LowPassFilter::kTauStep1Ms);
    TEST_ASSERT_EQUAL_UINT8(2, filter.shift());
    TEST_ASSERT_EQUAL_INT16(antes, valueOf(filter));
}

static void test_a_constante_de_tempo_em_ms_manda_no_coeficiente(void) {
    TEST_ASSERT_EQUAL_UINT8(4, LowPassFilter::shiftFor(800, 50));
    TEST_ASSERT_EQUAL_UINT8(5, LowPassFilter::shiftFor(800, 25));
    TEST_ASSERT_EQUAL_UINT8(3, LowPassFilter::shiftFor(800, 100));
    TEST_ASSERT_EQUAL_UINT32(788, LowPassFilter::realizedTimeConstantMs(5, 25));
    TEST_ASSERT_EQUAL_UINT32(750, LowPassFilter::realizedTimeConstantMs(3, 100));

    LowPassFilter rapido(800, 25);
    LowPassFilter padrao(800, 50);
    rapido.reload(deg(0));
    padrao.reload(deg(0));
    const int ticksRapido = ticksToCross(rapido, 19, 12, 400);
    const int ticksPadrao = ticksToCross(padrao, 19, 12, 400);
    TEST_ASSERT_GREATER_THAN_INT(0, ticksRapido);
    TEST_ASSERT_GREATER_THAN_INT(0, ticksPadrao);
    TEST_ASSERT_INT32_WITHIN(200, ticksPadrao * 50, ticksRapido * 25);
}

static void test_periodo_de_amostragem_zero_deixa_o_filtro_em_passagem_direta(void) {
    LowPassFilter filter(800, 0);
    TEST_ASSERT_EQUAL_UINT8(0, filter.shift());
    filter.reload(deg(0));
    TEST_ASSERT_EQUAL_INT16(7, filter.update(deg(7)).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(-7, filter.update(deg(-7)).deciDegrees());
}

static void test_reset_apaga_a_leitura_e_a_recarga_recomeca(void) {
    LowPassFilter filter(LowPassFilter::kTauDefaultMs, kTs);
    filter.reload(deg(321));
    TEST_ASSERT_TRUE(filter.primed());
    filter.reset();
    TEST_ASSERT_FALSE(filter.primed());
    TEST_ASSERT_FALSE(filter.value().valid());

    filter.reload(Angle::invalid());
    TEST_ASSERT_FALSE(filter.primed());

    filter.reload(deg(-88));
    TEST_ASSERT_EQUAL_INT16(-88, valueOf(filter));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_A6_os_quatro_degraus_dao_os_coeficientes_do_projeto);
    RUN_TEST(test_A6_default_de_fabrica_e_08_s);
    RUN_TEST(test_REQ_MEA_04_convergencia_ao_degrau_no_ajuste_de_08_s);
    RUN_TEST(test_REQ_MEA_04_os_quatro_degraus_convergem_cada_um_no_seu_tempo);
    RUN_TEST(test_REQ_MEA_04_sem_zona_morta_em_toda_a_faixa_no_ajuste_padrao);
    RUN_TEST(test_REQ_MEA_04_sem_zona_morta_no_degrau_mais_lento);
    RUN_TEST(test_A6_recarga_dispara_em_20_grau_e_nao_em_19);
    RUN_TEST(test_A6_recarga_vale_nos_dois_sentidos_e_em_qualquer_ajuste);
    RUN_TEST(test_A6_recarga_mede_do_estado_filtrado_e_nao_da_amostra_anterior);
    RUN_TEST(test_sem_amostra_valida_nao_existe_angulo);
    RUN_TEST(test_amostra_invalida_nao_envenena_o_estado);
    RUN_TEST(test_enlace_com_perda_de_quadros_nao_estica_a_constante_de_tempo);
    RUN_TEST(test_salto_maximo_de_menos_900_a_mais_900_nao_estoura);
    RUN_TEST(test_regime_permanente_e_exato_e_estavel);
    RUN_TEST(test_o_valor_filtrado_de_menos_x_espelha_o_de_mais_x);
    RUN_TEST(test_A6_trocar_o_degrau_nao_reinicializa_o_estado);
    RUN_TEST(test_a_constante_de_tempo_em_ms_manda_no_coeficiente);
    RUN_TEST(test_periodo_de_amostragem_zero_deixa_o_filtro_em_passagem_direta);
    RUN_TEST(test_reset_apaga_a_leitura_e_a_recarga_recomeca);
    return UNITY_END();
}
