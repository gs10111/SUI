// Testes de dominio do agregado Parameters.
// REQ-PER-01: falta de energia - os parametros permanecem gravados sem bateria e a leitura
//             relativa (o offset de PSET) e restabelecida na religacao (manual 7, L308).
// REQ-RST-01: o Reset Geral repoe os padroes de fabrica da Tabela 2 (manual 5.11, L239 a L249).
// REQ-RST-02: o Reset Geral RESTAURA - nao apaga - a calibracao analogica medida em fabrica
//             (manual 5.11, L240). factoryDefaults() entrega o NOMINAL de projeto, que e
//             ultimo recurso, e nao a calibracao do jig.
// Tabela 2 (manual L250 a L267): defaults conferidos campo a campo, inclusive Operacao Limite 1
//             e 3 em + (modulo) com +005,0 graus e Operacao Limite 2 e 4 em Off.
// Decisao A8 (opcao C, aprovada em 2026-08-31): parametros que comandam rele e calibracao
//             analogica vivem em REGISTROS SEPARADOS - um blob corrompido de um grupo nao pode
//             derrubar o outro grupo, e uma carga recusada nao altera campo nenhum, nem no
//             caminho do CRC nem no caminho da faixa.
// Decisao A9: offset := P - dir * bruto, faixa de +/-180,0 graus (o dobro da faixa de medicao).
// Decisao A14 (opcao A): trim com neutro em 5000, faixa -5000 a +4999 LSB em torno do codigo
//             nominal de cada ponto - o que torna impossivel calibracao degenerada ou invertida.
// Este agregado nao tem prazo nenhum (A13: nao efetiva e nao cronometra), por isso nao recebe
// IClock e nao ha relogio neste arquivo.
#include <string.h>
#include <unity.h>

#include "domain/parameters.h"
#include "proto/crc16.h"

using domain::Angle;
using domain::Axis;
using domain::LimitId;
using domain::LimitOp;
using domain::Parameters;
using domain::SensorDir;

#define ASSERT_ERR(esperado, resultado) \
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(esperado), static_cast<uint8_t>((resultado).err))

#define ASSERT_ERR_MSG(esperado, resultado, msg)                                       \
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(esperado),                     \
                                    static_cast<uint8_t>((resultado).err), (msg))

void setUp(void) {}
void tearDown(void) {}

static const LimitId kLimites[4] = {LimitId::X1, LimitId::X2, LimitId::Y1, LimitId::Y2};

// Conjunto que nao coincide com nenhum default, para que qualquer campo perdido ou trocado
// na serializacao apareca.
static Parameters parametrosDoCliente(void) {
    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.setPreset(Axis::X, Angle::fromDeciDegrees(-123)));
    ASSERT_ERR(Err::Ok, p.setPreset(Axis::Y, Angle::fromDeciDegrees(456)));
    ASSERT_ERR(Err::Ok, p.setPresetOffset(Axis::X, -1100));
    ASSERT_ERR(Err::Ok, p.setPresetOffset(Axis::Y, 88));
    ASSERT_ERR(Err::Ok, p.setLimitValue(LimitId::X1, Angle::fromDeciDegrees(-900)));
    ASSERT_ERR(Err::Ok, p.setLimitValue(LimitId::X2, Angle::fromDeciDegrees(900)));
    ASSERT_ERR(Err::Ok, p.setLimitValue(LimitId::Y1, Angle::fromDeciDegrees(-1)));
    ASSERT_ERR(Err::Ok, p.setLimitValue(LimitId::Y2, Angle::fromDeciDegrees(325)));
    ASSERT_ERR(Err::Ok, p.setLimitOp(LimitId::X1, LimitOp::Off));
    ASSERT_ERR(Err::Ok, p.setLimitOp(LimitId::X2, LimitOp::GreaterEqual));
    ASSERT_ERR(Err::Ok, p.setLimitOp(LimitId::Y1, LimitOp::LessEqual));
    ASSERT_ERR(Err::Ok, p.setLimitOp(LimitId::Y2, LimitOp::Absolute));
    ASSERT_ERR(Err::Ok, p.setSensorDir(Axis::X, SensorDir::CounterClockwise));
    ASSERT_ERR(Err::Ok, p.setSensorDir(Axis::Y, SensorDir::Clockwise));
    ASSERT_ERR(Err::Ok, p.setPassword(9007));
    ASSERT_ERR(Err::Ok, p.setCalFullScale(Axis::X, Angle::fromDeciDegrees(300)));
    ASSERT_ERR(Err::Ok, p.setCalFullScale(Axis::Y, Angle::fromDeciDegrees(900)));
    ASSERT_ERR(Err::Ok, p.setCalZeroCode(Axis::X, 32700));
    ASSERT_ERR(Err::Ok, p.setCalZeroCode(Axis::Y, 32900));
    ASSERT_ERR(Err::Ok, p.setCalFullScaleCode(Axis::X, 58000));
    ASSERT_ERR(Err::Ok, p.setCalFullScaleCode(Axis::Y, 58982));
    return p;
}

static void blobDeParametros(const Parameters& origem, uint8_t* dst) {
    uint16_t escritos = 0;
    ASSERT_ERR(Err::Ok, origem.serializeParams(dst, Parameters::kParamBlobSize, escritos));
    TEST_ASSERT_EQUAL_UINT16(Parameters::kParamBlobSize, escritos);
}

static void blobDeCalibracao(const Parameters& origem, uint8_t* dst) {
    uint16_t escritos = 0;
    ASSERT_ERR(Err::Ok, origem.serializeCal(dst, Parameters::kCalBlobSize, escritos));
    TEST_ASSERT_EQUAL_UINT16(Parameters::kCalBlobSize, escritos);
}

// Reassina o blob depois de uma alteracao proposital, para separar "CRC reprovado" de
// "conteudo invalido com CRC bom".
static void reassinar(uint8_t* blob, uint16_t tamanho) {
    const uint16_t crc = crc16Modbus(blob, static_cast<size_t>(tamanho - 2));
    blob[tamanho - 2] = static_cast<uint8_t>(crc & 0xFFu);
    blob[tamanho - 1] = static_cast<uint8_t>(crc >> 8);
}

// Blob de calibracao como o jig grava em ParamSlot::FactoryCal: valores MEDIDOS na unidade,
// nao os nominais de projeto. Serve para provar que o Reset Geral restaura estes numeros.
static void blobDeFabricaMedido(uint8_t* dst) {
    Parameters jig = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, jig.setCalPair(Axis::X, 32610, 58840));
    ASSERT_ERR(Err::Ok, jig.setCalPair(Axis::Y, 32805, 59110));
    blobDeCalibracao(jig, dst);
}

// --- Tabela 2: defaults de fabrica campo a campo (REQ-RST-01) ---

