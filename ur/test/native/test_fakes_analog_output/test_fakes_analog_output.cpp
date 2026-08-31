// Prova de contrato do fake da porta IAnalogOutput (LSP).
//
// O fake so vale se for substituivel por Xtr300AnalogOutput sem que o dominio perceba: mesmas
// pre-condicoes, mesma semantica de erro, mesmos limites. Um fake mais permissivo que o alvo
// faz a suite inteira mentir - o dominio passa no host e a placa manda -10,00 V onde devia
// mandar -11,00 V, que e a unica diferenca entre "sensora morta" e "estrutura saturada".
//
// Estes testes prendem os pontos em que fake e adaptador poderiam divergir, e sao os mesmos
// pontos em que a revisao adversarial pegou o adaptador:
//  1. o codigo de falha 3932 ATRAVESSA o grampo e vale escrita legitima (kOk);
//  2. o grampo duro e [5243, 61342], a faixa que o dominio pode emitir, e nao os
//     6554..58982 da saturacao de +/-10,00 Vcc, que e do angulo e mora em AnalogScaler;
//  3. escrita fora da faixa devolve Err::Range E grava o valor grampeado;
//  4. antes de begin() nada foi comandado: lastCode() e o codigo de POR, nao a falha;
//  5. begin() parkeia os dois eixos no codigo de falha e NUNCA escreve 0x8000 (0,00 V).
//
// Amarracao ao dominio, que e o que torna o item 2 verificavel e nao opiniao: todo codigo que
// domain::AnalogScaler pode entregar - inclusive o espelho de fundo de escala negativo de um
// par calibrado no piso do gate de A14 - tem de passar pela porta sem Err::Range.
#include <stdint.h>
#include <unity.h>

#include "domain/analog_scaler.h"
#include "domain/angle.h"
#include "domain/ui/calibration_wizard.h"
#include "fakes/fake_analog_output.h"

using domain::AnalogScaler;
using domain::Angle;
using domain::CalibrationWizard;
using test::FakeAnalogOutput;

void setUp(void) {}
void tearDown(void) {}

// A porta proibe copia (IAnalogOutput tem copia e atribuicao apagadas), entao o helper
// inicia por referencia em vez de devolver por valor.
static void iniciar(FakeAnalogOutput& ao) {
    TEST_ASSERT_TRUE(ao.begin().ok());
}

// --- estado antes de begin() ---

static void test_antes_de_begin_nada_foi_comandado(void) {
    // O DAC8562 faz POR em zero-scale com a referencia interna desligada: a saida real esta
    // encostada no trilho negativo, ~-12,5 V. Devolver o codigo de falha aqui anunciaria um
    // -11,00 V que o CLP nunca viu.
    FakeAnalogOutput ao;
    TEST_ASSERT_FALSE(ao.ready());
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kPorCode, ao.lastCode(AnalogAxis::X));
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kPorCode, ao.lastCode(AnalogAxis::Y));
    TEST_ASSERT_NOT_EQUAL(ao.faultCode(), ao.lastCode(AnalogAxis::X));
}

static void test_antes_de_begin_toda_escrita_e_notinit(void) {
    FakeAnalogOutput ao;
    TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, 32768).err == Err::NotInit);
    TEST_ASSERT_TRUE(ao.writeBoth(32768, 32768).err == Err::NotInit);
    TEST_ASSERT_TRUE(ao.writeFaultLevel().err == Err::NotInit);
    TEST_ASSERT_TRUE(ao.setMode(AoMode::Voltage).err == Err::NotInit);
    TEST_ASSERT_EQUAL_UINT32(0u, ao.writeCount());
}

// --- energizacao: passo 5 da ordem de boot ---

static void test_begin_parkeia_os_dois_eixos_no_codigo_de_falha(void) {
    FakeAnalogOutput ao;
    iniciar(ao);
    TEST_ASSERT_TRUE(ao.ready());
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kFaultCode, ao.lastCode(AnalogAxis::X));
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kFaultCode, ao.lastCode(AnalogAxis::Y));
    TEST_ASSERT_EQUAL_UINT8(2, ao.logCount());
}

