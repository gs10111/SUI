// Angulo de inclinacao de um eixo, em decimos de grau inteiros.
//
// Manual SUI-DI141388XY secoes 2.1 (L21) e 5.5 (L130 a L133): faixa +/-90,0 graus, resolucao
// 0,1 grau, indicacao sempre em graus no formato +XXX,X com uma casa decimal fixa.
//
// Por que int16 e nao float: REQ-MEA-05 limita o erro de arredondamento a 0,05 grau em toda a
// cadeia, e o ponto de atuacao dos reles e ajustado no decimo de grau (L133). Em decimos
// inteiros o erro de representacao e zero por construcao, e nao existe caminho em que somar e
// subtrair o mesmo Preset devolva um valor diferente do de partida. Ponto flutuante so pode
// aparecer na borda de apresentacao, nunca aqui.
//
// O estado invalido nao e um valor sentinela disfarcado de angulo: enquanto o enlace com a
// sensora nao entrega quadro valido nao existe leitura, e nenhuma operacao pode inventar uma.
// Por isso invalido e absorvente - deslocar ou inverter um angulo invalido continua invalido.
#pragma once

#include <stdint.h>

namespace domain {

class Angle {
public:
    static constexpr int16_t kMinDeciDeg = -900;
    static constexpr int16_t kMaxDeciDeg = 900;

    // "+045,0" mais o terminador.
    static constexpr uint8_t kTextLen = 6;
    static constexpr uint8_t kTextCap = kTextLen + 1;

    constexpr Angle() : deci_(0), valid_(false) {}

    // Recusa fora de faixa em vez de saturar em silencio: um valor fora de faixa vindo de um
    // parametro gravado ou de um campo editado e defeito, nao leitura extrema.
    static constexpr Angle fromDeciDegrees(int16_t deci) {
        return (deci < kMinDeciDeg || deci > kMaxDeciDeg) ? Angle() : Angle(deci);
    }

    // Porta de entrada do valor cru do sensor, que pode chegar fora de faixa. Aceita 32 bits
    // para que o chamador nao precise truncar antes e provocar wrap.
    static constexpr Angle clamped(int32_t deci) {
        return Angle(static_cast<int16_t>(deci < kMinDeciDeg   ? kMinDeciDeg
                                          : deci > kMaxDeciDeg ? kMaxDeciDeg
                                                               : deci));
    }

    static constexpr Angle invalid() { return Angle(); }

    constexpr bool valid() const { return valid_; }
    constexpr int16_t deciDegrees() const { return deci_; }
    constexpr int16_t absDeciDegrees() const { return deci_ < 0 ? static_cast<int16_t>(-deci_) : deci_; }

    // Inversao de sinal do Sentido do Sensor (secao 5.8). Exata nos extremos porque a faixa e
    // simetrica: -(-900) cabe em int16 sem saturar.
    constexpr Angle negated() const {
        return valid_ ? Angle(static_cast<int16_t>(-deci_)) : Angle();
    }

    // Offset do Preset. A soma e feita em 32 bits e so depois saturada, conforme a formula
    // unica aprovada em A9: leitura = clamp(dir * bruto + offset, -900, +900).
    constexpr Angle offsetBy(int16_t offsetDeci) const {
        return valid_ ? clamped(static_cast<int32_t>(deci_) + offsetDeci) : Angle();
    }

    constexpr bool operator==(const Angle& other) const {
        return valid_ == other.valid_ && (!valid_ || deci_ == other.deci_);
    }
    constexpr bool operator!=(const Angle& other) const { return !(*this == other); }

    // Escreve "+XXX,X" ou "-XXX,X" com largura constante, ou "---,-" quando nao ha leitura.
    // Largura constante importa: sem ela o display danca ao cruzar 9,9 para 10,0 e ao cruzar
    // o zero. Devolve false sem tocar no buffer se ele nao couber.
    bool format(char* out, uint8_t cap) const {
        if (out == nullptr || cap < kTextCap) {
            return false;
        }
        if (!valid_) {
            out[0] = '-';
            out[1] = '-';
            out[2] = '-';
            out[3] = ',';
            out[4] = '-';
            out[5] = '\0';
            return true;
        }
        const int16_t magnitude = absDeciDegrees();
        const int16_t inteiro = static_cast<int16_t>(magnitude / 10);
        const int16_t decimo = static_cast<int16_t>(magnitude % 10);
        out[0] = (deci_ < 0) ? '-' : '+';
        out[1] = static_cast<char>('0' + (inteiro / 100));
        out[2] = static_cast<char>('0' + ((inteiro / 10) % 10));
        out[3] = static_cast<char>('0' + (inteiro % 10));
        out[4] = ',';
        out[5] = static_cast<char>('0' + decimo);
        out[6] = '\0';
        return true;
    }

private:
    explicit constexpr Angle(int16_t deci) : deci_(deci), valid_(true) {}

    int16_t deci_;
    bool valid_;
};

}  // namespace domain


