// Item t8: bobina do AX1RC-5V e margem de acionamento. Folha 2/2 (RL2..RL5, BC337, base 2K + 1N4007, PD 1K).
// Datasheets: AX1RC-5V (Fujitsu), BC337 (Nexperia), LM2575-5 (TI SNVS106).
#include <stdint.h>
#include <stdio.h>

#include "board_pins.h"
#include "core/cmd_parser.h"
#include "core/ctx.h"
#include "core/test_runner.h"
#include "status.h"
#include "verdict.h"

namespace {

constexpr uint8_t kMaxAttempts = 3;
constexpr uint16_t kAnswerCap = 16;

constexpr float kRailNominalV = 5.0f;
constexpr float kVbeV = 0.7f;
constexpr float kVdiodeV = 0.7f;
constexpr float kRbaseOhm = 2000.0f;
constexpr float kRpulldownOhm = 1000.0f;

constexpr float kCoilOhmMin = 50.0f;
constexpr float kCoilOhmMax = 1000.0f;

constexpr float kHfeFail = 200.0f;
constexpr float kHfeWarn = 150.0f;

constexpr float kPullInFailV = 4.5f;
constexpr float kPullInWarnV = 4.0f;

constexpr float kEspTxReserveMa = 150.0f;
constexpr float kAnalogReserveMa = 20.0f;
constexpr float kSupplyUsePct = 80.0f;
constexpr float kCoilDeviationPct = 25.0f;

char g_whatBuf[160];
char g_whereBuf[160];
char g_expectedBuf[160];
char g_noteBuf[96];
char g_answerBuf[kAnswerCap];

struct CoilData {
    float coilOhm;
    float coilNomMa;
    float rail3v3V;
    float baseMa;
    float hfeMin;
    float dropOutV;
    float pullInV;
    float coilRealMa;
};

enum class Reply : uint8_t { Ok, Aborted, GaveUp };

TestResult abortResult() {
    return TestResult(Verdict::Abort, "abortado pelo operador");
}

TestResult skipResult(const char* what) {
    snprintf(g_noteBuf, sizeof(g_noteBuf), "%s: %u tentativas invalidas", what,
             static_cast<unsigned>(kMaxAttempts));
    return TestResult(Verdict::Skip, g_noteBuf);
}

Reply askNumber(Ctx& ctx, const char* prompt, float lo, float hi, float& out) {
    for (uint8_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (!ctx.op.askLine(prompt, g_answerBuf, kAnswerCap)) {
            return ctx.op.aborted() ? Reply::Aborted : Reply::GaveUp;
        }
        float value = 0.0f;
        if (!cmd::parseFloat(g_answerBuf, value)) {
            ctx.op.info("entrada invalida '%s': digite um numero, por exemplo 139.0 ou 139,0", g_answerBuf);
            continue;
        }
        if (value < lo || value > hi) {
            ctx.op.info("valor %.3f fora da faixa aceita nesta pergunta (%.3f a %.3f)",
                        static_cast<double>(value), static_cast<double>(lo), static_cast<double>(hi));
            continue;
        }
        out = value;
        return Reply::Ok;
    }
    return Reply::GaveUp;
}

bool readNumber(Ctx& ctx, const char* prompt, const char* what, float lo, float hi, float& out,
                TestResult& stop) {
    const Reply reply = askNumber(ctx, prompt, lo, hi, out);
    if (reply == Reply::Ok) {
        return true;
    }
    stop = (reply == Reply::Aborted) ? abortResult() : skipResult(what);
    return false;
}

void printIntro(Ctx& ctx) {
    ctx.op.info("--- bobina do AX1RC-5V e margem de acionamento (folha 2/2) ---");
    ctx.op.info("pergunta em aberto do projeto: tensao e corrente da bobina, tensao minima de");
    ctx.op.info("operacao e capacidade da fonte - aqui isso vira numero medido, nao catalogo");
    ctx.op.info("acionamento ativo em alto: BC337 com base em 2K mais 1N4007 e pull-down de 1K");
    ctx.op.info("tenha em maos: ohmimetro, amperimetro e fonte de bancada ajustavel");
}

bool stepCoilResistance(Ctx& ctx, CoilData& data, TestResult& stop) {
    ctx.op.info("[1/5] resistencia da bobina e corrente nominal");
    snprintf(g_whatBuf, sizeof(g_whatBuf),
             "com a placa DESENERGIZADA, medir a resistencia da bobina de %s com ohmimetro",
             board::kRelayMap[0].relay);
    snprintf(g_whereBuf, sizeof(g_whereBuf), "terminais da bobina de %s (rele da rede %s)",
             board::kRelayMap[0].relay, board::kRelayMap[0].net);
    snprintf(g_expectedBuf, sizeof(g_expectedBuf),
             "leitura estavel; um AX1RC-5V fica entre %.0f e %.0f ohm",
             static_cast<double>(kCoilOhmMin), static_cast<double>(kCoilOhmMax));

    uint8_t confirmTries = 0;
    for (;;) {
        ctx.op.step(g_whatBuf, g_whereBuf, g_expectedBuf);
        if (!readNumber(ctx, "resistencia da bobina (ohm):", "resistencia da bobina", 1.0f, 100000.0f,
                        data.coilOhm, stop)) {
            return false;
        }
        if (data.coilOhm >= kCoilOhmMin && data.coilOhm <= kCoilOhmMax) {
            break;
        }
        ctx.op.info("ATENCAO: %.1f ohm esta fora do esperado para um AX1RC-5V (%.0f a %.0f ohm)",
                    static_cast<double>(data.coilOhm), static_cast<double>(kCoilOhmMin),
                    static_cast<double>(kCoilOhmMax));
        ctx.op.info("causa provavel: bobina aberta ou em curto, ponta no terminal errado, placa energizada");
        if (ctx.op.askYes("a leitura esta certa e voce confirma esse valor?")) {
            break;
        }
        if (ctx.op.aborted()) {
            stop = abortResult();
            return false;
        }
        ++confirmTries;
        if (confirmTries >= kMaxAttempts) {
            stop = TestResult(Verdict::Skip, "R de bobina fora da faixa e nao confirmada");
            return false;
        }
        ctx.op.info("refaca a medida nos terminais da bobina de %s", board::kRelayMap[0].relay);
    }

    data.coilNomMa = (kRailNominalV / data.coilOhm) * 1000.0f;
    ctx.op.info("Rbobina medida = %.1f ohm", static_cast<double>(data.coilOhm));
    ctx.op.info("Inom = %.2f V / %.1f ohm = %.1f mA por bobina", static_cast<double>(kRailNominalV),
                static_cast<double>(data.coilOhm), static_cast<double>(data.coilNomMa));
    return true;
}

bool stepBaseDrive(Ctx& ctx, CoilData& data, TestResult& stop) {
    ctx.op.info("[2/5] corrente de base do BC337 e hFE exigido");
    ctx.op.step("medir a tensao real do trilho +3V3 com a placa energizada",
                "trilho +3V3 (mesmo que alimenta os GPIO de acionamento dos reles)",
                "o valor real do multimetro; o firmware nao assume 3,300 V");
    if (!readNumber(ctx, "tensao medida no trilho +3V3 (V):", "tensao do +3V3", 2.0f, 4.0f, data.rail3v3V,
                    stop)) {
        return false;
    }

    data.baseMa =
        ((data.rail3v3V - kVbeV - kVdiodeV) / kRbaseOhm - (kVbeV / kRpulldownOhm)) * 1000.0f;
    ctx.op.info("Ib = (%.3f - %.3f Vbe - %.3f Vd) / %.0f - (%.3f / %.0f)",
                static_cast<double>(data.rail3v3V), static_cast<double>(kVbeV),
                static_cast<double>(kVdiodeV), static_cast<double>(kRbaseOhm),
                static_cast<double>(kVbeV), static_cast<double>(kRpulldownOhm));
    ctx.op.info("Ib = %.3f mA com o trilho +3V3 real de %.3f V", static_cast<double>(data.baseMa),
                static_cast<double>(data.rail3v3V));
    if (data.baseMa <= 0.0f) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "Ib=%.3f mA (+3V3=%.3f V): base sem corrente",
                 static_cast<double>(data.baseMa), static_cast<double>(data.rail3v3V));
        ctx.op.info("REPROVA: o pull-down de 1K desvia toda a corrente do resistor de base de 2K");
        ctx.op.info("causa provavel: +3V3 afundado ou 1N4007 da base com queda maior que a prevista");
        stop = TestResult(Verdict::Fail, g_noteBuf);
        return false;
    }

    data.hfeMin = data.coilNomMa / data.baseMa;
    ctx.op.info("hFE minimo exigido = Icoil / Ib = %.1f mA / %.3f mA = %.0f",
                static_cast<double>(data.coilNomMa), static_cast<double>(data.baseMa),
                static_cast<double>(data.hfeMin));
    if (data.hfeMin > kHfeFail) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "hFE exigido %.0f > %.0f com Ib de %.3f mA",
                 static_cast<double>(data.hfeMin), static_cast<double>(kHfeFail),
                 static_cast<double>(data.baseMa));
        ctx.op.info("REPROVA: BC337 tem hFE tipico de 100 a 250 e a pior caixa com temperatura");
        ctx.op.info("nao garante %.0f; a bobina de %.1f mA nao satura com Ib de %.3f mA",
                    static_cast<double>(data.hfeMin), static_cast<double>(data.coilNomMa),
                    static_cast<double>(data.baseMa));
        ctx.op.info("causa provavel: resistor de base de 2K alto demais para esta bobina");
        stop = TestResult(Verdict::Fail, g_noteBuf);
        return false;
    }
    if (data.hfeMin >= kHfeWarn) {
        ctx.op.info("ALERTA de margem: hFE exigido %.0f esta entre %.0f e %.0f - passa, mas sem folga",
                    static_cast<double>(data.hfeMin), static_cast<double>(kHfeWarn),
                    static_cast<double>(kHfeFail));
    } else {
        ctx.op.info("margem folgada: hFE exigido %.0f abaixo de %.0f", static_cast<double>(data.hfeMin),
                    static_cast<double>(kHfeWarn));
    }
    return true;
}

