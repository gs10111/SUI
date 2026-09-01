// src/app/application.cpp
// Implementacao do ciclo de controle descrito em application.h. Tudo aqui e inteiro: decimos de
// grau em int16, estado do filtro em Q8 dentro do LowPassFilter, codigo de DAC em uint16. A
// unica fonte de tempo e o IClock injetado, e a unica fonte de dado angular e a transacao
// Modbus da porta ISensorLink. A ordem do finishCycle() e a ordem da base comum: aceitacao da
// amostra -> saude do enlace -> um passo do filtro por eixo -> Sentido e Preset -> avaliacao dos
// quatro limites -> escrita dos quatro reles -> escrita das duas saidas analogicas -> token de
// liveness do watchdog. O batimento vem por ultimo de proposito: renovar o token antes de
// escrever os reles diria ao STWD100 que o ciclo de seguranca fechou quando ele ainda nao
// fechou.
#include "app/application.h"

#include "domain/ui/preset_wizard.h"

namespace app {

Application::Application(const IClock& clockRef, ISensorLink& linkRef, IRelayBank& relayRef,
                         IAnalogOutput& analogRef, IWatchdog& watchdogRef)
    : clock_(clockRef),
      link_(linkRef),
      relays_(relayRef),
      analog_(analogRef),
      watchdog_(watchdogRef),
      active_(domain::Parameters::factoryDefaults()),
      pending_(),
      evaluator_(clockRef),
      filter_{{domain::LowPassFilter::kTauDefaultMs, kCyclePeriodMs},
              {domain::LowPassFilter::kTauDefaultMs, kCyclePeriodMs}},
      scaler_(),
      raw_(),
      reading_(),
      pub_(),
      sample_(),
      cycleStartMs_(clockRef.nowMs()),
      faultSinceMs_(clockRef.nowMs()),
      lastGoodMs_(clockRef.nowMs()),
      pendingCommitMs_(0),
      overrideSinceMs_{clockRef.nowMs(), clockRef.nowMs()},
      faultStampMs_(),
      faultStampCount_(0),
      faultStampHead_(0),
      cycles_(0),
      faultEvents_(0),
      relayErrors_(0),
      analogErrors_(0),
      overrideCode_{domain::AnalogScaler::kFaultCode, domain::AnalogScaler::kFaultCode},
      analogCode_{domain::AnalogScaler::kFaultCode, domain::AnalogScaler::kFaultCode},
      sensorStatus_(0),
      lastBeat_(0),
      verdict_(LinkPoll::Idle),
      link_state_(LinkHealth::Awaiting),
      badRun_(0),
      goodRun_(0),
      overrideActive_{false, false},
      pendingValid_(false),
      pendingConfigLatched_(false),
      pendingConfigLatchedValid_(false),
      pendingLinkLatchClear_(false),
      pendingCommitValid_(false),
      commitCredit_(false),
      haveBeat_(false),
      reloadPending_(true),
      cycleOpen_(false),
      configLatched_(false),
      linkLatched_(false),
      analogDead_(false),
      stale_(false),
      relayBankDead_(false) {
    // O buffer publicado nasce coerente com o estado seguro: a IHM pode chamar snapshot()
    // antes do primeiro ciclo da tarefa ctrl e tem de ler "aguardando, quatro reles em alarme,
    // duas saidas em 3932", nunca um struct zerado que pareceria enlace saudavel em 0,0 grau.
    latchSnapshot();
}

uint8_t Application::axisIndex(domain::Axis axis) {
    return (axis == domain::Axis::Y) ? 1u : 0u;
}

const domain::LimitOps& Application::opsFor(domain::LimitOp op) {
    switch (op) {
        case domain::LimitOp::GreaterEqual: return domain::ops::kAtLeast;
        case domain::LimitOp::LessEqual: return domain::ops::kAtMost;
        case domain::LimitOp::Absolute: return domain::ops::kModulus;
        case domain::LimitOp::Off: break;
    }
    return domain::ops::kOff;
}

Status Application::begin(const domain::Parameters& params) {
    active_ = params;
    pendingValid_ = false;
    applyRules();
    applyScalers();
    reloadPending_ = true;
    haveBeat_ = false;
    badRun_ = 0;
    goodRun_ = 0;
    link_state_ = LinkHealth::Awaiting;
    faultSinceMs_ = clock_.nowMs();
    lastGoodMs_ = clock_.nowMs();
    stale_ = false;
    latchSnapshot();
    return kOk;
}

void Application::publishParameters(const domain::Parameters& params) {
    pending_ = params;
    pendingValid_ = true;
}

void Application::requestAnalogOverride(domain::Axis axis, uint16_t code) {
    const uint8_t i = axisIndex(axis);
    overrideCode_[i] = code;
    overrideActive_[i] = true;
    // Carimbo renovado a CADA pedido: enquanto a IHM viver ela republica o override a cada
    // passagem de 50 ms e o prazo nunca vence. Quem morre e quem perde o eixo.
    overrideSinceMs_[i] = clock_.nowMs();
}

void Application::clearAnalogOverride(domain::Axis axis) {
    overrideActive_[axisIndex(axis)] = false;
}

void Application::setConfigLatched(bool latched) {
    pendingConfigLatched_ = latched;
    pendingConfigLatchedValid_ = true;
}

void Application::initConfigLatched(bool latched) {
    configLatched_ = latched;
    pendingConfigLatchedValid_ = false;
}

void Application::clearLinkLatch() {
    // Pedido, nao escrita. Quem apaga linkLatched_ e applyPublished(), sob o portMUX, na mesma
    // tarefa que o escreve em updateHealth() - senao o clear do loop() e o set da ctrl se
    // atropelam e um dos dois se perde.
    pendingLinkLatchClear_ = true;
}

void Application::noteCommitWindow(uint32_t elapsedMs) {
    if (elapsedMs > kNvsCommitBudgetMs) {
        return;
    }
    pendingCommitMs_ = elapsedMs;
    pendingCommitValid_ = true;
}

void Application::noteFaultEntry(uint32_t nowMs) {
    ++faultEvents_;
    faultStampMs_[faultStampHead_] = nowMs;
    faultStampHead_ = static_cast<uint8_t>((faultStampHead_ + 1u) % kFaultsToLatch);
    if (faultStampCount_ < kFaultsToLatch) {
        ++faultStampCount_;
    }
    if (faultStampCount_ < kFaultsToLatch) {
        return;
    }
    // faultStampHead_ aponta agora para a entrada MAIS ANTIGA das cinco guardadas. Se as cinco
    // couberem na janela, trava. elapsedMs() e a subtracao unsigned de ports/i_clock.h, entao a
    // conta atravessa o wrap de 2^32 ms sem caso especial.
    const uint32_t oldestMs = faultStampMs_[faultStampHead_];
    if (elapsedMs(oldestMs, nowMs) <= kFlapWindowMs) {
        linkLatched_ = true;
    }
}

void Application::setFilterTimeConstant(uint16_t timeConstantMs) {
    for (uint8_t i = 0; i < kAppAxisCount; ++i) {
        filter_[i].setTimeConstant(timeConstantMs);
    }
}

void Application::applyPublished() {
    if (pendingConfigLatchedValid_) {
        configLatched_ = pendingConfigLatched_;
        pendingConfigLatchedValid_ = false;
    }
    if (pendingLinkLatchClear_) {
        linkLatched_ = false;
        // A janela tambem zera. Rearmar e dizer "vi e tratei"; deixar quatro carimbos velhos no
        // anel faria a proxima falha isolada travar de novo na hora, e o operador teria de
        // rearmar a cada evento - que e o ponteamento em campo que A11 descreve.
        faultStampCount_ = 0;
        faultStampHead_ = 0;
        pendingLinkLatchClear_ = false;
    }
    if (pendingCommitValid_) {
        commitCredit_ = true;
        pendingCommitValid_ = false;
    }
    if (!pendingValid_) {
        return;
    }
    active_ = pending_;
    pendingValid_ = false;
    applyRules();
    applyScalers();
}

void Application::applyRules() {
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        const domain::LimitId id = static_cast<domain::LimitId>(i);
        const domain::Angle value = active_.limitValue(id);
        const int16_t deci = value.valid() ? value.deciDegrees() : static_cast<int16_t>(0);
        const domain::LimitRule rule(opsFor(active_.limitOp(id)), deci);
        evaluator_.setRule(static_cast<LimitChannel>(i), rule);
    }
}

