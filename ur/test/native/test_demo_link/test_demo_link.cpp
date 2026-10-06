// Sensora SIMULADA do build de demonstracao (env:esp32dev-demo). Existe so para mostrar a tela a
// um cliente sem sensora na bancada; o env de producao nao a instancia.
//
// O que estes testes exigem dela:
//   - fala o protocolo da porta como a sensora real: Idle sem pedido, Busy durante o tempo de
//     ida e volta, Fresh depois, com status valido e batimento avancando;
//   - entrega decimo QUANTIZADO, como o fio real - o centesimo continua vindo do filtro da UR;
//   - o sinal varia (rampa lenta mais ruido de alguns centesimos), para que a indicacao de duas
//     casas tenha o que mostrar, e fica abaixo dos 5,0 graus de fabrica, para os reles ficarem
//     quietos ate alguem programar um limite;
//   - e deterministica: a mesma sequencia de relogio da a mesma sequencia de amostras.
#include <unity.h>

#include "app/application.h"
#include "app/demo_sensor_link.h"
#include "fakes/fake_analog_output.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_relay_bank.h"
#include "fakes/fake_watchdog.h"

using app::DemoSensorLink;
using test::FakeClock;

void setUp(void) {}
void tearDown(void) {}

namespace {

// Um pedido completo: request, espera o tempo de ida e volta, poll.
LinkPoll pedir(FakeClock& relogio, DemoSensorLink& link, SensorSample& out) {
    TEST_ASSERT_TRUE(link.request().ok());
    relogio.advanceMs(DemoSensorLink::kTurnaroundMs);
    return link.poll(out);
}

}  // namespace

static void test_sem_pedido_nao_ha_resposta(void) {
    FakeClock relogio;
    DemoSensorLink link(relogio);
    TEST_ASSERT_TRUE(link.begin().ok());
    SensorSample s{};
    TEST_ASSERT_TRUE(link.poll(s) == LinkPoll::Idle);
    TEST_ASSERT_FALSE(link.busy());
}

static void test_responde_depois_do_tempo_de_ida_e_volta(void) {
    FakeClock relogio;
    DemoSensorLink link(relogio);
    TEST_ASSERT_TRUE(link.begin().ok());
    TEST_ASSERT_TRUE(link.request().ok());
    SensorSample s{};
    TEST_ASSERT_TRUE(link.poll(s) == LinkPoll::Busy);
    TEST_ASSERT_TRUE(link.busy());
    relogio.advanceMs(DemoSensorLink::kTurnaroundMs);
    TEST_ASSERT_TRUE(link.poll(s) == LinkPoll::Fresh);
    TEST_ASSERT_FALSE(link.busy());
    TEST_ASSERT_EQUAL_HEX16(kStsDataValid, s.status);
    TEST_ASSERT_EQUAL_HEX16(0x00C1, s.whoAmI);
    TEST_ASSERT_EQUAL_UINT32(relogio.nowMs(), s.atMs);
    TEST_ASSERT_TRUE(DemoSensorLink::kTurnaroundMs < link.timeoutMs());
}

static void test_batimento_avanca_a_cada_amostra(void) {
    FakeClock relogio;
    DemoSensorLink link(relogio);
    TEST_ASSERT_TRUE(link.begin().ok());
    SensorSample a{};
    SensorSample b{};
    TEST_ASSERT_TRUE(pedir(relogio, link, a) == LinkPoll::Fresh);
    relogio.advanceMs(30);
    TEST_ASSERT_TRUE(pedir(relogio, link, b) == LinkPoll::Fresh);
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(a.heartbeat + 1u), b.heartbeat);
}