bool stepPullIn(Ctx& ctx, CoilData& data, TestResult& stop) {
    ctx.op.info("[3/5] tensao de acionamento (pull-in) e de desacionamento (drop-out)");
    const Status stOn = ctx.relays.set(0, true);
    if (stOn.failed()) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "%s nao energiza para o ensaio (%s)",
                 board::kRelayMap[0].net, errName(stOn.err));
        stop = TestResult(Verdict::Fail, g_noteBuf);
        return false;
    }
    ctx.op.info("%s (%s) energizado pelo IO%d", board::kRelayMap[0].net, board::kRelayMap[0].relay,
                static_cast<int>(board::kRelayMap[0].pin));

    snprintf(g_whereBuf, sizeof(g_whereBuf), "fonte de bancada na alimentacao da placa e continuidade em %s",
             board::kRelayMap[0].screwTerminals);
    ctx.op.step("BAIXAR devagar a tensao da fonte de bancada ate o rele ABRIR",
                g_whereBuf, "anotar a tensao exata em que o contato abriu (drop-out)");
    if (!readNumber(ctx, "tensao de desacionamento (drop-out) em V:", "tensao de drop-out", 0.0f, 6.0f,
                    data.dropOutV, stop)) {
        ctx.relays.allOff();
        return false;
    }
    ctx.op.step("SUBIR devagar a tensao da fonte ate o rele FECHAR de novo",
                g_whereBuf, "anotar a tensao exata em que o contato fechou (pull-in)");
    if (!readNumber(ctx, "tensao de acionamento (pull-in) em V:", "tensao de pull-in", 0.0f, 6.0f,
                    data.pullInV, stop)) {
        ctx.relays.allOff();
        return false;
    }
    ctx.relays.allOff();

    ctx.op.info("devolva a fonte de bancada para %.2f V antes de seguir",
                static_cast<double>(kRailNominalV));
    if (!ctx.op.askYes("a fonte de bancada esta de volta em 5,00 V?")) {
        if (ctx.op.aborted()) {
            stop = abortResult();
            return false;
        }
        stop = TestResult(Verdict::Skip, "fonte nao confirmada de volta em 5,00 V");
        return false;
    }

    const float marginPct = (kRailNominalV - data.pullInV) / kRailNominalV * 100.0f;
    ctx.op.info("drop-out = %.2f V   pull-in = %.2f V", static_cast<double>(data.dropOutV),
                static_cast<double>(data.pullInV));
    ctx.op.info("margem = (%.2f - %.2f) / %.2f = %.1f%% sobre os 5 V nominais",
                static_cast<double>(kRailNominalV), static_cast<double>(data.pullInV),
                static_cast<double>(kRailNominalV), static_cast<double>(marginPct));
    if (data.pullInV < data.dropOutV) {
        ctx.op.info("ATENCAO: pull-in menor que drop-out - histerese invertida, confira se as duas");
        ctx.op.info("leituras nao foram trocadas ao responder");
    }
    if (data.pullInV > kPullInFailV) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "pull-in %.2f V, margem %.1f%% (min 10%%)",
                 static_cast<double>(data.pullInV), static_cast<double>(marginPct));
        ctx.op.info("REPROVA: pull-in de %.2f V passa de %.2f V e deixa menos de 10%% de margem",
                    static_cast<double>(data.pullInV), static_cast<double>(kPullInFailV));
        ctx.op.info("causa provavel: bobina fora de especificacao, queda no BC337 ou +5V baixo sob carga");
        stop = TestResult(Verdict::Fail, g_noteBuf);
        return false;
    }
    if (data.pullInV >= kPullInWarnV) {
        ctx.op.info("ALERTA de margem: pull-in de %.2f V esta entre %.2f e %.2f V - passa apertado",
                    static_cast<double>(data.pullInV), static_cast<double>(kPullInWarnV),
                    static_cast<double>(kPullInFailV));
    } else {
        ctx.op.info("margem folgada: pull-in de %.2f V abaixo de %.2f V",
                    static_cast<double>(data.pullInV), static_cast<double>(kPullInWarnV));
    }
    return true;
}

