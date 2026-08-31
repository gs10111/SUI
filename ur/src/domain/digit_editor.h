// Editor de campo numerico digito a digito, com sinal opcional, para todos os campos de
// edicao da IHM: senha (4 digitos, sem sinal), Preset e Valor Limite (+XXX,X) e trim da
// Auto Calibracao (4 digitos, sem sinal).
//
// Manual SUI-DI141388XY (docs/manual-cliente-sui-2026.txt, citado por numero de linha do
// arquivo bruto):
//   5.3  L101 - login: "Use os botoes UP e DOWN para editar o valor do digito piscando e
//                pressione Menu para que o digito a esquerda fique editavel (piscando)".
//   5.6  L155, L156 e L157 - Preset: MENU seleciona o digito, a tecla UP altera o valor do
//                digito selecionado e a tecla DOWN altera o SINAL do valor programado.
//   5.7  L172 e L173 - trim da Auto Calibracao ("Ajuste 0Vcc:0000"): MENU seleciona o digito
//                e as teclas UP e DOWN alteram seu valor.
//   5.9  L213 - Valor Limite: mesma edicao digito a digito.
//   5.10 L231, L232 e L233 - "Edita senha:1234", digito selecionado piscando, UP e DOWN
//                ajustam o valor do digito.
//
// CONTRADICAO DO PROPRIO MANUAL, registrada porque e a justificativa da regra implementada:
// L213 (Valor Limite) manda usar "as teclas UP e DOWN para alterar seu valor E a tecla DOWN
// para definir o sinal", o que atribui dois papeis a mesma tecla; L156 e L157 (Preset)
// separam UP=valor e DOWN=sinal. A implementacao segue L156/L157 por forca da "regra unica de
// cursor" de A13, e a consequencia deliberada e que nos campos COM sinal nao existe tecla de
// decremento de digito: so UP, com rolagem circular de 10. Nos campos SEM sinal (senha, L233,
// e trim, L173) UP e DOWN rolam o digito nos dois sentidos.
//
// Decisao A13, sub-item "regra unica de cursor", valida para senha, valor limite, preset e
// trim: o campo abre no digito mais a direita, MENU move para a esquerda e a rolagem e
// circular dentro do campo.
//
// A13 tambem fecha o que a confirmacao faz com valor fora de faixa: RECUSA explicita, com
// mensagem, e nunca clamp silencioso. Um tecnico que digita 1200 decimos num setpoint de rele
// de seguranca nao pode receber 900 gravado sem aviso, entao confirm() e consulta PURA: apenas
// informa, nao toca no valor nem no cursor - quem grava e o chamador, e so quando o resultado
// e Ok. O pisca de 2000 ms da mensagem de recusa (A13) NAO mora aqui: e da camada de
// aplicacao, que e quem tem IClock; DigitEditor nao recebe relogio de proposito.
//
// ESTADO SEGURO: o editor nasce FECHADO e qualquer open() reprovado o devolve a FECHADO, em
// vez de deixa-lo vivo na spec anterior. Tela morta e recuperavel; tela mostrando o campo
// errado com a faixa errada nao e.
#pragma once

#include <stdint.h>

namespace domain {

enum class ConfirmResult : uint8_t {
    Ok,
    OutOfRange,
};

// Descricao do campo, fornecida pelo chamador a cada abertura. `decimals` e apenas
// apresentacao: o valor trafega sempre inteiro, em decimos de grau nos campos angulares e em
// LSB no trim, porque o dominio nao tem ponto flutuante.
//
// `outOfRangeMessage` TEM de apontar para literal ou buffer com duracao ESTATICA: open() copia
// a spec por valor e guarda o ponteiro, que sobrevive a chamada. nullptr e tolerado e vira
// string vazia no acessor, para que o caminho de recusa nunca entregue ponteiro nulo ao
// IDisplay.
struct DigitFieldSpec {
    uint8_t digits;
    uint8_t decimals;
    bool signedField;
    int16_t min;
    int16_t max;
    const char* outOfRangeMessage;
};

class DigitEditor {
public:
    static constexpr uint8_t kMaxDigits = 4;

    // Sinal, digitos, virgula e terminador.
    static constexpr uint8_t kTextCap = kMaxDigits + 3;

    DigitEditor();

    // Recusa spec impossivel e valor que nao cabe no campo, em vez de truncar em silencio.
    // Qualquer recusa deixa o editor FECHADO.
    bool open(const DigitFieldSpec& spec, int16_t value);

    void up();
    void down();
    void menu();

    ConfirmResult confirm() const;
    const char* outOfRangeMessage() const;

    int16_t value() const;
    bool negative() const;

    // Indice do digito que pisca: 0 e o mais a esquerda.
    uint8_t cursor() const;

    // Onde esse digito cai no texto de format(), para o display piscar o caractere certo.
    uint8_t cursorTextIndex() const;

    bool format(char* out, uint8_t cap) const;

private:
    void close();

    DigitFieldSpec spec_;
    uint8_t digit_[kMaxDigits];
    uint8_t cursor_;
    bool negative_;
};

}  // namespace domain
