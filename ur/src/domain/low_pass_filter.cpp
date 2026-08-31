// Implementacao do EMA Q8 com recarga por salto. Contrato, numeros, aritmetica e as razoes de
// seguranca (zona morta, recarga, retencao de ordem zero) estao em domain/low_pass_filter.h.
//
// Notas de aritmetica que nao cabem no contrato:
//  - realizedTimeConstantMs usa (2 << shift) - 1, que vale 2^(shift+1) - 1, e arredonda a
//    divisao por 2 para cima. Com shift no teto de 12 e periodo no teto de uint16 o produto
//    chega a 65535 * 8191 = 536,8e6, oito vezes abaixo do teto de uint32;
//  - shiftFor varre os coeficientes possiveis e fica com o de menor erro absoluto contra a
//    constante pedida. O empate fica com o MENOR k, que e o filtro mais rapido: em equipamento
//    de seguranca o desempate se resolve a favor de menos atraso de atuacao;
//  - periodo de amostragem zero e defeito do chamador, nao deste modulo. Ele nao trava nem
//    divide por zero: todas as constantes realizaveis viram 0, o menor erro cai em k = 0 e o
//    filtro fica em passagem direta, que e o comportamento mais seguro dos disponiveis;
//  - o arredondamento de Q8 para decimo de grau e meio-para-longe-do-zero, o mesmo criterio ja
//    usado em analog_scaler.cpp. Sem isso o valor filtrado de -x deixaria de ser o espelho do
//    de +x, e a operacao "+" da secao 5.9 (modulo) atuaria em angulos diferentes conforme o
//    lado da inclinacao;
//  - o passo e calculado sobre o MODULO da diferenca e so depois recebe o sinal. Um
//    deslocamento aritmetico direto sobre a diferenca com sinal arredonda para -infinito, o que
//    torna a descida mais rapida que a subida: com o mesmo degrau de 1,7 grau o filtro exibe
//    +0,7 quando o de sinal trocado ja exibe -0,8. As duas versoes convergem exatamente e
//    nenhuma tem zona morta, mas so esta e ESPELHADA, e o espelhamento e requisito da operacao
//    "+" de 5.9 (L207 e L208), que compara o MODULO do angulo: sem ele o mesmo rele atuaria em
//    instantes diferentes conforme o lado para o qual a estrutura inclina.
#include "domain/low_pass_filter.h"

namespace domain {

uint32_t LowPassFilter::realizedTimeConstantMs(uint8_t shift, uint16_t samplePeriodMs) {
    const uint8_t k = (shift > kMaxShift) ? kMaxShift : shift;
    const uint32_t weight = (2u << k) - 1u;
    return (static_cast<uint32_t>(samplePeriodMs) * weight + 1u) / 2u;
}

uint8_t LowPassFilter::shiftFor(uint16_t timeConstantMs, uint16_t samplePeriodMs) {
    const uint32_t wanted = timeConstantMs;
    uint8_t best = 0;
    uint32_t bestError = 0xFFFFFFFFu;
    for (uint8_t k = 0; k <= kMaxShift; ++k) {
        const uint32_t realized = realizedTimeConstantMs(k, samplePeriodMs);
        const uint32_t error = (realized > wanted) ? (realized - wanted) : (wanted - realized);
        if (error < bestError) {
            bestError = error;
            best = k;
        }
    }
    return best;
}

LowPassFilter::LowPassFilter(uint16_t timeConstantMs, uint16_t samplePeriodMs)
    : state_(0),
      holdDeci_(0),
      periodMs_(samplePeriodMs),
      shift_(shiftFor(timeConstantMs, samplePeriodMs)),
      primed_(false),
      reloaded_(false),
      held_(false) {}

void LowPassFilter::setTimeConstant(uint16_t timeConstantMs) {
    shift_ = shiftFor(timeConstantMs, periodMs_);
}

void LowPassFilter::reset() {
    state_ = 0;
    holdDeci_ = 0;
    primed_ = false;
    reloaded_ = false;
    held_ = false;
}

void LowPassFilter::reload(const Angle& sample) {
    if (!sample.valid()) {
        return;
    }
    holdDeci_ = sample.deciDegrees();
    state_ = static_cast<int32_t>(holdDeci_) * kScale;
    primed_ = true;
    reloaded_ = true;
    held_ = false;
}

Angle LowPassFilter::value() const {
    if (!primed_) {
        return Angle::invalid();
    }
    const int32_t deci = (state_ >= 0) ? ((state_ + kScale / 2) / kScale)
                                       : -((-state_ + kScale / 2) / kScale);
    return Angle::clamped(deci);
}

Angle LowPassFilter::update(const Angle& sample) {
    reloaded_ = false;
    held_ = !sample.valid();
    if (sample.valid()) {
        holdDeci_ = sample.deciDegrees();
        if (!primed_) {
            reload(sample);
            return value();
        }
    } else if (!primed_) {
        return Angle::invalid();
    }
    const int32_t target = static_cast<int32_t>(holdDeci_) * kScale;
    const int32_t diff = target - state_;
    const int32_t jump = static_cast<int32_t>(kJumpReloadDeci) * kScale;
    if (diff >= jump || diff <= -jump) {
        state_ = target;
        reloaded_ = true;
        return value();
    }
    if (diff != 0) {
        const int32_t magnitude = (diff > 0) ? diff : -diff;
        int32_t step = magnitude >> shift_;
        if (step == 0) {
            step = 1;
        }
        state_ += (diff > 0) ? step : -step;
    }
    return value();
}

}  // namespace domain