void Application::applyScalers() {
    for (uint8_t i = 0; i < kAppAxisCount; ++i) {
        const domain::Axis axis = static_cast<domain::Axis>(i);
        const domain::Angle full = active_.calFullScale(axis);
        const int16_t fullDeci =
            full.valid() ? full.deciDegrees() : domain::AnalogScaler::kFactoryFullScaleDeci;
        domain::AnalogScaler built;
        if (domain::AnalogScaler::make(active_.calZeroCode(axis), active_.calFullScaleCode(axis),
                                       fullDeci, built)) {
            scaler_[i] = built;
        } else {
            scaler_[i] = domain::AnalogScaler();
        }
    }
}

void Application::noteStall(uint32_t delayMs) {
    if (delayMs <= kNvsCommitBudgetMs) {
        // Tick perdido dentro do orcamento do commit de NVS: NAO conta como transacao
        // invalida e nao mexe nos contadores de saude (decisao 2 item 16). A guarda dura de
        // idade continua valendo por cima disso e e ela que protege o rele.
        return;
    }
    const uint32_t nowMs = clock_.nowMs();
    if (link_state_ != LinkHealth::CommFault) {
        noteFaultEntry(nowMs);
    }
    link_state_ = LinkHealth::CommFault;
    faultSinceMs_ = nowMs;
    reloadPending_ = true;
    goodRun_ = 0;
    badRun_ = kFailsToFault;
}

