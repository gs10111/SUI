// Regra de um limite: o valor programado, a operacao do manual e a banda de histerese, mais
// a traducao unica de estado de rele em nivel de bobina.
//
// Manual SUI-DI141388XY secao 5.9: L204 (Off, o rele permanece em repouso, independentemente
// do angulo medido), L205 (>=, acionado quando o angulo medido for maior ou igual ao valor
// programado), L206 (<=), L207 e L208 (+ modulo, acionado quando o MODULO do angulo medido
// for maior ou igual ao MODULO do valor programado, nos dois sentidos), L223 (o numero
// comparado e a leitura do display, com Preset e Sentido do Sensor ja aplicados) e secao 5.5
// L142 (o ponto de atuacao do rele e ajustado exatamente no angulo desejado).
//
// Decisao A3, aprovada: a histerese de 0,3 grau (3 decimos) atua SO na liberacao. Por isso
// cada operacao tem DOIS predicados independentes, e nao um so negado: entre atacar e liberar
// existe uma banda em que o rele conserva o estado que ja tem. O ataque continua exato em L,
// que e o que preserva L142; o que se desloca e a liberacao, que o manual nunca especificou.
//
// Decisao A3, caso degenerado de "+": com |L| <= 2 decimos o ponto de liberacao |a| <= |L|-3
// nao existe, entao "+" ataca e nunca mais libera. E leitura literal de L207 e comportamento
// especificado, nao defeito; a Tabela 2 (L258 e L262) entrega os limites 2 e 4 em +000,0 grau
// justamente com Operacao Off, e nao com "+". As duas operacoes DIRECIONAIS (">=" e "<=") nao
// tem esse caso: o ponto de liberacao delas e grampeado na faixa representavel de Angle, de
// modo que nenhum ajuste legal de L200 (-90,0 a +90,0 graus) produz rele latchado.
//
// Decisao A1: coilLevel() e a UNICA funcao do produto que traduz "alarme" ou "normal" em
// nivel de bobina. A polaridade entra por PARAMETRO (o chamador passa
// urbase::kRelayFailSafePolarity), nunca por #ifdef espalhado, porque A1 ainda depende da
// medicao M2 e as duas polaridades tem de ser testaveis sem recompilar o dominio.
//
// OCP: LimitOp e um par de predicados sobre a propria regra. Acrescentar uma operacao nova -
// por exemplo uma janela entre A e B - e escrever mais um LimitOp e apontar a regra para ele.
// LimitEvaluator nunca pergunta qual e a operacao: so chama attacks() e releases(). Nao ha
// switch sobre operacao em lugar nenhum do dominio.
//
// O angulo chega aqui em decimos de grau ja validados. Quem recusa amostra invalida, saturada
// ou fora de +/-90,0 (decisao A4) e LimitEvaluator, e por isso os predicados nao conhecem
// Angle::invalid() e nao podem inventar leitura.
#pragma once

#include <stdint.h>

#include "ports/i_relay_bank.h"

namespace domain {

constexpr int16_t kHysteresisDeci = 3;

class LimitRule;

struct LimitOp {
    bool (*attacks)(const LimitRule& rule, int16_t angleDeci);
    bool (*releases)(const LimitRule& rule, int16_t angleDeci);
};

namespace ops {

extern const LimitOp kOff;
extern const LimitOp kAtLeast;
extern const LimitOp kAtMost;
extern const LimitOp kModulus;

}  // namespace ops

class LimitRule {
public:
    constexpr LimitRule()
        : op_(&ops::kOff), valueDeci_(0), hysteresisDeci_(kHysteresisDeci) {}

    constexpr LimitRule(const LimitOp& operation, int16_t valueDeci,
                        int16_t hysteresisDeci = kHysteresisDeci)
        : op_(&operation), valueDeci_(valueDeci), hysteresisDeci_(hysteresisDeci) {}

    constexpr int16_t valueDeci() const { return valueDeci_; }
    constexpr int16_t hysteresisDeci() const { return hysteresisDeci_; }

    bool attacks(int16_t angleDeci) const { return op_->attacks(*this, angleDeci); }
    bool releases(int16_t angleDeci) const { return op_->releases(*this, angleDeci); }

private:
    const LimitOp* op_;
    int16_t valueDeci_;
    int16_t hysteresisDeci_;
};

constexpr bool coilLevel(RelayState relayState, bool failSafePolarity) {
    return (relayState == RelayState::Clear) ? failSafePolarity : !failSafePolarity;
}

}  // namespace domain
