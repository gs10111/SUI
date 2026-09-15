// O PORTAO DO PONTO DE ACESSO. O que estes testes protegem e o radio nao ficar ligado depois que
// a manutencao foi embora - que e o unico ganho de seguranca real deste produto depois que a
// senha do WiFi virou fixa e publicada.
#include <unity.h>

#include "ota_gate.h"

void setUp(void) {}
void tearDown(void) {}

namespace {
constexpr uint32_t kT0 = 0xFFFFF000u;  // perto do wrap de 2^32, de proposito
}

// O codigo e o codigo. Um portao que aceite "quase" nao e portao.
static void test_so_o_codigo_certo_abre(void) {
    TEST_ASSERT_TRUE(ota::codigoCorreto(1976));
    TEST_ASSERT_FALSE(ota::codigoCorreto(1975));
    TEST_ASSERT_FALSE(ota::codigoCorreto(1977));
    TEST_ASSERT_FALSE(ota::codigoCorreto(0));
    TEST_ASSERT_FALSE(ota::codigoCorreto(1234));  // a senha do Modo Programacao NAO serve
}

static void test_nasce_fechado(void) {
    ota::ApGate p;
    TEST_ASSERT_FALSE(p.ativo());
    TEST_ASSERT_EQUAL_UINT16(0, p.restanteS(kT0));
    // Ticar um portao fechado nao pode abri-lo.
    p.tick(kT0, true, true);
    TEST_ASSERT_FALSE(p.ativo());
}

// O CASO COMUM DE VERDADE: o tecnico ativou, foi chamado para outra coisa e esqueceu. Nada chega,
// e o radio nao pode ficar ligado ate alguem passar por ali de novo.
static void test_ativou_e_nada_chegou_cai_sozinho(void) {
    ota::ApGate p;
    p.ativar(kT0);
    TEST_ASSERT_TRUE(p.ativo());

    p.tick(kT0 + 5u, false, false);
    TEST_ASSERT_TRUE(p.ativo());
    p.tick(kT0 + ota::kSemAtividadeMs - 1u, false, false);
    TEST_ASSERT_TRUE(p.ativo());
    p.tick(kT0 + ota::kSemAtividadeMs, false, false);
    TEST_ASSERT_FALSE(p.ativo());
}

// O CASO QUE O CRITERIO DE ATIVIDADE EXISTE PARA COBRIR, e que o criterio antigo - "ha cliente
// associado" - deixava passar: o celular fica no bolso, associado a rede do equipamento, por
// horas, sem pedir nada. Associacao nao e uso.
static void test_celular_associado_e_parado_nao_segura_o_radio(void) {
    ota::ApGate p;
    p.ativar(kT0);
    // Muitos tiques, cliente presente o tempo todo, mas NADA chegando.
    for (uint32_t t = kT0; ; t += 60000u) {
        p.tick(t, /*houveAtividade=*/false, false);
        if (!p.ativo()) {
            TEST_ASSERT_TRUE(static_cast<uint32_t>(t - kT0) >= ota::kSemAtividadeMs);
            return;
        }
        TEST_ASSERT_TRUE_MESSAGE(static_cast<uint32_t>(t - kT0) < ota::kSemAtividadeMs,
                                 "passou do prazo e continuou no ar");
    }
}

// E o prazo e o que o operador pediu: vinte minutos.
static void test_o_prazo_sem_atividade_e_de_vinte_minutos(void) {
    TEST_ASSERT_EQUAL_UINT32(20u * 60u * 1000u, ota::kSemAtividadeMs);
}

