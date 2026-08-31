// src/ports/i_indicator.h
// Sinalizacao luminosa CONTROLAVEL por firmware. Na DE-PURI-DI261924 existe UMA
// unica: CN4-1 (net LED_TEST = IO2), que e o "LED LIG" do manual. Os quatro LEDs
// de limite do CN3 penduram no MESMO net da base do BC337 de cada rele: nao ha
// como acende-los sem acionar o rele, portanto eles NAO pertencem a esta porta -
// pertencem a IRelayBank e sao efeito colateral fisico dela.
// Alvo: GpioIndicator (src/platform/gpio_indicator.cpp) - IO2, ativo alto,
//       cadencia derivada de IClock; IO2 e pino de strapping, escrito so depois do boot.
// Fake: FakeIndicator (test/native) - guarda o padrao corrente e conta transicoes.
// REQ:  MAN-5-L67 ("o LED LIG e acionado, indicando que a UR esta alimentada e em
//       operacao"), decisao 8 item H (piscar 2 Hz enquanto durar a falha de link).
#pragma once

#include <stdint.h>

#include "status.h"

enum class IndicatorId : uint8_t {
    Power = 0,  // CN4-1 "LED LIG"
};

constexpr uint8_t kIndicatorCount = 1;

enum class IndicatorPattern : uint8_t {
    Off = 0,      // apagado
    On,           // aceso continuo: equipamento vivo e link sadio
    Blink1Hz,     // 500 ms aceso / 500 ms apagado
    Blink2Hz,     // 250 ms / 250 ms: falha de comunicacao com a sensora
};

class IIndicator {
public:
    virtual ~IIndicator() = default;
    IIndicator(const IIndicator&) = delete;
    IIndicator& operator=(const IIndicator&) = delete;

    // Deixa o pino em nivel definido (apagado) antes de virar saida.
    virtual Status begin() = 0;

    virtual uint8_t count() const = 0;
    virtual Status set(IndicatorId id, IndicatorPattern pattern) = 0;
    virtual IndicatorPattern pattern(IndicatorId id) const = 0;

    // Avanca a cadencia de piscar. Chamada uma vez por ciclo de controle.
    // Nao bloqueia e nao decide nada: quem escolhe o padrao e o dominio.
    virtual void service() = 0;

    // Nivel eletrico corrente do pino, para o roteiro de fabrica. NAO e leitura
    // de volta do LED: nao ha realimentacao optica nesta placa.
    virtual bool driven(IndicatorId id) const = 0;

protected:
    IIndicator() = default;
};
