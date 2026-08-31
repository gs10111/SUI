// Conversao do angulo exibido em codigo de DAC da saida analogica bipolar, em aritmetica
// inteira, e gate unico de plausibilidade de um par de calibracao.
//
// Manual SUI-DI141388XY 5.7: L164 (dois pontos, 0,0 grau = 0,00 Vcc e o angulo de fundo de
// escala = +10,00 Vcc), L165 (saida bipolar e simetrica em relacao ao zero), L184 (a
// proporcao resultante) e L185 (inclinacao acima do fundo de escala mantem a saida saturada
// em +/-10,00 Vcc). Faixa do fundo de escala 0,1 a 90,0 graus: Tabela 1, L119 e L120. Par de
// fabrica 0,0 grau = 0,00 Vcc e 45,0 graus = +10,00 Vcc: Tabela 2, L254 e L255.
// REQ-CAL-06, REQ-CAL-07, REQ-MEA-05.
//
// Malha fechada, fato do projeto: V_OUT = 5*(V_DAC - 2,50 V) = 25*D/65536 - 12,5 V, logo
// D = 32768 + 2621,44*V_OUT. Zero em 32768, -10,00 V em 6554, +10,00 V em 58982, vao nominal
// de 26214 codigos, 1 codigo = 381,47 uV. Decisao A2: o nivel de falha e o codigo cru 3932
// (-11,00 V), escrito FORA da cadeia de calibracao, e o modo corrente esta proibido, de modo
// que existe um unico numero de estado seguro na saida.
//
// Por que a saturacao e aplicada ao ANGULO e nao ao codigo: depois da Auto Calibracao o
// codigo que vale +10,00 V naquela placa e calFsCode, nao 58982. Grampear o codigo nos
// valores nominais truncaria a calibracao de toda placa que precise de trim positivo de
// ganho; grampear o angulo em +/-fundo de escala satura exatamente nos +/-10,00 V reais, que
// e o que L185 pede, e ainda elimina por construcao o estouro de inteiro do fundo de escala
// de 0,1 grau, onde o divisor 1 multiplicaria o vao por 900.
//
// Gate de plausibilidade UNICO (decisao A14, que mandou unificar os tres criterios que as
// decisoes 6, 9 e 10 traziam). Um par so existe se make() o aceitar, e make() aceita se e
// somente se:
//   (a) fundo de escala em 1..900 decimos de grau (Tabela 1) - tambem impede divisao por zero;
//   (b) vao = calFsCode - calZeroCode maior ou igual a 20971 codigos, isto e, no maximo -20 %
//       de erro de escala sobre os 26214 nominais (decisao 6 item 12);
//   (c) o espelho de -10,00 V, 2*calZeroCode - calFsCode, nao pode cair abaixo de 5243, e o
//       codigo de fundo de escala nao pode passar de 61342.
// O teto de +20 % do criterio (b) nao aparece no codigo porque (c) o subsome: de
// z - s >= 5243 e z + s <= 61342 sai s <= 28049, mais apertado que os 31457 de +20 %.
//
// Os dois numeros de (c) tem origens diferentes e por isso nao sao simetricos em torno de
// 32768. O piso 5243 e o codigo de falha 3932 mais 1311 codigos (0,500 V), da decisao 9 item
// 11: abaixo dele o marcador de -11,00 V, que e escrito cru, cairia DENTRO da faixa util
// daquela placa e a distincao de A2 (sensora morta x estrutura saturada) morreria. O teto
// 61342 e o grampo aprovado na decisao 6 item 7 (DECISIONS.md L1604), "o codigo entregue ao
// DAC e grampeado na faixa 6554..61342", que vale para as duas telas de medicao do
// assistente. Um par com fundo de escala acima de 61342 NUNCA foi medido pelo tecnico: na
// tela "Ajuste 10Vcc" a saida real teria ficado presa no grampo enquanto o campo continuava
// contando, e o ponto gravado ficaria ate 261 codigos (~100 mV) acima da tensao que o
// voltimetro mostrou. O limite de excursao de +12 V (codigo 64487) continua respeitado, com
// folga maior, mas nao e ele que fixa o numero.
#pragma once

#include <stdint.h>

#include "domain/angle.h"

namespace domain {

class AnalogScaler {
public:
    static constexpr uint16_t kZeroCode = 32768;
    static constexpr uint16_t kMinus10VCode = 6554;
    static constexpr uint16_t kPlus10VCode = 58982;
    static constexpr uint16_t kFaultCode = 3932;
    static constexpr int16_t kFactoryFullScaleDeci = 450;

    static constexpr int32_t kNominalSpan = 26214;
    static constexpr int32_t kSpanMin = 20971;
    static constexpr int32_t kFaultMarginCodes = 1311;
    static constexpr int32_t kCodeMin = kFaultCode + kFaultMarginCodes;
    static constexpr int32_t kCodeMax = 61342;

    static constexpr int16_t kFullScaleMinDeci = 1;
    static constexpr int16_t kFullScaleMaxDeci = 900;

    constexpr AnalogScaler()
        : zero_(kZeroCode), full_(kPlus10VCode), fullScaleDeci_(kFactoryFullScaleDeci) {}

    static bool make(uint16_t zeroCode, uint16_t fullScaleCode, int16_t fullScaleAngleDeci,
                     AnalogScaler& out);

    uint16_t codeFor(const Angle& angle) const;

    uint16_t zeroCode() const { return zero_; }
    uint16_t fullScaleCode() const { return full_; }
    uint16_t mirrorCode() const;
    int16_t fullScaleAngleDeci() const { return fullScaleDeci_; }

private:
    constexpr AnalogScaler(uint16_t zeroCode, uint16_t fullScaleCode, int16_t fullScaleAngleDeci)
        : zero_(zeroCode), full_(fullScaleCode), fullScaleDeci_(fullScaleAngleDeci) {}

    uint16_t zero_;
    uint16_t full_;
    int16_t fullScaleDeci_;
};

}  // namespace domain
