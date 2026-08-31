// Testes de dominio da cadeia analogica: AnalogScaler (angulo -> codigo de DAC) e
// AnalogCalibration (os dois pontos da Auto Calibracao de 5.7 e a maquina do assistente).
//
// REQ-CAL-01 a CAL-08 e REQ-MEA-05. Manual: docs/manual-cliente-sui-2026.txt secao 5.7
// (L164, L165, L172, L177, L180, L184, L185, L187) e Tabela 1 L119/L120 (fundo de escala de
// 0,1 a 90,0 graus), Tabela 2 L254/L255 (par de fabrica 0,0 grau = 0,00 Vcc e 45,0 grau =
// +10,00 Vcc).
//
// Decisoes: A2 (nivel de falha -11,00 V = codigo 3932, fora da faixa util; modo corrente
// proibido) e A14 (campo de trim de 4 digitos com neutro em 5000, gate de plausibilidade
// unico no commit, buffer do assistente nunca gravado fora do passo final).
//
// Fato fechado da malha bipolar: V_OUT = 25*D/65536 - 12,5 V; 6554 = -10,00 V,
// 32768 = 0,00 V, 58982 = +10,00 V, vao nominal de 26214 codigos.
//
// Nao existe relogio falso aqui, e isso e deliberado: nenhuma das propriedades deste modulo
// tem prazo. Os prazos da Auto Calibracao - marcadores de -11,00 V por 1000 ms, teto absoluto
// de override de 300 s, inatividade de 120 s, hold de 3 s - sao da camada de aplicacao que
// hospeda esta classe (decisao 6 itens 6 a 8); o dominio aqui e ORDEM e ARITMETICA. Nenhum
// teste deste arquivo declara relogio proprio, e nenhum precisa do FakeClock canonico de
// test/fakes/fake_clock.h.
//
// Notas das sondas de fronteira, que sao numeros e nao opiniao:
//  - piso do gate: o espelho de -10,00 V, 2*zero - fundo, tem de ficar em 5243 ou acima
//    (decisao 9 item 11). make(24903, 45874) poe o espelho exatamente sobre o codigo de falha
//    3932, make(26456, 47670) o poe em 5242 e make(26457, 47671) em 5243. So o ultimo passa;
//  - teto do gate: 61342 e o grampo do assistente da decisao 6 item 7 (DECISIONS.md L1604).
//    make(35128, 61342) passa e make(35129, 61343) nao, e o segundo par e alcancavel pelo
//    assistente com campo de zero 7361 e campo de ganho 5000;
//  - janela do trim: o campo de 4 digitos representa zero em [27768, 37767] e vao em
//    [21214, 31213]; fora disso restore() recusa em vez de exibir o valor mais proximo;
//  - o par de serializacao tem os tres bytes baixos IMPARES (angulo 899 = 0x0383,
//    zero 32869 = 0x8065, fundo 58983 = 0xE667) para que o teste enxergue o bit 0 de cada
//    campo, e as assercoes por byte ancoram a ordem documentada no cabecalho do registro.
#include <stdint.h>
#include <unity.h>

#include "domain/analog_calibration.h"
#include "domain/analog_scaler.h"
#include "domain/angle.h"

using domain::AnalogCalibration;
using domain::AnalogScaler;
using domain::Angle;

void setUp(void) {}
void tearDown(void) {}

static AnalogScaler scalerCom(uint16_t zeroCode, uint16_t fullScaleCode, int16_t fsDeci) {
    AnalogScaler s;
    TEST_ASSERT_TRUE(AnalogScaler::make(zeroCode, fullScaleCode, fsDeci, s));
    return s;
}

static uint16_t codeOf(const AnalogScaler& s, int16_t deci) {
    return s.codeFor(Angle::fromDeciDegrees(deci));
}

static void levaAteOAjusteDoGanho(AnalogCalibration& cal, uint16_t zeroField, int16_t fsDeci) {
    TEST_ASSERT_TRUE(cal.begin().ok());
    TEST_ASSERT_TRUE(cal.setZeroField(zeroField).ok());
    TEST_ASSERT_TRUE(cal.confirmZero().ok());
    TEST_ASSERT_TRUE(cal.setFullScaleAngle(fsDeci).ok());
    TEST_ASSERT_TRUE(cal.confirmFullScaleAngle().ok());
}

static void montaRegistro(uint8_t* out, int16_t fsDeci, uint16_t zeroCode, uint16_t fullCode) {
    const uint16_t angle = static_cast<uint16_t>(fsDeci);
    out[0] = static_cast<uint8_t>(angle & 0xFFu);
    out[1] = static_cast<uint8_t>((angle >> 8) & 0xFFu);
    out[2] = static_cast<uint8_t>(zeroCode & 0xFFu);
    out[3] = static_cast<uint8_t>((zeroCode >> 8) & 0xFFu);
    out[4] = static_cast<uint8_t>(fullCode & 0xFFu);
    out[5] = static_cast<uint8_t>((fullCode >> 8) & 0xFFu);
}

