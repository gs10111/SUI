// NOME E SENHA DO PONTO DE ACESSO. O que estes testes protegem nao e criptografia - a senha
// padrao esta no firmware e o firmware vai para o cliente, o que e escolha declarada da
// Decisao 17. O que eles protegem e o equipamento nao ficar ABERTO, sem senha nenhuma.
//
// O caso que mata: WiFi.softAP(ssid, senha) com uma senha de 7 caracteres nao devolve erro - sobe
// o ponto de acesso ABERTO. Uma senha mal formada vinda da producao viraria, em silencio, um
// equipamento sem senha nenhuma.
#include <string.h>
#include <unity.h>

#include "ota_credentials.h"

void setUp(void) {}
void tearDown(void) {}

namespace {
const uint8_t kMacA[ota::kMacBytes] = {0x3C, 0x71, 0xBF, 0x12, 0x34, 0x56};
const uint8_t kMacB[ota::kMacBytes] = {0x3C, 0x71, 0xBF, 0x12, 0x34, 0x57};  // vizinho de bancada
}  // namespace

// Dois equipamentos no mesmo patio nao podem apresentar o mesmo nome: conectar no errado e subir
// firmware no equipamento errado.
static void test_o_ssid_distingue_a_placa_e_o_equipamento(void) {
    char a[ota::kSsidMaxChars + 1u];
    char b[ota::kSsidMaxChars + 1u];
    char sensora[ota::kSsidMaxChars + 1u];

    TEST_ASSERT_TRUE(ota::apSsid(ota::kAlvoSupervisora, kMacA, a, sizeof(a)) > 0);
    TEST_ASSERT_TRUE(ota::apSsid(ota::kAlvoSupervisora, kMacB, b, sizeof(b)) > 0);
    TEST_ASSERT_TRUE(ota::apSsid(ota::kAlvoSensora, kMacA, sensora, sizeof(sensora)) > 0);

    TEST_ASSERT_EQUAL_STRING("SUI-UR-123456", a);
    TEST_ASSERT_EQUAL_STRING("SUI-UR-123457", b);
    TEST_ASSERT_EQUAL_STRING("SUI-SEN-123456", sensora);
    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
    TEST_ASSERT_TRUE(strcmp(a, sensora) != 0);
}

// Um SSID truncado pela metade e um equipamento que ninguem acha. Melhor falhar e o chamador
// saber do que apresentar "SUI-UR-1234".
//
// E "devolveu 0" NAO BASTA como criterio: uma versao sem as guardas escreve fora do buffer e
// DEPOIS devolve 0, e o teste passaria por acaso enquanto o firmware corrompe a pilha. Por isso
// cada chamada roda com sentinela em volta e o teste confere que nada fora dos n bytes foi
// tocado. Foi um mutante sobrevivente que apontou isto.
static void test_ssid_que_nao_cabe_falha_sem_escrever_fora_do_buffer(void) {
    constexpr size_t kSentinela = 16;
    constexpr uint8_t kMarca = 0xAA;

    // "SUI-UR-123456" = 13 caracteres + terminador = 14 bytes. Varre de 1 ate folgado.
    for (size_t n = 1; n <= 20; ++n) {
        char arena[20 + 2 * kSentinela];
        for (size_t i = 0; i < sizeof(arena); ++i) {
            arena[i] = static_cast<char>(kMarca);
        }
        char* buf = arena + kSentinela;
        const size_t escritos = ota::apSsid(ota::kAlvoSupervisora, kMacA, buf, n);

        if (n >= 14u) {
            TEST_ASSERT_EQUAL_UINT32(13, escritos);
            TEST_ASSERT_EQUAL_STRING("SUI-UR-123456", buf);
        } else {
            TEST_ASSERT_EQUAL_UINT32(0, escritos);
        }
        // Nada antes do buffer, e nada em nenhum byte a partir de n.
        for (size_t i = 0; i < kSentinela; ++i) {
            TEST_ASSERT_EQUAL_HEX8(kMarca, static_cast<uint8_t>(arena[i]));
        }
        for (size_t i = kSentinela + n; i < sizeof(arena); ++i) {
            TEST_ASSERT_EQUAL_HEX8(kMarca, static_cast<uint8_t>(arena[i]));
        }
    }

    TEST_ASSERT_EQUAL_UINT32(0, ota::apSsid(ota::kAlvoSupervisora, kMacA, nullptr, 64));
    char buf[32];
    TEST_ASSERT_EQUAL_UINT32(0, ota::apSsid(ota::kAlvoSupervisora, nullptr, buf, sizeof(buf)));
}