bool stepConsumption(Ctx& ctx, CoilData& data, TestResult& stop) {
    ctx.op.info("[4/5] consumo real por bobina");
    ctx.op.info("mantenha a fonte em 5,00 V e o amperimetro no mesmo ponto nas duas leituras");
    ctx.relays.allOff();

    ctx.op.step("medir a corrente total de entrada com os 4 reles DESLIGADOS",
                "amperimetro em serie com a alimentacao da placa", "leitura estavel, em mA");
    float idleMa = 0.0f;
    if (!readNumber(ctx, "corrente com os reles desligados (mA):", "corrente em repouso", 0.0f, 20000.0f,
                    idleMa, stop)) {
        return false;
    }

    const Status stAll = ctx.relays.allOn();
    if (stAll.failed()) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "allOn recusado pelo banco de reles (%s)", errName(stAll.err));
        stop = TestResult(Verdict::Fail, g_noteBuf);
        return false;
    }
    ctx.op.step("medir a corrente total de entrada com os 4 reles LIGADOS",
                "amperimetro em serie com a alimentacao da placa", "leitura estavel, em mA");
    float loadMa = 0.0f;
    if (!readNumber(ctx, "corrente com os 4 reles ligados (mA):", "corrente sob carga", 0.0f, 20000.0f,
                    loadMa, stop)) {
        ctx.relays.allOff();
        return false;
    }
    ctx.relays.allOff();

    const float deltaMa = loadMa - idleMa;
    if (deltaMa <= 0.0f) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "consumo nao subiu: %.1f mA x %.1f mA",
                 static_cast<double>(loadMa), static_cast<double>(idleMa));
        ctx.op.info("REPROVA: com 4 bobinas ligadas a corrente saiu de %.1f mA para %.1f mA",
                    static_cast<double>(idleMa), static_cast<double>(loadMa));
        ctx.op.info("causa provavel: amperimetro fora do laco, bobinas sem acionar ou leituras trocadas");
        stop = TestResult(Verdict::Fail, g_noteBuf);
        return false;
    }

    data.coilRealMa = deltaMa / static_cast<float>(board::kRelayCount);
    const float diffPct = (data.coilRealMa - data.coilNomMa) / data.coilNomMa * 100.0f;
    const float absDiffPct = (diffPct < 0.0f) ? -diffPct : diffPct;
    ctx.op.info("delta = %.1f - %.1f = %.1f mA para %u bobinas", static_cast<double>(loadMa),
                static_cast<double>(idleMa), static_cast<double>(deltaMa),
                static_cast<unsigned>(board::kRelayCount));
    ctx.op.info("corrente real por bobina = %.1f mA (Inom calculada no passo 1 = %.1f mA)",
                static_cast<double>(data.coilRealMa), static_cast<double>(data.coilNomMa));
    ctx.op.info("diferenca = %+.1f%% em relacao a Inom", static_cast<double>(diffPct));
    if (absDiffPct > kCoilDeviationPct) {
        ctx.op.info("ALERTA: diferenca acima de %.0f%% - confira a resistencia lida no passo 1,",
                    static_cast<double>(kCoilDeviationPct));
        ctx.op.info("o ponto de medida da corrente e se a fonte estava em 5,00 V nas duas leituras");
    }
    return true;
}

