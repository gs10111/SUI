// As quatro operacoes de limite do manual SUI-DI141388XY secao 5.9 (L204 a L208), cada uma
// como um par de predicados independentes, conforme a decisao A3: ataque exato no valor
// programado (L142) e histerese de 3 decimos somente na liberacao.
//
// Off (L204):  nunca ataca; se estiver sinalizado por qualquer outro caminho, libera.
// >=  (L205):  ataca em a >= L;    libera em a <= grampo(L - 3).
// <=  (L206):  ataca em a <= L;    libera em a >= grampo(L + 3).
// +   (L207):  ataca em |a| >= |L|; libera em |a| <= |L| - 3, e SO existe liberacao com
//              |L| >= 3 (caso degenerado da decisao A3).
//
// POR QUE O GRAMPO. O ponto de liberacao das duas operacoes direcionais e L -/+ 3, e L pode
// ser programado em qualquer decimo de -90,0 a +90,0 graus (L200). Com ">=" em L <= -898 o
// ponto L-3 cai fora da faixa que Angle sabe representar, e nenhuma leitura possivel o
// alcanca: o rele atacaria em -89,8 graus e ficaria atacado para sempre, inclusive em -90,0,
// onde a condicao de L205 e FALSA. Isso e latch, e a decisao A3 e explicita ("alarme angular
// SEM latch: o rele acompanha a inclinacao real"). Grampeando o ponto em Angle::kMinDeciDeg /
// kMaxDeciDeg a banda encolhe nos ultimos 3 decimos da faixa - preco correto por preservar a
// ausencia de latch, e o mesmo cuidado que a guarda (limit >= band) ja dava a "+".
//
// A aritmetica de liberacao sobe para int32 porque L -/+ 3 pode sair de int16 nas contas
// intermediarias; o resultado volta grampeado a faixa de Angle.
#include "domain/limit_rule.h"

#include "domain/angle.h"

namespace domain {
namespace {

constexpr int32_t kFloorDeci = Angle::kMinDeciDeg;
constexpr int32_t kCeilingDeci = Angle::kMaxDeciDeg;

int32_t magnitude(int32_t value) {
    return value < 0 ? -value : value;
}

int32_t clampToRange(int32_t deci) {
    return deci < kFloorDeci ? kFloorDeci : (deci > kCeilingDeci ? kCeilingDeci : deci);
}

bool offAttacks(const LimitRule&, int16_t) {
    return false;
}

bool offReleases(const LimitRule&, int16_t) {
    return true;
}

bool atLeastAttacks(const LimitRule& rule, int16_t angleDeci) {
    return static_cast<int32_t>(angleDeci) >= static_cast<int32_t>(rule.valueDeci());
}

bool atLeastReleases(const LimitRule& rule, int16_t angleDeci) {
    const int32_t point = clampToRange(static_cast<int32_t>(rule.valueDeci()) -
                                       static_cast<int32_t>(rule.hysteresisDeci()));
    return static_cast<int32_t>(angleDeci) <= point;
}

bool atMostAttacks(const LimitRule& rule, int16_t angleDeci) {
    return static_cast<int32_t>(angleDeci) <= static_cast<int32_t>(rule.valueDeci());
}

bool atMostReleases(const LimitRule& rule, int16_t angleDeci) {
    const int32_t point = clampToRange(static_cast<int32_t>(rule.valueDeci()) +
                                       static_cast<int32_t>(rule.hysteresisDeci()));
    return static_cast<int32_t>(angleDeci) >= point;
}

bool modulusAttacks(const LimitRule& rule, int16_t angleDeci) {
    return magnitude(angleDeci) >= magnitude(rule.valueDeci());
}

bool modulusReleases(const LimitRule& rule, int16_t angleDeci) {
    const int32_t band = static_cast<int32_t>(rule.hysteresisDeci());
    const int32_t limit = magnitude(rule.valueDeci());
    return (limit >= band) && (magnitude(angleDeci) <= limit - band);
}

}  // namespace

namespace ops {

const LimitOp kOff{offAttacks, offReleases};
const LimitOp kAtLeast{atLeastAttacks, atLeastReleases};
const LimitOp kAtMost{atMostAttacks, atMostReleases};
const LimitOp kModulus{modulusAttacks, modulusReleases};

}  // namespace ops

}  // namespace domain
