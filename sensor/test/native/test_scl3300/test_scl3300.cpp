// Testes de host da matematica do SCL3300 da PUSI-DI261930 (Murata SCL3300, datasheet Rev.4 Doc 4921).
// Vetores: as 25 palavras de comando de 32 bits ja com CRC e o quadro de resposta 0x0500DC1C.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "drivers/scl3300_math.h"

namespace {

struct CmdVector {
    uint32_t word;
    const char* label;
};

constexpr CmdVector kCommandWords[] = {
    {0xB4002098u, "SW_RESET"},
    {0xB400046Bu, "POWER_DOWN"},
    {0xB400001Fu, "MODE_1"},
    {0xB4000102u, "MODE_2"},
    {0xB4000225u, "MODE_3"},
    {0xB4000338u, "MODE_4"},
    {0xB0001F6Fu, "ENABLE_ANGLE"},
    {0x240000C7u, "READ_ANG_X"},
    {0x280000CDu, "READ_ANG_Y"},
    {0x2C0000CBu, "READ_ANG_Z"},
    {0x140000EFu, "READ_TEMP"},
    {0x180000E5u, "READ_STATUS"},
    {0x1C0000E3u, "READ_ERR_FLAG1"},
    {0x200000C1u, "READ_ERR_FLAG2"},
    {0x40000091u, "READ_WHOAMI"},
    {0x040000F7u, "READ_ACC_X"},
    {0x080000FDu, "READ_ACC_Y"},
    {0x0C0000FBu, "READ_ACC_Z"},
    {0x100000E9u, "READ_STO"},
    {0x340000DFu, "READ_CMD"},
    {0xFC000073u, "BANK_0"},
    {0xFC00016Eu, "BANK_1"},
    {0x7C0000B3u, "READ_CURRENT_BANK"},
    {0x640000A7u, "READ_SERIAL1"},
    {0x680000ADu, "READ_SERIAL2"},
};

constexpr size_t kCommandWordCount = sizeof(kCommandWords) / sizeof(kCommandWords[0]);

constexpr uint32_t kBodyMask = 0xFFFFFF00u;

constexpr uint32_t kAccXReply = 0x0500DC1Cu;

constexpr uint16_t kWhoAmIData = 0x00C1u;

uint8_t crcByteOf(uint32_t value) {
    return static_cast<uint8_t>(value & 0xFFu);
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

static void test_crc8ReproduzOsVetoresDoDatasheet(void) {
    TEST_ASSERT_EQUAL_UINT32(25u, static_cast<uint32_t>(kCommandWordCount));
    for (size_t i = 0; i < kCommandWordCount; ++i) {
        const uint32_t cmd = kCommandWords[i].word;
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(crcByteOf(cmd), scl::crc8(cmd), kCommandWords[i].label);
    }
}

static void test_crc8CobreApenasOs24Msbs(void) {
    for (size_t i = 0; i < kCommandWordCount; ++i) {
        const uint32_t cmd = kCommandWords[i].word;
        const uint8_t want = crcByteOf(cmd);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(want, scl::crc8(cmd & kBodyMask), kCommandWords[i].label);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(want, scl::crc8((cmd & kBodyMask) | 0x5Au),
                                       kCommandWords[i].label);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(want, scl::crc8((cmd & kBodyMask) | 0xFFu),
                                       kCommandWords[i].label);
    }
}

static void test_withCrcMontaAPalavraCompleta(void) {
    TEST_ASSERT_EQUAL_HEX32(0xB4002098u, scl::withCrc(0xB4002000u));
    TEST_ASSERT_EQUAL_HEX32(0x240000C7u, scl::withCrc(0x24000000u));
    for (size_t i = 0; i < kCommandWordCount; ++i) {
        const uint32_t cmd = kCommandWords[i].word;
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(cmd, scl::withCrc(cmd & kBodyMask),
                                        kCommandWords[i].label);
    }
}

static void test_frameCrcOkAceitaOsVetores(void) {
    for (size_t i = 0; i < kCommandWordCount; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(scl::frameCrcOk(kCommandWords[i].word), kCommandWords[i].label);
    }
    TEST_ASSERT_TRUE(scl::frameCrcOk(kAccXReply));
}

static void test_frameCrcOkRejeitaBitAlterado(void) {
    TEST_ASSERT_FALSE(scl::frameCrcOk(0x0500DD1Cu));
    TEST_ASSERT_FALSE(scl::frameCrcOk(kAccXReply ^ 0x00000100u));
    TEST_ASSERT_FALSE(scl::frameCrcOk(kAccXReply ^ 0x00800000u));
    TEST_ASSERT_FALSE(scl::frameCrcOk(kAccXReply ^ 0x00000001u));
    TEST_ASSERT_FALSE(scl::frameCrcOk(0xB4002099u));
    TEST_ASSERT_FALSE(scl::frameCrcOk(0xB4002198u));
}

static void test_rsEstaDentroDaAreaCobertaPeloCrc(void) {
    TEST_ASSERT_FALSE(scl::frameCrcOk(0x0400DC1Cu));
    TEST_ASSERT_FALSE(scl::frameCrcOk(0x0600DC1Cu));
    TEST_ASSERT_FALSE(scl::frameCrcOk(0x0700DC1Cu));
    TEST_ASSERT_EQUAL_HEX32(0x0700DC1Fu, scl::withCrc(0x0700DC00u));
    TEST_ASSERT_TRUE(scl::frameCrcOk(0x0700DC1Fu));
}

static void test_extracaoDeCampos(void) {
    TEST_ASSERT_EQUAL_HEX8(0x01u, scl::frameOpcode(kAccXReply));
    TEST_ASSERT_EQUAL_UINT8(1u, scl::returnStatus(kAccXReply));
    TEST_ASSERT_EQUAL_HEX16(0x00DCu, scl::frameData(kAccXReply));

    TEST_ASSERT_EQUAL_HEX8(0x01u, scl::frameOpcode(0x040000F7u));
    TEST_ASSERT_EQUAL_HEX8(0x04u, scl::frameOpcode(0x100000E9u));
    TEST_ASSERT_EQUAL_HEX8(0x05u, scl::frameOpcode(0x140000EFu));
    TEST_ASSERT_EQUAL_HEX8(0x06u, scl::frameOpcode(0x180000E5u));
    TEST_ASSERT_EQUAL_HEX8(0x09u, scl::frameOpcode(0x240000C7u));
    TEST_ASSERT_EQUAL_HEX8(0x0Au, scl::frameOpcode(0x280000CDu));
    TEST_ASSERT_EQUAL_HEX8(0x0Bu, scl::frameOpcode(0x2C0000CBu));
    TEST_ASSERT_EQUAL_HEX8(0x10u, scl::frameOpcode(0x40000091u));

    TEST_ASSERT_EQUAL_UINT8(0u, scl::returnStatus(0x240000C7u));
    TEST_ASSERT_EQUAL_UINT8(0u, scl::returnStatus(0xB4002098u));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, scl::frameData(0x240000C7u));
    TEST_ASSERT_EQUAL_HEX16(0x0020u, scl::frameData(0xB4002098u));
    TEST_ASSERT_EQUAL_HEX16(0x001Fu, scl::frameData(0xB0001F6Fu));
    TEST_ASSERT_EQUAL_HEX16(0x0003u, scl::frameData(0xB4000338u));

    const uint32_t whoAmIReply = scl::withCrc(0x41000000u | (static_cast<uint32_t>(kWhoAmIData) << 8));
    TEST_ASSERT_TRUE(scl::frameCrcOk(whoAmIReply));
    TEST_ASSERT_EQUAL_HEX16(kWhoAmIData, scl::frameData(whoAmIReply));
    TEST_ASSERT_EQUAL_UINT8(1u, scl::returnStatus(whoAmIReply));
}

static void test_rsOfNosQuatroValores(void) {
    TEST_ASSERT_EQUAL_UINT8(0u, static_cast<uint8_t>(scl::Rs::Startup));
    TEST_ASSERT_EQUAL_UINT8(1u, static_cast<uint8_t>(scl::Rs::Ok));
    TEST_ASSERT_EQUAL_UINT8(2u, static_cast<uint8_t>(scl::Rs::Reserved));
    TEST_ASSERT_EQUAL_UINT8(3u, static_cast<uint8_t>(scl::Rs::Error));

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(scl::Rs::Startup),
                            static_cast<uint8_t>(scl::rsOf(0x04000000u)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(scl::Rs::Ok),
                            static_cast<uint8_t>(scl::rsOf(0x05000000u)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(scl::Rs::Reserved),
                            static_cast<uint8_t>(scl::rsOf(0x06000000u)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(scl::Rs::Error),
                            static_cast<uint8_t>(scl::rsOf(0x07000000u)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(scl::Rs::Ok),
                            static_cast<uint8_t>(scl::rsOf(kAccXReply)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(scl::Rs::Error),
                            static_cast<uint8_t>(scl::rsOf(0xFFFFFFFFu)));
}

static void test_rsNameNosQuatroValores(void) {
    const char* names[4];
    names[0] = scl::rsName(scl::Rs::Startup);
    names[1] = scl::rsName(scl::Rs::Ok);
    names[2] = scl::rsName(scl::Rs::Reserved);
    names[3] = scl::rsName(scl::Rs::Error);
    for (size_t i = 0; i < 4u; ++i) {
        TEST_ASSERT_NOT_NULL(names[i]);
        TEST_ASSERT_TRUE(strlen(names[i]) > 0u);
    }
    for (size_t i = 0; i < 4u; ++i) {
        for (size_t j = i + 1u; j < 4u; ++j) {
            TEST_ASSERT_TRUE(strcmp(names[i], names[j]) != 0);
        }
    }
}

static void test_angleDeciDegrees(void) {
    TEST_ASSERT_EQUAL_INT16(0, scl::angleDeciDegrees(0x0000u));
    TEST_ASSERT_EQUAL_INT16(900, scl::angleDeciDegrees(0x4000u));
    TEST_ASSERT_EQUAL_INT16(218, scl::angleDeciDegrees(0x0F88u));
    TEST_ASSERT_EQUAL_INT16(450, scl::angleDeciDegrees(0x2000u));
    TEST_ASSERT_EQUAL_INT16(-450, scl::angleDeciDegrees(0xE000u));
    TEST_ASSERT_EQUAL_INT16(-900, scl::angleDeciDegrees(0xC000u));
    TEST_ASSERT_EQUAL_INT16(-1800, scl::angleDeciDegrees(0x8000u));
}

static void test_temperatureDeciC(void) {
    TEST_ASSERT_EQUAL_INT16(266, scl::temperatureDeciC(0x161Eu));
    TEST_ASSERT_EQUAL_INT16(-2730, scl::temperatureDeciC(0x0000u));
    TEST_ASSERT_EQUAL_INT16(-190, scl::temperatureDeciC(4800u));
    TEST_ASSERT_EQUAL_INT16(-84, scl::temperatureDeciC(5000u));
}

static void test_statusSatEhOBit6(void) {
    TEST_ASSERT_EQUAL_HEX16(0x0040u, static_cast<uint16_t>(scl::kStatusSat));
    TEST_ASSERT_EQUAL_HEX16(static_cast<uint16_t>(1u << 6), static_cast<uint16_t>(scl::kStatusSat));
}

static void test_constantesDeComando(void) {
    TEST_ASSERT_EQUAL_HEX32(0xB4002098u, static_cast<uint32_t>(scl::kCmdSwReset));
    TEST_ASSERT_EQUAL_HEX32(0xB400001Fu, static_cast<uint32_t>(scl::kCmdMode1));
    TEST_ASSERT_EQUAL_HEX32(0xB4000102u, static_cast<uint32_t>(scl::kCmdMode2));
    TEST_ASSERT_EQUAL_HEX32(0xB4000225u, static_cast<uint32_t>(scl::kCmdMode3));
    TEST_ASSERT_EQUAL_HEX32(0xB4000338u, static_cast<uint32_t>(scl::kCmdMode4));
    TEST_ASSERT_EQUAL_HEX32(0xB0001F6Fu, static_cast<uint32_t>(scl::kCmdEnableAngle));
    TEST_ASSERT_EQUAL_HEX32(0x240000C7u, static_cast<uint32_t>(scl::kCmdReadAngX));
    TEST_ASSERT_EQUAL_HEX32(0x280000CDu, static_cast<uint32_t>(scl::kCmdReadAngY));
    TEST_ASSERT_EQUAL_HEX32(0x2C0000CBu, static_cast<uint32_t>(scl::kCmdReadAngZ));
    TEST_ASSERT_EQUAL_HEX32(0x140000EFu, static_cast<uint32_t>(scl::kCmdReadTemp));
    TEST_ASSERT_EQUAL_HEX32(0x180000E5u, static_cast<uint32_t>(scl::kCmdReadStatus));
    TEST_ASSERT_EQUAL_HEX32(0x40000091u, static_cast<uint32_t>(scl::kCmdReadWhoAmI));
    TEST_ASSERT_EQUAL_HEX32(0x1C0000E3u, static_cast<uint32_t>(scl::kCmdReadErrFlag1));
    TEST_ASSERT_EQUAL_HEX32(0x200000C1u, static_cast<uint32_t>(scl::kCmdReadErrFlag2));
    TEST_ASSERT_EQUAL_HEX32(0xFC000073u, static_cast<uint32_t>(scl::kCmdBank0));
    TEST_ASSERT_EQUAL_HEX32(0xFC00016Eu, static_cast<uint32_t>(scl::kCmdBank1));
}

// --- criterio do autoteste contra o datasheet (Tabelas 27, 31, 33 e 23) ---------------------
//
// O criterio antigo era "flag2 != 0". A Tabela 33 diz textualmente, no bit D4 (DPWR):
// "[After star-up or reset] This flag is set high. No actions needed." Com o criterio antigo,
// uma sensora perfeitamente sadia reprovava para sempre, porque DPWR fica alto depois de todo
// start-up e ler ERR_FLAG nao reseta nada (secao 6.4). O mesmo vale para MODE_CHANGE (D9),
// que e alto justamente porque NOS pedimos o modo no passo 4 da Tabela 11.
// Tudo o mais continua reprovando: o objetivo aqui e tolerar dois bits nomeados, nao afrouxar.

static void test_dpwrSozinhoNaoReprovaOAutoteste(void) {
    TEST_ASSERT_FALSE(scl::selfTestFaulty(0x0000u, 0x0000u, scl::kErr2Dpwr));
}

static void test_modeChangeDeErrFlag2NaoReprova(void) {
    TEST_ASSERT_FALSE(scl::selfTestFaulty(0x0000u, 0x0000u, scl::kErr2ModeChange));
    TEST_ASSERT_FALSE(scl::selfTestFaulty(
        0x0000u, 0x0000u, static_cast<uint16_t>(scl::kErr2Dpwr | scl::kErr2ModeChange)));
}

// O caso medido na bancada em 2026-08-31: capacitor do pino D_EXTC (C8) sem conexao eletrica.
// Se este teste passar a aceitar 0x4010, a UR volta a comandar rele com leitura sem credito.
static void test_dExtCReprovaMesmoAcompanhadoDeDpwr(void) {
    TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, 0x0000u, 0x4010u));
    TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, 0x0000u, scl::kErr2DExtC));
}

static void test_cadaBitDeErroDocumentadoDeErrFlag2Reprova(void) {
    const uint16_t bits[] = {
        scl::kErr2Clk,   scl::kErr2TempSat, scl::kErr2Apwr2,  scl::kErr2Vref,
        scl::kErr2Apwr,  scl::kErr2MemoryCrc, scl::kErr2Pd,   scl::kErr2Vdd,
        scl::kErr2Agnd,  scl::kErr2AExtC,   scl::kErr2DExtC,
    };
    for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); ++i) {
        TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, 0x0000u, bits[i]));
        // e continua reprovando acompanhado dos dois benignos, que e como aparecem em campo
        TEST_ASSERT_TRUE(scl::selfTestFaulty(
            0x0000u, 0x0000u,
            static_cast<uint16_t>(bits[i] | scl::kErr2Dpwr | scl::kErr2ModeChange)));
    }
}

