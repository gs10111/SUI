// Prova de contrato do fake de IParameterStore (LSP).
//
// O fake so vale se for substituivel pelo NvsParameterStore sem que o dominio perceba: mesmas
// pre-condicoes, mesma semantica de erro, mesmos limites, mesmos numeros publicados. Um fake
// mais permissivo que o alvo faz a suite inteira mentir - o dominio passa no host e falha na
// placa. Cada teste aqui prende UM ponto em que o fake poderia ser generoso demais, e cada um
// deles tem a linha correspondente em src/adapters/nvs_parameter_store.cpp:
//
//   NotInit antes do begin()          -> write/read/erase comecam por 'if (!ready_)'
//   Param antes de tocar a midia      -> slot fora da enum, ponteiro nulo, cap 0, len 0/acima
//   Storage de slot nunca escrito     -> getBytesLength() == 0
//   Param (NAO Range) de buffer curto -> a porta e explicita; o firmware de fabrica divergia
//   Storage de releitura divergente   -> a comparacao byte a byte antes do kOk
//   erase() idempotente               -> apagar slot ausente e kOk
//   48 B e 250 ms                     -> os numeros da porta, nao os do adaptador
//   writeCount so apos a releitura    -> ++writeCount_ e a ultima linha de write()
//
// E os tres injetores existem para o unico caminho que na placa exigiria corte de energia com
// osciloscopio: escrita truncada, corrupcao de um byte e Err::Storage sob demanda.
#include <string.h>
#include <unity.h>

#include "fakes/fake_parameter_store.h"

using test::FakeParameterStore;

#define ASSERT_ERR(esperado, resultado) \
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(esperado), static_cast<uint8_t>((resultado).err))

void setUp(void) {}
void tearDown(void) {}

namespace {

// Registro de 48 B, o tamanho que a porta publica. O conteudo nao importa: este armazem move
// bytes e nao interpreta nenhum deles.
constexpr uint16_t kLen = 48;

void fill(uint8_t* buf, uint16_t len, uint8_t seed) {
    for (uint16_t i = 0; i < len; ++i) {
        buf[i] = static_cast<uint8_t>(seed + i);
    }
}

}  // namespace

// --- pre-condicao: nada funciona antes do begin() ---

static void test_recusa_tudo_antes_do_begin(void) {
    FakeParameterStore store;
    uint8_t buf[kLen];
    uint16_t outLen = 0xFFFFu;
    fill(buf, kLen, 1);

    TEST_ASSERT_FALSE(store.ready());
    TEST_ASSERT_FALSE(store.exists(ParamSlot::BankA));
    ASSERT_ERR(Err::NotInit, store.read(ParamSlot::BankA, buf, kLen, outLen));
    ASSERT_ERR(Err::NotInit, store.write(ParamSlot::BankA, buf, kLen));
    ASSERT_ERR(Err::NotInit, store.erase(ParamSlot::BankA));
}

static void test_notinit_vence_argumento_invalido(void) {
    // A ordem importa: no adaptador a checagem de prontidao vem ANTES da de parametro. Um fake
    // que devolvesse Err::Param aqui esconderia um driver nao inicializado.
    FakeParameterStore store;
    uint16_t outLen = 0;
    ASSERT_ERR(Err::NotInit, store.read(ParamSlot::BankA, nullptr, 0, outLen));
    ASSERT_ERR(Err::NotInit, store.write(ParamSlot::BankA, nullptr, 0));
}

static void test_begin_pode_reprovar_e_o_store_fica_nao_pronto(void) {
    // Particao virgem ou corrompida que nem o apagamento recupera: begin() devolve Err::Storage
    // e o dominio ve store nao pronto - comportamento seguro, e ele tem de ser testavel.
    FakeParameterStore store;
    store.setBeginResult(Err::Storage);
    ASSERT_ERR(Err::Storage, store.begin());
    TEST_ASSERT_FALSE(store.ready());
}

// --- limites publicados ---

