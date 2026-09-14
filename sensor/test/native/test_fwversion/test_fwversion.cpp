// O REGISTRADOR 6 (FW_VERSION) TEM DE DIZER A VERDADE.
//
// PENDENCIA P5 de docs/protocolo-rs485.md, aberta desde que o contrato existe. O registrador 6
// estava HARDCODED em (0 << 8) | 1 = 0x0001, com duas constantes kFwMajor/kFwMinor em
// sensor/src/main.cpp, completamente independentes do fw_version do platformio.ini.
//
// O defeito ficou invisivel por coincidencia: fw_version e "0.1.0", que codifica em 0x0001 -
// exatamente o valor cravado. Subir para 0.2.0 e o registrador continuaria dizendo 0x0001, e a
// UR receberia, 20 vezes por segundo, a versao errada da sensora com que esta falando.
//
// POR QUE ISSO IMPORTA AGORA. Sem versao honesta nao ha como verificar se um OTA subiu, nao ha
// como detectar rollback, e nao ha gate de compatibilidade: uma sensora atualizada que mude o
// significado ou a escala de qualquer registrador passa por TODOS os testes de transporte
// (byteCount, CRC, offsets) e a UR comanda os quatro reles com o numero errado, sem aviso.
#include <unity.h>

#include "sensor_map.h"

void setUp(void) {}
void tearDown(void) {}

static void test_codifica_maior_e_menor_nos_dois_bytes(void) {
    TEST_ASSERT_EQUAL_HEX16(0x0001, sensormap::fwVersionReg("0.1.0"));
    TEST_ASSERT_EQUAL_HEX16(0x0102, sensormap::fwVersionReg("1.2.3"));
    TEST_ASSERT_EQUAL_HEX16(0x0A0B, sensormap::fwVersionReg("10.11.12"));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, sensormap::fwVersionReg("255.255.0"));
}

// O PATCH NAO ENTRA, e isso e deliberado: sao dois bytes so, e o contrato de fio publica
// (major << 8) | minor. Duas versoes que diferem so no patch sao a MESMA para a UR.
static void test_o_patch_e_ignorado(void) {
    TEST_ASSERT_EQUAL_HEX16(sensormap::fwVersionReg("1.2.0"), sensormap::fwVersionReg("1.2.99"));
}

// Sem o terceiro campo, ou sem campo nenhum: o contrato nao pode depender de o
// platformio.ini estar bem formado.
static void test_formas_curtas_nao_inventam_numero(void) {
    TEST_ASSERT_EQUAL_HEX16(0x0102, sensormap::fwVersionReg("1.2"));
    TEST_ASSERT_EQUAL_HEX16(0x0100, sensormap::fwVersionReg("1"));
    TEST_ASSERT_EQUAL_HEX16(0x0000, sensormap::fwVersionReg(""));
    TEST_ASSERT_EQUAL_HEX16(0x0000, sensormap::fwVersionReg(nullptr));
}

// Campo acima de 255 satura no byte em vez de transbordar para o byte do vizinho: um major de
// 256 que virasse 0x0000 seria indistinguivel de "registrador nunca escrito".
static void test_campo_grande_satura_e_nao_transborda_para_o_vizinho(void) {
    TEST_ASSERT_EQUAL_HEX16(0xFF05, sensormap::fwVersionReg("300.5.0"));
    TEST_ASSERT_EQUAL_HEX16(0x01FF, sensormap::fwVersionReg("1.300.0"));
}

// Lixo em QUALQUER lugar invalida a string INTEIRA, e nao so o campo onde ele esta. Analisado
// campo a campo, "v1.2.3" devolveria 0x0002 - um numero plausivel, que passa por versao boa e vai
// para o fio. Um platformio.ini mal formado tem de produzir "nao publicou", nunca uma versao
// inventada.
static void test_lixo_em_qualquer_lugar_invalida_a_string_inteira(void) {
    TEST_ASSERT_EQUAL_HEX16(0x0000, sensormap::fwVersionReg("v1.2.3"));
    TEST_ASSERT_EQUAL_HEX16(0x0000, sensormap::fwVersionReg("1.x.3"));
    TEST_ASSERT_EQUAL_HEX16(0x0000, sensormap::fwVersionReg("1.2.3-rc1"));
    TEST_ASSERT_EQUAL_HEX16(0x0000, sensormap::fwVersionReg("1 2 3"));
    TEST_ASSERT_EQUAL_HEX16(0x0000, sensormap::fwVersionReg("..."));
}

// O QUE A BUILD DE VERDADE PUBLICA. Este e o teste que prende a pendencia P5: o valor do
// registrador tem de sair do FW_VERSION do platformio.ini, e nao de uma constante paralela.
static void test_a_versao_do_build_e_a_que_vai_para_o_fio(void) {
    TEST_ASSERT_EQUAL_HEX16(sensormap::fwVersionReg(FW_VERSION), sensormap::kFwVersionReg);
    // E ela NAO pode ser zero: zero e indistinguivel de "a sensora nunca publicou nada", que e
    // exatamente o estado que a UR precisa conseguir distinguir depois de um OTA.
    TEST_ASSERT_NOT_EQUAL(0, sensormap::kFwVersionReg);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_codifica_maior_e_menor_nos_dois_bytes);
    RUN_TEST(test_o_patch_e_ignorado);
    RUN_TEST(test_formas_curtas_nao_inventam_numero);
    RUN_TEST(test_campo_grande_satura_e_nao_transborda_para_o_vizinho);
    RUN_TEST(test_lixo_em_qualquer_lugar_invalida_a_string_inteira);
    RUN_TEST(test_a_versao_do_build_e_a_que_vai_para_o_fio);
    return UNITY_END();
}