static void test_RST_01_defaults_da_tabela_2_preset_e_sentido_do_sensor(void) {
    const Parameters p = Parameters::factoryDefaults();
    // L252/L253: Ajusta Preset X e Y = +000,0 graus.
    TEST_ASSERT_EQUAL_INT16(0, p.preset(Axis::X).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(0, p.preset(Axis::Y).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(0, p.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(0, p.presetOffsetDeci(Axis::Y));
    // L264/L265: Sentido Sensor X e Y = Horario.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SensorDir::Clockwise),
                            static_cast<uint8_t>(p.sensorDir(Axis::X)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SensorDir::Clockwise),
                            static_cast<uint8_t>(p.sensorDir(Axis::Y)));
}

static void test_RST_01_defaults_da_tabela_2_limites_1_e_3_em_modulo_com_5_graus(void) {
    const Parameters p = Parameters::factoryDefaults();
    // L256/L257 e L260/L261: Operacao Limite 1 e 3 = + (modulo), Limite 1 e 3 = +005,0 graus.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LimitOp::Absolute),
                            static_cast<uint8_t>(p.limitOp(LimitId::X1)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LimitOp::Absolute),
                            static_cast<uint8_t>(p.limitOp(LimitId::Y1)));
    TEST_ASSERT_EQUAL_INT16(50, p.limitValue(LimitId::X1).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(50, p.limitValue(LimitId::Y1).deciDegrees());
}

static void test_RST_01_defaults_da_tabela_2_limites_2_e_4_em_off_com_zero(void) {
    const Parameters p = Parameters::factoryDefaults();
    // L258/L259 e L262/L263: Operacao Limite 2 e 4 = Off, Limite 2 e 4 = +000,0 graus.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LimitOp::Off),
                            static_cast<uint8_t>(p.limitOp(LimitId::X2)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LimitOp::Off),
                            static_cast<uint8_t>(p.limitOp(LimitId::Y2)));
    TEST_ASSERT_EQUAL_INT16(0, p.limitValue(LimitId::X2).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(0, p.limitValue(LimitId::Y2).deciDegrees());
}

static void test_RST_01_default_da_tabela_2_da_senha_e_1234(void) {
    // L266/L267: Senha = 1234.
    TEST_ASSERT_EQUAL_UINT16(1234, Parameters::factoryDefaults().password());
}

static void test_RST_02_reset_geral_recarrega_a_calibracao_do_slot_de_fabrica(void) {
    // L240: o Reset Geral "restaura os ajustes de calibracao das saidas analogicas realizados
    // durante o processo de FABRICACAO". Esses ajustes sao medidos unidade a unidade pelo jig e
    // vivem em ParamSlot::FactoryCal - nao sao os codigos nominais de projeto. Uma unidade cujo
    // jig gravou zeroCode 32610 tem de voltar com 32610, nao com 32768.
    uint8_t deFabrica[Parameters::kCalBlobSize];
    blobDeFabricaMedido(deFabrica);

    Parameters p = parametrosDoCliente();
    p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.loadCal(deFabrica, sizeof(deFabrica)));

    TEST_ASSERT_EQUAL_UINT16(32610, p.calZeroCode(Axis::X));
    TEST_ASSERT_EQUAL_UINT16(58840, p.calFullScaleCode(Axis::X));
    TEST_ASSERT_EQUAL_UINT16(32805, p.calZeroCode(Axis::Y));
    TEST_ASSERT_EQUAL_UINT16(59110, p.calFullScaleCode(Axis::Y));
    // E os campos de rele voltaram todos para a Tabela 2.
    TEST_ASSERT_EQUAL_INT16(50, p.limitValue(LimitId::X1).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(0, p.preset(Axis::X).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(0, p.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_UINT16(1234, p.password());
}

static void test_RST_02_calibracao_nominal_e_o_ultimo_recurso(void) {
    // Sem o slot de fabrica legivel resta o nominal de projeto da cadeia bipolar: 0,00 V no
    // codigo 32768 e +10,00 V no 58982, com fundo de escala de 45,0 graus (Tabela 2, L254/L255).
    const Parameters p = Parameters::factoryDefaults();
    TEST_ASSERT_EQUAL_INT16(450, p.calFullScale(Axis::X).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(450, p.calFullScale(Axis::Y).deciDegrees());
    TEST_ASSERT_EQUAL_UINT16(Parameters::kNominalCalZeroCode, p.calZeroCode(Axis::X));
    TEST_ASSERT_EQUAL_UINT16(Parameters::kNominalCalZeroCode, p.calZeroCode(Axis::Y));
    TEST_ASSERT_EQUAL_UINT16(Parameters::kNominalCalFullScaleCode, p.calFullScaleCode(Axis::X));
    TEST_ASSERT_EQUAL_UINT16(Parameters::kNominalCalFullScaleCode, p.calFullScaleCode(Axis::Y));
    TEST_ASSERT_EQUAL_UINT16(32768, Parameters::kNominalCalZeroCode);
    TEST_ASSERT_EQUAL_UINT16(58982, Parameters::kNominalCalFullScaleCode);
}

static void test_RST_01_reset_de_fabrica_sobrescreve_todo_campo_de_parametro(void) {
    // L249: o Reset Geral apaga todas as configuracoes do usuario. Nenhum campo do registro que
    // comanda rele pode sobreviver por esquecimento. A calibracao NAO entra nesta comparacao:
    // ela e restaurada do slot de fabrica, nao apagada (L240, teste acima).
    uint8_t parUsado[Parameters::kParamBlobSize];
    uint8_t parLimpo[Parameters::kParamBlobSize];

    Parameters p = parametrosDoCliente();
    p = Parameters::factoryDefaults();
    blobDeParametros(p, parUsado);
    blobDeParametros(Parameters::factoryDefaults(), parLimpo);

    TEST_ASSERT_EQUAL_INT(0, memcmp(parUsado, parLimpo, sizeof(parUsado)));
}

// --- Validacao de seletor e de faixa, campo a campo ---

// Envelope com sentinelas em volta do agregado: se um seletor invalido escrever fora dos
// limites do objeto, a escrita cai numa das faixas de 0x5A e o teste denuncia sem depender de
// AddressSanitizer.
struct AgregadoComSentinela {
    uint8_t antes[64];
    Parameters p;
    uint8_t depois[64];
};

static void test_PRM_seletor_invalido_e_recusado_sem_tocar_em_memoria(void) {
    // A IHM mapeia indice de menu para Axis/LimitId. Indice errado tem de virar Err::Param, e
    // nunca escrita fora dos limites do agregado que comanda os quatro reles.
    AgregadoComSentinela caixa;
    memset(caixa.antes, 0x5A, sizeof(caixa.antes));
    memset(caixa.depois, 0x5A, sizeof(caixa.depois));
    caixa.p = parametrosDoCliente();

    uint8_t parAntes[Parameters::kParamBlobSize];
    uint8_t calAntes[Parameters::kCalBlobSize];
    blobDeParametros(caixa.p, parAntes);
    blobDeCalibracao(caixa.p, calAntes);

    const Axis eixosInvalidos[2] = {static_cast<Axis>(2), static_cast<Axis>(200)};
    for (unsigned i = 0; i < 2; ++i) {
        const Axis eixo = eixosInvalidos[i];
        ASSERT_ERR(Err::Param, caixa.p.setPreset(eixo, Angle::fromDeciDegrees(100)));
        ASSERT_ERR(Err::Param, caixa.p.setPresetOffset(eixo, 100));
        ASSERT_ERR(Err::Param, caixa.p.setSensorDir(eixo, SensorDir::CounterClockwise));
        ASSERT_ERR(Err::Param, caixa.p.setCalFullScale(eixo, Angle::fromDeciDegrees(450)));
        ASSERT_ERR(Err::Param, caixa.p.setCalZeroCode(eixo, 32768));
        ASSERT_ERR(Err::Param, caixa.p.setCalFullScaleCode(eixo, 58982));
        ASSERT_ERR(Err::Param, caixa.p.setCalPair(eixo, 32768, 58982));
        // Getter sem canal de erro devolve o valor SEGURO, nunca memoria vizinha.
        TEST_ASSERT_FALSE(caixa.p.preset(eixo).valid());
        TEST_ASSERT_FALSE(caixa.p.calFullScale(eixo).valid());
        TEST_ASSERT_EQUAL_INT16(0, caixa.p.presetOffsetDeci(eixo));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SensorDir::Clockwise),
                                static_cast<uint8_t>(caixa.p.sensorDir(eixo)));
        TEST_ASSERT_EQUAL_UINT16(Parameters::kNominalCalZeroCode, caixa.p.calZeroCode(eixo));
        TEST_ASSERT_EQUAL_UINT16(Parameters::kNominalCalFullScaleCode,
                                 caixa.p.calFullScaleCode(eixo));
    }

    const LimitId limitesInvalidos[2] = {static_cast<LimitId>(4), static_cast<LimitId>(40)};
    for (unsigned i = 0; i < 2; ++i) {
        const LimitId id = limitesInvalidos[i];
        ASSERT_ERR(Err::Param, caixa.p.setLimitValue(id, Angle::fromDeciDegrees(100)));
        ASSERT_ERR(Err::Param, caixa.p.setLimitOp(id, LimitOp::Absolute));
        TEST_ASSERT_FALSE(caixa.p.limitValue(id).valid());
        // Rele em repouso e o estado seguro (manual 5.9, L208).
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LimitOp::Off),
                                static_cast<uint8_t>(caixa.p.limitOp(id)));
    }

    uint8_t parDepois[Parameters::kParamBlobSize];
    uint8_t calDepois[Parameters::kCalBlobSize];
    blobDeParametros(caixa.p, parDepois);
    blobDeCalibracao(caixa.p, calDepois);
    TEST_ASSERT_EQUAL_INT(0, memcmp(parAntes, parDepois, sizeof(parAntes)));
    TEST_ASSERT_EQUAL_INT(0, memcmp(calAntes, calDepois, sizeof(calAntes)));

    for (unsigned i = 0; i < sizeof(caixa.antes); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5A, caixa.antes[i]);
        TEST_ASSERT_EQUAL_UINT8(0x5A, caixa.depois[i]);
    }
}