// Atividade RENOVA. Quem esta usando nao pode ter o radio derrubado embaixo.
static void test_atividade_renova_o_prazo(void) {
    ota::ApGate p;
    p.ativar(kT0);
    uint32_t t = kT0;
    // O passo fica logo abaixo do prazo de inatividade E o total, abaixo do teto: senao este
    // teste mediria o TETO e nao a renovacao, que foi o que aconteceu na primeira escrita dele.
    const uint32_t passo = ota::kSemAtividadeMs - 1000u;
    const uint32_t voltas = ota::kTetoMs / passo;
    TEST_ASSERT_TRUE_MESSAGE(voltas >= 2u, "sem duas voltas nao ha renovacao a observar");
    for (uint32_t i = 0; i < voltas; ++i) {
        t += passo;
        p.tick(t, true, false);
        TEST_ASSERT_TRUE(p.ativo());
    }
}

// ...e quando ela para, o prazo volta a correr do instante da ULTIMA coisa que chegou.
static void test_depois_que_a_atividade_para_o_prazo_corre_de_novo(void) {
    ota::ApGate p;
    p.ativar(kT0);
    const uint32_t ultimo = kT0 + 30000u;
    p.tick(ultimo, true, false);
    TEST_ASSERT_TRUE(p.ativo());

    p.tick(ultimo + ota::kSemAtividadeMs - 1u, false, false);
    TEST_ASSERT_TRUE(p.ativo());
    p.tick(ultimo + ota::kSemAtividadeMs, false, false);
    TEST_ASSERT_FALSE(p.ativo());
}

// A PROPRIA PAGINA consulta o estado a cada 700 ms enquanto estiver aberta. Uma aba esquecida
// aberta e atividade de verdade e renovaria o prazo para sempre. O teto existe so para isso.
static void test_o_teto_derruba_mesmo_com_uso_continuo(void) {
    ota::ApGate p;
    p.ativar(kT0);
    uint32_t t = kT0;
    // CONTA ITERACOES, e nao "t < kT0 + kTetoMs": kT0 esta perto do wrap de 2^32 de proposito, a
    // soma estoura, e a condicao daria falsa de cara - o laco nao rodaria e o teste passaria sem
    // testar nada. Foi exatamente o que aconteceu na primeira escrita deste arquivo.
    const uint32_t passoMs = 60000u;
    const uint32_t passos = ota::kTetoMs / passoMs + 5u;
    for (uint32_t i = 0; i < passos && p.ativo(); ++i) {
        t += passoMs;
        p.tick(t, true, false);
    }
    TEST_ASSERT_FALSE(p.ativo());
    TEST_ASSERT_TRUE(ota::kTetoMs > ota::kSemAtividadeMs);
}

// MAS O TETO NAO ATROPELA UMA GRAVACAO EM CURSO. Derrubar o radio no meio deixaria a particao
// ociosa pela metade e a maquina em alarme ate a sessao morrer por prazo proprio.
static void test_o_teto_nao_derruba_no_meio_de_uma_gravacao(void) {
    ota::ApGate p;
    p.ativar(kT0);
    p.tick(kT0 + ota::kTetoMs + 60000u, true, /*sessaoEmCurso=*/true);
    TEST_ASSERT_TRUE(p.ativo());
    // Acabou a gravacao: ai sim cai no tique seguinte.
    p.tick(kT0 + ota::kTetoMs + 60001u, true, false);
    TEST_ASSERT_FALSE(p.ativo());
}

// E o prazo de inatividade tambem nao pode matar uma sessao. O caso e real: durante a escrita na
// flash o laco fica preso dentro de handleClient() e pode nao ticar com atividade nova por
// segundos seguidos.
static void test_gravacao_em_curso_segura_tambem_o_prazo_de_inatividade(void) {
    ota::ApGate p;
    p.ativar(kT0);
    p.tick(kT0 + ota::kSemAtividadeMs + 1000u, false, /*sessaoEmCurso=*/true);
    TEST_ASSERT_TRUE(p.ativo());
}