void Application::startCycle() {
    // NAO ha applyPublished() aqui: quem o chama e a tarefa ctrl, sozinha, dentro da seccao
    // critica que a IHM tambem toma. Ver o cabecalho e ctrlTask() em src/main.cpp.
    cycleStartMs_ = clock_.nowMs();
    verdict_ = LinkPoll::Idle;
    cycleOpen_ = true;
    if (link_.busy()) {
        link_.abort();
    }
    const Status st = link_.request();
    if (st.failed()) {
        verdict_ = LinkPoll::Timeout;
        cycleOpen_ = false;
    }
}

bool Application::pollCycle() {
    if (!cycleOpen_) {
        return true;
    }
    SensorSample incoming{};
    const LinkPoll verdict = link_.poll(incoming);
    if (verdict != LinkPoll::Busy) {
        verdict_ = verdict;
        if (verdict == LinkPoll::Fresh) {
            sample_ = incoming;
        }
        cycleOpen_ = false;
        return true;
    }
    if (deadlineReached(cycleStartMs_, clock_.nowMs(), kCycleGuardMs)) {
        link_.abort();
        verdict_ = LinkPoll::Timeout;
        cycleOpen_ = false;
        return true;
    }
    return false;
}

bool Application::accept(const SensorSample& sample, uint32_t nowMs) const {
    if (sample.status != kAcceptedStatus) {
        return false;
    }
    if (elapsedMs(sample.atMs, nowMs) > kDataMaxAgeMs) {
        return false;
    }
    return domain::Angle::fromDeciDegrees(sample.xDeci).valid() &&
           domain::Angle::fromDeciDegrees(sample.yDeci).valid();
}

void Application::updateHealth(bool good, uint32_t nowMs) {
    if (good) {
        badRun_ = 0;
        if (link_state_ == LinkHealth::Awaiting) {
            link_state_ = LinkHealth::Ok;
            goodRun_ = 0;
            return;
        }
        if (link_state_ == LinkHealth::Ok) {
            return;
        }
        if (goodRun_ < 0xFFu) {
            ++goodRun_;
        }
        if (goodRun_ >= kGoodsToRecover &&
            deadlineReached(faultSinceMs_, nowMs, kFaultMinDwellMs)) {
            link_state_ = LinkHealth::Ok;
            goodRun_ = 0;
        }
        return;
    }
    goodRun_ = 0;
    if (badRun_ < kFailsToFault) {
        ++badRun_;
    }
    if (badRun_ < kFailsToFault) {
        return;
    }
    if (link_state_ == LinkHealth::CommFault || link_state_ == LinkHealth::SensorFault) {
        return;
    }
    link_state_ = (verdict_ == LinkPoll::Fresh) ? LinkHealth::SensorFault : LinkHealth::CommFault;
    faultSinceMs_ = nowMs;
    reloadPending_ = true;
    noteFaultEntry(nowMs);
}