// --- REQ-CAL-06: par de fabrica e fundo de escala programavel por eixo ---

static void test_REQ_CAL_06_par_de_fabrica_e_o_da_tabela_2(void) {
    const AnalogScaler s;
    TEST_ASSERT_EQUAL_UINT16(32768, s.zeroCode());
    TEST_ASSERT_EQUAL_UINT16(58982, s.fullScaleCode());
    TEST_ASSERT_EQUAL_UINT16(6554, s.mirrorCode());
    TEST_ASSERT_EQUAL_INT16(450, s.fullScaleAngleDeci());
}

static void test_REQ_CAL_06_aceita_todo_fundo_de_escala_de_01_a_900_graus(void) {
    AnalogScaler s;
    for (int16_t fs = 1; fs <= 900; ++fs) {
        TEST_ASSERT_TRUE(AnalogScaler::make(32768, 58982, fs, s));
        TEST_ASSERT_EQUAL_INT16(fs, s.fullScaleAngleDeci());
    }
    TEST_ASSERT_FALSE(AnalogScaler::make(32768, 58982, 0, s));
    TEST_ASSERT_FALSE(AnalogScaler::make(32768, 58982, 901, s));
    TEST_ASSERT_FALSE(AnalogScaler::make(32768, 58982, -450, s));
}

static void test_REQ_CAL_06_fundo_de_escala_de_90_graus_da_exatamente_mais_10V(void) {
    const AnalogScaler s = scalerCom(32768, 58982, 900);
    TEST_ASSERT_EQUAL_UINT16(58982, codeOf(s, 900));
    TEST_ASSERT_EQUAL_UINT16(6554, codeOf(s, -900));
    TEST_ASSERT_EQUAL_UINT16(45875, codeOf(s, 450));
}

static void test_REQ_CAL_06_fundo_de_escala_de_um_decimo_de_grau(void) {
    const AnalogScaler s = scalerCom(32768, 58982, 1);
    TEST_ASSERT_EQUAL_UINT16(32768, codeOf(s, 0));
    TEST_ASSERT_EQUAL_UINT16(58982, codeOf(s, 1));
    TEST_ASSERT_EQUAL_UINT16(6554, codeOf(s, -1));
    TEST_ASSERT_EQUAL_UINT16(58982, codeOf(s, 900));
    TEST_ASSERT_EQUAL_UINT16(6554, codeOf(s, -900));
}

static void test_REQ_CAL_06_fundo_de_escala_nao_altera_a_faixa_do_display(void) {
    const AnalogScaler curto = scalerCom(32768, 58982, 450);
    const AnalogScaler longo = scalerCom(32768, 58982, 900);
    TEST_ASSERT_EQUAL_UINT16(58982, codeOf(curto, 450));
    TEST_ASSERT_EQUAL_UINT16(45875, codeOf(longo, 450));
}

// --- REQ-CAL-07: saturacao em +/-10,00 V acima do fundo de escala ---

static void test_REQ_CAL_07_satura_nos_dois_sentidos_sem_wrap_de_inteiro(void) {
    const int16_t fundos[] = {1, 2, 50, 450, 899, 900};
    for (unsigned i = 0; i < sizeof(fundos) / sizeof(fundos[0]); ++i) {
        const AnalogScaler s = scalerCom(32768, 58982, fundos[i]);
        for (int16_t v = -900; v <= 900; ++v) {
            const uint16_t code = codeOf(s, v);
            TEST_ASSERT_TRUE(code >= s.mirrorCode());
            TEST_ASSERT_TRUE(code <= s.fullScaleCode());
        }
        TEST_ASSERT_EQUAL_UINT16(58982, codeOf(s, 900));
        TEST_ASSERT_EQUAL_UINT16(6554, codeOf(s, -900));
        TEST_ASSERT_EQUAL_UINT16(58982, codeOf(s, fundos[i]));
        TEST_ASSERT_EQUAL_UINT16(6554, codeOf(s, static_cast<int16_t>(-fundos[i])));
    }
}

static void test_REQ_CAL_07_satura_no_codigo_calibrado_e_nao_no_nominal(void) {
    const AnalogScaler s = scalerCom(33000, 60000, 450);
    TEST_ASSERT_EQUAL_UINT16(60000, codeOf(s, 450));
    TEST_ASSERT_EQUAL_UINT16(60000, codeOf(s, 900));
    TEST_ASSERT_EQUAL_UINT16(6000, codeOf(s, -450));
    TEST_ASSERT_EQUAL_UINT16(6000, codeOf(s, -900));
}