static void test_qualquerBitDeErrFlag1Reprova(void) {
    TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, scl::kErr1Mem, 0x0000u));
    TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, scl::kErr1AfeSat, 0x0000u));
    TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, scl::kErr1AdcSat, 0x0000u));
    TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, 0x0002u, 0x0000u));
}

static void test_statusMantemPwrEModeChangeBenignos(void) {
    TEST_ASSERT_FALSE(scl::selfTestFaulty(scl::kStatusStartupBenign, 0x0000u, 0x0000u));
    TEST_ASSERT_TRUE(scl::selfTestFaulty(scl::kStatusPinContinuity, 0x0000u, 0x0000u));
    TEST_ASSERT_TRUE(scl::selfTestFaulty(scl::kStatusSat, 0x0000u, 0x0000u));
    TEST_ASSERT_TRUE(scl::selfTestFaulty(scl::kStatusMem, 0x0000u, 0x0000u));
}

static void test_sensoraSadiaPassa(void) {
    TEST_ASSERT_FALSE(scl::selfTestFaulty(0x0000u, 0x0000u, 0x0000u));
    // exatamente o que uma sensora sadia devolve depois do start-up da Tabela 11
    TEST_ASSERT_FALSE(scl::selfTestFaulty(scl::kStatusPwr, 0x0000u, scl::kErr2Dpwr));
}