void Application::driveRelays(bool fresh) {
    domain::LimitInput in{};
    in.x = reading_[0];
    in.y = reading_[1];
    in.fresh = fresh && !configLatched_;
    // O avaliador roda SEMPRE, mesmo com a guarda de idade ligada: ele e quem mede os prazos de
    // ataque e liberacao de A3 e nao pode perder ciclos. Mas a guarda dura passa por cima do
    // veredito dele - decisao 5 item 30, "os quatro reles vao ao estado de ALARME,
    // INDEPENDENTEMENTE do contador de transacoes invalidas". Sem isto o alarme ainda esperaria
    // os 3 ciclos de kInvalidCyclesToFault (150 ms) que o avaliador exige, e sao justamente
    // esses 150 ms que a guarda existe para nao pagar depois de um bloqueio.
    RelayMask wanted = evaluator_.update(in);
    // A7: o LATCH DE FLAPPING passa por cima do veredito exatamente como a guarda dura de idade,
    // e pela MESMA razao - sem ele o alarme ainda esperaria os 3 ciclos (150 ms) que o avaliador
    // leva para declarar falha por conta propria, e sao justamente esses 150 ms que uma falha ja
    // TRAVADA nao pode pagar. O avaliador continua rodando com dado fresco de verdade (nao ha
    // "&& !linkLatched_" na linha do in.fresh acima): assim os prazos de ataque e liberacao de A3
    // seguem medindo o angulo real durante todo o tempo em que o latch estiver armado, e no
    // instante do rearme o veredito dele ja e o correto, em vez de ter ficado congelado.
    // Repare no que este latch NAO faz: nao segura link_state_ em falha. A saude do enlace continua
    // sendo medida e publicada com honestidade - o operador precisa ver que o cabo voltou - e o
    // que fica travado sao os QUATRO RELES e as duas saidas, ate o rearme humano. E isso que
    // NormalScreen desenha na combinacao "link == Ok com linkLatched armado" (normal_screen.cpp,
    // o texto de rearme subindo para a primeira linha) e o que docs/ihm-estados.md B7 descreve:
    // "com o enlace ja recuperado e o latch ainda armado ... a tela continua sendo B7, porque os
    // quatro reles continuam em alarme ate o rearme".
    if (stale_ || linkLatched_) {
        wanted = kRelayMaskAllSignalled;
    }
    if (relays_.applyMask(wanted).failed()) {
        ++relayErrors_;
        if (relays_.signalAll().failed()) {
            ++relayErrors_;
            // Os dois caminhos de escrita reprovaram no mesmo ciclo. O unico estado em que
            // isso acontece no adaptador real e ready_ == false, ou seja, begin() nao
            // conseguiu PROVAR que comanda os quatro pinos - e dai em diante todas as escritas
            // devolvem NotInit para sempre, com os quatro reles congelados no nivel que o
            // begin() escreveu. Nao ha para onde escalar dentro do firmware: a unica saida com
            // sinalizacao e parar de renovar o token de liveness e deixar o STWD100 resetar a
            // placa. Na polaridade fail-safe o reset desenergiza as quatro bobinas = alarme.
            // O defeito e LATCHADO: um banco que reprovou nao volta a ser confiavel.
            relayBankDead_ = true;
        }
    }
}

void Application::driveAnalog(uint32_t nowMs) {
    for (uint8_t i = 0; i < kAppAxisCount; ++i) {
        // Prazo do override ANTES do switch: IHM que parou de republicar perde o eixo.
        if (overrideActive_[i] && deadlineReached(overrideSinceMs_[i], nowMs, kOverrideMaxAgeMs)) {
            overrideActive_[i] = false;
        }
        if (configLatched_) {
            analogCode_[i] = analog_.faultCode();
        } else if (overrideActive_[i]) {
            analogCode_[i] = overrideCode_[i];
        } else if (stale_ || linkLatched_ || link_state_ != LinkHealth::Ok ||
                   !reading_[i].valid()) {
            analogCode_[i] = analog_.faultCode();
        } else {
            analogCode_[i] = scaler_[i].codeFor(reading_[i]);
        }
    }
    if (analog_.writeBoth(analogCode_[0], analogCode_[1]).failed()) {
        ++analogErrors_;
        // NEM TODA REPROVACAO E MORTE, e confundir as duas seria pior que nao olhar. O adaptador
        // devolve Err::Range quando GRAMPEOU um codigo na faixa util e escreveu assim mesmo: a
        // escrita valeu, o DAC esta vivo, e latchar ali derrubaria o canal por um grampo
        // legitimo. Morte e o adaptador deixar de estar PRONTO - begin() que nao conseguiu
        // provar a cadeia SPI/DAC8562 - e dai em diante toda escrita devolve NotInit para
        // sempre. ready() da porta e exatamente essa pergunta, e este e o unico lugar do
        // produto que precisa faze-la.
        if (!analog_.ready()) {
            analogDead_ = true;
        }
    }
}

