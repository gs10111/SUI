// Testes da fila de slots sujos da NVS: app::PersistQueue, a maquina que decide qual dos dois
// registros de A8 gravar em cada passagem do loop() e o que fazer quando a gravacao reprova.
//
// POR QUE ESTA SUITE EXISTE. Ate a etapa 8 esta maquina era uma mascara de bits solta dentro de
// servicePersist(), no composition root, e o composition root nao tem suite. Ela tinha um
// defeito com consequencia de campo: limpava o bit ANTES de gravar e, na falha, zerava a
// MASCARA INTEIRA - apagando junto o bit do outro banco, que ainda nao tinha sido tentado. O
// caso concreto e o Reset Geral, unico caminho que suja os dois slots de proposito (decisao 1
// item 28) e que e fatiado em duas passagens do loop() para nao encostar dois commits de 250 ms
// nos 800 ms do token de liveness: falhando o BankA, o BankB nunca era tentado e a calibracao
// restaurada sumia da flash com uma unica mensagem generica "Falha de gravacao!". Na
// energizacao seguinte o bloco ausente ou velho voltava pela porta da NVS.
//
// O que esta preso aqui:
//  - decisao 2 item 16 / DECISIONS.md 2.1 item 5: UMA escrita de NVS por passagem, nunca duas;
//  - A8: os dois registros sao INDEPENDENTES, e a falha de um nao pode tocar no outro;
//  - o teto de tentativas, que existe para que uma NVS que reprova sempre nao vire apagamento
//    de setor a 20 Hz - o que e pior do que a perda que ele tenta evitar;
//  - a desistencia e por SLOT e e consumida uma unica vez, para a IHM nao repetir a mensagem a
//    cada passagem do loop().
#include <unity.h>

#include "app/persist_queue.h"

using app::PersistQueue;
using Slot = app::PersistQueue::Slot;

void setUp(void) {}
void tearDown(void) {}

namespace {

// Roda uma passagem do loop(): pega o slot da vez, "grava" com o resultado dado e devolve qual
// slot foi tentado. Devolve false quando nao havia nada a gravar.
bool umaPassagem(PersistQueue& fila, bool sucesso, Slot& tentado) {
    if (!fila.nextSlot(tentado)) {
        return false;
    }
    fila.noteResult(tentado, sucesso);
    return true;
}

}  // namespace

static void test_fila_vazia_nao_pede_gravacao_nenhuma(void) {
    PersistQueue fila;
    Slot alvo = Slot::Cal;
    TEST_ASSERT_FALSE(fila.anyDirty());
    TEST_ASSERT_FALSE(fila.nextSlot(alvo));
    TEST_ASSERT_FALSE(fila.dirty(Slot::Params));
    TEST_ASSERT_FALSE(fila.dirty(Slot::Cal));
}

static void test_uma_escrita_por_passagem_e_os_parametros_vao_na_frente(void) {
    // Gravar os dois na mesma passagem orcaria 500 ms de cache-off, cinco vezes o que a base
    // comum tolera sem fatiar. E quem vai na frente e o registro que COMANDA RELE.
    PersistQueue fila;
    fila.markDirty(Slot::Cal);
    fila.markDirty(Slot::Params);

    Slot tentado = Slot::Cal;
    TEST_ASSERT_TRUE(umaPassagem(fila, true, tentado));
    TEST_ASSERT_TRUE_MESSAGE(tentado == Slot::Params, "os parametros vao antes da calibracao");
    TEST_ASSERT_FALSE(fila.dirty(Slot::Params));
    TEST_ASSERT_TRUE_MESSAGE(fila.dirty(Slot::Cal), "o outro slot espera a proxima passagem");

    TEST_ASSERT_TRUE(umaPassagem(fila, true, tentado));
    TEST_ASSERT_TRUE(tentado == Slot::Cal);
    TEST_ASSERT_FALSE(fila.anyDirty());
}

