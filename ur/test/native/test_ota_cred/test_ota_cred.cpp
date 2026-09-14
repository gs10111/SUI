// NOME E SENHA DO PONTO DE ACESSO. O que estes testes protegem nao e criptografia - e o
// equipamento nao ficar gravavel por quem passar perto do patio.
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

// A senha derivada tem de servir para WPA2 - senao o caminho degradado sobe um ponto de acesso
// aberto, que e pior do que nao subir.
static void test_a_senha_derivada_serve_para_wpa2(void) {
    char pw[ota::kPasswordChars + 1u];
    ota::derivedPassword(kMacA, pw);
    TEST_ASSERT_EQUAL_UINT32(ota::kPasswordChars, strlen(pw));
    TEST_ASSERT_TRUE(ota::passwordWellFormed(pw));
    TEST_ASSERT_TRUE(ota::kPasswordChars >= ota::kWpa2MinChars);
}

// Alguem vai ler isto de uma etiqueta pequena e digitar no celular. 0/O e 1/I/L sao o mesmo
// caractere para um olho cansado.
static void test_a_senha_nao_usa_caracteres_que_se_confundem_na_etiqueta(void) {
    char pw[ota::kPasswordChars + 1u];
    const uint8_t* macs[2] = {kMacA, kMacB};
    for (int m = 0; m < 2; ++m) {
        ota::derivedPassword(macs[m], pw);
        for (size_t i = 0; i < ota::kPasswordChars; ++i) {
            TEST_ASSERT_TRUE(strchr("0O1IL", pw[i]) == nullptr);
            TEST_ASSERT_TRUE(strchr(ota::passwordAlphabet(), pw[i]) != nullptr);
        }
    }
    // E o alfabeto tem de ter mesmo 32 entradas: um alfabeto menor faria o resto enviesar as
    // primeiras letras.
    TEST_ASSERT_EQUAL_UINT32(ota::kAlphabetSize, strlen(ota::passwordAlphabet()));
}

// UM RESUMO DE 32 BYTES USADO ERRADO da uma senha de 12 caracteres IGUAIS - deterministica,
// diferente entre placas, toda dentro do alfabeto, e com 5 bits de entropia em vez de 60. Passou
// por todos os outros testes daqui; foi um mutante sobrevivente que apontou a falta deste.
static void test_a_senha_usa_o_resumo_inteiro_e_nao_um_byte_so(void) {
    const uint8_t macs[4][ota::kMacBytes] = {{0x3C, 0x71, 0xBF, 0x12, 0x34, 0x56},
                                             {0x3C, 0x71, 0xBF, 0x12, 0x34, 0x57},
                                             {0x24, 0x6F, 0x28, 0x00, 0x00, 0x01},
                                             {0xA4, 0xCF, 0x12, 0xFF, 0xFE, 0xFD}};
    for (int m = 0; m < 4; ++m) {
        char pw[ota::kPasswordChars + 1u];
        ota::derivedPassword(macs[m], pw);
        bool visto[256] = {false};
        size_t distintos = 0;
        for (size_t i = 0; i < ota::kPasswordChars; ++i) {
            const unsigned char c = static_cast<unsigned char>(pw[i]);
            if (!visto[c]) {
                visto[c] = true;
                ++distintos;
            }
        }
        // Com 12 sorteios em 32 simbolos, menos de 6 distintos e praticamente impossivel por
        // acaso e certeiro quando o resumo esta sendo desperdicado.
        TEST_ASSERT_TRUE(distintos >= 6);
    }
}

// Deterministica: o operador anotou a senha ha cinco minutos. Se ela mudar no reinicio, ele nao
// entra mais e o equipamento vira caminhonete.
static void test_a_senha_derivada_nao_muda_entre_reinicios(void) {
    char um[ota::kPasswordChars + 1u];
    char dois[ota::kPasswordChars + 1u];
    ota::derivedPassword(kMacA, um);
    ota::derivedPassword(kMacA, dois);
    TEST_ASSERT_EQUAL_STRING(um, dois);
}

// E dois equipamentos nao podem cair na mesma senha - um MAC de diferenca tem de mudar tudo.
static void test_equipamentos_diferentes_tem_senhas_diferentes(void) {
    char a[ota::kPasswordChars + 1u];
    char b[ota::kPasswordChars + 1u];
    ota::derivedPassword(kMacA, a);
    ota::derivedPassword(kMacB, b);
    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
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
    RUN_TEST(test_a_senha_derivada_serve_para_wpa2);
    RUN_TEST(test_a_senha_nao_usa_caracteres_que_se_confundem_na_etiqueta);
    RUN_TEST(test_a_senha_usa_o_resumo_inteiro_e_nao_um_byte_so);
    RUN_TEST(test_a_senha_derivada_nao_muda_entre_reinicios);
    RUN_TEST(test_equipamentos_diferentes_tem_senhas_diferentes);
    RUN_TEST(test_senha_que_o_wpa2_recusaria_e_reprovada_aqui);
    return UNITY_END();
}
