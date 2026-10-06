// app/demo_sensor_link.h
// Sensora SIMULADA, so para DEMONSTRACAO da tela a um cliente sem sensora na bancada.
//
// ONDE ELA EXISTE. Apenas no env:esp32dev-demo (UR_DEMO_SENSOR=1), que troca o enlace que a
// aplicacao consome por esta classe. O env de producao (esp32dev) nao a instancia, e o env de
// demo nao gera pacote .ota: um firmware de demonstracao nao tem caminho para entrar na frota
// pelo portal. O splash e o console dizem "DEMO".
//
// O QUE ELA NAO E. Nao e medicao. Os reles e a saida analogica obedecem a ela como obedeceriam a
// sensora real - e o objetivo, a tela tem de se comportar igual -, entao uma placa com este
// firmware NUNCA vai para campo.
//
// O SINAL. Por eixo, uma rampa triangular lenta (dezenas de segundos) mais um ruido de alguns
// centesimos, quantizado em DECIMO como no fio real: o centesimo da indicacao de duas casas
// continua saindo do filtro da UR (Decisao 18, Emenda 1), e o ruido e o que da a ele o que
// mostrar. Tudo abaixo dos 5,0 graus de fabrica, para os reles ficarem quietos ate alguem
// programar um limite. Inteiro e deterministico: mesmo relogio, mesmas amostras.
#pragma once

#include <stdint.h>

#include "ports/i_clock.h"
#include "ports/i_sensor_link.h"

namespace app {

class DemoSensorLink final : public ISensorLink {
public:
    // Ida e volta tipica do enlace real a 19200 baud (18 ms), dentro do prazo de 30 ms.
    static constexpr uint32_t kTurnaroundMs = 18;
    static constexpr uint32_t kTimeoutMs = 30;

    explicit DemoSensorLink(const IClock& clock);

    Status begin() override;
    Status request() override;
    LinkPoll poll(SensorSample& out) override;
    void abort() override;
    bool busy() const override { return waiting_; }
    uint32_t timeoutMs() const override { return kTimeoutMs; }
    uint32_t baud() const override { return 19200u; }
    uint8_t slaveAddress() const override { return 1u; }
    const char* protocolName() const override { return "DEMO (sensora simulada)"; }
    const LinkStats& stats() const override { return stats_; }
    void resetStats() override { stats_ = LinkStats{}; }

private:
    // Centesimos de um eixo no instante t: base + triangulo(amplitude, periodo) + ruido.
    int16_t centiAt(uint32_t tMs, int16_t baseCenti, int16_t ampCenti, uint32_t periodMs);
    uint32_t nextNoise();

    const IClock& clock_;
    LinkStats stats_;
    uint32_t sentAtMs_;
    uint32_t noise_;
    uint16_t heartbeat_;
    bool waiting_;
};

}  // namespace app