bool stepSupplyBudget(Ctx& ctx, const CoilData& data, TestResult& stop) {
    ctx.op.info("[5/5] capacidade da fonte no pior caso");
    ctx.op.info("informe os dois limites em mA disponiveis no trilho +5V (LM2575-5 de catalogo = 1000 mA)");
    float regMa = 0.0f;
    if (!readNumber(ctx, "corrente nominal do LM2575-5 (mA):", "capacidade do LM2575-5", 1.0f, 20000.0f,
                    regMa, stop)) {
        return false;
    }
    float supplyMa = 0.0f;
    if (!readNumber(ctx, "corrente da fonte chaveada de entrada no +5V (mA):", "capacidade da fonte", 1.0f,
                    20000.0f, supplyMa, stop)) {
        return false;
    }

    const float limitMa = (regMa < supplyMa) ? regMa : supplyMa;
    const float coilWorstMa = (data.coilRealMa > data.coilNomMa) ? data.coilRealMa : data.coilNomMa;
    const float coilsMa = coilWorstMa * static_cast<float>(board::kRelayCount);
    const float worstMa = coilsMa + kEspTxReserveMa + kAnalogReserveMa;
    const float budgetMa = limitMa * (kSupplyUsePct / 100.0f);

    ctx.op.info("corrente por bobina usada no pior caso = %.1f mA (a maior entre medida e nominal)",
                static_cast<double>(coilWorstMa));
    ctx.op.info("pior caso = %u x %.1f + %.0f (ESP32 em transmissao) + %.0f (saida analogica)",
                static_cast<unsigned>(board::kRelayCount), static_cast<double>(coilWorstMa),
                static_cast<double>(kEspTxReserveMa), static_cast<double>(kAnalogReserveMa));
    ctx.op.info("pior caso = %.1f mA   limite menor informado = %.0f mA   %.0f%% dele = %.1f mA",
                static_cast<double>(worstMa), static_cast<double>(limitMa),
                static_cast<double>(kSupplyUsePct), static_cast<double>(budgetMa));
    if (worstMa > budgetMa) {
        snprintf(g_noteBuf, sizeof(g_noteBuf), "pior caso %.0f mA > %.0f%% de %.0f mA",
                 static_cast<double>(worstMa), static_cast<double>(kSupplyUsePct),
                 static_cast<double>(limitMa));
        ctx.op.info("REPROVA: %.1f mA de pior caso passam dos %.1f mA de teto (%.0f%% de %.0f mA)",
                    static_cast<double>(worstMa), static_cast<double>(budgetMa),
                    static_cast<double>(kSupplyUsePct), static_cast<double>(limitMa));
        ctx.op.info("causa provavel: bobinas de corrente alta demais ou fonte subdimensionada para 4 reles");
        stop = TestResult(Verdict::Fail, g_noteBuf);
        return false;
    }
    ctx.op.info("folga = %.1f mA abaixo do teto de %.0f%%", static_cast<double>(budgetMa - worstMa),
                static_cast<double>(kSupplyUsePct));
    return true;
}

