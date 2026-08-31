// Implementacao do editor de campo digito a digito.
// Manual SUI-DI141388XY: 5.3 L101, 5.6 L155 a L157, 5.7 L172 e L173, 5.9 L213, 5.10 L231 a
// L233. Decisao A13: regra unica de cursor e recusa explicita de valor fora de faixa.
//
// Nos campos COM sinal a tecla DOWN e do SINAL e nao do digito (L157), entao o decremento de
// digito nao existe nesses campos; nos campos SEM sinal DOWN rola o digito para tras (L173 e
// L233). Qualquer open() reprovado chama close(): o editor volta ao estado seguro FECHADO em
// vez de continuar respondendo pela spec anterior.
#include "domain/digit_editor.h"

namespace domain {

namespace {

int32_t potenciaDe10(uint8_t expoente) {
    int32_t potencia = 1;
    for (uint8_t i = 0; i < expoente; ++i) {
        potencia *= 10;
    }
    return potencia;
}

const char kSemMensagem[] = "";

}  // namespace

DigitEditor::DigitEditor()
    : spec_{0, 0, false, 0, 0, kSemMensagem}, digit_{0, 0, 0, 0}, cursor_(0), negative_(false) {}

void DigitEditor::close() {
    spec_.digits = 0;
    spec_.decimals = 0;
    spec_.signedField = false;
    spec_.min = 0;
    spec_.max = 0;
    spec_.outOfRangeMessage = kSemMensagem;
    for (uint8_t posicao = 0; posicao < kMaxDigits; ++posicao) {
        digit_[posicao] = 0;
    }
    cursor_ = 0;
    negative_ = false;
}

bool DigitEditor::open(const DigitFieldSpec& spec, int16_t value) {
    close();
    if (spec.digits == 0 || spec.digits > kMaxDigits) {
        return false;
    }
    if (spec.decimals >= spec.digits) {
        return false;
    }
    if (spec.min > spec.max) {
        return false;
    }
    if (value < 0 && !spec.signedField) {
        return false;
    }
    int32_t magnitude = (value < 0) ? -static_cast<int32_t>(value) : static_cast<int32_t>(value);
    if (magnitude > potenciaDe10(spec.digits) - 1) {
        return false;
    }
    spec_ = spec;
    negative_ = (value < 0);
    for (uint8_t posicao = spec_.digits; posicao > 0; --posicao) {
        digit_[posicao - 1] = static_cast<uint8_t>(magnitude % 10);
        magnitude /= 10;
    }
    cursor_ = static_cast<uint8_t>(spec_.digits - 1);
    return true;
}

void DigitEditor::up() {
    if (spec_.digits == 0) {
        return;
    }
    digit_[cursor_] = static_cast<uint8_t>((digit_[cursor_] + 1) % 10);
}

void DigitEditor::down() {
    if (spec_.digits == 0) {
        return;
    }
    if (spec_.signedField) {
        negative_ = !negative_;
        return;
    }
    digit_[cursor_] = static_cast<uint8_t>((digit_[cursor_] + 9) % 10);
}

void DigitEditor::menu() {
    if (spec_.digits == 0) {
        return;
    }
    cursor_ = (cursor_ == 0) ? static_cast<uint8_t>(spec_.digits - 1)
                             : static_cast<uint8_t>(cursor_ - 1);
}

ConfirmResult DigitEditor::confirm() const {
    if (spec_.digits == 0) {
        return ConfirmResult::OutOfRange;
    }
    const int16_t atual = value();
    return (atual < spec_.min || atual > spec_.max) ? ConfirmResult::OutOfRange : ConfirmResult::Ok;
}

const char* DigitEditor::outOfRangeMessage() const {
    return (spec_.outOfRangeMessage != nullptr) ? spec_.outOfRangeMessage : kSemMensagem;
}

int16_t DigitEditor::value() const {
    int32_t acumulado = 0;
    for (uint8_t posicao = 0; posicao < spec_.digits; ++posicao) {
        acumulado = acumulado * 10 + digit_[posicao];
    }
    if (negative_) {
        acumulado = -acumulado;
    }
    return static_cast<int16_t>(acumulado);
}

bool DigitEditor::negative() const { return negative_; }

uint8_t DigitEditor::cursor() const { return cursor_; }

uint8_t DigitEditor::cursorTextIndex() const {
    uint8_t indice = cursor_;
    if (spec_.signedField) {
        ++indice;
    }
    if (spec_.decimals > 0 && cursor_ >= static_cast<uint8_t>(spec_.digits - spec_.decimals)) {
        ++indice;
    }
    return indice;
}

bool DigitEditor::format(char* out, uint8_t cap) const {
    if (spec_.digits == 0 || out == nullptr) {
        return false;
    }
    uint8_t necessario = static_cast<uint8_t>(spec_.digits + 1);
    if (spec_.signedField) {
        ++necessario;
    }
    if (spec_.decimals > 0) {
        ++necessario;
    }
    if (cap < necessario) {
        return false;
    }
    uint8_t escrito = 0;
    if (spec_.signedField) {
        out[escrito++] = negative_ ? '-' : '+';
    }
    const uint8_t virgulaAntesDe = static_cast<uint8_t>(spec_.digits - spec_.decimals);
    for (uint8_t posicao = 0; posicao < spec_.digits; ++posicao) {
        if (spec_.decimals > 0 && posicao == virgulaAntesDe) {
            out[escrito++] = ',';
        }
        out[escrito++] = static_cast<char>('0' + digit_[posicao]);
    }
    out[escrito] = '\0';
    return true;
}

}  // namespace domain
