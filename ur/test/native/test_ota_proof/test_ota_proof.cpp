// A PROVA DE BOOT: o que uma imagem recem-subida tem de demonstrar antes de ser dada por boa.
//
// POR QUE ESTE MODULO EXISTE. Conferido no core instalado
// (cores/esp32/esp32-hal-misc.c:203-238, com CONFIG_APP_ROLLBACK_ENABLE=y): initArduino() chama
// esp_ota_mark_app_valid_cancel_rollback() ANTES do setup(), guardado apenas por dois simbolos
// FRACOS - verifyRollbackLater(), que devolve false, e verifyOta(), que devolve true. Ou seja,
// de fabrica o ESP32 marca como VALIDA qualquer imagem que apenas chegue a rodar initArduino().
//
// Consequencia pratica: uma sensora atualizada que sobe, mas nao fala com o SCL3300, fica
// marcada como boa e NAO ha rollback automatico. A 500 m, dentro de um modulo, isso e
// caminhonete com cabo USB.
//
// A saida e sobrescrever verifyRollbackLater() com uma definicao FORTE que devolve true - o core
// pula o bloco inteiro - e passar a decidir aqui, com criterio explicito. Este arquivo prende o
// criterio; o gancho em si e uma linha por placa.
//
// O QUE A PROVA NAO PODE SER. Nao pode ser "o firmware subiu": e exatamente o que o core ja
// fazia. Tem de ser evidencia de que a FUNCAO do equipamento voltou - para a UR, enlace com a
// sensora; para a sensora, o inclinometro respondendo. E tem de ter PRAZO: uma prova sem prazo
// deixa a particao em PENDING_VERIFY para sempre, e a placa reseta no proximo ciclo de energia
// com rollback, num laco que ninguem entende.
#include <unity.h>

#include "ota_proof.h"

void setUp(void) {}
void tearDown(void) {}

namespace {
constexpr uint32_t kT0 = 0xFFFFF000u;  // perto do wrap de 2^32, de proposito
}

static void test_nasce_provando_e_nao_aprovado(void) {
    ota::BootProof prova;
    prova.begin(kT0);
    TEST_ASSERT_TRUE(prova.verdict(kT0) == ota::ProofVerdict::Provando);
    TEST_ASSERT_EQUAL_UINT8(0, prova.goodCycles());
}

// ESQUECER begin() TEM DE FALHAR PARA O LADO SEGURO: sem ancora ninguem marca a particao como
// valida, ela fica em PENDING_VERIFY e o proximo ciclo de energia volta para a imagem anterior.
// Recusar a imagem nova e o erro certo; aceita-la sem prova e o errado.
static void test_sem_begin_nunca_aprova_e_nunca_reprova_por_prazo(void) {
    ota::BootProof prova;
    TEST_ASSERT_FALSE(prova.started());
    TEST_ASSERT_TRUE(prova.verdict(kT0 + ota::kProofDeadlineMs * 10u) == ota::ProofVerdict::Provando);
    for (uint8_t i = 0; i < ota::kProofGoodCycles * 3u; ++i) {
        prova.noteGood(kT0 + i);
    }
    TEST_ASSERT_FALSE(prova.approved());
}

static void test_aprova_com_ciclos_bons_suficientes(void) {
    ota::BootProof prova;
    prova.begin(kT0);
    for (uint8_t i = 1; i < ota::kProofGoodCycles; ++i) {
        prova.noteGood(kT0 + i);
        TEST_ASSERT_TRUE(prova.verdict(kT0 + i) == ota::ProofVerdict::Provando);
    }
    prova.noteGood(kT0 + ota::kProofGoodCycles);
    TEST_ASSERT_TRUE(prova.verdict(kT0 + ota::kProofGoodCycles) == ota::ProofVerdict::Aprovado);
}

// Um ciclo ruim ZERA a contagem. A prova e de funcionamento CONTINUO: cinco ciclos bons
// intercalados com ruins descrevem um equipamento intermitente, que e justamente o que nao pode
// ser dado por bom e marcado como valido para sempre.
static void test_ciclo_ruim_zera_a_contagem(void) {
    ota::BootProof prova;
    prova.begin(kT0);
    for (uint8_t i = 0; i < ota::kProofGoodCycles - 1u; ++i) {
        prova.noteGood(kT0 + i);
    }
    prova.noteBad();
    TEST_ASSERT_EQUAL_UINT8(0, prova.goodCycles());
    TEST_ASSERT_TRUE(prova.verdict(kT0 + 10u) == ota::ProofVerdict::Provando);
}