// Tabela 23: limiares de exemplo do STO por modo. Modo fora de 1..4 cai no modo 1, igual ao
// clampMode() do driver, para que console e driver nunca discordem do limiar em uso.
static void test_stoThresholdPorModo(void) {
    TEST_ASSERT_EQUAL_INT16(1800, scl::stoThreshold(1));
    TEST_ASSERT_EQUAL_INT16(900, scl::stoThreshold(2));
    TEST_ASSERT_EQUAL_INT16(3600, scl::stoThreshold(3));
    TEST_ASSERT_EQUAL_INT16(3600, scl::stoThreshold(4));
    TEST_ASSERT_EQUAL_INT16(1800, scl::stoThreshold(0));
    TEST_ASSERT_EQUAL_INT16(1800, scl::stoThreshold(9));
}

static void test_stoOutOfRangeUsaORamoComSinal(void) {
    // 0xFFEA = -22, medido na bancada: elemento sensor saudavel em qualquer modo
    TEST_ASSERT_FALSE(scl::stoOutOfRange(0xFFEAu, 1));
    TEST_ASSERT_FALSE(scl::stoOutOfRange(0xFFEAu, 3));
    TEST_ASSERT_FALSE(scl::stoOutOfRange(0x0000u, 3));
    // limiar e inclusivo: 3600 ainda passa, 3601 nao
    TEST_ASSERT_FALSE(scl::stoOutOfRange(3600u, 3));
    TEST_ASSERT_TRUE(scl::stoOutOfRange(3601u, 3));
    TEST_ASSERT_FALSE(scl::stoOutOfRange(static_cast<uint16_t>(-3600), 3));
    TEST_ASSERT_TRUE(scl::stoOutOfRange(static_cast<uint16_t>(-3601), 3));
    // o mesmo bruto muda de veredito conforme o modo
    TEST_ASSERT_FALSE(scl::stoOutOfRange(2000u, 3));
    TEST_ASSERT_TRUE(scl::stoOutOfRange(2000u, 1));
    TEST_ASSERT_TRUE(scl::stoOutOfRange(2000u, 2));
}