static void test_PRM_preset_recusa_fora_da_faixa_de_medicao(void) {
    // Manual 5.4 L117/L118 e 5.6 L144: -90,0 a +90,0 graus.
    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.setPreset(Axis::X, Angle::fromDeciDegrees(-900)));
    ASSERT_ERR(Err::Ok, p.setPreset(Axis::Y, Angle::fromDeciDegrees(900)));
    ASSERT_ERR(Err::Range, p.setPreset(Axis::X, Angle::fromDeciDegrees(901)));
    ASSERT_ERR(Err::Range, p.setPreset(Axis::Y, Angle::fromDeciDegrees(-901)));
    ASSERT_ERR(Err::Range, p.setPreset(Axis::X, Angle::invalid()));
}

static void test_A9_offset_de_pset_cobre_a_faixa_completa_de_180_graus(void) {
    // A9: offset := P - dir * bruto. Com P em [-900,+900] e dir*bruto em [-900,+900], o offset
    // vai de -1800 a +1800. Exemplo real: equipamento em -60,0 graus, operador programa Preset
    // +50,0 e aperta PSET -> offset = 500 - (-600) = 1100 decimos, alem da faixa de MEDICAO.
    // Se o offset fosse um Angle, este PSET falharia em silencio e os quatro reles passariam a
    // atuar em angulo diferente do programado (manual 5.9, L222).
    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.setPresetOffset(Axis::X, -1800));
    TEST_ASSERT_EQUAL_INT16(-1800, p.presetOffsetDeci(Axis::X));
    ASSERT_ERR(Err::Ok, p.setPresetOffset(Axis::X, -1100));
    ASSERT_ERR(Err::Ok, p.setPresetOffset(Axis::X, 1100));
    TEST_ASSERT_EQUAL_INT16(1100, p.presetOffsetDeci(Axis::X));
    ASSERT_ERR(Err::Ok, p.setPresetOffset(Axis::Y, 1800));
    ASSERT_ERR(Err::Range, p.setPresetOffset(Axis::X, 1801));
    ASSERT_ERR(Err::Range, p.setPresetOffset(Axis::Y, -1801));
    ASSERT_ERR(Err::Range, p.setPresetOffset(Axis::X, 32767));
    // O valor recusado nao entra: o offset de X continua sendo o ultimo aceito.
    TEST_ASSERT_EQUAL_INT16(1100, p.presetOffsetDeci(Axis::X));

    // E o extremo sobrevive ao round-trip pelo blob, com CRC bom.
    uint8_t blob[Parameters::kParamBlobSize];
    ASSERT_ERR(Err::Ok, p.setPresetOffset(Axis::X, 1800));
    blobDeParametros(p, blob);
    Parameters lido = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, lido.loadParams(blob, sizeof(blob)));
    TEST_ASSERT_EQUAL_INT16(1800, lido.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(1800, lido.presetOffsetDeci(Axis::Y));
}

static void test_PRM_limite_recusa_fora_da_faixa_e_angulo_invalido(void) {
    // Manual 5.9 L206: cada limite e programado de -90,0 a +90,0 graus, passo de 0,1.
    Parameters p = Parameters::factoryDefaults();
    for (unsigned i = 0; i < 4; ++i) {
        ASSERT_ERR(Err::Ok, p.setLimitValue(kLimites[i], Angle::fromDeciDegrees(-900)));
        ASSERT_ERR(Err::Ok, p.setLimitValue(kLimites[i], Angle::fromDeciDegrees(900)));
        ASSERT_ERR(Err::Range, p.setLimitValue(kLimites[i], Angle::fromDeciDegrees(901)));
        ASSERT_ERR(Err::Range, p.setLimitValue(kLimites[i], Angle::invalid()));
    }
}