// A SENHA PADRAO DE FABRICA TEM DE SERVIR PARA WPA2. Ela e uma constante de texto num arquivo de
// cabecalho: uma edicao desatenta - um caractere a menos, um acento, um espaco no fim - passa por
// toda revisao humana e faz o softAP subir ABERTO em toda placa da frota na proxima gravacao.
// Este teste e a unica coisa entre essa edicao e o campo.
static void test_a_senha_padrao_serve_para_wpa2(void) {
    const char* padrao = ota::defaultPassword();
    TEST_ASSERT_NOT_NULL(padrao);
    TEST_ASSERT_TRUE_MESSAGE(ota::passwordWellFormed(padrao),
                             "a senha padrao nao serve para WPA2 - o softAP subiria ABERTO");
    TEST_ASSERT_TRUE(strlen(padrao) >= ota::kWpa2MinChars);
    TEST_ASSERT_TRUE(strlen(padrao) <= ota::kWpa2MaxChars);
}

// E TEM DE SER A QUE ESTA ESCRITA NA DOCUMENTACAO E NA ETIQUETA. Trocar a constante sem trocar
// docs/ota.md produz uma frota que ninguem consegue atualizar: a senha certa existe, esta no
// firmware, e nao esta em lugar nenhum que alguem leia.
static void test_a_senha_padrao_e_a_que_esta_documentada(void) {
    TEST_ASSERT_EQUAL_STRING("dieletrons-2025", ota::defaultPassword());
}

// A senha padrao NAO distingue um equipamento do outro - e essa e exatamente a propriedade que
// ela nao tem. O teste existe para que ninguem a confunda com a senha por equipamento da
// Decisao 15 item 8: quem quiser aquela propriedade tem de gravar senha em NVS na producao.
// O que continua distinguindo equipamentos e o SSID, e so ele.
static void test_a_senha_padrao_e_igual_em_toda_a_frota_e_o_ssid_nao(void) {
    char a[ota::kSsidMaxChars + 1u];
    char b[ota::kSsidMaxChars + 1u];
    ota::apSsid(ota::kAlvoSupervisora, kMacA, a, sizeof(a));
    ota::apSsid(ota::kAlvoSupervisora, kMacB, b, sizeof(b));
    TEST_ASSERT_TRUE_MESSAGE(strcmp(a, b) != 0, "o SSID tem de distinguir equipamentos");
    // A senha nao tem argumento nenhum: nao ha o que variar por placa.
    TEST_ASSERT_EQUAL_STRING(ota::defaultPassword(), ota::defaultPassword());
}

// O CASO QUE MATA. WiFi.softAP() com senha de 7 caracteres NAO devolve erro: sobe o ponto de
// acesso aberto. Quem chama tem de recusar antes.
static void test_senha_que_o_wpa2_recusaria_e_reprovada_aqui(void) {
    TEST_ASSERT_FALSE(ota::passwordWellFormed(nullptr));
    TEST_ASSERT_FALSE(ota::passwordWellFormed(""));
    TEST_ASSERT_FALSE(ota::passwordWellFormed("1234567"));   // 7: um a menos
    TEST_ASSERT_TRUE(ota::passwordWellFormed("12345678"));   // 8: o minimo
    TEST_ASSERT_FALSE(ota::passwordWellFormed("senha\tcom\ttab"));
    TEST_ASSERT_FALSE(ota::passwordWellFormed("acentuacao\xC3\xA7"));

    char longa[ota::kWpa2MaxChars + 3u];
    for (size_t i = 0; i < sizeof(longa) - 1u; ++i) {
        longa[i] = 'A';
    }
    longa[ota::kWpa2MaxChars] = '\0';
    TEST_ASSERT_TRUE(ota::passwordWellFormed(longa));   // 63: o maximo
    longa[ota::kWpa2MaxChars] = 'A';
    longa[ota::kWpa2MaxChars + 1u] = '\0';
    TEST_ASSERT_FALSE(ota::passwordWellFormed(longa));  // 64: um a mais
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_o_ssid_distingue_a_placa_e_o_equipamento);
    RUN_TEST(test_ssid_que_nao_cabe_falha_sem_escrever_fora_do_buffer);
    RUN_TEST(test_a_senha_padrao_serve_para_wpa2);
    RUN_TEST(test_a_senha_padrao_e_a_que_esta_documentada);
    RUN_TEST(test_a_senha_padrao_e_igual_em_toda_a_frota_e_o_ssid_nao);
    RUN_TEST(test_senha_que_o_wpa2_recusaria_e_reprovada_aqui);
    return UNITY_END();
}
