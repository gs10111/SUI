// Sensora simulada do build de demonstracao. Ver o cabecalho: nunca vai para campo.
#include "app/demo_sensor_link.h"

namespace app {
namespace {

// Eixo X: 3,12 graus +/- 0,35 em 20 s. Eixo Y do produto (ANG_Z): -1,47 grau +/- 0,25 em 31 s.
// Periodos primos entre si para os dois eixos nao andarem juntos.
constexpr int16_t kBaseXCenti = 312;
constexpr int16_t kAmpXCenti = 35;
constexpr uint32_t kPeriodXMs = 20000;
constexpr int16_t kBaseZCenti = -147;
constexpr int16_t kAmpZCenti = 25;
constexpr uint32_t kPeriodZMs = 31000;

// Ruido uniforme de -8 a +8 centesimos: da ordem do decimo pico a pico, que e o que faz o
// quantizador de decimo alternar e o filtro da UR ter centesimo para mostrar.
constexpr uint32_t kNoiseSpan = 17;
constexpr int32_t kNoiseHalf = 8;

int16_t centiToDeci(int32_t centi) {
    return static_cast<int16_t>((centi >= 0) ? (centi + 5) / 10 : -((-centi + 5) / 10));
}

}  // namespace

DemoSensorLink::DemoSensorLink(const IClock& clock)
    : clock_(clock), stats_(), sentAtMs_(0), noise_(0x2545F491u), heartbeat_(0),
      waiting_(false) {}

Status DemoSensorLink::begin() {
    waiting_ = false;
    return kOk;
}

Status DemoSensorLink::request() {
    sentAtMs_ = clock_.nowMs();
    waiting_ = true;
    ++stats_.requests;
    return kOk;
}

LinkPoll DemoSensorLink::poll(SensorSample& out) {
    if (!waiting_) {
        return LinkPoll::Idle;
    }
    const uint32_t agora = clock_.nowMs();
    if (agora - sentAtMs_ < kTurnaroundMs) {
        return LinkPoll::Busy;
    }
    waiting_ = false;
    ++stats_.fresh;
    ++heartbeat_;
    SensorSample s{};
    s.xDeci = centiToDeci(centiAt(agora, kBaseXCenti, kAmpXCenti, kPeriodXMs));
    s.yDeci = 0;
    s.zDeci = centiToDeci(centiAt(agora, kBaseZCenti, kAmpZCenti, kPeriodZMs));
    s.status = kStsDataValid;
    s.tempDeciC = 250;
    s.whoAmI = 0x00C1;
    s.fwVersion = 0x0002;
    s.heartbeat = heartbeat_;
    s.atMs = agora;
    out = s;
    return LinkPoll::Fresh;
}

void DemoSensorLink::abort() { waiting_ = false; }

int16_t DemoSensorLink::centiAt(uint32_t tMs, int16_t baseCenti, int16_t ampCenti,
                                uint32_t periodMs) {
    // Triangulo de -amp a +amp: sobe na primeira metade do periodo, desce na segunda.
    const uint32_t fase = tMs % periodMs;
    const uint32_t meio = periodMs / 2u;
    const int32_t amp = ampCenti;
    const int32_t subida = (fase < meio)
                               ? static_cast<int32_t>((fase * 2u * static_cast<uint32_t>(amp)) / meio)
                               : static_cast<int32_t>(((periodMs - fase) * 2u *
                                                       static_cast<uint32_t>(amp)) / meio);
    const int32_t triangulo = subida - amp;
    const int32_t ruido = static_cast<int32_t>(nextNoise() % kNoiseSpan) - kNoiseHalf;
    return static_cast<int16_t>(static_cast<int32_t>(baseCenti) + triangulo + ruido);
}

// xorshift32: deterministico, sem estado global, suficiente para ruido de demonstracao.
uint32_t DemoSensorLink::nextNoise() {
    uint32_t x = noise_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    noise_ = x;
    return x;
}

}  // namespace app