static void test_PRM_operacao_de_limite_recusa_codigo_desconhecido(void) {
    // Manual 5.9 L208 a L211: so existem Off, >=, <= e + (modulo).
    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.setLimitOp(LimitId::X1, LimitOp::Off));
    ASSERT_ERR(Err::Ok, p.setLimitOp(LimitId::X1, LimitOp::Absolute));
    ASSERT_ERR(Err::Range, p.setLimitOp(LimitId::X1, static_cast<LimitOp>(4)));
    ASSERT_ERR(Err::Range, p.setLimitOp(LimitId::X1, static_cast<LimitOp>(255)));
}

static void test_PRM_sentido_do_sensor_recusa_codigo_desconhecido(void) {
    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.setSensorDir(Axis::Y, SensorDir::CounterClockwise));
    ASSERT_ERR(Err::Range, p.setSensorDir(Axis::Y, static_cast<SensorDir>(2)));
    ASSERT_ERR(Err::Range, p.setSensorDir(Axis::Y, static_cast<SensorDir>(255)));
}

static void test_PRM_senha_recusa_acima_de_quatro_digitos(void) {
    // Manual 5.10 L228: a senha tem quatro digitos.
    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.setPassword(0));
    ASSERT_ERR(Err::Ok, p.setPassword(9999));
    ASSERT_ERR(Err::Range, p.setPassword(10000));
    ASSERT_ERR(Err::Range, p.setPassword(65535));
}

static void test_PRM_fundo_de_escala_recusa_zero_e_negativo(void) {
    // Manual 5.7: o fundo de escala e o angulo que corresponde a +10,00 Vcc. Zero ou negativo
    // nao define proporcao nenhuma.
    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.setCalFullScale(Axis::X, Angle::fromDeciDegrees(1)));
    ASSERT_ERR(Err::Ok, p.setCalFullScale(Axis::X, Angle::fromDeciDegrees(900)));
    ASSERT_ERR(Err::Range, p.setCalFullScale(Axis::X, Angle::fromDeciDegrees(0)));
    ASSERT_ERR(Err::Range, p.setCalFullScale(Axis::X, Angle::fromDeciDegrees(-450)));
    ASSERT_ERR(Err::Range, p.setCalFullScale(Axis::X, Angle::fromDeciDegrees(901)));
    ASSERT_ERR(Err::Range, p.setCalFullScale(Axis::X, Angle::invalid()));
}

static void test_A14_codigo_de_dac_fica_dentro_da_janela_de_trim(void) {
    // A14 opcao A: trim de -5000 a +4999 LSB em torno do codigo NOMINAL de cada ponto. Zero em
    // [27768, 37767] e ganho em [53982, 63981]. A janela do ganho tem de deixar passar codigo
    // ACIMA do nominal (erro de ganho negativo do DAC/referencia): 63981 vale cerca de +11,9 V,
    // dentro do trilho de +/-15 V, e sem isso a unidade simplesmente nao calibra.
    TEST_ASSERT_EQUAL_UINT16(27768, Parameters::kCalZeroCodeMin);
    TEST_ASSERT_EQUAL_UINT16(37767, Parameters::kCalZeroCodeMax);
    TEST_ASSERT_EQUAL_UINT16(53982, Parameters::kCalFullScaleCodeMin);
    TEST_ASSERT_EQUAL_UINT16(63981, Parameters::kCalFullScaleCodeMax);

    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.setCalZeroCode(Axis::X, 27768));
    ASSERT_ERR(Err::Ok, p.setCalZeroCode(Axis::X, 37767));
    ASSERT_ERR(Err::Range, p.setCalZeroCode(Axis::X, 27767));
    ASSERT_ERR(Err::Range, p.setCalZeroCode(Axis::X, 37768));
    ASSERT_ERR(Err::Range, p.setCalZeroCode(Axis::X, 0));
    // Manual 5.7 L254: 0,0 grau vale 0,00 Vcc. O codigo de -10,00 V (6554) nunca e zero.
    ASSERT_ERR(Err::Range, p.setCalZeroCode(Axis::X, 6554));

    ASSERT_ERR(Err::Ok, p.setCalFullScaleCode(Axis::X, 53982));
    ASSERT_ERR(Err::Ok, p.setCalFullScaleCode(Axis::X, 63981));
    ASSERT_ERR(Err::Range, p.setCalFullScaleCode(Axis::X, 53981));
    ASSERT_ERR(Err::Range, p.setCalFullScaleCode(Axis::X, 63982));
    ASSERT_ERR(Err::Range, p.setCalFullScaleCode(Axis::X, 65535));
}

static void test_A14_par_de_calibracao_degenerado_ou_invertido_e_recusado(void) {
    // Manual 5.7: a saida tem de ser "proporcional e simetrica". Par degenerado (zero igual ao
    // ganho) prende a saida num unico valor para qualquer angulo; par invertido faz a saida
    // decrescer quando o angulo cresce. As duas janelas do trim de A14 sao disjuntas e
    // ordenadas, entao nenhum dos dois casos tem como ser aceito.
    TEST_ASSERT_TRUE(Parameters::kCalZeroCodeMax < Parameters::kCalFullScaleCodeMin);

    Parameters p = Parameters::factoryDefaults();
    // Degenerado: zero e ganho no mesmo codigo.
    ASSERT_ERR(Err::Range, p.setCalFullScaleCode(Axis::Y, 32768));
    ASSERT_ERR(Err::Range, p.setCalPair(Axis::Y, 32768, 32768));
    // Invertido: ganho abaixo do zero.
    ASSERT_ERR(Err::Range, p.setCalZeroCode(Axis::Y, 58982));
    ASSERT_ERR(Err::Range, p.setCalFullScaleCode(Axis::Y, 6554));
    ASSERT_ERR(Err::Range, p.setCalPair(Axis::Y, 58982, 6554));
    TEST_ASSERT_EQUAL_UINT16(Parameters::kNominalCalZeroCode, p.calZeroCode(Axis::Y));
    TEST_ASSERT_EQUAL_UINT16(Parameters::kNominalCalFullScaleCode, p.calFullScaleCode(Axis::Y));

    // E um registro com CRC bom carregando o par degenerado tambem e recusado (A8: CRC
    // aprovado nao autoriza valor fora de faixa).
    uint8_t calBlob[Parameters::kCalBlobSize];
    blobDeCalibracao(parametrosDoCliente(), calBlob);
    calBlob[12] = 0x00;  // zeroCode Y = 32768
    calBlob[13] = 0x80;
    calBlob[16] = 0x00;  // fullScaleCode Y = 32768
    calBlob[17] = 0x80;
    reassinar(calBlob, Parameters::kCalBlobSize);
    ASSERT_ERR(Err::Range, p.loadCal(calBlob, sizeof(calBlob)));
}

