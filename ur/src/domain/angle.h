// Angulo de inclinacao de um eixo, em decimos de grau inteiros.
//
// Manual SUI-DI141388XY secoes 2.1 (L21) e 5.5 (L130 a L133): faixa +/-90,0 graus, resolucao
// 0,1 grau. A INDICACAO sai com uma casa (+XXX,X, o formato do manual) ou sem casa (+XXX),
// escolhido no menu - Decisao 18, desvio declarado do manual. O valor guardado e sempre decimo.
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

// Quantas casas a INDICACAO mostra (Decisao 18). So apresentacao: reles, limites, Preset e saida
// analogica continuam em decimo inteiro.
//
// DUAS CASAS (Emenda 1 da Decisao 18): a sensora entrega decimo, entao o centesimo da leitura NAO
// vem do fio - vem do estado interno do filtro da UR (LowPassFilter, ponto fixo Q8), a media das
// ultimas amostras. Ele so tem conteudo abaixo do decimo quando o sinal varia entre decimos; com
// o sinal parado o 0 final diz apenas que a sensora nao mudou de decimo, e num degrau ele mostra a
// resposta do filtro. E resolucao de indicacao, nao exatidao (+/-0,09 grau continua valendo).
// Valor que so existe em decimo exato (limite, offset) sai com o 0 final, que e verdade.
enum class AngleDecimals : uint8_t {
    Zero = 0,  // "+045"
    One = 1,   // "+045,0" - o formato do manual, padrao de fabrica
    Two = 2,   // "+045,37"
};

// "+045,37" mais o terminador; os textos com menos casas sao mais curtos e cabem no mesmo buffer.
constexpr uint8_t kDeciTextCap = 8;

// DONO UNICO do texto de um valor em decimos de grau: a leitura (Angle::format) e o offset de
// Preset (PresetWizard::formatDeci, faixa +/-1800) passam por aqui, para que os dois nunca
// arredondem diferente. Largura constante - sinal sempre presente e tres digitos inteiros - para
// o numero nao dancar ao cruzar o zero e o 10.
//
// Sem casa: inteiro mais proximo com o meio indo para longe do zero, a mesma convencao da
// conversao da sensora. O sinal acompanha o NUMERO MOSTRADO: -0,4 vira "+000", porque "-000"
// diria que ha leitura negativa onde a tela mostra zero.
//
// int16 de proposito: e o tipo de todo decimo do produto (leitura, limite, offset), e o modulo
// calculado em 32 bits nao tem caso de estouro - nem em -32768.
inline bool formatDeciText(int16_t deci, AngleDecimals decimals, char* out, uint8_t cap) {
    if (out == nullptr || cap < kDeciTextCap) {
        return false;
    }
    const int32_t magnitude = (deci < 0) ? -static_cast<int32_t>(deci) : deci;
    const bool semCasa = (decimals == AngleDecimals::Zero);
    const int32_t inteiro = semCasa ? (magnitude + 5) / 10 : magnitude / 10;
    if (inteiro > 999) {
        return false;
    }
    const bool negativo = semCasa ? (deci < 0 && inteiro > 0) : (deci < 0);
    out[0] = negativo ? '-' : '+';
    out[1] = static_cast<char>('0' + (inteiro / 100));
    out[2] = static_cast<char>('0' + ((inteiro / 10) % 10));
    out[3] = static_cast<char>('0' + (inteiro % 10));
    if (semCasa) {
        out[4] = '\0';
        return true;
    }
    out[4] = ',';
    out[5] = static_cast<char>('0' + (magnitude % 10));
    if (decimals == AngleDecimals::Two) {
        out[6] = '0';   // decimo exato: o centesimo e zero de verdade
        out[7] = '\0';
        return true;
    }
    out[6] = '\0';
    return true;
}

// Texto de um valor em CENTESIMOS de grau, "+045,37", mesma largura e mesma regra de sinal de
// formatDeciText. Sem arredondamento: quem estima o centesimo ja entrega inteiro. int16 cobre
// +/-327,67, mais que o dobro da faixa de medicao.
inline bool formatCentiText(int16_t centi, char* out, uint8_t cap) {
    if (out == nullptr || cap < kDeciTextCap) {
        return false;
    }
    const int32_t magnitude = (centi < 0) ? -static_cast<int32_t>(centi) : centi;
    const int32_t inteiro = magnitude / 100;
    out[0] = (centi < 0) ? '-' : '+';
    out[1] = static_cast<char>('0' + (inteiro / 100));
    out[2] = static_cast<char>('0' + ((inteiro / 10) % 10));
    out[3] = static_cast<char>('0' + (inteiro % 10));
    out[4] = ',';
    out[5] = static_cast<char>('0' + ((magnitude / 10) % 10));
    out[6] = static_cast<char>('0' + (magnitude % 10));
    out[7] = '\0';
    return true;
}

class Angle {
public:
    static constexpr int16_t kMinDeciDeg = -900;
    static constexpr int16_t kMaxDeciDeg = 900;

    // "+045,37" mais o terminador (o pior caso, duas casas).
    static constexpr uint8_t kTextLen = 7;
    static constexpr uint8_t kTextCap = kTextLen + 1;
    static_assert(kTextCap == kDeciTextCap, "Angle e formatDeciText tem de concordar no buffer");

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

    // Escreve a leitura com a quantidade de casas pedida, ou o traco quando nao ha leitura -
    // "---,-" ou "---", na mesma largura do numero. Devolve false sem tocar no buffer se ele
    // nao couber.
    bool format(char* out, uint8_t cap, AngleDecimals decimals) const {
        if (out == nullptr || cap < kTextCap) {
            return false;
        }
        if (!valid_) {
            out[0] = '-';
            out[1] = '-';
            out[2] = '-';
            if (decimals == AngleDecimals::Zero) {
                out[3] = '\0';
                return true;
            }
            out[3] = ',';
            out[4] = '-';
            if (decimals == AngleDecimals::Two) {
                out[5] = '-';
                out[6] = '\0';
                return true;
            }
            out[5] = '\0';
            return true;
        }
        return formatDeciText(deci_, decimals, out, cap);
    }

private:
    explicit constexpr Angle(int16_t deci) : deci_(deci), valid_(true) {}

    int16_t deci_;
    bool valid_;
};

}  // namespace domain