static void test_O_ACHADO_falha_de_um_slot_NAO_apaga_o_outro(void) {
    // O defeito, virado teste. Reset Geral: os dois slots sujos, o primeiro reprova. O segundo
    // TEM de continuar na fila - era ele que a mascara zerada jogava fora, levando junto a
    // calibracao de fabrica restaurada e sem nenhum indicio alem de uma mensagem generica.
    PersistQueue fila;
    fila.markDirty(Slot::Params);
    fila.markDirty(Slot::Cal);

    Slot tentado = Slot::Cal;
    TEST_ASSERT_TRUE(umaPassagem(fila, false, tentado));
    TEST_ASSERT_TRUE(tentado == Slot::Params);
    TEST_ASSERT_TRUE_MESSAGE(fila.dirty(Slot::Cal),
                             "a falha do BankA nao pode apagar o BankB da fila");
    TEST_ASSERT_TRUE_MESSAGE(fila.dirty(Slot::Params),
                             "o slot que reprovou continua sujo ate o teto de tentativas");

    // E a calibracao chega a ser gravada, mesmo com os parametros ainda reprovando.
    bool calGravada = false;
    for (uint8_t passagem = 0; passagem < 6u; ++passagem) {
        if (!fila.nextSlot(tentado)) {
            break;
        }
        const bool sucesso = (tentado == Slot::Cal);
        fila.noteResult(tentado, sucesso);
        if (sucesso) {
            calGravada = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(calGravada, "o BankB tem de chegar a ser tentado e gravado");
    TEST_ASSERT_FALSE(fila.dirty(Slot::Cal));
}

static void test_o_slot_que_reprova_e_retentado_e_o_sucesso_limpa_o_bit(void) {
    PersistQueue fila;
    fila.markDirty(Slot::Params);

    Slot tentado = Slot::Cal;
    TEST_ASSERT_TRUE(umaPassagem(fila, false, tentado));
    TEST_ASSERT_EQUAL_UINT8(1u, fila.attempts(Slot::Params));
    TEST_ASSERT_TRUE(fila.dirty(Slot::Params));

    TEST_ASSERT_TRUE(umaPassagem(fila, false, tentado));
    TEST_ASSERT_EQUAL_UINT8(2u, fila.attempts(Slot::Params));
    TEST_ASSERT_TRUE(fila.dirty(Slot::Params));

    // A tentativa que da certo limpa o bit E zera o contador: um erro isolado nao pode encurtar
    // o orcamento da proxima gravacao.
    TEST_ASSERT_TRUE(umaPassagem(fila, true, tentado));
    TEST_ASSERT_FALSE(fila.dirty(Slot::Params));
    TEST_ASSERT_EQUAL_UINT8(0u, fila.attempts(Slot::Params));

    Slot desistiu = Slot::Params;
    TEST_ASSERT_FALSE_MESSAGE(fila.takeGaveUp(desistiu),
                              "recuperar na retentativa nao e desistir");
}

static void test_o_teto_e_TRES_tentativas_e_a_terceira_desiste(void) {
    // Numero escrito aqui, nao referenciado da classe: sem teto o loop() ficaria apagando setor
    // a 20 Hz para sempre; com teto grande demais o operador ficaria minutos sem saber.
    TEST_ASSERT_EQUAL_UINT8(3u, PersistQueue::kMaxAttempts);

    PersistQueue fila;
    fila.markDirty(Slot::Cal);
    Slot tentado = Slot::Params;
    Slot desistiu = Slot::Params;

    TEST_ASSERT_TRUE(umaPassagem(fila, false, tentado));
    TEST_ASSERT_TRUE(fila.dirty(Slot::Cal));
    TEST_ASSERT_FALSE(fila.takeGaveUp(desistiu));

    TEST_ASSERT_TRUE(umaPassagem(fila, false, tentado));
    TEST_ASSERT_TRUE_MESSAGE(fila.dirty(Slot::Cal), "a SEGUNDA falha ainda nao desiste");
    TEST_ASSERT_FALSE(fila.takeGaveUp(desistiu));

    TEST_ASSERT_TRUE(umaPassagem(fila, false, tentado));
    TEST_ASSERT_FALSE_MESSAGE(fila.dirty(Slot::Cal), "na TERCEIRA falha o slot sai da fila");
    TEST_ASSERT_TRUE(fila.takeGaveUp(desistiu));
    TEST_ASSERT_TRUE_MESSAGE(desistiu == Slot::Cal, "a desistencia nomeia o banco");

    // Consumo unico: a IHM mostra a mensagem uma vez, nao a cada passagem do loop().
    TEST_ASSERT_FALSE(fila.takeGaveUp(desistiu));
}

static void test_marcar_sujo_de_novo_devolve_o_orcamento_de_tentativas(void) {
    // Um pedido novo do operador e um pedido novo. Sem isto, um slot que desistiu de manha
    // entregaria a gravacao da tarde ja no ultimo credito.
    PersistQueue fila;
    fila.markDirty(Slot::Params);
    Slot tentado = Slot::Cal;
    for (uint8_t i = 0; i < PersistQueue::kMaxAttempts; ++i) {
        TEST_ASSERT_TRUE(umaPassagem(fila, false, tentado));
    }
    Slot desistiu = Slot::Cal;
    TEST_ASSERT_TRUE(fila.takeGaveUp(desistiu));

    fila.markDirty(Slot::Params);
    TEST_ASSERT_EQUAL_UINT8(0u, fila.attempts(Slot::Params));
    TEST_ASSERT_TRUE(umaPassagem(fila, false, tentado));
    TEST_ASSERT_TRUE_MESSAGE(fila.dirty(Slot::Params),
                             "com o orcamento renovado uma falha isolada nao desiste");
    TEST_ASSERT_FALSE(fila.takeGaveUp(desistiu));
}

static void test_marcar_sujo_duas_vezes_nao_faz_duas_gravacoes(void) {
    // O Modo Programacao publica e suja a cada confirmacao; a fila e um conjunto, nao um balde.
    PersistQueue fila;
    fila.markDirty(Slot::Params);
    fila.markDirty(Slot::Params);
    fila.markDirty(Slot::Params);

    Slot tentado = Slot::Cal;
    TEST_ASSERT_TRUE(umaPassagem(fila, true, tentado));
    TEST_ASSERT_FALSE_MESSAGE(fila.anyDirty(), "tres marcacoes valem uma gravacao");
}

static void test_slot_fora_do_enum_nao_corrompe_a_fila(void) {
    // O seletor pode vir de um cast (o mesmo argumento de Parameters::axisValid): enum de C++
    // nao restringe o valor castado, e um indice fora do vetor viraria escrita vizinha.
    PersistQueue fila;
    const Slot invalido = static_cast<Slot>(7);
    fila.markDirty(invalido);
    fila.noteResult(invalido, false);
    TEST_ASSERT_FALSE(fila.anyDirty());
    TEST_ASSERT_FALSE(fila.dirty(invalido));
    TEST_ASSERT_EQUAL_UINT8(0u, fila.attempts(invalido));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fila_vazia_nao_pede_gravacao_nenhuma);
    RUN_TEST(test_uma_escrita_por_passagem_e_os_parametros_vao_na_frente);
    RUN_TEST(test_O_ACHADO_falha_de_um_slot_NAO_apaga_o_outro);
    RUN_TEST(test_o_slot_que_reprova_e_retentado_e_o_sucesso_limpa_o_bit);
    RUN_TEST(test_o_teto_e_TRES_tentativas_e_a_terceira_desiste);
    RUN_TEST(test_marcar_sujo_de_novo_devolve_o_orcamento_de_tentativas);
    RUN_TEST(test_marcar_sujo_duas_vezes_nao_faz_duas_gravacoes);
    RUN_TEST(test_slot_fora_do_enum_nao_corrompe_a_fila);
    return UNITY_END();
}