// --- BYPASS DE BANCADA (2026-09-01) ----------------------------------------------------------
//
// Pedido do bigboss: poder exercitar toda a cadeia limite -> rele -> LED -> saida analogica
// ANTES de o C8 ser consertado. Sem isto a UR recusa toda leitura, os quatro reles ficam em
// alarme por A5 e nenhum ajuste de limite pode ser observado.
//
// O bypass TOLERA EXATAMENTE DOIS BITS, os dois que o capacitor ausente do pino D_EXTC produz:
// PIN_CONTINUITY no STATUS e D_EXT_C no ERR_FLAG2. Todo o resto continua reprovando - inclusive
// A_EXT_C, que e o capacitor do outro lado do chip e cuja ausencia satura o front-end e
// FALSIFICA o angulo. Um bypass que tolerasse tudo nao seria auxilio de bancada, seria
// desligar a supervisao.
//
// E runtime, com padrao DESLIGADO, para nao existir binario compilado com ele ligado por
// esquecimento: cada energizacao volta a recusar.

static void test_bypass_tolera_pin_continuity_e_d_ext_c(void) {
    // 0x4010 = D_EXT_C + DPWR, exatamente o medido na bancada
    TEST_ASSERT_TRUE(scl::selfTestFaulty(scl::kStatusPinContinuity, 0x0000u, 0x4010u, false));
    TEST_ASSERT_FALSE(scl::selfTestFaulty(scl::kStatusPinContinuity, 0x0000u, 0x4010u, true));
}

