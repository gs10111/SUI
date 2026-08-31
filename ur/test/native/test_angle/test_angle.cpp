// Testes de dominio do tipo Angle.
// REQ-MEA-01: faixa +/-90,0 graus por eixo, resolucao 0,1 grau, formato +XXX,X com uma casa fixa.
// REQ-MEA-05: o firmware nao pode introduzir erro de arredondamento acima de 0,05 grau em
//             nenhuma etapa da cadeia - por isso o dominio inteiro trabalha em decimos de grau
//             inteiros e nao existe conversao para ponto flutuante aqui dentro.
// Manual: docs/manual-cliente-sui-2026.txt, secoes 2.1 (L21) e 5.5 (L130 a L133).
#include <string.h>
#include <unity.h>

#include "domain/angle.h"

using domain::Angle;

void setUp(void) {}
void tearDown(void) {}

// --- REQ-MEA-01: faixa e resolucao ---

static void test_REQ_MEA_01_aceita_os_dois_extremos_da_faixa(void) {
    TEST_ASSERT_TRUE(Angle::fromDeciDegrees(-900).valid());
    TEST_ASSERT_TRUE(Angle::fromDeciDegrees(900).valid());
    TEST_ASSERT_EQUAL_INT16(-900, Angle::fromDeciDegrees(-900).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(900, Angle::fromDeciDegrees(900).deciDegrees());
}

static void test_REQ_MEA_01_recusa_um_decimo_alem_de_cada_extremo(void) {
    TEST_ASSERT_FALSE(Angle::fromDeciDegrees(-901).valid());
    TEST_ASSERT_FALSE(Angle::fromDeciDegrees(901).valid());
}

static void test_REQ_MEA_01_resolucao_e_de_um_decimo_de_grau(void) {
    // Dois angulos separados por 0,1 grau tem de ser distinguiveis: e o que garante que o
    // ponto de atuacao do rele pode ser ajustado exatamente no angulo desejado (L133).
    const Angle a = Angle::fromDeciDegrees(50);
    const Angle b = Angle::fromDeciDegrees(51);
    TEST_ASSERT_NOT_EQUAL(a.deciDegrees(), b.deciDegrees());
    TEST_ASSERT_EQUAL_INT16(1, static_cast<int16_t>(b.deciDegrees() - a.deciDegrees()));
}

static void test_REQ_MEA_01_angulo_invalido_nao_entrega_leitura(void) {
    const Angle invalido = Angle::invalid();
    TEST_ASSERT_FALSE(invalido.valid());
}

// --- REQ-MEA-01: formato de exibicao +XXX,X ---

static void test_REQ_MEA_01_formata_com_sinal_tres_digitos_e_uma_casa(void) {
    char texto[Angle::kTextCap];

    TEST_ASSERT_TRUE(Angle::fromDeciDegrees(450).format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("+045,0", texto);

    TEST_ASSERT_TRUE(Angle::fromDeciDegrees(0).format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("+000,0", texto);

    TEST_ASSERT_TRUE(Angle::fromDeciDegrees(250).format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("+025,0", texto);

    TEST_ASSERT_TRUE(Angle::fromDeciDegrees(-900).format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-090,0", texto);

    TEST_ASSERT_TRUE(Angle::fromDeciDegrees(-1).format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-000,1", texto);
}

static void test_REQ_MEA_01_formato_tem_sempre_a_mesma_largura(void) {
    // Largura fixa: o display nao pode dancar quando a leitura passa de 9,9 para 10,0 nem
    // quando cruza o zero.
    char texto[Angle::kTextCap];
    for (int16_t v = -900; v <= 900; ++v) {
        TEST_ASSERT_TRUE(Angle::fromDeciDegrees(v).format(texto, sizeof(texto)));
        TEST_ASSERT_EQUAL_UINT(6u, static_cast<unsigned>(strlen(texto)));
        TEST_ASSERT_TRUE(texto[0] == '+' || texto[0] == '-');
        TEST_ASSERT_EQUAL_CHAR(',', texto[4]);
    }
}

static void test_REQ_MEA_01_formato_recusa_buffer_curto_sem_escrever(void) {
    char texto[4] = {'z', 'z', 'z', 'z'};
    TEST_ASSERT_FALSE(Angle::fromDeciDegrees(450).format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_CHAR('z', texto[0]);
}

static void test_REQ_MEA_01_angulo_invalido_formata_como_traco(void) {
    char texto[Angle::kTextCap];
    TEST_ASSERT_TRUE(Angle::invalid().format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("---,-", texto);
}

// --- REQ-MEA-05: nenhuma etapa introduz erro ---

static void test_REQ_MEA_05_negar_duas_vezes_devolve_o_mesmo_valor(void) {
    // A inversao de sinal do Sentido do Sensor e aplicada e desfeita sem perda.
    for (int16_t v = -900; v <= 900; ++v) {
        const Angle original = Angle::fromDeciDegrees(v);
        TEST_ASSERT_EQUAL_INT16(v, original.negated().negated().deciDegrees());
    }
}

static void test_REQ_MEA_05_negar_e_exato_nos_extremos(void) {
    TEST_ASSERT_EQUAL_INT16(900, Angle::fromDeciDegrees(-900).negated().deciDegrees());
    TEST_ASSERT_EQUAL_INT16(-900, Angle::fromDeciDegrees(900).negated().deciDegrees());
    TEST_ASSERT_EQUAL_INT16(0, Angle::fromDeciDegrees(0).negated().deciDegrees());
}

static void test_REQ_MEA_05_deslocar_e_desfazer_devolve_o_mesmo_valor(void) {
    // Enquanto o resultado couber na faixa, aplicar o offset do Preset e retira-lo tem de
    // devolver exatamente o valor de partida.
    const int16_t offsets[] = {-900, -451, -1, 0, 1, 451, 900};
    for (int16_t v = -450; v <= 450; v += 3) {
        for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
            const int32_t soma = static_cast<int32_t>(v) + offsets[i];
            if (soma < -900 || soma > 900) {
                continue;
            }
            const Angle deslocado = Angle::fromDeciDegrees(v).offsetBy(offsets[i]);
            TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(soma), deslocado.deciDegrees());
            TEST_ASSERT_EQUAL_INT16(v, deslocado.offsetBy(static_cast<int16_t>(-offsets[i])).deciDegrees());
        }
    }
}

// --- Saturacao: A9 fixa clamp(dir * bruto + offset, -900, +900) ---

static void test_A9_deslocamento_satura_na_faixa_e_nao_estoura_o_inteiro(void) {
    TEST_ASSERT_EQUAL_INT16(900, Angle::fromDeciDegrees(900).offsetBy(900).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(-900, Angle::fromDeciDegrees(-900).offsetBy(-900).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(900, Angle::fromDeciDegrees(800).offsetBy(300).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(-900, Angle::fromDeciDegrees(-800).offsetBy(-300).deciDegrees());
}

static void test_A9_clamp_aceita_entrada_de_32_bits_fora_da_faixa(void) {
    // O bruto do sensor pode chegar fora de faixa; o clamp e a unica porta de entrada.
    TEST_ASSERT_EQUAL_INT16(900, Angle::clamped(32767).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(-900, Angle::clamped(-32768).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(123, Angle::clamped(123).deciDegrees());
    TEST_ASSERT_TRUE(Angle::clamped(32767).valid());
}

static void test_A9_deslocar_angulo_invalido_continua_invalido(void) {
    // Sem quadro valido do sensor nao existe leitura: o Preset nao pode inventar uma.
    TEST_ASSERT_FALSE(Angle::invalid().offsetBy(100).valid());
    TEST_ASSERT_FALSE(Angle::invalid().negated().valid());
}

// --- Comparacao ---

static void test_REQ_LIM_05_compara_por_valor_e_por_modulo(void) {
    TEST_ASSERT_TRUE(Angle::fromDeciDegrees(50) == Angle::fromDeciDegrees(50));
    TEST_ASSERT_FALSE(Angle::fromDeciDegrees(50) == Angle::fromDeciDegrees(51));
    TEST_ASSERT_EQUAL_INT16(50, Angle::fromDeciDegrees(-50).absDeciDegrees());
    TEST_ASSERT_EQUAL_INT16(900, Angle::fromDeciDegrees(-900).absDeciDegrees());
    TEST_ASSERT_EQUAL_INT16(0, Angle::fromDeciDegrees(0).absDeciDegrees());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_REQ_MEA_01_aceita_os_dois_extremos_da_faixa);
    RUN_TEST(test_REQ_MEA_01_recusa_um_decimo_alem_de_cada_extremo);
    RUN_TEST(test_REQ_MEA_01_resolucao_e_de_um_decimo_de_grau);
    RUN_TEST(test_REQ_MEA_01_angulo_invalido_nao_entrega_leitura);
    RUN_TEST(test_REQ_MEA_01_formata_com_sinal_tres_digitos_e_uma_casa);
    RUN_TEST(test_REQ_MEA_01_formato_tem_sempre_a_mesma_largura);
    RUN_TEST(test_REQ_MEA_01_formato_recusa_buffer_curto_sem_escrever);
    RUN_TEST(test_REQ_MEA_01_angulo_invalido_formata_como_traco);
    RUN_TEST(test_REQ_MEA_05_negar_duas_vezes_devolve_o_mesmo_valor);
    RUN_TEST(test_REQ_MEA_05_negar_e_exato_nos_extremos);
    RUN_TEST(test_REQ_MEA_05_deslocar_e_desfazer_devolve_o_mesmo_valor);
    RUN_TEST(test_A9_deslocamento_satura_na_faixa_e_nao_estoura_o_inteiro);
    RUN_TEST(test_A9_clamp_aceita_entrada_de_32_bits_fora_da_faixa);
    RUN_TEST(test_A9_deslocar_angulo_invalido_continua_invalido);
    RUN_TEST(test_REQ_LIM_05_compara_por_valor_e_por_modulo);
    return UNITY_END();
}