// Reativar RENOVA: e o gesto de quem esta na frente do painel dizendo "ainda estou aqui".
static void test_reativar_renova_os_prazos(void) {
    ota::ApGate p;
    p.ativar(kT0);
    p.tick(kT0 + ota::kSemAtividadeMs - 1u, false, false);
    TEST_ASSERT_TRUE(p.ativo());

    p.ativar(kT0 + ota::kSemAtividadeMs - 1u);
    p.tick(kT0 + ota::kSemAtividadeMs + 100u, false, false);
    TEST_ASSERT_TRUE_MESSAGE(p.ativo(), "reativar tem de zerar o relogio");
}

static void test_desativar_fecha_na_hora(void) {
    ota::ApGate p;
    p.ativar(kT0);
    p.tick(kT0 + 100u, true, false);
    p.desativar();
    TEST_ASSERT_FALSE(p.ativo());
    TEST_ASSERT_EQUAL_UINT16(0, p.restanteS(kT0 + 200u));
}

// A supervisora fica energizada meses; um portao aberto perto do wrap de 2^32 nao pode cair na
// hora nem deixar de cair nunca.
static void test_os_prazos_atravessam_o_wrap_de_2_elevado_a_32(void) {
    ota::ApGate p;
    const uint32_t t = 0xFFFFFF00u;
    p.ativar(t);
    p.tick(t + 5u, false, false);
    TEST_ASSERT_TRUE(p.ativo());
    p.tick(t + ota::kSemAtividadeMs / 2u, false, false);
    TEST_ASSERT_TRUE(p.ativo());
    p.tick(t + ota::kSemAtividadeMs - 1u, false, false);
    TEST_ASSERT_TRUE(p.ativo());
    p.tick(t + ota::kSemAtividadeMs, false, false);
    TEST_ASSERT_FALSE(p.ativo());
}

// O que a tela mostra tem de encolher com o tempo, senao e so enfeite.
static void test_o_tempo_restante_encolhe_e_nunca_mente(void) {
    ota::ApGate p;
    p.ativar(kT0);
    const uint16_t inicio = p.restanteS(kT0);
    TEST_ASSERT_EQUAL_UINT16(ota::kSemAtividadeMs / 1000u, inicio);

    const uint16_t meio = p.restanteS(kT0 + ota::kSemAtividadeMs / 2u);
    TEST_ASSERT_TRUE(meio < inicio);
    TEST_ASSERT_TRUE(meio > 0);

    TEST_ASSERT_EQUAL_UINT16(0, p.restanteS(kT0 + ota::kSemAtividadeMs));
    // E nunca pode passar do teto, mesmo com atividade renovando o prazo ocioso.
    ota::ApGate q;
    q.ativar(kT0);
    q.tick(kT0 + ota::kTetoMs - 30000u, true, false);
    TEST_ASSERT_TRUE(q.restanteS(kT0 + ota::kTetoMs - 30000u) <= 30u);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_so_o_codigo_certo_abre);
    RUN_TEST(test_nasce_fechado);
    RUN_TEST(test_ativou_e_nada_chegou_cai_sozinho);
    RUN_TEST(test_celular_associado_e_parado_nao_segura_o_radio);
    RUN_TEST(test_o_prazo_sem_atividade_e_de_vinte_minutos);
    RUN_TEST(test_atividade_renova_o_prazo);
    RUN_TEST(test_depois_que_a_atividade_para_o_prazo_corre_de_novo);
    RUN_TEST(test_o_teto_derruba_mesmo_com_uso_continuo);
    RUN_TEST(test_o_teto_nao_derruba_no_meio_de_uma_gravacao);
    RUN_TEST(test_gravacao_em_curso_segura_tambem_o_prazo_de_inatividade);
    RUN_TEST(test_reativar_renova_os_prazos);
    RUN_TEST(test_desativar_fecha_na_hora);
    RUN_TEST(test_os_prazos_atravessam_o_wrap_de_2_elevado_a_32);
    RUN_TEST(test_o_tempo_restante_encolhe_e_nunca_mente);
    return UNITY_END();
}
