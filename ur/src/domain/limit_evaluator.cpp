// Implementacao do comparador dos quatro limites. Contrato, citacoes de manual e as decisoes
// A1, A3, A4 e A5 estao em domain/limit_evaluator.h; aqui nao ha regra nova.
//
// Ordem de um ciclo valido: valida a leitura -> atualiza o teto anti-chatter -> avalia o
// predicado do estado atual (attacks() com o rele livre, releases() com o rele sinalizado) ->
// cobra o prazo -> comuta. Um ciclo invalido nao avalia nada: congela os prazos e conta para
// a falha. O construtor nasce no estado seguro, e nao no estado limpo.
#include "domain/limit_evaluator.h"

namespace domain {

LimitEvaluator::LimitEvaluator(const IClock& clockRef)
    : clock_(clockRef), channels_(), lastUpdateMs_(clockRef.nowMs()),
      invalidRun_(kInvalidCyclesToFault), linkFaulted_(true) {
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        channels_[i].rule = LimitRule();
        channels_[i].relayState = RelayState::Signalled;
        channels_[i].timing = false;
        channels_[i].ceiling = false;
        channels_[i].sinceMs = lastUpdateMs_;
        for (uint8_t k = 0; k < kChatterMemory; ++k) {
            channels_[i].attackMs[k] = lastUpdateMs_;
        }
        channels_[i].attackHead = 0;
        channels_[i].attackCount = 0;
    }
}

uint8_t LimitEvaluator::indexOf(LimitChannel channel) {
    return static_cast<uint8_t>(channel);
}

void LimitEvaluator::setRule(LimitChannel channel, const LimitRule& newRule) {
    Channel& target = channels_[indexOf(channel)];
    target.rule = newRule;
    target.timing = false;
}

RelayState LimitEvaluator::state(LimitChannel channel) const {
    return channels_[indexOf(channel)].relayState;
}

uint32_t LimitEvaluator::releaseConfirmMs(LimitChannel channel) const {
    return channels_[indexOf(channel)].ceiling ? kReleaseCeilingMs : kReleaseConfirmMs;
}

RelayMask LimitEvaluator::mask() const {
    RelayMask result = kRelayMaskAllClear;
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        if (channels_[i].relayState == RelayState::Signalled) {
            result = static_cast<RelayMask>(result | static_cast<RelayMask>(1u << i));
        }
    }
    return result;
}

RelayMask LimitEvaluator::update(const LimitInput& in) {
    const uint32_t nowMs = clock_.nowMs();
    const bool usable = in.fresh && in.x.valid() && in.y.valid();

    if (!usable) {
        freeze(nowMs);
        if (invalidRun_ < kInvalidCyclesToFault) {
            ++invalidRun_;
        }
        if (invalidRun_ >= kInvalidCyclesToFault) {
            enterFault();
        }
        lastUpdateMs_ = nowMs;
        return mask();
    }

    invalidRun_ = 0;
    linkFaulted_ = false;
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        const Angle& reading = (i < 2) ? in.x : in.y;
        evaluate(channels_[i], reading.deciDegrees(), nowMs);
    }
    lastUpdateMs_ = nowMs;
    return mask();
}

void LimitEvaluator::freeze(uint32_t nowMs) {
    const uint32_t held = elapsedMs(lastUpdateMs_, nowMs);
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        if (channels_[i].timing) {
            channels_[i].sinceMs += held;
        }
    }
}

void LimitEvaluator::enterFault() {
    linkFaulted_ = true;
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        channels_[i].relayState = RelayState::Signalled;
        channels_[i].timing = false;
    }
}

void LimitEvaluator::evaluate(Channel& channel, int16_t angleDeci, uint32_t nowMs) {
    refreshCeiling(channel, nowMs);

    const bool atRest = (channel.relayState == RelayState::Clear);
    const bool holds = atRest ? channel.rule.attacks(angleDeci) : channel.rule.releases(angleDeci);
    if (!holds) {
        channel.timing = false;
        return;
    }
    if (!channel.timing) {
        channel.timing = true;
        channel.sinceMs = nowMs;
    }

    const uint32_t spanMs =
        atRest ? kAttackConfirmMs : (channel.ceiling ? kReleaseCeilingMs : kReleaseConfirmMs);
    if (!deadlineReached(channel.sinceMs, nowMs, spanMs)) {
        return;
    }

    channel.timing = false;
    if (atRest) {
        channel.relayState = RelayState::Signalled;
        registerAttack(channel, nowMs);
        refreshCeiling(channel, nowMs);
    } else {
        channel.relayState = RelayState::Clear;
    }
}

void LimitEvaluator::registerAttack(Channel& channel, uint32_t nowMs) {
    channel.attackMs[channel.attackHead] = nowMs;
    channel.attackHead = static_cast<uint8_t>((channel.attackHead + 1u) % kChatterMemory);
    if (channel.attackCount < kChatterMemory) {
        channel.attackCount = static_cast<uint8_t>(channel.attackCount + 1u);
    }
}

void LimitEvaluator::refreshCeiling(Channel& channel, uint32_t nowMs) {
    const uint16_t attacks = attacksInWindow(channel, nowMs);
    if (attacks >= kChatterCeilingEnter) {
        channel.ceiling = true;
    } else if (attacks <= kChatterCeilingExit) {
        channel.ceiling = false;
    }
}

uint16_t LimitEvaluator::attacksInWindow(const Channel& channel, uint32_t nowMs) const {
    uint16_t total = 0;
    for (uint8_t k = 0; k < channel.attackCount; ++k) {
        if (elapsedMs(channel.attackMs[k], nowMs) < kChatterWindowMs) {
            total = static_cast<uint16_t>(total + 1u);
        }
    }
    return total;
}

}  // namespace domain