static void test_begin_nunca_passa_por_zero_volt(void) {
    // DECISIONS.md L606 e decisao 7 item (b): a saida sai do trilho negativo DIRETO para o
    // nivel de falha. 0,00 V e a leitura legitima mais provavel de estrutura nivelada e a
    // assinatura fisica da UR sem energia - nao pode aparecer nem por meio milissegundo.
    FakeAnalogOutput ao;
    iniciar(ao);
    TEST_ASSERT_FALSE(ao.everWrote(FakeAnalogOutput::kZeroCode));
    TEST_ASSERT_FALSE(ao.everWrote(FakeAnalogOutput::kPorCode));

    FakeAnalogOutput::Write w{};
    TEST_ASSERT_TRUE(ao.logAt(0, w));
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kFaultCode, w.code);
    TEST_ASSERT_TRUE(w.axis == AnalogAxis::X);
    TEST_ASSERT_TRUE(ao.logAt(1, w));
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kFaultCode, w.code);
    TEST_ASSERT_TRUE(w.axis == AnalogAxis::Y);
}

// --- o codigo de falha atravessa o grampo ---

static void test_codigo_de_falha_e_escrita_legitima_por_eixo(void) {
    // O assistente da decisao 6 precisa do marcador de -11,00 V em UM eixo so; writeFaultLevel()
    // escreve os dois e nao serve. O caminho e write(eixo, 3932), e ele nao pode ser grampeado
    // nem reprovado.
    FakeAnalogOutput ao;
    iniciar(ao);
    ao.clearLog();
    TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, FakeAnalogOutput::kFaultCode).ok());
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kFaultCode, ao.lastCode(AnalogAxis::X));

    TEST_ASSERT_TRUE(ao.write(AnalogAxis::Y, 32768).ok());
    TEST_ASSERT_EQUAL_UINT16(32768, ao.lastCode(AnalogAxis::Y));
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kFaultCode, ao.lastCode(AnalogAxis::X));
}

static void test_codigo_de_falha_atravessa_tambem_no_writeBoth(void) {
    FakeAnalogOutput ao;
    iniciar(ao);
    TEST_ASSERT_TRUE(ao.writeBoth(FakeAnalogOutput::kFaultCode, 58982).ok());
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kFaultCode, ao.lastCode(AnalogAxis::X));
    TEST_ASSERT_EQUAL_UINT16(58982, ao.lastCode(AnalogAxis::Y));
}

static void test_o_codigo_de_falha_fica_fora_da_faixa_util(void) {
    FakeAnalogOutput ao;
    TEST_ASSERT_TRUE(ao.faultCode() < ao.codeMin());
    TEST_ASSERT_TRUE(ao.codeMin() - ao.faultCode() >= 1311);
    TEST_ASSERT_TRUE(ao.codeMin() < ao.midScaleCode());
    TEST_ASSERT_TRUE(ao.midScaleCode() < ao.codeMax());
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, ao.fullScaleCode());
}

// --- grampo duro e Err::Range ---

static void test_fora_de_faixa_grampeia_e_grava_o_valor_grampeado(void) {
    // Em equipamento de seguranca a saida nunca fica no valor errado por causa de um erro de
    // faixa a montante: o Status reprova E o codigo grampeado vai para o CI.
    FakeAnalogOutput ao;
    iniciar(ao);

    TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, 100).err == Err::Range);
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kCodeMin, ao.lastCode(AnalogAxis::X));

    TEST_ASSERT_TRUE(ao.write(AnalogAxis::Y, 65535).err == Err::Range);
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kCodeMax, ao.lastCode(AnalogAxis::Y));

    TEST_ASSERT_TRUE(ao.writeBoth(0, 65535).err == Err::Range);
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kCodeMin, ao.lastCode(AnalogAxis::X));
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kCodeMax, ao.lastCode(AnalogAxis::Y));
}