static void test_bypass_nao_tolera_mais_nada(void) {
    const uint16_t status[] = {scl::kStatusSat, scl::kStatusMem, scl::kStatusClk,
                               scl::kStatusPd,  scl::kStatusTemp, scl::kStatusDigi1,
                               scl::kStatusDigi2};
    for (size_t i = 0; i < sizeof(status) / sizeof(status[0]); ++i) {
        const uint16_t comBypassAtivo =
            static_cast<uint16_t>(status[i] | scl::kStatusPinContinuity);
        TEST_ASSERT_TRUE(scl::selfTestFaulty(comBypassAtivo, 0x0000u, 0x0000u, true));
    }

    // A_EXT_C e o capacitor do CORE ANALOGICO: sem ele o front-end satura e o angulo sai errado.
    // Tolera-lo seria comandar rele com numero falso, que e o modo de falha que este produto
    // existe para evitar.
    TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, 0x0000u, scl::kErr2AExtC, true));

    const uint16_t flag2[] = {scl::kErr2Clk,  scl::kErr2TempSat,   scl::kErr2Apwr2,
                              scl::kErr2Vref, scl::kErr2Apwr,      scl::kErr2MemoryCrc,
                              scl::kErr2Pd,   scl::kErr2Vdd,       scl::kErr2Agnd};
    for (size_t i = 0; i < sizeof(flag2) / sizeof(flag2[0]); ++i) {
        const uint16_t comDExtC = static_cast<uint16_t>(flag2[i] | scl::kErr2DExtC);
        TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, 0x0000u, comDExtC, true));
    }

    // ERR_FLAG1 nao tem bit tolerado em nenhum modo
    TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, scl::kErr1Mem, 0x0000u, true));
    TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, scl::kErr1AfeSat, 0x0000u, true));
    TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, scl::kErr1AdcSat, 0x0000u, true));
}

