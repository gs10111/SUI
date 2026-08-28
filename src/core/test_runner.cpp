// Execucao de teste: cabecalho, estado seguro em toda saida, relatorio legivel e linha CSV. Sem acesso direto a hardware.
#include "core/test_runner.h"

#include <stddef.h>

#include "core/ctx.h"
#include "status.h"
#include "verdict.h"

namespace {

constexpr uint16_t kHumanCap = 1024;
constexpr uint16_t kCsvCap = 512;

void copyText(char* dst, size_t cap, const char* src) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    size_t i = 0;
    if (src != nullptr) {
        while (i + 1 < cap && src[i] != '\0') {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

bool verdictValid(Verdict v) {
    return static_cast<uint8_t>(v) <= static_cast<uint8_t>(Verdict::Abort);
}

}  // namespace

TestRunner::TestRunner(Ctx& ctx) : ctx_(ctx), suiteAborted_(false), busy_(false) {}

TestResult TestRunner::runOne(ITest& test) {
    const bool wasBusy = busy_;
    busy_ = true;

    ctx_.io.printf("---- [%s] %s ----\r\n", test.id(), test.name());

    ctx_.op.clearSkipped();
    TestResult result = test.run(ctx_);
    ctx_.safe.enterSafeState();

    if (!verdictValid(result.verdict)) {
        result = TestResult(Verdict::Fail, "veredito invalido");
    } else if (ctx_.op.aborted()) {
        const bool ownNote =
            (result.verdict == Verdict::Abort) && (result.note != nullptr) && (result.note[0] != '\0');
        result = TestResult(Verdict::Abort, ownNote ? result.note : "abortado pelo operador");
    } else if (result.verdict == Verdict::NotRun) {
        result = TestResult(Verdict::Fail, "teste nao produziu veredito");
    } else if (result.verdict == Verdict::Pass && ctx_.op.skipped()) {
        result = TestResult(Verdict::Skip, "algum ponto foi pulado ou expirou: item nao verificado");
    }

    const char* const detail = (result.note != nullptr) ? result.note : "";
    const uint32_t stampMs = ctx_.io.nowMs();

    const Status recorded = ctx_.report.record(test.id(), test.name(), result.verdict, detail, stampMs);
    if (recorded.failed()) {
        ctx_.io.printf("aviso: item nao registrado no relatorio (%s)\r\n", errName(recorded.err));
    }

    if (detail[0] != '\0') {
        ctx_.io.printf("[%s] %s : %s - %s (%lu ms)\r\n", test.id(), test.name(), verdictName(result.verdict), detail,
                       static_cast<unsigned long>(stampMs));
    } else {
        ctx_.io.printf("[%s] %s : %s (%lu ms)\r\n", test.id(), test.name(), verdictName(result.verdict),
                       static_cast<unsigned long>(stampMs));
    }

    busy_ = wasBusy;
    return result;
}

bool TestRunner::runById(const char* id, TestResult& out) {
    ITest* const target = TestRegistry::find(id);
    if (target == nullptr) {
        return false;
    }
    out = runOne(*target);
    return true;
}

bool TestRunner::runOneById(const char* id, TestResult& out) {
    return runById(id, out);
}

void TestRunner::runSuite() {
    const bool wasBusy = busy_;
    busy_ = true;
    suiteAborted_ = false;

    ctx_.op.clearAbort();

    {
        char serialText[kReportSerialLen];
        char dateText[kReportDateLen];
        copyText(serialText, sizeof(serialText), ctx_.report.serial());
        copyText(dateText, sizeof(dateText), ctx_.report.date());
        ctx_.report.clear();
        ctx_.report.setSerial(serialText);
        ctx_.report.setDate(dateText);
    }
    ctx_.report.setMeta(ctx_.fwVersion, ctx_.boardRev);
    ctx_.safe.enterSafeState();

    const uint8_t total = TestRegistry::count();
    ctx_.io.printf("==== SELFTEST: %u teste(s) ====\r\n", static_cast<unsigned>(total));

    for (uint8_t i = 0; i < total; ++i) {
        ITest* const target = TestRegistry::at(i);
        if (target == nullptr) {
            continue;
        }
        if (ctx_.op.aborted()) {
            suiteAborted_ = true;
            break;
        }
        const TestResult result = runOne(*target);
        if (ctx_.op.aborted() || result.verdict == Verdict::Abort) {
            suiteAborted_ = true;
            break;
        }
        if (result.verdict == Verdict::Fail && target->abortsSuiteOnFail()) {
            suiteAborted_ = true;
            ctx_.io.printf("teste critico [%s] reprovou - suite interrompida\r\n", target->id());
            break;
        }
    }

    if (suiteAborted_) {
        ctx_.io.writeLine("SUITE INTERROMPIDA - relatorio parcial");
    }

    {
        char human[kHumanCap];
        ctx_.report.formatHuman(human, kHumanCap);
        ctx_.io.write(human);
    }
    {
        char csv[kCsvCap];
        ctx_.report.formatCsv(csv, kCsvCap);
        ctx_.io.write(csv);
    }

    const Status saved = ctx_.report.save(ctx_.kv);
    if (saved.failed()) {
        ctx_.io.printf("aviso: relatorio nao salvo na nvs (%s)\r\n", errName(saved.err));
    }

    ctx_.safe.enterSafeState();
    busy_ = wasBusy;
}