static void test_as_bordas_da_faixa_util_nao_sao_erro(void) {
    FakeAnalogOutput ao;
    iniciar(ao);
    TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, FakeAnalogOutput::kCodeMin).ok());
    TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, FakeAnalogOutput::kCodeMax).ok());
    TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, FakeAnalogOutput::kCodeMin - 1).err == Err::Range);
    TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, FakeAnalogOutput::kCodeMax + 1).err == Err::Range);
}

static void test_eixo_inexistente_e_param_e_nao_escreve(void) {
    FakeAnalogOutput ao;
    iniciar(ao);
    const uint32_t antes = ao.writeCount();
    TEST_ASSERT_TRUE(ao.write(static_cast<AnalogAxis>(7), 32768).err == Err::Param);
    TEST_ASSERT_EQUAL_UINT32(antes, ao.writeCount());
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kPorCode, ao.lastCode(static_cast<AnalogAxis>(7)));
    TEST_ASSERT_EQUAL_UINT8(kAnalogAxisCount, ao.axisCount());
}

// --- os limites sao os do dominio, nao os do manual 5.7 L185 ---

static void test_os_limites_da_porta_sao_os_do_dominio(void) {
    // O grampo de +/-10,00 Vcc (6554..58982) e do ANGULO, dentro de AnalogScaler::codeFor().
    // O da porta e o grampo eletrico, e tem de ser exatamente a faixa que o dominio emite.
    FakeAnalogOutput ao;
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(AnalogScaler::kCodeMin), ao.codeMin());
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(AnalogScaler::kCodeMax), ao.codeMax());
    TEST_ASSERT_EQUAL_UINT16(AnalogScaler::kFaultCode, ao.faultCode());
    TEST_ASSERT_EQUAL_UINT16(AnalogScaler::kZeroCode, ao.midScaleCode());
    TEST_ASSERT_EQUAL_UINT16(CalibrationWizard::kCodeClampMax, ao.codeMax());
    TEST_ASSERT_EQUAL_UINT16(CalibrationWizard::kFaultCode, ao.faultCode());
    TEST_ASSERT_TRUE(ao.codeMax() > 58982);
}

static void test_tudo_que_o_dominio_emite_passa_sem_range(void) {
    // Par no PISO do gate de A14: mirrorCode() = 5243, o menor codigo que uma placa calibrada
    // pode apresentar. Com um grampo em 6554 a saturacao negativa seria truncada e devolveria
    // Err::Range a cada 50 ms em regime.
    AnalogScaler s;
    TEST_ASSERT_TRUE(AnalogScaler::make(26457, 47671, 450, s));
    TEST_ASSERT_EQUAL_UINT16(5243, s.mirrorCode());

    FakeAnalogOutput ao;
    iniciar(ao);
    for (int16_t deci = -900; deci <= 900; ++deci) {
        const uint16_t code = s.codeFor(Angle::fromDeciDegrees(deci));
        TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, code).ok());
        TEST_ASSERT_EQUAL_UINT16(code, ao.lastCode(AnalogAxis::X));
    }
    // Angulo invalido devolve o codigo de falha pelo caminho ordinario de escrita.
    const uint16_t falha = s.codeFor(Angle::invalid());
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kFaultCode, falha);
    TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, falha).ok());
}

static void test_teto_do_gate_de_calibracao_passa_sem_range(void) {
    // Trim POSITIVO de ganho: a medicao 14 aceita ate +4000 LSB. Um teto em 58982 reprovaria
    // placa boa no comissionamento e congelaria o voltimetro com os digitos ainda contando.
    AnalogScaler s;
    TEST_ASSERT_TRUE(AnalogScaler::make(35128, 61342, 450, s));
    FakeAnalogOutput ao;
    iniciar(ao);
    TEST_ASSERT_TRUE(ao.write(AnalogAxis::Y, s.fullScaleCode()).ok());
    TEST_ASSERT_EQUAL_UINT16(61342, ao.lastCode(AnalogAxis::Y));
}

// --- nivel de falha, modo e ausencia de leitura de volta ---

static void test_writeFaultLevel_leva_os_dois_eixos(void) {
    FakeAnalogOutput ao;
    iniciar(ao);
    TEST_ASSERT_TRUE(ao.writeBoth(32768, 58982).ok());
    TEST_ASSERT_TRUE(ao.writeFaultLevel().ok());
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kFaultCode, ao.lastCode(AnalogAxis::X));
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kFaultCode, ao.lastCode(AnalogAxis::Y));
}