static void test_numeros_publicados_sao_os_da_porta(void) {
    FakeParameterStore store;
    TEST_ASSERT_EQUAL_UINT16(48u, store.capacityBytes());
    TEST_ASSERT_EQUAL_UINT32(250u, store.writeBudgetMs());
}

static void test_recusa_len_zero_e_len_acima_da_capacidade(void) {
    FakeParameterStore store;
    uint8_t buf[kLen + 16];
    fill(buf, sizeof(buf), 7);
    TEST_ASSERT_TRUE(store.begin().ok());

    ASSERT_ERR(Err::Param, store.write(ParamSlot::BankA, buf, 0));
    ASSERT_ERR(Err::Param, store.write(ParamSlot::BankA, buf, kLen + 1));
    ASSERT_ERR(Err::Param, store.write(ParamSlot::BankA, nullptr, kLen));
    // e nenhuma dessas recusas pode ter tocado a midia
    TEST_ASSERT_FALSE(store.exists(ParamSlot::BankA));
}

static void test_recusa_slot_fora_da_enumeracao(void) {
    FakeParameterStore store;
    uint8_t buf[kLen];
    uint16_t outLen = 0;
    fill(buf, kLen, 3);
    TEST_ASSERT_TRUE(store.begin().ok());

    const ParamSlot invalido = static_cast<ParamSlot>(kParamSlotCount);
    ASSERT_ERR(Err::Param, store.write(invalido, buf, kLen));
    ASSERT_ERR(Err::Param, store.read(invalido, buf, kLen, outLen));
    ASSERT_ERR(Err::Param, store.erase(invalido));
    TEST_ASSERT_FALSE(store.exists(invalido));
    TEST_ASSERT_EQUAL_UINT32(0u, store.writeCount(invalido));
    TEST_ASSERT_EQUAL_STRING("?", store.slotName(invalido));
}

static void test_recusa_leitura_com_destino_nulo_ou_cap_zero(void) {
    FakeParameterStore store;
    uint8_t buf[kLen];
    uint16_t outLen = 0xFFFFu;
    fill(buf, kLen, 5);
    TEST_ASSERT_TRUE(store.begin().ok());
    TEST_ASSERT_TRUE(store.write(ParamSlot::BankA, buf, kLen).ok());

    ASSERT_ERR(Err::Param, store.read(ParamSlot::BankA, nullptr, kLen, outLen));
    TEST_ASSERT_EQUAL_UINT16(0u, outLen);
    ASSERT_ERR(Err::Param, store.read(ParamSlot::BankA, buf, 0, outLen));
    TEST_ASSERT_EQUAL_UINT16(0u, outLen);
}

// --- ida e volta ---

static void test_grava_e_rele_o_mesmo_conteudo(void) {
    FakeParameterStore store;
    uint8_t escrito[kLen];
    uint8_t lido[kLen];
    uint16_t outLen = 0;
    fill(escrito, kLen, 0x20);
    memset(lido, 0, sizeof(lido));
    TEST_ASSERT_TRUE(store.begin().ok());

    ASSERT_ERR(Err::Ok, store.write(ParamSlot::BankA, escrito, kLen));
    TEST_ASSERT_TRUE(store.exists(ParamSlot::BankA));
    ASSERT_ERR(Err::Ok, store.read(ParamSlot::BankA, lido, kLen, outLen));
    TEST_ASSERT_EQUAL_UINT16(kLen, outLen);
    TEST_ASSERT_EQUAL_INT(0, memcmp(escrito, lido, kLen));
}

static void test_slot_nunca_escrito_e_storage_nao_param(void) {
    FakeParameterStore store;
    uint8_t lido[kLen];
    uint16_t outLen = 0xFFFFu;
    TEST_ASSERT_TRUE(store.begin().ok());

    ASSERT_ERR(Err::Storage, store.read(ParamSlot::FactoryCal, lido, kLen, outLen));
    TEST_ASSERT_EQUAL_UINT16(0u, outLen);
    TEST_ASSERT_FALSE(store.exists(ParamSlot::FactoryCal));
}