static void test_REQ_CAL_07_zero_grau_da_o_codigo_de_zero_em_qualquer_fundo_de_escala(void) {
    for (int16_t fs = 1; fs <= 900; ++fs) {
        TEST_ASSERT_EQUAL_UINT16(32768, codeOf(scalerCom(32768, 58982, fs), 0));
    }
    TEST_ASSERT_EQUAL_UINT16(33000, codeOf(scalerCom(33000, 60000, 450), 0));
}

static void test_A14_simetria_ponto_a_ponto_em_torno_do_codigo_de_zero(void) {
    const int16_t fundos[] = {1, 3, 7, 450, 900};
    const uint16_t zeros[] = {32768, 30000, 35000};
    const uint16_t vaos[] = {26214, 21214, 26214};
    for (unsigned f = 0; f < sizeof(fundos) / sizeof(fundos[0]); ++f) {
        for (unsigned c = 0; c < sizeof(zeros) / sizeof(zeros[0]); ++c) {
            const uint16_t zero = zeros[c];
            const uint16_t fsCode = static_cast<uint16_t>(zero + vaos[c]);
            const AnalogScaler s = scalerCom(zero, fsCode, fundos[f]);
            for (int16_t v = 0; v <= 900; ++v) {
                const int32_t mais = static_cast<int32_t>(codeOf(s, v)) - static_cast<int32_t>(zero);
                const int32_t menos =
                    static_cast<int32_t>(codeOf(s, static_cast<int16_t>(-v))) - static_cast<int32_t>(zero);
                TEST_ASSERT_EQUAL_INT32(mais, -menos);
            }
        }
    }
}

// --- REQ-MEA-05: erro de arredondamento abaixo de 0,05 grau equivalente ---

static void test_REQ_MEA_05_erro_de_arredondamento_abaixo_de_005_grau(void) {
    const int16_t fundos[] = {1, 2, 3, 7, 13, 100, 450, 899, 900};
    const uint16_t zeros[] = {32768, 30000, 35000};
    const uint16_t vaos[] = {26214, 21214, 26214};
    for (unsigned f = 0; f < sizeof(fundos) / sizeof(fundos[0]); ++f) {
        for (unsigned c = 0; c < sizeof(zeros) / sizeof(zeros[0]); ++c) {
            const int16_t fundo = fundos[f];
            const int64_t vao = static_cast<int64_t>(vaos[c]);
            const AnalogScaler s =
                scalerCom(zeros[c], static_cast<uint16_t>(zeros[c] + vaos[c]), fundo);
            for (int16_t v = static_cast<int16_t>(-fundo); v <= fundo; ++v) {
                const int64_t code = static_cast<int64_t>(codeOf(s, v));
                const int64_t zero = static_cast<int64_t>(s.zeroCode());
                const int64_t residuo = (code - zero) * static_cast<int64_t>(fundo) - vao * static_cast<int64_t>(v);
                const int64_t modulo = residuo < 0 ? -residuo : residuo;
                TEST_ASSERT_TRUE(2 * modulo <= static_cast<int64_t>(fundo));
                TEST_ASSERT_TRUE(2 * modulo < vao);
            }
        }
    }
}

static void test_REQ_MEA_05_arredonda_ao_codigo_mais_proximo_e_e_exato_na_metade(void) {
    const AnalogScaler s = scalerCom(32768, 58982, 900);
    TEST_ASSERT_EQUAL_UINT16(32768 + 26214 / 2, codeOf(s, 450));
    TEST_ASSERT_EQUAL_UINT16(32768 - 26214 / 2, codeOf(s, -450));
    TEST_ASSERT_EQUAL_UINT16(32768 + 204, codeOf(s, 7));
    TEST_ASSERT_EQUAL_UINT16(32768 - 204, codeOf(s, -7));
}

// --- A2: nivel de falha em -11,00 V, codigo 3932, fora da faixa util ---

static void test_A2_angulo_invalido_da_o_codigo_de_falha(void) {
    const AnalogScaler s;
    TEST_ASSERT_EQUAL_UINT16(3932, AnalogScaler::kFaultCode);
    TEST_ASSERT_EQUAL_UINT16(3932, s.codeFor(Angle::invalid()));
}

static void test_A2_codigo_de_falha_fica_fora_da_faixa_util_em_toda_calibracao_aceita(void) {
    const int16_t fundos[] = {1, 450, 900};
    const uint16_t zeros[] = {32768, 27768, 37767};
    for (unsigned f = 0; f < sizeof(fundos) / sizeof(fundos[0]); ++f) {
        for (unsigned c = 0; c < sizeof(zeros) / sizeof(zeros[0]); ++c) {
            const AnalogScaler s =
                scalerCom(zeros[c], static_cast<uint16_t>(zeros[c] + 21214), fundos[f]);
            for (int16_t v = -900; v <= 900; ++v) {
                const uint16_t code = codeOf(s, v);
                TEST_ASSERT_TRUE(code >= AnalogScaler::kCodeMin);
                TEST_ASSERT_NOT_EQUAL(AnalogScaler::kFaultCode, code);
            }
        }
    }
    TEST_ASSERT_TRUE(AnalogScaler::kFaultCode < AnalogScaler::kCodeMin);
    TEST_ASSERT_TRUE(AnalogScaler::kCodeMin - AnalogScaler::kFaultCode >= 1311);
}