static void test_A14_par_de_calibracao_e_gravado_de_uma_vez_ou_nenhuma(void) {
    // Sub-item de A14: meio par de calibracao e pior que nenhuma calibracao, porque nao tem
    // assinatura observavel. O commit do assistente grava zero e ganho juntos.
    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.setCalPair(Axis::X, 30000, 60000));
    TEST_ASSERT_EQUAL_UINT16(30000, p.calZeroCode(Axis::X));
    TEST_ASSERT_EQUAL_UINT16(60000, p.calFullScaleCode(Axis::X));

    // Ganho fora da janela: NENHUM dos dois campos pode mudar.
    ASSERT_ERR(Err::Range, p.setCalPair(Axis::X, 31000, 40000));
    TEST_ASSERT_EQUAL_UINT16(30000, p.calZeroCode(Axis::X));
    TEST_ASSERT_EQUAL_UINT16(60000, p.calFullScaleCode(Axis::X));

    // Zero fora da janela: idem.
    ASSERT_ERR(Err::Range, p.setCalPair(Axis::X, 60000, 61000));
    TEST_ASSERT_EQUAL_UINT16(30000, p.calZeroCode(Axis::X));
    TEST_ASSERT_EQUAL_UINT16(60000, p.calFullScaleCode(Axis::X));

    // E o eixo Y ficou intocado o tempo todo.
    TEST_ASSERT_EQUAL_UINT16(Parameters::kNominalCalZeroCode, p.calZeroCode(Axis::Y));
    TEST_ASSERT_EQUAL_UINT16(Parameters::kNominalCalFullScaleCode, p.calFullScaleCode(Axis::Y));
}

static void test_PRM_campo_recusado_preserva_o_valor_anterior(void) {
    // Um setpoint de rele nunca pode ficar num estado intermediario por causa de uma
    // digitacao invalida.
    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.setLimitValue(LimitId::X1, Angle::fromDeciDegrees(120)));
    ASSERT_ERR(Err::Range, p.setLimitValue(LimitId::X1, Angle::fromDeciDegrees(-901)));
    TEST_ASSERT_EQUAL_INT16(120, p.limitValue(LimitId::X1).deciDegrees());
    ASSERT_ERR(Err::Range, p.setPassword(12345));
    TEST_ASSERT_EQUAL_UINT16(1234, p.password());
}

// --- Serializacao (REQ-PER-01) ---