static void test_buffer_curto_e_param_e_nao_range(void) {
    // Ponto em que o firmware de teste de fabrica divergia (Err::Range). A porta manda Param.
    FakeParameterStore store;
    uint8_t escrito[kLen];
    uint8_t curto[kLen - 1];
    uint16_t outLen = 0xFFFFu;
    fill(escrito, kLen, 0x40);
    TEST_ASSERT_TRUE(store.begin().ok());
    TEST_ASSERT_TRUE(store.write(ParamSlot::BankB, escrito, kLen).ok());

    ASSERT_ERR(Err::Param, store.read(ParamSlot::BankB, curto, sizeof(curto), outLen));
    TEST_ASSERT_EQUAL_UINT16(0u, outLen);
}

static void test_buffer_maior_que_o_gravado_devolve_o_comprimento_gravado(void) {
    FakeParameterStore store;
    uint8_t escrito[20];
    uint8_t lido[kLen];
    uint16_t outLen = 0;
    fill(escrito, sizeof(escrito), 0x11);
    TEST_ASSERT_TRUE(store.begin().ok());
    TEST_ASSERT_TRUE(store.write(ParamSlot::FactoryCal, escrito, sizeof(escrito)).ok());

    ASSERT_ERR(Err::Ok, store.read(ParamSlot::FactoryCal, lido, kLen, outLen));
    TEST_ASSERT_EQUAL_UINT16(20u, outLen);
    TEST_ASSERT_EQUAL_INT(0, memcmp(escrito, lido, 20));
}

// --- banco duplo: uma chave nunca toca a outra ---

static void test_escrever_um_banco_nao_toca_no_outro(void) {
    // E daqui, e so daqui, que vem a atomicidade da decisao 2: nao ha transacao da NVS, ha
    // alternancia de chave. Se o fake deixasse um write vazar para o outro slot, o teste de
    // auto-cura do ParamStoreLogic passaria por motivo errado.
    FakeParameterStore store;
    uint8_t a[kLen];
    uint8_t b[kLen];
    uint8_t lido[kLen];
    uint16_t outLen = 0;
    fill(a, kLen, 0xA0);
    fill(b, kLen, 0x0B);
    TEST_ASSERT_TRUE(store.begin().ok());
    TEST_ASSERT_TRUE(store.write(ParamSlot::BankA, a, kLen).ok());
    TEST_ASSERT_TRUE(store.write(ParamSlot::BankB, b, kLen).ok());

    store.truncateNextWrite(8);
    ASSERT_ERR(Err::Storage, store.write(ParamSlot::BankA, a, kLen));

    ASSERT_ERR(Err::Ok, store.read(ParamSlot::BankB, lido, kLen, outLen));
    TEST_ASSERT_EQUAL_UINT16(kLen, outLen);
    TEST_ASSERT_EQUAL_INT(0, memcmp(b, lido, kLen));
}

// --- injecao: os tres pontos de corte de energia ---

static void test_escrita_truncada_reprova_e_deixa_o_slot_presente(void) {
    // Corte depois do nvs_set_blob: a chave JA esta na midia com conteudo curto. exists() tem
    // de dizer PRESENTE - e o dominio, com o CRC, e que declara o banco reprovado. Um fake que
    // respondesse "ausente" mandaria a auto-cura pelo ramo errado (decisao 2, itens 4 e 10).
    FakeParameterStore store;
    uint8_t escrito[kLen];
    uint16_t outLen = 0;
    fill(escrito, kLen, 0x55);
    TEST_ASSERT_TRUE(store.begin().ok());

    store.truncateNextWrite(16);
    ASSERT_ERR(Err::Storage, store.write(ParamSlot::BankA, escrito, kLen));
    TEST_ASSERT_TRUE(store.exists(ParamSlot::BankA));
    TEST_ASSERT_EQUAL_UINT16(16u, store.rawLen(ParamSlot::BankA));
    TEST_ASSERT_EQUAL_UINT32(0u, store.writeCount(ParamSlot::BankA));

    uint8_t lido[kLen];
    ASSERT_ERR(Err::Ok, store.read(ParamSlot::BankA, lido, kLen, outLen));
    TEST_ASSERT_EQUAL_UINT16(16u, outLen);
}