// --- A14: gate de plausibilidade unico ---

static void test_A14_gate_recusa_vao_absurdo_e_calibracao_invertida(void) {
    AnalogScaler s;
    TEST_ASSERT_FALSE(AnalogScaler::make(32768, 42768, 450, s));
    TEST_ASSERT_FALSE(AnalogScaler::make(32768, 65535, 450, s));
    TEST_ASSERT_FALSE(AnalogScaler::make(58982, 32768, 450, s));
    TEST_ASSERT_FALSE(AnalogScaler::make(32768, 32768, 450, s));
    TEST_ASSERT_FALSE(AnalogScaler::make(32768, static_cast<uint16_t>(32768 + 20970), 450, s));
    TEST_ASSERT_TRUE(AnalogScaler::make(32768, static_cast<uint16_t>(32768 + 20971), 450, s));
    TEST_ASSERT_TRUE(AnalogScaler::make(32768, 58982, 450, s));
}

static void test_A14_gate_recusa_par_que_encosta_no_codigo_de_falha(void) {
    AnalogScaler s;
    TEST_ASSERT_EQUAL_INT32(5243, AnalogScaler::kCodeMin);
    TEST_ASSERT_FALSE(AnalogScaler::make(24903, 45874, 450, s));
    TEST_ASSERT_FALSE(AnalogScaler::make(26456, 47670, 450, s));
    TEST_ASSERT_TRUE(AnalogScaler::make(26457, 47671, 450, s));
    TEST_ASSERT_EQUAL_UINT16(5243, s.mirrorCode());
    TEST_ASSERT_TRUE(AnalogScaler::make(26458, 47672, 450, s));
    TEST_ASSERT_EQUAL_UINT16(5244, s.mirrorCode());
}

static void test_A14_teto_do_gate_e_o_grampo_de_61342_do_assistente(void) {
    AnalogScaler s;
    TEST_ASSERT_EQUAL_INT32(61342, AnalogScaler::kCodeMax);
    TEST_ASSERT_TRUE(AnalogScaler::make(35128, 61342, 450, s));
    TEST_ASSERT_EQUAL_UINT16(61342, s.fullScaleCode());
    TEST_ASSERT_FALSE(AnalogScaler::make(35129, 61343, 450, s));

    AnalogCalibration cal;
    levaAteOAjusteDoGanho(cal, 7360, 450);
    TEST_ASSERT_TRUE(cal.setGainField(5000).ok());
    TEST_ASSERT_TRUE(cal.commit().ok());
    TEST_ASSERT_EQUAL_UINT16(61342, cal.scaler().fullScaleCode());

    AnalogCalibration acima;
    levaAteOAjusteDoGanho(acima, 7361, 450);
    TEST_ASSERT_TRUE(acima.setGainField(5000).ok());
    TEST_ASSERT_TRUE(acima.commit().err == Err::Range);
    TEST_ASSERT_EQUAL_UINT16(58982, acima.scaler().fullScaleCode());
}

static void test_A14_gate_nao_recusa_placa_boa(void) {
    AnalogScaler s;
    for (int32_t tz = -300; tz <= 300; tz += 20) {
        for (int32_t tg = -700; tg <= 700; tg += 20) {
            const uint16_t zero = static_cast<uint16_t>(32768 + tz);
            const uint16_t fsCode = static_cast<uint16_t>(58982 + tg);
            TEST_ASSERT_TRUE(AnalogScaler::make(zero, fsCode, 450, s));
        }
    }
}

// --- REQ-CAL-01 e CAL-03: maquina do assistente de Auto Calibracao ---

static void test_REQ_CAL_01_o_assistente_comeca_no_ajuste_do_zero(void) {
    AnalogCalibration cal;
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Idle);
    TEST_ASSERT_TRUE(cal.begin().ok());
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Zero);
    TEST_ASSERT_TRUE(cal.begin().err == Err::Busy);
}