// Dois minutos de amostras a 50 ms: o sinal fica na faixa prometida, varia entre decimos e nunca
// passa dos 5,0 graus de fabrica.
static void test_sinal_varia_e_fica_abaixo_do_limite_de_fabrica(void) {
    FakeClock relogio;
    DemoSensorLink link(relogio);
    TEST_ASSERT_TRUE(link.begin().ok());
    int16_t minX = 32767;
    int16_t maxX = -32768;
    int16_t minZ = 32767;
    int16_t maxZ = -32768;
    for (uint32_t i = 0; i < 2400u; ++i) {
        SensorSample s{};
        TEST_ASSERT_TRUE(pedir(relogio, link, s) == LinkPoll::Fresh);
        relogio.advanceMs(50 - DemoSensorLink::kTurnaroundMs);
        minX = (s.xDeci < minX) ? s.xDeci : minX;
        maxX = (s.xDeci > maxX) ? s.xDeci : maxX;
        minZ = (s.zDeci < minZ) ? s.zDeci : minZ;
        maxZ = (s.zDeci > maxZ) ? s.zDeci : maxZ;
    }
    TEST_ASSERT_TRUE_MESSAGE(maxX - minX >= 5, "X tem de andar, senao a tela fica parada");
    TEST_ASSERT_TRUE_MESSAGE(maxZ - minZ >= 3, "Y (ANG_Z) tem de andar");
    TEST_ASSERT_TRUE(maxX < 50 && minX > -50);
    TEST_ASSERT_TRUE(maxZ < 50 && minZ > -50);
}

static void test_mesma_sequencia_de_relogio_da_as_mesmas_amostras(void) {
    FakeClock r1;
    FakeClock r2;
    DemoSensorLink a(r1);
    DemoSensorLink b(r2);
    TEST_ASSERT_TRUE(a.begin().ok());
    TEST_ASSERT_TRUE(b.begin().ok());
    for (int i = 0; i < 200; ++i) {
        SensorSample sa{};
        SensorSample sb{};
        pedir(r1, a, sa);
        pedir(r2, b, sb);
        TEST_ASSERT_EQUAL_INT16(sa.xDeci, sb.xDeci);
        TEST_ASSERT_EQUAL_INT16(sa.zDeci, sb.zDeci);
        r1.advanceMs(32);
        r2.advanceMs(32);
    }
}

// De ponta a ponta com a aplicacao real: o enlace fica Ok, os reles quietos, e o centesimo do
// filtro sai do multiplo de 10 em algum momento - que e o que o cliente vai ver na tela.
static void test_aplicacao_com_a_sensora_simulada_mostra_centesimo(void) {
    FakeClock relogio;
    DemoSensorLink link(relogio);
    test::FakeRelayBank reles(relogio, true);
    test::FakeAnalogOutput analogico;
    test::FakeWatchdog wdt;
    app::Application aplicacao(relogio, link, reles, analogico, wdt);
    TEST_ASSERT_TRUE(link.begin().ok());
    TEST_ASSERT_TRUE(reles.begin().ok());
    TEST_ASSERT_TRUE(analogico.begin().ok());
    TEST_ASSERT_TRUE(aplicacao.begin(domain::Parameters::factoryDefaults()).ok());

    bool centesimoVivo = false;
    for (uint32_t i = 0; i < 1200u; ++i) {
        const uint32_t t0 = relogio.nowMs();
        aplicacao.applyPublished();
        aplicacao.startCycle();
        uint16_t guarda = 0;
        while (!aplicacao.pollCycle()) {
            relogio.advanceMs(1);
            ++guarda;
            TEST_ASSERT_TRUE_MESSAGE(guarda < 200u, "pollCycle nunca fechou");
        }
        aplicacao.finishCycle();
        aplicacao.latchSnapshot();
        const uint32_t usado = relogio.nowMs() - t0;
        if (usado < app::Application::kCyclePeriodMs) {
            relogio.advanceMs(app::Application::kCyclePeriodMs - usado);
        }
        const app::Application::Snapshot snap = aplicacao.snapshot();
        if (snap.reading[0].valid() && (snap.readingCenti[0] % 10) != 0) {
            centesimoVivo = true;
        }
    }
    TEST_ASSERT_EQUAL_UINT8(kRelayMaskAllClear, aplicacao.snapshot().relayMask);
    TEST_ASSERT_TRUE(aplicacao.link() == app::LinkHealth::Ok);
    TEST_ASSERT_TRUE_MESSAGE(centesimoVivo, "a demonstracao tem de mostrar o centesimo andando");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sem_pedido_nao_ha_resposta);
    RUN_TEST(test_responde_depois_do_tempo_de_ida_e_volta);
    RUN_TEST(test_batimento_avanca_a_cada_amostra);
    RUN_TEST(test_sinal_varia_e_fica_abaixo_do_limite_de_fabrica);
    RUN_TEST(test_mesma_sequencia_de_relogio_da_as_mesmas_amostras);
    RUN_TEST(test_aplicacao_com_a_sensora_simulada_mostra_centesimo);
    return UNITY_END();
}
