// Os dois pontos da Auto Calibracao de um eixo (zero e ganho), a maquina do assistente de
// 5.7 e a serializacao do par para a memoria nao volatil.
//
// Manual SUI-DI141388XY 5.7: L166 (calibracao por eixo, com fundo de escala proprio), L170 a
// L174 (Etapa 1, ajuste do zero, campo de 4 digitos e confirmacao por hold), L176 a L182
// (Etapa 2, angulo de fundo de escala e ajuste do ganho), L183 (gravacao na EEPROM), L184
// (retorno automatico ao Modo Normal) e L187 ("o ajuste do zero deve ser sempre realizado
// antes do ajuste do ganho, pois a referencia de 0,00 Vcc e utilizada no calculo da
// proporcao"). REQ-CAL-01 a REQ-CAL-05, e REQ-CAL-08.
//
// REQ-CAL-03: a ordem zero -> angulo de fundo de escala -> ganho e estado, nao convencao de
// chamada. Todo passo fora de ordem devolve Err::NotCalibrated sem tocar em nada, e por isso
// nao existe caminho em que o ganho seja calculado sobre um zero que o tecnico nao confirmou.
//
// REQ-CAL-05: commit() bem sucedido devolve o assistente ao passo Idle, que e como o dominio
// expressa o retorno automatico ao Modo Normal de L184. A tela e a temporizacao da mensagem
// "Alteracao bem sucedida!" sao da IHM; o que pertence aqui e o fato de o assistente deixar
// de existir no mesmo instante em que o par e gravado.
//
// Decisao A14, adotada aqui:
//  - campo de trim de 4 digitos com NEUTRO EM 5000, faixa 0000..9999, um digito = 1 codigo do
//    DAC = 381,47 uV. Zero da placa = 32768 + (campo - 5000); codigo de fundo de escala =
//    zero confirmado + 26214 + (campo - 5000). Desvio do manual: as figuras de L172 e L180
//    passam a ler "Ajuste 0Vcc:5000" e "Ajuste 10Vcc:5000";
//  - as duas telas abrem no VALOR CORRENTE, nunca em 0000: abrir em 0000 saltaria a saida em
//    -1,907 V no instante em que a tela aparece, na frente do voltimetro. begin() e o UNICO
//    ponto que carrega o buffer do assistente a partir do par corrente - nem restore() nem
//    commit() o fazem, porque entre o boot e a abertura do assistente o buffer nao tem
//    significado e um segundo ponto de carga so criaria um estado que nenhum teste distingue;
//  - o buffer do assistente NAO e gravado por timeout nem por qualquer saida que nao seja o
//    passo final: abort() devolve o par anterior integro. Meio par gravado (zero novo com
//    ganho velho) e pior que nenhuma calibracao, porque produz saida plausivel e errada, sem
//    assinatura observavel;
//  - gate de plausibilidade UNICO no commit, que e o AnalogScaler::make(). Recusa devolve
//    Err::Range, NAO grava e MANTEM o assistente na tela de ajuste do ganho com os digitos
//    preservados, para que o tecnico corrija em vez de recomecar.
//
// JANELA DO TRIM, e por que ela e criterio de restore() e nao grampo silencioso. O gate de
// regime de AnalogScaler::make() e mais largo que o que os dois campos de 4 digitos
// conseguem representar: make() aceita zero de 26214 a 40371 e vao de 20971 a 28049,
// enquanto o campo so representa zero em [27768, 37767] e vao em [21214, 31213]. Um registro
// vindo da NVS na faixa intermediaria era aceito e depois GRAMPEADO na leitura dos campos, e
// o grampo dessincronizava a tela do par gravado: com zero = 27000 a IHM abria
// "Ajuste 0Vcc:0000" e, se o tecnico apenas confirmasse as telas sem tocar em digito nenhum,
// o commit gravava 27768 - 768 codigos, 292 mV de deslocamento silencioso, exatamente o modo
// de falha que A14 existe para impedir, so que pela porta da NVS. Por isso restore() aplica
// DOIS criterios, o gate de regime e a janela do trim, e recusa antes de tocar em scaler_.
// Registro recusado cai na regra ja aprovada da decisao 6 item 17: par de fabrica, eixo
// opera. O gate de make() nao foi apertado para isso porque sao criterios diferentes - um
// diz o que a cadeia analogica pode emitir, o outro diz o que o assistente pode editar.
//
// Registro ausente ou reprovado (decisao 6 item 17): restore() devolve false e o eixo
// permanece com o par de fabrica da Tabela 2 (32768, 58982, 45,0 graus), operando
// normalmente. Nao vai ao nivel de falha: os reles, que sao o canal primario de seguranca,
// nao dependem da calibracao analogica, e parquear o eixo em -11,00 V trocaria uma
// degradacao de exatidao pela perda total do canal analogico.
#pragma once

#include <stdint.h>

#include "domain/analog_scaler.h"
#include "status.h"

namespace domain {

class AnalogCalibration {
public:
    enum class Step : uint8_t {
        Idle = 0,
        Zero,
        FullScaleAngle,
        Gain,
    };

    static constexpr uint16_t kTrimNeutral = 5000;
    static constexpr uint16_t kTrimFieldMax = 9999;
    static constexpr uint8_t kRecordBytes = 6;

    AnalogCalibration();

    const AnalogScaler& scaler() const { return scaler_; }
    Step step() const { return step_; }

    uint16_t zeroField() const { return zeroField_; }
    uint16_t gainField() const { return gainField_; }
    int16_t fullScaleAngle() const { return fullScaleDeci_; }

    Status begin();
    Status setZeroField(uint16_t field);
    Status confirmZero();
    Status setFullScaleAngle(int16_t deci);
    Status confirmFullScaleAngle();
    Status setGainField(uint16_t field);
    Status commit();
    void abort();

    bool serialize(uint8_t* out, uint8_t cap) const;
    bool restore(const uint8_t* in, uint8_t len);

private:
    static bool trimFieldsOf(const AnalogScaler& scaler, uint16_t& zeroField, uint16_t& gainField);
    void loadFields();
    int32_t bufferZeroCode() const;
    int32_t bufferFullScaleCode() const;

    AnalogScaler scaler_;
    Step step_;
    uint16_t zeroField_;
    uint16_t gainField_;
    int16_t fullScaleDeci_;
};

}  // namespace domain