static void test_modo_corrente_e_recusado_e_a_selecao_e_declarada(void) {
    // modeSelectable() e declaracao de FIACAO (jumpers em "uC", como o passo 5 pressupoe); a
    // recusa do modo corrente e a guarda de A2 / decisao 9 item 15, coisa diferente.
    FakeAnalogOutput ao;
    iniciar(ao);
    TEST_ASSERT_TRUE(ao.modeSelectable());
    TEST_ASSERT_TRUE(ao.setMode(AoMode::Current).err == Err::Unsupported);
    TEST_ASSERT_TRUE(ao.mode() == AoMode::Voltage);
    TEST_ASSERT_TRUE(ao.setMode(AoMode::Voltage).ok());
    TEST_ASSERT_TRUE(ao.mode() == AoMode::Voltage);
}

static void test_nao_ha_leitura_de_volta(void) {
    // O DAC8562 nao tem readback e nao existe ADC de conferencia: lastCode() e cache de
    // ESCRITA. Nenhuma decisao de rele pode depender desta porta, e o fake nao pode sugerir
    // o contrario.
    FakeAnalogOutput ao;
    iniciar(ao);
    TEST_ASSERT_FALSE(ao.readbackAvailable());
}

static void test_falha_de_barramento_nao_grava_e_propaga(void) {
    FakeAnalogOutput ao;
    iniciar(ao);
    TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, 32768).ok());
    ao.failNext(Err::Io);
    TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, 58982).err == Err::Io);
    TEST_ASSERT_EQUAL_UINT16(32768, ao.lastCode(AnalogAxis::X));
    TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, 58982).ok());
}

static void test_begin_que_falha_nao_declara_pronto(void) {
    // begin() nao pode devolver kOk sem ter conseguido: se o barramento reprova, ready() fica
    // falso e toda escrita seguinte e NotInit.
    FakeAnalogOutput ao;
    ao.failNext(Err::Io);
    TEST_ASSERT_TRUE(ao.begin().err == Err::Io);
    TEST_ASSERT_FALSE(ao.ready());
    TEST_ASSERT_TRUE(ao.write(AnalogAxis::X, 32768).err == Err::NotInit);
    TEST_ASSERT_EQUAL_UINT16(FakeAnalogOutput::kPorCode, ao.lastCode(AnalogAxis::X));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_antes_de_begin_nada_foi_comandado);
    RUN_TEST(test_antes_de_begin_toda_escrita_e_notinit);
    RUN_TEST(test_begin_parkeia_os_dois_eixos_no_codigo_de_falha);
    RUN_TEST(test_begin_nunca_passa_por_zero_volt);
    RUN_TEST(test_codigo_de_falha_e_escrita_legitima_por_eixo);
    RUN_TEST(test_codigo_de_falha_atravessa_tambem_no_writeBoth);
    RUN_TEST(test_o_codigo_de_falha_fica_fora_da_faixa_util);
    RUN_TEST(test_fora_de_faixa_grampeia_e_grava_o_valor_grampeado);
    RUN_TEST(test_as_bordas_da_faixa_util_nao_sao_erro);
    RUN_TEST(test_eixo_inexistente_e_param_e_nao_escreve);
    RUN_TEST(test_os_limites_da_porta_sao_os_do_dominio);
    RUN_TEST(test_tudo_que_o_dominio_emite_passa_sem_range);
    RUN_TEST(test_teto_do_gate_de_calibracao_passa_sem_range);
    RUN_TEST(test_writeFaultLevel_leva_os_dois_eixos);
    RUN_TEST(test_modo_corrente_e_recusado_e_a_selecao_e_declarada);
    RUN_TEST(test_nao_ha_leitura_de_volta);
    RUN_TEST(test_falha_de_barramento_nao_grava_e_propaga);
    RUN_TEST(test_begin_que_falha_nao_declara_pronto);
    return UNITY_END();
}