static void test_bypass_desligado_e_o_padrao_do_argumento(void) {
    // A mesma chamada de tres argumentos usada pelo resto do codigo continua ESTRITA.
    TEST_ASSERT_TRUE(scl::selfTestFaulty(scl::kStatusPinContinuity, 0x0000u, 0x0000u));
    TEST_ASSERT_TRUE(scl::selfTestFaulty(0x0000u, 0x0000u, scl::kErr2DExtC));
}

static void test_mascara_de_falha_dura_do_status_encolhe_so_do_bit_tolerado(void) {
    const uint16_t estrita = scl::statusHardMask(false);
    const uint16_t comBypass = scl::statusHardMask(true);
    TEST_ASSERT_TRUE((estrita & scl::kStatusPinContinuity) != 0u);
    TEST_ASSERT_TRUE((comBypass & scl::kStatusPinContinuity) == 0u);
    // e a diferenca entre as duas e EXATAMENTE esse bit
    TEST_ASSERT_EQUAL_HEX16(scl::kStatusPinContinuity,
                            static_cast<uint16_t>(estrita ^ comBypass));
    // SAT continua fora da mascara dura nos dois modos (tem tratamento proprio)
    TEST_ASSERT_TRUE((estrita & scl::kStatusSat) == 0u);
    TEST_ASSERT_TRUE((comBypass & scl::kStatusSat) == 0u);
}

// --- O SEGUNDO PORTAO DO BYPASS: os bits RS ---------------------------------------------------
//
// Medido na bancada em 2026-09-01, com o bypass ja ligado: STATUS aceito (0x0001 tolerado), e
// mesmo assim a leitura saia BUSY com RS_SCL = 3. O datasheet explica (6.3.1): os bits RS
// sinalizam que existe erro no STATUS e so voltam a b'01' depois de o STATUS ser lido LIMPO.
// Como o PIN_CONTINUITY reaparece a cada ciclo enquanto o C8 estiver aberto, o RS nunca sai de
// erro - e o driver derrubava a leitura por causa dele, com o STATUS ja perdoado.
//
// A regra: com o bypass ligado, o RS de erro deixa de ser fatal SO quando o STATUS lido no
// mesmo burst nao traz nada alem do que foi tolerado. Qualquer outro bit aceso volta a tornar
// o RS fatal - senao o bypass viraria "ignore todo RS de erro", que e coisa completamente
// diferente do que foi pedido.

