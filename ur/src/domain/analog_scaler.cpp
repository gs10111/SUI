// Implementacao da conversao angulo -> codigo de DAC. Contrato, numeros e justificativas em
// domain/analog_scaler.h (manual 5.7 L164/L165/L184/L185, decisoes A2 e A14).
//
// Orcamento de arredondamento (REQ-MEA-05): a divisao arredonda para o inteiro mais proximo
// com desempate para longe do zero, entao o residuo em codigo nao passa de meio codigo e o
// erro equivalente em angulo vale meio codigo * fundo de escala / vao, no maximo
// 0,5*900/20971 = 0,022 decimo de grau, ou 0,0022 grau - vinte vezes abaixo dos 0,05 grau do
// requisito. O arredondamento simetrico tambem e o que faz codeFor(-a) ser o espelho exato de
// codeFor(+a) em torno do codigo de zero, como L165 exige.
//
// Maior produto possivel: vao 28049 * 900 decimos = 25,2e6, cabe folgado em int32.
#include "domain/analog_scaler.h"

namespace domain {

bool AnalogScaler::make(uint16_t zeroCode, uint16_t fullScaleCode, int16_t fullScaleAngleDeci,
                        AnalogScaler& out) {
    if (fullScaleAngleDeci < kFullScaleMinDeci || fullScaleAngleDeci > kFullScaleMaxDeci) {
        return false;
    }
    const int32_t zero = static_cast<int32_t>(zeroCode);
    const int32_t span = static_cast<int32_t>(fullScaleCode) - zero;
    if (span < kSpanMin) {
        return false;
    }
    if ((zero - span) < kCodeMin || (zero + span) > kCodeMax) {
        return false;
    }
    out = AnalogScaler(zeroCode, fullScaleCode, fullScaleAngleDeci);
    return true;
}

uint16_t AnalogScaler::mirrorCode() const {
    return static_cast<uint16_t>(2 * static_cast<int32_t>(zero_) - static_cast<int32_t>(full_));
}

uint16_t AnalogScaler::codeFor(const Angle& angle) const {
    if (!angle.valid()) {
        return kFaultCode;
    }
    const int32_t limit = static_cast<int32_t>(fullScaleDeci_);
    int32_t deci = static_cast<int32_t>(angle.deciDegrees());
    if (deci > limit) {
        deci = limit;
    } else if (deci < -limit) {
        deci = -limit;
    }
    const int32_t zero = static_cast<int32_t>(zero_);
    const int32_t span = static_cast<int32_t>(full_) - zero;
    const int32_t product = span * deci;
    const int32_t half = limit / 2;
    const int32_t offset =
        (product >= 0) ? ((product + half) / limit) : -((-product + half) / limit);
    return static_cast<uint16_t>(zero + offset);
}

}  // namespace domain