static void test_PER_01_round_trip_dos_parametros_preserva_todos_os_campos(void) {
    uint8_t blob[Parameters::kParamBlobSize];
    const Parameters origem = parametrosDoCliente();
    blobDeParametros(origem, blob);

    Parameters lido = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, lido.loadParams(blob, sizeof(blob)));

    TEST_ASSERT_EQUAL_INT16(-123, lido.preset(Axis::X).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(456, lido.preset(Axis::Y).deciDegrees());
    // L308: a leitura relativa volta sozinha na religacao porque o offset de PSET e gravado.
    TEST_ASSERT_EQUAL_INT16(-1100, lido.presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT16(88, lido.presetOffsetDeci(Axis::Y));
    TEST_ASSERT_EQUAL_INT16(-900, lido.limitValue(LimitId::X1).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(900, lido.limitValue(LimitId::X2).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(-1, lido.limitValue(LimitId::Y1).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(325, lido.limitValue(LimitId::Y2).deciDegrees());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LimitOp::Off),
                            static_cast<uint8_t>(lido.limitOp(LimitId::X1)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LimitOp::GreaterEqual),
                            static_cast<uint8_t>(lido.limitOp(LimitId::X2)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LimitOp::LessEqual),
                            static_cast<uint8_t>(lido.limitOp(LimitId::Y1)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LimitOp::Absolute),
                            static_cast<uint8_t>(lido.limitOp(LimitId::Y2)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SensorDir::CounterClockwise),
                            static_cast<uint8_t>(lido.sensorDir(Axis::X)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SensorDir::Clockwise),
                            static_cast<uint8_t>(lido.sensorDir(Axis::Y)));
    TEST_ASSERT_EQUAL_UINT16(9007, lido.password());

    uint8_t reserializado[Parameters::kParamBlobSize];
    blobDeParametros(lido, reserializado);
    TEST_ASSERT_EQUAL_INT(0, memcmp(blob, reserializado, sizeof(blob)));
}

static void test_PER_01_round_trip_da_calibracao_preserva_todos_os_campos(void) {
    uint8_t blob[Parameters::kCalBlobSize];
    blobDeCalibracao(parametrosDoCliente(), blob);

    Parameters lido = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, lido.loadCal(blob, sizeof(blob)));

    TEST_ASSERT_EQUAL_INT16(300, lido.calFullScale(Axis::X).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(900, lido.calFullScale(Axis::Y).deciDegrees());
    TEST_ASSERT_EQUAL_UINT16(32700, lido.calZeroCode(Axis::X));
    TEST_ASSERT_EQUAL_UINT16(32900, lido.calZeroCode(Axis::Y));
    TEST_ASSERT_EQUAL_UINT16(58000, lido.calFullScaleCode(Axis::X));
    TEST_ASSERT_EQUAL_UINT16(58982, lido.calFullScaleCode(Axis::Y));

    uint8_t reserializado[Parameters::kCalBlobSize];
    blobDeCalibracao(lido, reserializado);
    TEST_ASSERT_EQUAL_INT(0, memcmp(blob, reserializado, sizeof(blob)));
}

static void test_PER_01_blob_carrega_magic_e_versao_declarados(void) {
    // Um blob sem identidade propria nao pode ser distinguido de lixo nem de outra versao
    // de firmware.
    uint8_t par[Parameters::kParamBlobSize];
    uint8_t cal[Parameters::kCalBlobSize];
    blobDeParametros(Parameters::factoryDefaults(), par);
    blobDeCalibracao(Parameters::factoryDefaults(), cal);

    TEST_ASSERT_EQUAL_UINT8(0x31, par[0]);
    TEST_ASSERT_EQUAL_UINT8(0x52, par[1]);
    TEST_ASSERT_EQUAL_UINT8(0x50, par[2]);
    TEST_ASSERT_EQUAL_UINT8(0x44, par[3]);
    TEST_ASSERT_EQUAL_UINT8(Parameters::kParamVersion, par[4]);
    TEST_ASSERT_EQUAL_UINT8(0, par[5]);

    TEST_ASSERT_EQUAL_UINT8(0x31, cal[0]);
    TEST_ASSERT_EQUAL_UINT8(0x52, cal[1]);
    TEST_ASSERT_EQUAL_UINT8(0x43, cal[2]);
    TEST_ASSERT_EQUAL_UINT8(0x44, cal[3]);
    TEST_ASSERT_EQUAL_UINT8(Parameters::kCalVersion, cal[4]);
    TEST_ASSERT_EQUAL_UINT8(0, cal[5]);
}

static void test_PER_01_serializacao_recusa_buffer_curto_sem_escrever(void) {
    // O buffer e do CHAMADOR: a checagem de cap e a unica coisa entre um chamador distraido e
    // um estouro de 32 ou de 20 bytes.
    uint8_t curto[Parameters::kParamBlobSize - 1];
    memset(curto, 0xAA, sizeof(curto));
    uint16_t escritos = 123;
    ASSERT_ERR(Err::Param,
               Parameters::factoryDefaults().serializeParams(curto, sizeof(curto), escritos));
    TEST_ASSERT_EQUAL_UINT16(0, escritos);
    TEST_ASSERT_EQUAL_UINT8(0xAA, curto[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, curto[sizeof(curto) - 1]);

    escritos = 123;
    ASSERT_ERR(Err::Param, Parameters::factoryDefaults().serializeParams(nullptr, 64, escritos));
    TEST_ASSERT_EQUAL_UINT16(0, escritos);

    uint8_t curtoCal[Parameters::kCalBlobSize - 1];
    memset(curtoCal, 0xAA, sizeof(curtoCal));
    escritos = 123;
    ASSERT_ERR(Err::Param,
               Parameters::factoryDefaults().serializeCal(curtoCal, sizeof(curtoCal), escritos));
    TEST_ASSERT_EQUAL_UINT16(0, escritos);
    TEST_ASSERT_EQUAL_UINT8(0xAA, curtoCal[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, curtoCal[sizeof(curtoCal) - 1]);

    escritos = 123;
    ASSERT_ERR(Err::Param, Parameters::factoryDefaults().serializeCal(nullptr, 64, escritos));
    TEST_ASSERT_EQUAL_UINT16(0, escritos);
}

static void test_PER_01_load_aceita_buffer_maior_que_o_registro(void) {
    // IParameterStore::read devolve o slot inteiro: o outLen pode ser maior que o registro, com
    // o resto em flash apagada (0xFF). Isso nao e blob curto e nao pode virar Err::Param.
    const uint16_t kSlot = 48;
    uint8_t slot[kSlot];
    const Parameters origem = parametrosDoCliente();

    memset(slot, 0xFF, sizeof(slot));
    uint8_t par[Parameters::kParamBlobSize];
    blobDeParametros(origem, par);
    memcpy(slot, par, sizeof(par));
    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.loadParams(slot, kSlot));
    TEST_ASSERT_EQUAL_UINT16(9007, p.password());

    memset(slot, 0xFF, sizeof(slot));
    uint8_t cal[Parameters::kCalBlobSize];
    blobDeCalibracao(origem, cal);
    memcpy(slot, cal, sizeof(cal));
    ASSERT_ERR(Err::Ok, p.loadCal(slot, kSlot));
    TEST_ASSERT_EQUAL_UINT16(32700, p.calZeroCode(Axis::X));
}

// --- Corrupcao recusada (Decisao A8) ---

static void test_A8_blob_de_parametros_com_crc_errado_e_recusado(void) {
    uint8_t blob[Parameters::kParamBlobSize];
    blobDeParametros(parametrosDoCliente(), blob);
    blob[Parameters::kParamBlobSize - 1] =
        static_cast<uint8_t>(blob[Parameters::kParamBlobSize - 1] ^ 0x01u);

    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Crc, p.loadParams(blob, sizeof(blob)));
}

static void test_A8_um_unico_bit_invertido_no_corpo_e_recusado(void) {
    // Um bit trocado no setpoint de um rele tem de ser recusado, nao arredondado.
    uint8_t original[Parameters::kParamBlobSize];
    blobDeParametros(parametrosDoCliente(), original);
    for (uint16_t byte = 0; byte < Parameters::kParamBlobSize; ++byte) {
        for (uint8_t bit = 0; bit < 8; ++bit) {
            uint8_t blob[Parameters::kParamBlobSize];
            memcpy(blob, original, sizeof(blob));
            blob[byte] = static_cast<uint8_t>(blob[byte] ^ (1u << bit));
            Parameters p = Parameters::factoryDefaults();
            TEST_ASSERT_TRUE(p.loadParams(blob, sizeof(blob)).failed());
        }
    }
}

static void test_A8_blob_de_versao_desconhecida_e_recusado(void) {
    uint8_t blob[Parameters::kParamBlobSize];
    blobDeParametros(parametrosDoCliente(), blob);
    blob[4] = static_cast<uint8_t>(Parameters::kParamVersion + 1);
    reassinar(blob, Parameters::kParamBlobSize);

    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Unsupported, p.loadParams(blob, sizeof(blob)));

    uint8_t calBlob[Parameters::kCalBlobSize];
    blobDeCalibracao(parametrosDoCliente(), calBlob);
    calBlob[4] = static_cast<uint8_t>(Parameters::kCalVersion + 7);
    reassinar(calBlob, Parameters::kCalBlobSize);
    ASSERT_ERR(Err::Unsupported, p.loadCal(calBlob, sizeof(calBlob)));

    // Byte alto da versao tambem conta: 0x0101 nao e a versao 1.
    uint8_t altoPar[Parameters::kParamBlobSize];
    blobDeParametros(parametrosDoCliente(), altoPar);
    altoPar[5] = 0x01;
    reassinar(altoPar, Parameters::kParamBlobSize);
    ASSERT_ERR(Err::Unsupported, p.loadParams(altoPar, sizeof(altoPar)));

    uint8_t altoCal[Parameters::kCalBlobSize];
    blobDeCalibracao(parametrosDoCliente(), altoCal);
    altoCal[5] = 0x01;
    reassinar(altoCal, Parameters::kCalBlobSize);
    ASSERT_ERR(Err::Unsupported, p.loadCal(altoCal, sizeof(altoCal)));
}

static void test_A8_blob_curto_demais_e_recusado(void) {
    uint8_t blob[Parameters::kParamBlobSize];
    blobDeParametros(parametrosDoCliente(), blob);

    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Param, p.loadParams(blob, Parameters::kParamBlobSize - 1));
    ASSERT_ERR(Err::Param, p.loadParams(blob, 0));
    ASSERT_ERR(Err::Param, p.loadParams(nullptr, Parameters::kParamBlobSize));

    uint8_t calBlob[Parameters::kCalBlobSize];
    blobDeCalibracao(parametrosDoCliente(), calBlob);
    ASSERT_ERR(Err::Param, p.loadCal(calBlob, Parameters::kCalBlobSize - 1));
    ASSERT_ERR(Err::Param, p.loadCal(nullptr, Parameters::kCalBlobSize));
}

static void test_A8_blob_truncado_no_meio_da_gravacao_e_recusado(void) {
    // Queda de energia no meio do apagamento de setor: metade do registro novo, metade
    // com a flash apagada em 0xFF.
    uint8_t blob[Parameters::kParamBlobSize];
    blobDeParametros(parametrosDoCliente(), blob);
    memset(blob + Parameters::kParamBlobSize / 2, 0xFF, Parameters::kParamBlobSize / 2);

    Parameters p = Parameters::factoryDefaults();
    TEST_ASSERT_TRUE(p.loadParams(blob, sizeof(blob)).failed());

    uint8_t apagado[Parameters::kParamBlobSize];
    memset(apagado, 0xFF, sizeof(apagado));
    TEST_ASSERT_TRUE(p.loadParams(apagado, sizeof(apagado)).failed());

    uint8_t zerado[Parameters::kParamBlobSize];
    memset(zerado, 0x00, sizeof(zerado));
    TEST_ASSERT_TRUE(p.loadParams(zerado, sizeof(zerado)).failed());
}