static void test_byte_corrompido_reprova_na_releitura(void) {
    FakeParameterStore store;
    uint8_t escrito[kLen];
    fill(escrito, kLen, 0x77);
    TEST_ASSERT_TRUE(store.begin().ok());

    store.corruptNextWrite(31, 0x01);
    ASSERT_ERR(Err::Storage, store.write(ParamSlot::BankB, escrito, kLen));
    TEST_ASSERT_TRUE(store.exists(ParamSlot::BankB));
    TEST_ASSERT_EQUAL_UINT16(kLen, store.rawLen(ParamSlot::BankB));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(escrito[31] ^ 0x01),
                            store.rawData(ParamSlot::BankB)[31]);
    TEST_ASSERT_EQUAL_UINT32(0u, store.writeCount(ParamSlot::BankB));
}

static void test_falha_de_midia_antes_da_escrita_preserva_o_conteudo(void) {
    FakeParameterStore store;
    uint8_t bom[kLen];
    uint8_t novo[kLen];
    uint8_t lido[kLen];
    uint16_t outLen = 0;
    fill(bom, kLen, 0x10);
    fill(novo, kLen, 0x90);
    TEST_ASSERT_TRUE(store.begin().ok());
    TEST_ASSERT_TRUE(store.write(ParamSlot::BankA, bom, kLen).ok());

    store.failNextWriteStorage();
    ASSERT_ERR(Err::Storage, store.write(ParamSlot::BankA, novo, kLen));

    ASSERT_ERR(Err::Ok, store.read(ParamSlot::BankA, lido, kLen, outLen));
    TEST_ASSERT_EQUAL_INT(0, memcmp(bom, lido, kLen));
    TEST_ASSERT_EQUAL_UINT32(1u, store.writeCount(ParamSlot::BankA));
}

static void test_falha_de_midia_na_leitura_e_storage(void) {
    FakeParameterStore store;
    uint8_t escrito[kLen];
    uint8_t lido[kLen];
    uint16_t outLen = 0xFFFFu;
    fill(escrito, kLen, 0x33);
    TEST_ASSERT_TRUE(store.begin().ok());
    TEST_ASSERT_TRUE(store.write(ParamSlot::BankA, escrito, kLen).ok());

    store.failNextReadStorage();
    ASSERT_ERR(Err::Storage, store.read(ParamSlot::BankA, lido, kLen, outLen));
    TEST_ASSERT_EQUAL_UINT16(0u, outLen);
    ASSERT_ERR(Err::Ok, store.read(ParamSlot::BankA, lido, kLen, outLen));
}

// --- contagem de escritas e apagamento ---

static void test_writecount_so_conta_gravacao_verificada(void) {
    FakeParameterStore store;
    uint8_t escrito[kLen];
    fill(escrito, kLen, 0x21);
    TEST_ASSERT_TRUE(store.begin().ok());

    TEST_ASSERT_EQUAL_UINT32(0u, store.writeCount(ParamSlot::BankA));
    TEST_ASSERT_TRUE(store.write(ParamSlot::BankA, escrito, kLen).ok());
    TEST_ASSERT_EQUAL_UINT32(1u, store.writeCount(ParamSlot::BankA));

    store.corruptNextWrite(0, 0xFF);
    ASSERT_ERR(Err::Storage, store.write(ParamSlot::BankA, escrito, kLen));
    TEST_ASSERT_EQUAL_UINT32(1u, store.writeCount(ParamSlot::BankA));

    // e a telemetria de desgaste e por slot, nao global
    TEST_ASSERT_EQUAL_UINT32(0u, store.writeCount(ParamSlot::BankB));
}