static void test_REQ_CAL_03_recusa_ir_ao_ganho_sem_o_zero_confirmado(void) {
    AnalogCalibration cal;
    TEST_ASSERT_TRUE(cal.begin().ok());
    TEST_ASSERT_TRUE(cal.setGainField(5100).err == Err::NotCalibrated);
    TEST_ASSERT_TRUE(cal.setFullScaleAngle(450).err == Err::NotCalibrated);
    TEST_ASSERT_TRUE(cal.confirmFullScaleAngle().err == Err::NotCalibrated);
    TEST_ASSERT_TRUE(cal.commit().err == Err::NotCalibrated);
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Zero);

    TEST_ASSERT_TRUE(cal.confirmZero().ok());
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::FullScaleAngle);
    TEST_ASSERT_TRUE(cal.setGainField(5100).err == Err::NotCalibrated);
    TEST_ASSERT_TRUE(cal.commit().err == Err::NotCalibrated);

    TEST_ASSERT_TRUE(cal.setFullScaleAngle(450).ok());
    TEST_ASSERT_TRUE(cal.confirmFullScaleAngle().ok());
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Gain);
    TEST_ASSERT_TRUE(cal.setGainField(5100).ok());
    TEST_ASSERT_TRUE(cal.commit().ok());
}

static void test_REQ_CAL_03_fora_do_assistente_nenhum_passo_e_aceito(void) {
    AnalogCalibration cal;
    TEST_ASSERT_TRUE(cal.setZeroField(5100).err == Err::NotCalibrated);
    TEST_ASSERT_TRUE(cal.confirmZero().err == Err::NotCalibrated);
    TEST_ASSERT_TRUE(cal.setFullScaleAngle(450).err == Err::NotCalibrated);
    TEST_ASSERT_TRUE(cal.setGainField(5100).err == Err::NotCalibrated);
    TEST_ASSERT_TRUE(cal.commit().err == Err::NotCalibrated);
}

static void test_REQ_CAL_03_ganho_e_calculado_sobre_o_zero_confirmado(void) {
    AnalogCalibration cal;
    levaAteOAjusteDoGanho(cal, 5100, 450);
    TEST_ASSERT_TRUE(cal.setGainField(5000).ok());
    TEST_ASSERT_TRUE(cal.commit().ok());
    TEST_ASSERT_EQUAL_UINT16(32868, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(59082, cal.scaler().fullScaleCode());
    TEST_ASSERT_EQUAL_UINT16(32868, cal.scaler().codeFor(Angle::fromDeciDegrees(0)));
    TEST_ASSERT_EQUAL_UINT16(59082, cal.scaler().codeFor(Angle::fromDeciDegrees(450)));
}

// --- REQ-CAL-05: concluida a calibracao, o equipamento volta ao Modo Normal (L184) ---

static void test_REQ_CAL_05_commit_devolve_ao_modo_normal(void) {
    AnalogCalibration cal;
    levaAteOAjusteDoGanho(cal, 5100, 450);
    TEST_ASSERT_TRUE(cal.setGainField(4900).ok());
    TEST_ASSERT_TRUE(cal.commit().ok());
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Idle);
    TEST_ASSERT_TRUE(cal.setZeroField(5000).err == Err::NotCalibrated);
    TEST_ASSERT_TRUE(cal.setGainField(5000).err == Err::NotCalibrated);
    TEST_ASSERT_TRUE(cal.commit().err == Err::NotCalibrated);
    TEST_ASSERT_TRUE(cal.begin().ok());
}

// --- A14: campo de trim de 4 digitos com neutro em 5000 ---

static void test_A14_campo_de_trim_tem_neutro_em_5000(void) {
    AnalogCalibration cal;
    TEST_ASSERT_TRUE(cal.begin().ok());
    TEST_ASSERT_EQUAL_UINT16(5000, cal.zeroField());
    TEST_ASSERT_EQUAL_UINT16(5000, cal.gainField());
    TEST_ASSERT_TRUE(cal.setZeroField(0).ok());
    TEST_ASSERT_TRUE(cal.setZeroField(9999).ok());
    TEST_ASSERT_TRUE(cal.setZeroField(10000).err == Err::Range);
    TEST_ASSERT_EQUAL_UINT16(9999, cal.zeroField());
    TEST_ASSERT_TRUE(cal.confirmZero().ok());
    TEST_ASSERT_TRUE(cal.setFullScaleAngle(450).ok());
    TEST_ASSERT_TRUE(cal.confirmFullScaleAngle().ok());
    TEST_ASSERT_TRUE(cal.setGainField(10000).err == Err::Range);
}