// Um campo do blob, escrito por cima de um registro legitimo e reassinado: o caso de CRC bom
// com conteudo impossivel.
struct CasoDeCampo {
    uint16_t off;
    uint8_t largura;  // 1 ou 2 bytes, little-endian
    uint16_t valor;
    const char* nome;
};

static void gravarCampo(uint8_t* blob, const CasoDeCampo& caso) {
    blob[caso.off] = static_cast<uint8_t>(caso.valor & 0xFFu);
    if (caso.largura == 2) {
        blob[caso.off + 1] = static_cast<uint8_t>(caso.valor >> 8);
    }
}

static void test_A8_cada_campo_fora_de_faixa_sob_crc_bom_e_recusado(void) {
    // Uma linha por validacao de campo do loadParams/loadCal. Cada caso confere as DUAS coisas:
    // que a carga devolve Err::Range e que NENHUM byte do registro em uso mudou - inclusive nos
    // campos lidos ANTES do campo corrompido, que sao os que uma implementacao que escreve
    // direto no grupo vivo ja teria sobrescrito.
    static const CasoDeCampo kCasosPar[] = {
        {6, 2, 901, "presetDeci X = +90,1"},
        {8, 2, 0xFC7B, "presetDeci Y = -90,1"},
        {10, 2, 1801, "presetOffsetDeci X = +180,1"},
        {12, 2, 0xF8F7, "presetOffsetDeci Y = -180,1"},
        {14, 2, 1000, "limitDeci X1 = +100,0"},
        {20, 2, 0xFC7B, "limitDeci Y2 = -90,1"},
        {22, 1, 9, "limitOp X1 = 9"},
        {25, 1, 4, "limitOp Y2 = 4"},
        {26, 1, 7, "sensorDir X = 7"},
        {27, 1, 2, "sensorDir Y = 2"},
        {28, 2, 10000, "password = 10000"},
    };
    static const CasoDeCampo kCasosCal[] = {
        {6, 2, 0, "calFullScaleDeci X = 0,0"},
        {8, 2, 901, "calFullScaleDeci Y = +90,1"},
        {10, 2, 0, "calZeroCode X = 0"},
        {10, 2, 27767, "calZeroCode X abaixo do trim"},
        {12, 2, 37768, "calZeroCode Y acima do trim"},
        {14, 2, 53981, "calFullScaleCode X abaixo do trim"},
        {16, 2, 65535, "calFullScaleCode Y = 0xFFFF"},
    };

    for (unsigned i = 0; i < sizeof(kCasosPar) / sizeof(kCasosPar[0]); ++i) {
        Parameters p = parametrosDoCliente();
        uint8_t antes[Parameters::kParamBlobSize];
        blobDeParametros(p, antes);

        uint8_t blob[Parameters::kParamBlobSize];
        memcpy(blob, antes, sizeof(blob));
        gravarCampo(blob, kCasosPar[i]);
        reassinar(blob, Parameters::kParamBlobSize);
        ASSERT_ERR_MSG(Err::Range, p.loadParams(blob, sizeof(blob)), kCasosPar[i].nome);

        uint8_t depois[Parameters::kParamBlobSize];
        blobDeParametros(p, depois);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(antes, depois, sizeof(antes), kCasosPar[i].nome);
    }

    for (unsigned i = 0; i < sizeof(kCasosCal) / sizeof(kCasosCal[0]); ++i) {
        Parameters p = parametrosDoCliente();
        uint8_t antes[Parameters::kCalBlobSize];
        blobDeCalibracao(p, antes);

        uint8_t blob[Parameters::kCalBlobSize];
        memcpy(blob, antes, sizeof(blob));
        gravarCampo(blob, kCasosCal[i]);
        reassinar(blob, Parameters::kCalBlobSize);
        ASSERT_ERR_MSG(Err::Range, p.loadCal(blob, sizeof(blob)), kCasosCal[i].nome);

        uint8_t depois[Parameters::kCalBlobSize];
        blobDeCalibracao(p, depois);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(antes, depois, sizeof(antes), kCasosCal[i].nome);
    }
}

static void test_A8_carga_recusada_por_faixa_nao_altera_nenhum_campo(void) {
    // O campo corrompido e o ULTIMO validado de cada registro (a senha, no off 28, e o
    // fullScaleCode do eixo Y, no off 16): se algum campo anterior tivesse sido escrito antes da
    // recusa, o blob de depois seria diferente do de antes.
    Parameters p = parametrosDoCliente();
    uint8_t antes[Parameters::kParamBlobSize];
    blobDeParametros(p, antes);

    uint8_t blob[Parameters::kParamBlobSize];
    memcpy(blob, antes, sizeof(blob));
    blob[28] = 0x10;  // password = 10000
    blob[29] = 0x27;
    reassinar(blob, Parameters::kParamBlobSize);
    ASSERT_ERR(Err::Range, p.loadParams(blob, sizeof(blob)));

    uint8_t depois[Parameters::kParamBlobSize];
    blobDeParametros(p, depois);
    TEST_ASSERT_EQUAL_INT(0, memcmp(antes, depois, sizeof(antes)));
    TEST_ASSERT_EQUAL_UINT16(9007, p.password());

    uint8_t calAntes[Parameters::kCalBlobSize];
    blobDeCalibracao(p, calAntes);
    uint8_t calBlob[Parameters::kCalBlobSize];
    memcpy(calBlob, calAntes, sizeof(calBlob));
    calBlob[16] = 0xFF;  // fullScaleCode Y = 0xFFFF
    calBlob[17] = 0xFF;
    reassinar(calBlob, Parameters::kCalBlobSize);
    ASSERT_ERR(Err::Range, p.loadCal(calBlob, sizeof(calBlob)));

    uint8_t calDepois[Parameters::kCalBlobSize];
    blobDeCalibracao(p, calDepois);
    TEST_ASSERT_EQUAL_INT(0, memcmp(calAntes, calDepois, sizeof(calAntes)));
    TEST_ASSERT_EQUAL_UINT16(32700, p.calZeroCode(Axis::X));
}

static void test_A8_carga_recusada_por_crc_nao_altera_nenhum_campo(void) {
    // Nem defaults silenciosos, nem carga parcial: quem estava valendo continua valendo.
    Parameters p = parametrosDoCliente();
    uint8_t antes[Parameters::kParamBlobSize];
    blobDeParametros(p, antes);

    uint8_t corrompido[Parameters::kParamBlobSize];
    memcpy(corrompido, antes, sizeof(corrompido));
    corrompido[14] = static_cast<uint8_t>(corrompido[14] ^ 0xFFu);
    ASSERT_ERR(Err::Crc, p.loadParams(corrompido, sizeof(corrompido)));

    uint8_t depois[Parameters::kParamBlobSize];
    blobDeParametros(p, depois);
    TEST_ASSERT_EQUAL_INT(0, memcmp(antes, depois, sizeof(antes)));
}