class Test08Coil : public ITest {
public:
    const char* id() const override { return "t8"; }
    const char* name() const override { return "Bobina dos reles e margem"; }
    uint8_t order() const override { return 7; }

    TestResult run(Ctx& ctx) override {
        ctx.relays.allOff();
        CoilData data{};
        const TestResult result = execute(ctx, data);
        ctx.relays.allOff();
        return result;
    }

private:
    static TestResult execute(Ctx& ctx, CoilData& data) {
        printIntro(ctx);

        TestResult stop;
        if (!stepCoilResistance(ctx, data, stop)) {
            return stop;
        }
        if (!stepBaseDrive(ctx, data, stop)) {
            return stop;
        }
        if (!stepPullIn(ctx, data, stop)) {
            return stop;
        }
        if (!stepConsumption(ctx, data, stop)) {
            return stop;
        }
        if (!stepSupplyBudget(ctx, data, stop)) {
            return stop;
        }

        snprintf(g_noteBuf, sizeof(g_noteBuf), "R=%.0f ohm I=%.1f mA hFE=%.0f Vpi=%.2f V",
                 static_cast<double>(data.coilOhm), static_cast<double>(data.coilRealMa),
                 static_cast<double>(data.hfeMin), static_cast<double>(data.pullInV));
        return TestResult(Verdict::Pass, g_noteBuf);
    }
};

}  // namespace

REGISTER_TEST(Test08Coil);