static void test_A14_um_digito_do_campo_vale_um_lsb_do_dac(void) {
    AnalogCalibration cal;
    levaAteOAjusteDoGanho(cal, 4900, 450);
    TEST_ASSERT_TRUE(cal.setGainField(5001).ok());
    TEST_ASSERT_TRUE(cal.commit().ok());
    TEST_ASSERT_EQUAL_UINT16(32668, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(58883, cal.scaler().fullScaleCode());
}

static void test_A14_as_telas_abrem_no_valor_corrente(void) {
    AnalogCalibration cal;
    levaAteOAjusteDoGanho(cal, 5100, 450);
    TEST_ASSERT_TRUE(cal.setGainField(4900).ok());
    TEST_ASSERT_TRUE(cal.commit().ok());
    TEST_ASSERT_EQUAL_UINT16(5100, cal.zeroField());
    TEST_ASSERT_EQUAL_UINT16(4900, cal.gainField());

    uint8_t outro[AnalogCalibration::kRecordBytes];
    montaRegistro(outro, 900, 33018, 59582);
    TEST_ASSERT_TRUE(cal.restore(outro, sizeof(outro)));
    TEST_ASSERT_TRUE(cal.begin().ok());
    TEST_ASSERT_EQUAL_UINT16(5250, cal.zeroField());
    TEST_ASSERT_EQUAL_UINT16(5350, cal.gainField());
    TEST_ASSERT_EQUAL_INT16(900, cal.fullScaleAngle());
}

static void test_REQ_CAL_06_fundo_de_escala_do_assistente_e_de_01_a_900_graus(void) {
    AnalogCalibration cal;
    TEST_ASSERT_TRUE(cal.begin().ok());
    TEST_ASSERT_TRUE(cal.confirmZero().ok());
    TEST_ASSERT_TRUE(cal.setFullScaleAngle(0).err == Err::Range);
    TEST_ASSERT_TRUE(cal.setFullScaleAngle(901).err == Err::Range);
    TEST_ASSERT_TRUE(cal.setFullScaleAngle(-450).err == Err::Range);
    TEST_ASSERT_TRUE(cal.setFullScaleAngle(1).ok());
    TEST_ASSERT_TRUE(cal.setFullScaleAngle(900).ok());
    TEST_ASSERT_EQUAL_INT16(900, cal.fullScaleAngle());
}

// --- REQ-CAL-04: o par zero+ganho e gravado inteiro ou nao e gravado ---

static void test_REQ_CAL_04_abortar_nao_grava_meio_par(void) {
    AnalogCalibration cal;
    levaAteOAjusteDoGanho(cal, 5100, 900);
    TEST_ASSERT_TRUE(cal.setGainField(5200).ok());
    cal.abort();
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Idle);
    TEST_ASSERT_EQUAL_UINT16(32768, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(58982, cal.scaler().fullScaleCode());
    TEST_ASSERT_EQUAL_INT16(450, cal.scaler().fullScaleAngleDeci());
    TEST_ASSERT_EQUAL_UINT16(5000, cal.zeroField());
    TEST_ASSERT_EQUAL_UINT16(5000, cal.gainField());
    TEST_ASSERT_EQUAL_INT16(450, cal.fullScaleAngle());
}

static void test_A14_gate_recusa_calibracao_absurda_sem_gravar(void) {
    AnalogCalibration cal;
    levaAteOAjusteDoGanho(cal, 0, 450);
    TEST_ASSERT_TRUE(cal.setGainField(9999).ok());
    TEST_ASSERT_TRUE(cal.commit().err == Err::Range);
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Gain);
    TEST_ASSERT_EQUAL_UINT16(9999, cal.gainField());
    TEST_ASSERT_EQUAL_UINT16(32768, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(58982, cal.scaler().fullScaleCode());

    cal.abort();
    levaAteOAjusteDoGanho(cal, 5000, 450);
    TEST_ASSERT_TRUE(cal.setGainField(5000).ok());
    TEST_ASSERT_TRUE(cal.commit().ok());
    TEST_ASSERT_EQUAL_UINT16(32768, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(58982, cal.scaler().fullScaleCode());
}

static void test_A14_commit_recusa_estouro_do_codigo_de_fundo_de_escala(void) {
    AnalogCalibration cal;
    levaAteOAjusteDoGanho(cal, 9999, 450);
    TEST_ASSERT_TRUE(cal.setGainField(9999).ok());
    TEST_ASSERT_TRUE(cal.commit().err == Err::Range);
    TEST_ASSERT_TRUE(cal.step() == AnalogCalibration::Step::Gain);
    TEST_ASSERT_EQUAL_UINT16(9999, cal.zeroField());
    TEST_ASSERT_EQUAL_UINT16(9999, cal.gainField());
    TEST_ASSERT_EQUAL_UINT16(32768, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(58982, cal.scaler().fullScaleCode());
}

// --- REQ-CAL-04: serializacao do par ---

static void test_REQ_CAL_04_serializa_e_restaura_o_par(void) {
    AnalogCalibration origem;
    levaAteOAjusteDoGanho(origem, 5101, 899);
    TEST_ASSERT_TRUE(origem.setGainField(4900).ok());
    TEST_ASSERT_TRUE(origem.commit().ok());
    TEST_ASSERT_EQUAL_UINT16(32869, origem.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(58983, origem.scaler().fullScaleCode());
    TEST_ASSERT_EQUAL_INT16(899, origem.scaler().fullScaleAngleDeci());

    uint8_t registro[AnalogCalibration::kRecordBytes];
    TEST_ASSERT_TRUE(origem.serialize(registro, sizeof(registro)));
    TEST_ASSERT_EQUAL_UINT8(0x83, registro[0]);
    TEST_ASSERT_EQUAL_UINT8(0x03, registro[1]);
    TEST_ASSERT_EQUAL_UINT8(0x65, registro[2]);
    TEST_ASSERT_EQUAL_UINT8(0x80, registro[3]);
    TEST_ASSERT_EQUAL_UINT8(0x67, registro[4]);
    TEST_ASSERT_EQUAL_UINT8(0xE6, registro[5]);

    AnalogCalibration destino;
    TEST_ASSERT_TRUE(destino.restore(registro, sizeof(registro)));
    TEST_ASSERT_EQUAL_UINT16(32869, destino.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(58983, destino.scaler().fullScaleCode());
    TEST_ASSERT_EQUAL_INT16(899, destino.scaler().fullScaleAngleDeci());
    TEST_ASSERT_TRUE(destino.begin().ok());
    TEST_ASSERT_EQUAL_UINT16(5101, destino.zeroField());
    TEST_ASSERT_EQUAL_UINT16(4900, destino.gainField());
    TEST_ASSERT_EQUAL_INT16(899, destino.fullScaleAngle());
}

static void test_REQ_CAL_04_serializacao_recusa_buffer_curto(void) {
    AnalogCalibration cal;
    uint8_t registro[AnalogCalibration::kRecordBytes];
    montaRegistro(registro, 450, 32768, 58982);
    TEST_ASSERT_FALSE(cal.restore(registro, AnalogCalibration::kRecordBytes - 1));
    TEST_ASSERT_TRUE(cal.restore(registro, AnalogCalibration::kRecordBytes));

    uint8_t saida[AnalogCalibration::kRecordBytes] = {0};
    TEST_ASSERT_FALSE(cal.serialize(saida, AnalogCalibration::kRecordBytes - 1));
    TEST_ASSERT_EQUAL_UINT8(0, saida[0]);
    TEST_ASSERT_EQUAL_UINT8(0, saida[5]);
    TEST_ASSERT_TRUE(cal.serialize(saida, AnalogCalibration::kRecordBytes));
    TEST_ASSERT_EQUAL_UINT8(0xC2, saida[0]);
    TEST_ASSERT_EQUAL_UINT8(0xE6, saida[5]);

    TEST_ASSERT_FALSE(cal.serialize(nullptr, AnalogCalibration::kRecordBytes));
    TEST_ASSERT_FALSE(cal.restore(nullptr, AnalogCalibration::kRecordBytes));
}

static void test_A14_registro_reprovado_mantem_o_par_de_fabrica(void) {
    AnalogCalibration cal;
    uint8_t registro[AnalogCalibration::kRecordBytes] = {0};
    TEST_ASSERT_FALSE(cal.restore(registro, sizeof(registro)));
    TEST_ASSERT_EQUAL_UINT16(32768, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(58982, cal.scaler().fullScaleCode());
    TEST_ASSERT_EQUAL_INT16(450, cal.scaler().fullScaleAngleDeci());

    montaRegistro(registro, 450, 32768, 42768);
    TEST_ASSERT_FALSE(cal.restore(registro, sizeof(registro)));
    TEST_ASSERT_EQUAL_UINT16(58982, cal.scaler().fullScaleCode());

    montaRegistro(registro, 450, 32768, 58982);
    TEST_ASSERT_TRUE(cal.restore(registro, sizeof(registro)));
}

static void test_REQ_CAL_04_restore_recusa_par_que_o_campo_de_trim_nao_representa(void) {
    AnalogCalibration cal;
    uint8_t registro[AnalogCalibration::kRecordBytes];

    montaRegistro(registro, 450, 27000, 48214);
    TEST_ASSERT_FALSE(cal.restore(registro, sizeof(registro)));
    TEST_ASSERT_EQUAL_UINT16(32768, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(58982, cal.scaler().fullScaleCode());

    montaRegistro(registro, 450, 37768, 58982);
    TEST_ASSERT_FALSE(cal.restore(registro, sizeof(registro)));
    TEST_ASSERT_EQUAL_UINT16(32768, cal.scaler().zeroCode());

    montaRegistro(registro, 450, 32768, 53768);
    TEST_ASSERT_FALSE(cal.restore(registro, sizeof(registro)));
    TEST_ASSERT_EQUAL_UINT16(58982, cal.scaler().fullScaleCode());

    montaRegistro(registro, 450, 37767, 61342);
    TEST_ASSERT_TRUE(cal.restore(registro, sizeof(registro)));
    TEST_ASSERT_TRUE(cal.begin().ok());
    TEST_ASSERT_EQUAL_UINT16(9999, cal.zeroField());
    TEST_ASSERT_EQUAL_UINT16(2361, cal.gainField());
    cal.abort();

    montaRegistro(registro, 450, 27768, 48982);
    TEST_ASSERT_TRUE(cal.restore(registro, sizeof(registro)));
    TEST_ASSERT_TRUE(cal.begin().ok());
    TEST_ASSERT_EQUAL_UINT16(0, cal.zeroField());
    TEST_ASSERT_EQUAL_UINT16(0, cal.gainField());
    TEST_ASSERT_TRUE(cal.confirmZero().ok());
    TEST_ASSERT_TRUE(cal.confirmFullScaleAngle().ok());
    TEST_ASSERT_TRUE(cal.commit().ok());
    TEST_ASSERT_EQUAL_UINT16(27768, cal.scaler().zeroCode());
    TEST_ASSERT_EQUAL_UINT16(48982, cal.scaler().fullScaleCode());
    TEST_ASSERT_EQUAL_INT16(450, cal.scaler().fullScaleAngleDeci());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_REQ_CAL_06_par_de_fabrica_e_o_da_tabela_2);
    RUN_TEST(test_REQ_CAL_06_aceita_todo_fundo_de_escala_de_01_a_900_graus);
    RUN_TEST(test_REQ_CAL_06_fundo_de_escala_de_90_graus_da_exatamente_mais_10V);
    RUN_TEST(test_REQ_CAL_06_fundo_de_escala_de_um_decimo_de_grau);
    RUN_TEST(test_REQ_CAL_06_fundo_de_escala_nao_altera_a_faixa_do_display);
    RUN_TEST(test_REQ_CAL_07_satura_nos_dois_sentidos_sem_wrap_de_inteiro);
    RUN_TEST(test_REQ_CAL_07_satura_no_codigo_calibrado_e_nao_no_nominal);
    RUN_TEST(test_REQ_CAL_07_zero_grau_da_o_codigo_de_zero_em_qualquer_fundo_de_escala);
    RUN_TEST(test_A14_simetria_ponto_a_ponto_em_torno_do_codigo_de_zero);
    RUN_TEST(test_REQ_MEA_05_erro_de_arredondamento_abaixo_de_005_grau);
    RUN_TEST(test_REQ_MEA_05_arredonda_ao_codigo_mais_proximo_e_e_exato_na_metade);
    RUN_TEST(test_A2_angulo_invalido_da_o_codigo_de_falha);
    RUN_TEST(test_A2_codigo_de_falha_fica_fora_da_faixa_util_em_toda_calibracao_aceita);
    RUN_TEST(test_A14_gate_recusa_vao_absurdo_e_calibracao_invertida);
    RUN_TEST(test_A14_gate_recusa_par_que_encosta_no_codigo_de_falha);
    RUN_TEST(test_A14_teto_do_gate_e_o_grampo_de_61342_do_assistente);
    RUN_TEST(test_A14_gate_nao_recusa_placa_boa);
    RUN_TEST(test_REQ_CAL_01_o_assistente_comeca_no_ajuste_do_zero);
    RUN_TEST(test_REQ_CAL_03_recusa_ir_ao_ganho_sem_o_zero_confirmado);
    RUN_TEST(test_REQ_CAL_03_fora_do_assistente_nenhum_passo_e_aceito);
    RUN_TEST(test_REQ_CAL_03_ganho_e_calculado_sobre_o_zero_confirmado);
    RUN_TEST(test_REQ_CAL_05_commit_devolve_ao_modo_normal);
    RUN_TEST(test_A14_campo_de_trim_tem_neutro_em_5000);
    RUN_TEST(test_A14_um_digito_do_campo_vale_um_lsb_do_dac);
    RUN_TEST(test_A14_as_telas_abrem_no_valor_corrente);
    RUN_TEST(test_REQ_CAL_06_fundo_de_escala_do_assistente_e_de_01_a_900_graus);
    RUN_TEST(test_REQ_CAL_04_abortar_nao_grava_meio_par);
    RUN_TEST(test_A14_gate_recusa_calibracao_absurda_sem_gravar);
    RUN_TEST(test_A14_commit_recusa_estouro_do_codigo_de_fundo_de_escala);
    RUN_TEST(test_REQ_CAL_04_serializa_e_restaura_o_par);
    RUN_TEST(test_REQ_CAL_04_serializacao_recusa_buffer_curto);
    RUN_TEST(test_A14_registro_reprovado_mantem_o_par_de_fabrica);
    RUN_TEST(test_REQ_CAL_04_restore_recusa_par_que_o_campo_de_trim_nao_representa);
    return UNITY_END();
}