// --- A8: os dois registros sao independentes ---

static void test_A8_calibracao_corrompida_nao_derruba_os_parametros(void) {
    uint8_t par[Parameters::kParamBlobSize];
    uint8_t cal[Parameters::kCalBlobSize];
    const Parameters origem = parametrosDoCliente();
    blobDeParametros(origem, par);
    blobDeCalibracao(origem, cal);
    cal[10] = static_cast<uint8_t>(cal[10] ^ 0xFFu);

    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.loadParams(par, sizeof(par)));
    ASSERT_ERR(Err::Crc, p.loadCal(cal, sizeof(cal)));

    // Os setpoints de rele continuam sendo os do cliente.
    TEST_ASSERT_EQUAL_INT16(-900, p.limitValue(LimitId::X1).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(325, p.limitValue(LimitId::Y2).deciDegrees());
    TEST_ASSERT_EQUAL_UINT16(9007, p.password());
    // E a calibracao permanece a que estava carregada, sem contaminar o outro grupo.
    TEST_ASSERT_EQUAL_UINT16(Parameters::kNominalCalZeroCode, p.calZeroCode(Axis::X));
    TEST_ASSERT_EQUAL_INT16(450, p.calFullScale(Axis::X).deciDegrees());
}

static void test_A8_parametros_corrompidos_nao_derrubam_a_calibracao(void) {
    uint8_t par[Parameters::kParamBlobSize];
    uint8_t cal[Parameters::kCalBlobSize];
    const Parameters origem = parametrosDoCliente();
    blobDeParametros(origem, par);
    blobDeCalibracao(origem, cal);
    par[6] = static_cast<uint8_t>(par[6] ^ 0xFFu);

    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Ok, p.loadCal(cal, sizeof(cal)));
    ASSERT_ERR(Err::Crc, p.loadParams(par, sizeof(par)));

    TEST_ASSERT_EQUAL_INT16(300, p.calFullScale(Axis::X).deciDegrees());
    TEST_ASSERT_EQUAL_UINT16(32700, p.calZeroCode(Axis::X));
    TEST_ASSERT_EQUAL_UINT16(58000, p.calFullScaleCode(Axis::X));
    // O grupo que comanda rele nao foi tocado pela carga da calibracao.
    TEST_ASSERT_EQUAL_INT16(50, p.limitValue(LimitId::X1).deciDegrees());
    TEST_ASSERT_EQUAL_UINT16(1234, p.password());
}

static void test_A8_um_registro_nao_e_aceito_no_lugar_do_outro(void) {
    // Slot trocado na NVS: o magic separa os dois registros.
    uint8_t par[Parameters::kParamBlobSize];
    blobDeParametros(parametrosDoCliente(), par);

    Parameters p = Parameters::factoryDefaults();
    ASSERT_ERR(Err::Storage, p.loadCal(par, sizeof(par)));

    uint8_t cal[Parameters::kCalBlobSize];
    blobDeCalibracao(parametrosDoCliente(), cal);
    uint8_t esticado[Parameters::kParamBlobSize];
    memset(esticado, 0x00, sizeof(esticado));
    memcpy(esticado, cal, sizeof(cal));
    ASSERT_ERR(Err::Storage, p.loadParams(esticado, sizeof(esticado)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_RST_01_defaults_da_tabela_2_preset_e_sentido_do_sensor);
    RUN_TEST(test_RST_01_defaults_da_tabela_2_limites_1_e_3_em_modulo_com_5_graus);
    RUN_TEST(test_RST_01_defaults_da_tabela_2_limites_2_e_4_em_off_com_zero);
    RUN_TEST(test_RST_01_default_da_tabela_2_da_senha_e_1234);
    RUN_TEST(test_RST_02_reset_geral_recarrega_a_calibracao_do_slot_de_fabrica);
    RUN_TEST(test_RST_02_calibracao_nominal_e_o_ultimo_recurso);
    RUN_TEST(test_RST_01_reset_de_fabrica_sobrescreve_todo_campo_de_parametro);
    RUN_TEST(test_PRM_seletor_invalido_e_recusado_sem_tocar_em_memoria);
    RUN_TEST(test_PRM_preset_recusa_fora_da_faixa_de_medicao);
    RUN_TEST(test_A9_offset_de_pset_cobre_a_faixa_completa_de_180_graus);
    RUN_TEST(test_PRM_limite_recusa_fora_da_faixa_e_angulo_invalido);
    RUN_TEST(test_PRM_operacao_de_limite_recusa_codigo_desconhecido);
    RUN_TEST(test_PRM_sentido_do_sensor_recusa_codigo_desconhecido);
    RUN_TEST(test_PRM_senha_recusa_acima_de_quatro_digitos);
    RUN_TEST(test_PRM_fundo_de_escala_recusa_zero_e_negativo);
    RUN_TEST(test_A14_codigo_de_dac_fica_dentro_da_janela_de_trim);
    RUN_TEST(test_A14_par_de_calibracao_degenerado_ou_invertido_e_recusado);
    RUN_TEST(test_A14_par_de_calibracao_e_gravado_de_uma_vez_ou_nenhuma);
    RUN_TEST(test_PRM_campo_recusado_preserva_o_valor_anterior);
    RUN_TEST(test_PER_01_round_trip_dos_parametros_preserva_todos_os_campos);
    RUN_TEST(test_PER_01_round_trip_da_calibracao_preserva_todos_os_campos);
    RUN_TEST(test_PER_01_blob_carrega_magic_e_versao_declarados);
    RUN_TEST(test_PER_01_serializacao_recusa_buffer_curto_sem_escrever);
    RUN_TEST(test_PER_01_load_aceita_buffer_maior_que_o_registro);
    RUN_TEST(test_A8_blob_de_parametros_com_crc_errado_e_recusado);
    RUN_TEST(test_A8_um_unico_bit_invertido_no_corpo_e_recusado);
    RUN_TEST(test_A8_blob_de_versao_desconhecida_e_recusado);
    RUN_TEST(test_A8_blob_curto_demais_e_recusado);
    RUN_TEST(test_A8_blob_truncado_no_meio_da_gravacao_e_recusado);
    RUN_TEST(test_A8_cada_campo_fora_de_faixa_sob_crc_bom_e_recusado);
    RUN_TEST(test_A8_carga_recusada_por_faixa_nao_altera_nenhum_campo);
    RUN_TEST(test_A8_carga_recusada_por_crc_nao_altera_nenhum_campo);
    RUN_TEST(test_A8_calibracao_corrompida_nao_derruba_os_parametros);
    RUN_TEST(test_A8_parametros_corrompidos_nao_derrubam_a_calibracao);
    RUN_TEST(test_A8_um_registro_nao_e_aceito_no_lugar_do_outro);
    return UNITY_END();
}