// PRAZO: sem ele a particao fica em PENDING_VERIFY para sempre.
static void test_reprova_quando_o_prazo_vence(void) {
    ota::BootProof prova;
    prova.begin(kT0);
    TEST_ASSERT_TRUE(prova.verdict(kT0 + ota::kProofDeadlineMs - 1u) == ota::ProofVerdict::Provando);
    TEST_ASSERT_TRUE(prova.verdict(kT0 + ota::kProofDeadlineMs) == ota::ProofVerdict::Reprovado);
}

static void test_o_prazo_atravessa_o_wrap_de_2_elevado_a_32(void) {
    // A UR fica energizada meses. Se a imagem subir perto do wrap, uma comparacao mal escrita
    // reprova na hora e a placa entra em rollback sem motivo.
    // kT0 = 0xFFFFF000: somar o prazo ATRAVESSA o wrap e aterrissa em 0x00006530. A subtracao
    // unsigned devolve exatamente kProofDeadlineMs; uma comparacao escrita como
    // "now > inicio + prazo" veria 0x6530 < 0xFFFFF000 e nunca reprovaria.
    ota::BootProof prova;
    prova.begin(kT0);
    TEST_ASSERT_TRUE(prova.verdict(kT0 + ota::kProofDeadlineMs - 1u) == ota::ProofVerdict::Provando);
    TEST_ASSERT_TRUE(prova.verdict(kT0 + ota::kProofDeadlineMs) == ota::ProofVerdict::Reprovado);
    ota::BootProof outra;
    outra.begin(kT0 + 0x100u);
    for (uint8_t i = 0; i < ota::kProofGoodCycles; ++i) {
        outra.noteGood(kT0 + 0x100u + i);
    }
    TEST_ASSERT_TRUE(outra.verdict(kT0 + 0x100u) == ota::ProofVerdict::Aprovado);
}

// APROVADO E DEFINITIVO dentro desta execucao: depois de marcada valida, a particao nao volta a
// PENDING_VERIFY, entao continuar "provando" seria mentira - e um ciclo ruim depois da aprovacao
// nao pode disparar rollback, que reiniciaria a placa em operacao normal.
static void test_aprovado_nao_volta_atras(void) {
    ota::BootProof prova;
    prova.begin(kT0);
    for (uint8_t i = 0; i < ota::kProofGoodCycles; ++i) {
        prova.noteGood(kT0 + i);
    }
    TEST_ASSERT_TRUE(prova.verdict(kT0 + 10u) == ota::ProofVerdict::Aprovado);
    prova.noteBad();
    TEST_ASSERT_TRUE(prova.verdict(kT0 + 20u) == ota::ProofVerdict::Aprovado);
    TEST_ASSERT_TRUE(prova.verdict(kT0 + ota::kProofDeadlineMs + 1u) == ota::ProofVerdict::Aprovado);
}

// E o prazo tem de caber na realidade do equipamento: a UR leva 5 quadros bons mais 2000 ms de
// permanencia minima para sair de falha de enlace, e a sensora ate 1700 ms para se recuperar de
// um afundamento. Um prazo curto demais reprova uma imagem boa.
static void test_o_prazo_e_maior_que_a_recuperacao_normal_do_equipamento(void) {
    TEST_ASSERT_TRUE(ota::kProofDeadlineMs >= 30000u);
    TEST_ASSERT_TRUE(ota::kProofGoodCycles >= 5u);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_nasce_provando_e_nao_aprovado);
    RUN_TEST(test_sem_begin_nunca_aprova_e_nunca_reprova_por_prazo);
    RUN_TEST(test_aprova_com_ciclos_bons_suficientes);
    RUN_TEST(test_ciclo_ruim_zera_a_contagem);
    RUN_TEST(test_reprova_quando_o_prazo_vence);
    RUN_TEST(test_o_prazo_atravessa_o_wrap_de_2_elevado_a_32);
    RUN_TEST(test_aprovado_nao_volta_atras);
    RUN_TEST(test_o_prazo_e_maior_que_a_recuperacao_normal_do_equipamento);
    return UNITY_END();
}