static void test_rs_de_erro_e_perdoado_quando_o_status_so_traz_o_bit_tolerado(void) {
    TEST_ASSERT_TRUE(scl::rsErrorExplainedByBypass(scl::kStatusPinContinuity, true));
    // acompanhado dos benignos de start-up continua explicado
    const uint16_t comBenignos = static_cast<uint16_t>(scl::kStatusPinContinuity |
                                                       scl::kStatusPwr | scl::kStatusModeChange);
    TEST_ASSERT_TRUE(scl::rsErrorExplainedByBypass(comBenignos, true));
    // STATUS limpo tambem: nao ha erro que o RS pudesse estar sinalizando
    TEST_ASSERT_TRUE(scl::rsErrorExplainedByBypass(0x0000u, true));
}

static void test_rs_de_erro_continua_fatal_com_qualquer_outro_bit(void) {
    const uint16_t outros[] = {scl::kStatusSat,   scl::kStatusMem,   scl::kStatusClk,
                               scl::kStatusPd,    scl::kStatusTemp,  scl::kStatusDigi1,
                               scl::kStatusDigi2};
    for (size_t i = 0; i < sizeof(outros) / sizeof(outros[0]); ++i) {
        const uint16_t comTolerado =
            static_cast<uint16_t>(outros[i] | scl::kStatusPinContinuity);
        TEST_ASSERT_FALSE(scl::rsErrorExplainedByBypass(outros[i], true));
        TEST_ASSERT_FALSE(scl::rsErrorExplainedByBypass(comTolerado, true));
    }
}

static void test_sem_bypass_nenhum_rs_de_erro_e_perdoado(void) {
    TEST_ASSERT_FALSE(scl::rsErrorExplainedByBypass(scl::kStatusPinContinuity, false));
    TEST_ASSERT_FALSE(scl::rsErrorExplainedByBypass(0x0000u, false));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_crc8ReproduzOsVetoresDoDatasheet);
    RUN_TEST(test_crc8CobreApenasOs24Msbs);
    RUN_TEST(test_withCrcMontaAPalavraCompleta);
    RUN_TEST(test_frameCrcOkAceitaOsVetores);
    RUN_TEST(test_frameCrcOkRejeitaBitAlterado);
    RUN_TEST(test_rsEstaDentroDaAreaCobertaPeloCrc);
    RUN_TEST(test_extracaoDeCampos);
    RUN_TEST(test_rsOfNosQuatroValores);
    RUN_TEST(test_rsNameNosQuatroValores);
    RUN_TEST(test_angleDeciDegrees);
    RUN_TEST(test_temperatureDeciC);
    RUN_TEST(test_statusSatEhOBit6);
    RUN_TEST(test_constantesDeComando);
    RUN_TEST(test_dpwrSozinhoNaoReprovaOAutoteste);
    RUN_TEST(test_modeChangeDeErrFlag2NaoReprova);
    RUN_TEST(test_dExtCReprovaMesmoAcompanhadoDeDpwr);
    RUN_TEST(test_cadaBitDeErroDocumentadoDeErrFlag2Reprova);
    RUN_TEST(test_qualquerBitDeErrFlag1Reprova);
    RUN_TEST(test_statusMantemPwrEModeChangeBenignos);
    RUN_TEST(test_sensoraSadiaPassa);
    RUN_TEST(test_stoThresholdPorModo);
    RUN_TEST(test_stoOutOfRangeUsaORamoComSinal);
    RUN_TEST(test_bypass_tolera_pin_continuity_e_d_ext_c);
    RUN_TEST(test_bypass_nao_tolera_mais_nada);
    RUN_TEST(test_bypass_desligado_e_o_padrao_do_argumento);
    RUN_TEST(test_mascara_de_falha_dura_do_status_encolhe_so_do_bit_tolerado);
    RUN_TEST(test_rs_de_erro_e_perdoado_quando_o_status_so_traz_o_bit_tolerado);
    RUN_TEST(test_rs_de_erro_continua_fatal_com_qualquer_outro_bit);
    RUN_TEST(test_sem_bypass_nenhum_rs_de_erro_e_perdoado);
    return UNITY_END();
}