void Application::finishCycle() {
    const uint32_t nowMs = clock_.nowMs();
    bool good = false;
    if (verdict_ == LinkPoll::Fresh) {
        good = accept(sample_, nowMs);
        if (good) {
            sensorStatus_ = sample_.status;
            if (haveBeat_ && sample_.heartbeat < lastBeat_) {
                reloadPending_ = true;
            }
            lastBeat_ = sample_.heartbeat;
            haveBeat_ = true;
        }
    }

    updateHealth(good, nowMs);

    // GUARDA DURA DE IDADE (decisao 5 item 30). Medida ANTES do carimbo da amostra desta
    // passagem, de proposito: e o ciclo que VOLTA de um bloqueio quem observa que o dado
    // envelheceu, e carimbar primeiro apagaria a evidencia sem que nenhum rele fosse tocado.
    if (commitCredit_) {
        // Janela de commit de NVS anunciada: o envelhecimento desta passagem foi declarado e os
        // reles ja estao congelados pela decisao 6. O credito vale UM ciclo e reancora o relogio
        // da guarda; a partir do proximo ciclo ela volta a valer inteira.
        commitCredit_ = false;
        stale_ = false;
        lastGoodMs_ = nowMs;
    } else {
        stale_ = deadlineReached(lastGoodMs_, nowMs, kHardStaleMs);
        if (good) {
            lastGoodMs_ = nowMs;
        }
    }

    for (uint8_t i = 0; i < kAppAxisCount; ++i) {
        const int16_t deci = (i == 0) ? sample_.xDeci : sample_.yDeci;
        const domain::Angle fresh =
            good ? domain::Angle::fromDeciDegrees(deci) : domain::Angle::invalid();
        raw_[i] = fresh;
        domain::Angle filtered;
        if (good && reloadPending_) {
            filter_[i].reload(fresh);
            filtered = filter_[i].value();
        } else {
            filtered = filter_[i].update(fresh);
        }
        reading_[i] = domain::ui::PresetWizard::reading(static_cast<domain::Axis>(i), filtered,
                                                        active_);
    }
    if (good && reloadPending_) {
        reloadPending_ = false;
    }

    driveRelays(good && link_state_ == LinkHealth::Ok && !stale_);
    driveAnalog(nowMs);

    ++cycles_;
    // O batimento e a ULTIMA acao do ciclo, e so sai se o banco de reles ainda responde:
    // renovar o token com o banco mudo diria ao STWD100 que o ciclo de seguranca fechou
    // quando ele nao fechou em lugar nenhum.
    if (!relayBankDead_) {
        watchdog_.heartbeat();
    }
}

void Application::latchSnapshot() {
    Snapshot out{};
    for (uint8_t i = 0; i < kAppAxisCount; ++i) {
        out.reading[i] = reading_[i];
        out.raw[i] = raw_[i];
        out.overriding[i] = overrideActive_[i];
        out.analogCode[i] = analogCode_[i];
    }
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        out.limitState[i] = evaluator_.state(static_cast<LimitChannel>(i));
    }
    // O que foi REALMENTE escrito no banco, nao o que o avaliador queria. Com applyMask()
    // reprovando, evaluator_.mask() mostraria a mascara desejada e a IHM mentiria sobre o
    // estado do hardware - o painel dizendo "sem alarme" com os quatro reles congelados.
    out.relayMask = relays_.mask();
    out.link = link_state_;
    out.sensorStatus = sensorStatus_;
    out.cycles = cycles_;
    out.faultEvents = faultEvents_;
    out.relayWriteErrors = relayErrors_;
    out.analogWriteErrors = analogErrors_;
    out.configLatched = configLatched_;
    out.linkLatched = linkLatched_;
    out.analogDead = analogDead_;
    out.stale = stale_;
    out.relayBankDead = relayBankDead_;
    pub_ = out;
}

Application::Snapshot Application::snapshot() const {
    return pub_;
}

}  // namespace app