static void test_erase_e_idempotente_e_zera_o_exists(void) {
    FakeParameterStore store;
    uint8_t escrito[kLen];
    uint8_t lido[kLen];
    uint16_t outLen = 0;
    fill(escrito, kLen, 0x60);
    TEST_ASSERT_TRUE(store.begin().ok());
    TEST_ASSERT_TRUE(store.write(ParamSlot::BankA, escrito, kLen).ok());

    ASSERT_ERR(Err::Ok, store.erase(ParamSlot::BankA));
    TEST_ASSERT_FALSE(store.exists(ParamSlot::BankA));
    ASSERT_ERR(Err::Storage, store.read(ParamSlot::BankA, lido, kLen, outLen));

    // Reset Geral pode apagar um banco que a queda de energia anterior ja deixou sem gravar
    ASSERT_ERR(Err::Ok, store.erase(ParamSlot::BankA));
    ASSERT_ERR(Err::Ok, store.erase(ParamSlot::FactoryCal));
}

static void test_erase_nao_zera_a_telemetria_de_desgaste(void) {
    // writeCount e desgaste de flash desde o boot; apagar a chave nao desgasta menos.
    FakeParameterStore store;
    uint8_t escrito[kLen];
    fill(escrito, kLen, 0x71);
    TEST_ASSERT_TRUE(store.begin().ok());
    TEST_ASSERT_TRUE(store.write(ParamSlot::BankA, escrito, kLen).ok());
    TEST_ASSERT_TRUE(store.erase(ParamSlot::BankA).ok());
    TEST_ASSERT_EQUAL_UINT32(1u, store.writeCount(ParamSlot::BankA));
}

static void test_nomes_de_slot_sao_as_chaves_da_nvs(void) {
    FakeParameterStore store;
    TEST_ASSERT_EQUAL_STRING("par_a", store.slotName(ParamSlot::BankA));
    TEST_ASSERT_EQUAL_STRING("par_b", store.slotName(ParamSlot::BankB));
    TEST_ASSERT_EQUAL_STRING("cal_fab", store.slotName(ParamSlot::FactoryCal));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_recusa_tudo_antes_do_begin);
    RUN_TEST(test_notinit_vence_argumento_invalido);
    RUN_TEST(test_begin_pode_reprovar_e_o_store_fica_nao_pronto);
    RUN_TEST(test_numeros_publicados_sao_os_da_porta);
    RUN_TEST(test_recusa_len_zero_e_len_acima_da_capacidade);
    RUN_TEST(test_recusa_slot_fora_da_enumeracao);
    RUN_TEST(test_recusa_leitura_com_destino_nulo_ou_cap_zero);
    RUN_TEST(test_grava_e_rele_o_mesmo_conteudo);
    RUN_TEST(test_slot_nunca_escrito_e_storage_nao_param);
    RUN_TEST(test_buffer_curto_e_param_e_nao_range);
    RUN_TEST(test_buffer_maior_que_o_gravado_devolve_o_comprimento_gravado);
    RUN_TEST(test_escrever_um_banco_nao_toca_no_outro);
    RUN_TEST(test_escrita_truncada_reprova_e_deixa_o_slot_presente);
    RUN_TEST(test_byte_corrompido_reprova_na_releitura);
    RUN_TEST(test_falha_de_midia_antes_da_escrita_preserva_o_conteudo);
    RUN_TEST(test_falha_de_midia_na_leitura_e_storage);
    RUN_TEST(test_writecount_so_conta_gravacao_verificada);
    RUN_TEST(test_erase_e_idempotente_e_zera_o_exists);
    RUN_TEST(test_erase_nao_zera_a_telemetria_de_desgaste);
    RUN_TEST(test_nomes_de_slot_sao_as_chaves_da_nvs);
    return UNITY_END();
}
