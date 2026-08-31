// Prova de contrato do FakeSensorLink (LSP) contra ports/i_sensor_link.h.
//
// Este arquivo existe porque o dominio do enlace (LinkSupervisor: 3 ciclos reprovados = os
// quatro reles em alarme, 10 aprovados restabelecem) vai ser calibrado contra o fake e rodar
// na placa contra o ModbusSensorLink. Se os dois discordarem em UM veredito, o supervisor
// passa no host e falha no patio - ou, no sentido perigoso, enlace degradado passa por
// saudavel com os reles no estado normal.
//
// Cada teste aqui prende um ponto em que fake e adaptador tem de coincidir, e cada ponto tem
// contrapartida verificavel em src/adapters/modbus_sensor_link.cpp:
//  - Fresh so com chegada DENTRO do prazo (adaptador: teste de deadline sobre lastRxMs_ antes
//    de preencher 'out');
//  - prazo contado do PRIMEIRO byte transmitido (adaptador: sentAtMs_ carimbado antes do
//    uart_write_bytes), 35 ms = urbase::kLinkTimeoutMs;
//  - um veredito por request(), inclusive quando a transmissao falha (failPending_ -> Timeout,
//    nunca Idle);
//  - quadro ruim so vira BadFrame no fim do prazo, porque o adaptador continua consumindo o
//    anel atras da resposta boa;
//  - 'out' intocado em tudo que nao for Fresh, e nenhuma reapresentacao da amostra anterior.
#include <unity.h>

#include "fakes/fake_clock.h"
#include "fakes/fake_sensor_link.h"

using test::FakeClock;
using test::FakeSensorLink;

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr int16_t kSentinelDeci = 0x5A5A;

SensorSample sentinel() {
    SensorSample sample{};
    sample.xDeci = kSentinelDeci;
    sample.yDeci = kSentinelDeci;
    sample.status = 0xDEAD;
    sample.atMs = 0xA5A5A5A5u;
    return sample;
}

bool untouched(const SensorSample& sample) {
    return sample.xDeci == kSentinelDeci && sample.yDeci == kSentinelDeci &&
           sample.status == 0xDEAD && sample.atMs == 0xA5A5A5A5u;
}

}  // namespace

// --- pre-condicoes e numeros da porta ---

static void test_numeros_do_enlace_sao_os_da_base_comum(void) {
    // 35 ms = urbase::kLinkTimeoutMs (DECISIONS.md:574 e :1846). O cabecalho da porta e
    // docs/protocolo-rs485.md 8.3 ainda dizem 30: divergencia de produto, aberta e escalada.
    // O que este teste garante e que fake e adaptador digam o MESMO numero, seja qual for.
    FakeClock clock;
    FakeSensorLink link(clock);

    TEST_ASSERT_EQUAL_UINT32(35u, link.timeoutMs());
    TEST_ASSERT_EQUAL_UINT32(19200u, link.baud());
    TEST_ASSERT_EQUAL_UINT8(1u, link.slaveAddress());
    TEST_ASSERT_EQUAL_STRING("modbus-rtu", link.protocolName());
}

static void test_request_antes_de_begin_reprova(void) {
    FakeClock clock;
    FakeSensorLink link(clock);

    TEST_ASSERT_TRUE(link.request().err == Err::NotInit);
    TEST_ASSERT_TRUE(link.begin().ok());
    TEST_ASSERT_TRUE(link.request().ok());
}

static void test_begin_e_idempotente(void) {
    // Na placa a segunda chamada NAO pode reinstalar o driver (heap depois do setup()); aqui
    // ela so re-arma o estado. Nos dois casos o efeito visivel e o mesmo: kOk e link parado.
    FakeClock clock;
    FakeSensorLink link(clock);

    TEST_ASSERT_TRUE(link.begin().ok());
    TEST_ASSERT_TRUE(link.request().ok());
    TEST_ASSERT_TRUE(link.begin().ok());
    TEST_ASSERT_FALSE(link.busy());

    SensorSample out = sentinel();
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Idle);
}

static void test_poll_sem_request_e_idle(void) {
    // Idle significa exatamente "request() ainda nao foi chamado". Nada mais pode devolver Idle.
    FakeClock clock;
    FakeSensorLink link(clock);
    link.begin();

    SensorSample out = sentinel();
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Idle);
    TEST_ASSERT_TRUE(untouched(out));
}

static void test_request_nao_sobrepoe_transacao(void) {
    FakeClock clock;
    FakeSensorLink link(clock);
    link.begin();
    link.replySample(FakeSensorLink::goodSample(100, -200, 7), 15u);

    TEST_ASSERT_TRUE(link.request().ok());
    TEST_ASSERT_TRUE(link.busy());
    TEST_ASSERT_TRUE(link.request().err == Err::Busy);
    TEST_ASSERT_EQUAL_UINT32(1u, link.stats().requests);
}

// --- veredito Fresh e o prazo ---

static void test_resposta_dentro_do_prazo_e_fresh_com_carimbo(void) {
    FakeClock clock(1000u);
    FakeSensorLink link(clock);
    link.begin();
    link.replySample(FakeSensorLink::goodSample(123, -456, 9), 15u);

    TEST_ASSERT_TRUE(link.request().ok());
    SensorSample out = sentinel();

    clock.advanceMs(10u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Busy);
    TEST_ASSERT_TRUE(untouched(out));

    clock.advanceMs(5u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Fresh);
    TEST_ASSERT_EQUAL_INT16(123, out.xDeci);
    TEST_ASSERT_EQUAL_INT16(-456, out.yDeci);
    TEST_ASSERT_EQUAL_UINT16(9u, out.heartbeat);
    TEST_ASSERT_EQUAL_UINT32(1015u, out.atMs);
    TEST_ASSERT_EQUAL_UINT32(1u, link.stats().fresh);
    TEST_ASSERT_EQUAL_UINT32(0u, link.stats().timeouts);
}

static void test_quadro_integro_depois_do_prazo_e_timeout_nao_fresh(void) {
    // O DEFEITO que a revisao pegou no adaptador: quadro completo e integro ja no anel faz o
    // veredito ser Fresh sem comparacao com o prazo. Aqui ele chega em 36 ms, 1 ms depois do
    // limite, e TEM de reprovar - senao o LinkSupervisor nunca ve na placa o Timeout que viu
    // no host e um enlace degradado passa por saudavel.
    FakeClock clock;
    FakeSensorLink link(clock);
    link.begin();
    link.replySample(FakeSensorLink::goodSample(10, 20, 3), 36u);

    TEST_ASSERT_TRUE(link.request().ok());
    SensorSample out = sentinel();

    clock.advanceMs(36u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Timeout);
    TEST_ASSERT_TRUE(untouched(out));
    TEST_ASSERT_EQUAL_UINT32(0u, link.stats().fresh);
    TEST_ASSERT_EQUAL_UINT32(1u, link.stats().timeouts);
}

static void test_limite_do_prazo_e_fechado_em_34_e_aberto_em_35(void) {
    // 35 ms contados do PRIMEIRO byte transmitido: 34 ms ainda e Fresh, 35 ja e Timeout.
    {
        FakeClock clock;
        FakeSensorLink link(clock);
        link.begin();
        link.replySample(FakeSensorLink::goodSample(1, 2, 4), 34u);
        link.request();
        clock.advanceMs(34u);
        SensorSample out = sentinel();
        TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Fresh);
    }
    {
        FakeClock clock;
        FakeSensorLink link(clock);
        link.begin();
        link.replySample(FakeSensorLink::goodSample(1, 2, 4), 35u);
        link.request();
        clock.advanceMs(35u);
        SensorSample out = sentinel();
        TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Timeout);
    }
}

static void test_prazo_atravessa_o_wrap_de_2_elevado_a_32(void) {
    // Prazo calculado com "a > b" em vez da subtracao unsigned de i_clock.h quebra aqui.
    FakeClock clock(0xFFFFFFF0u);
    FakeSensorLink link(clock);
    link.begin();
    link.replySample(FakeSensorLink::goodSample(5, 6, 11), 20u);

    TEST_ASSERT_TRUE(link.request().ok());
    clock.advanceMs(20u);  // 0xFFFFFFF0 + 20 envolve
    SensorSample out = sentinel();
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Fresh);
    TEST_ASSERT_EQUAL_UINT32(0x00000004u, out.atMs);
}

static void test_silencio_total_termina_em_timeout_no_fim_do_prazo(void) {
    FakeClock clock;
    FakeSensorLink link(clock);
    link.begin();
    link.replySilence();

    TEST_ASSERT_TRUE(link.request().ok());
    SensorSample out = sentinel();

    clock.advanceMs(34u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Busy);
    clock.advanceMs(1u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Timeout);
    TEST_ASSERT_TRUE(untouched(out));
    TEST_ASSERT_EQUAL_UINT32(1u, link.stats().timeouts);
}

// --- veredito por ciclo ---

static void test_um_fresh_por_request_e_depois_idle(void) {
    // A porta e explicita: cada request() produz no maximo um Fresh, e nunca ha reapresentacao
    // da amostra anterior - nao existe lastSample() nesta interface.
    FakeClock clock;
    FakeSensorLink link(clock);
    link.begin();
    link.replySample(FakeSensorLink::goodSample(77, 88, 12), 10u);
    link.request();
    clock.advanceMs(10u);

    SensorSample out = sentinel();
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Fresh);

    SensorSample again = sentinel();
    TEST_ASSERT_TRUE(link.poll(again) == LinkPoll::Idle);
    TEST_ASSERT_TRUE(untouched(again));
    TEST_ASSERT_FALSE(link.busy());
}

static void test_falha_de_transmissao_vira_timeout_e_nunca_idle(void) {
    // O DEFEITO que a revisao pegou: request() que falha deixava o objeto sem transacao, e o
    // poll seguinte dizia Idle ("nada aconteceu"). Uma falha permanente de TX nunca declararia
    // falha de enlace e o supervisor ficaria preso com os reles no estado normal.
    FakeClock clock;
    FakeSensorLink link(clock);
    link.begin();
    link.failNextRequest();

    TEST_ASSERT_TRUE(link.request().err == Err::Io);
    TEST_ASSERT_EQUAL_UINT32(1u, link.stats().requests);  // o ciclo existiu e conta
    TEST_ASSERT_TRUE(link.busy());                        // ainda deve um veredito

    SensorSample out = sentinel();
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Timeout);
    TEST_ASSERT_TRUE(untouched(out));
    TEST_ASSERT_EQUAL_UINT32(1u, link.stats().timeouts);
    TEST_ASSERT_FALSE(link.busy());

    // Colhido o veredito, o proximo ciclo anda normalmente.
    link.replySample(FakeSensorLink::goodSample(1, 1, 13), 5u);
    TEST_ASSERT_TRUE(link.request().ok());
    clock.advanceMs(5u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Fresh);
}

static void test_abort_cancela_e_volta_a_idle(void) {
    FakeClock clock;
    FakeSensorLink link(clock);
    link.begin();
    link.replySample(FakeSensorLink::goodSample(3, 4, 14), 10u);
    link.request();

    link.abort();
    TEST_ASSERT_FALSE(link.busy());

    SensorSample out = sentinel();
    clock.advanceMs(10u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Idle);
    TEST_ASSERT_TRUE(untouched(out));
}

// --- quadro ruim ---

static void test_quadro_ruim_so_vira_badframe_no_fim_do_prazo(void) {
    // O adaptador real continua consumindo o anel atras da resposta boa depois de um quadro
    // reprovado (ruido de linha aberta em 500 m de par trancado nao pode matar um ciclo bom),
    // entao o veredito ruim so pode sair no fim dos 35 ms. O fake reproduz a MESMA cadencia -
    // se ele respondesse BadFrame na hora, o dominio contaria ciclos em ritmo diferente do da
    // placa.
    FakeClock clock;
    FakeSensorLink link(clock);
    link.begin();
    link.replyBadFrame(FakeSensorLink::Bad::Crc, 5u);
    link.request();

    SensorSample out = sentinel();
    clock.advanceMs(5u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Busy);
    TEST_ASSERT_EQUAL_UINT32(1u, link.stats().crcErrors);

    clock.advanceMs(29u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Busy);

    clock.advanceMs(1u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::BadFrame);
    TEST_ASSERT_TRUE(untouched(out));
    TEST_ASSERT_EQUAL_UINT32(0u, link.stats().timeouts);  // ruido nao e silencio
}

static void test_endereco_e_funcao_errados_contam_framing(void) {
    // framingErrors e definido pela porta como endereco/funcao/byte count/comprimento. Uma
    // sensora respondendo em outro endereco tem de aparecer como enquadramento e nao como
    // silencio: "sensora muda" e "sensora no endereco errado" sao dois reparos diferentes.
    FakeClock clock;
    FakeSensorLink link(clock);
    link.begin();
    link.replyBadFrame(FakeSensorLink::Bad::Framing, 8u);
    link.request();

    SensorSample out = sentinel();
    clock.advanceMs(35u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::BadFrame);
    TEST_ASSERT_EQUAL_UINT32(1u, link.stats().framingErrors);
    TEST_ASSERT_EQUAL_UINT32(0u, link.stats().crcErrors);
}

static void test_excecao_modbus_e_badframe_com_codigo_guardado(void) {
    FakeClock clock;
    FakeSensorLink link(clock);
    link.begin();
    link.replyBadFrame(FakeSensorLink::Bad::Exception, 6u, 0x03);
    link.request();

    SensorSample out = sentinel();
    clock.advanceMs(35u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::BadFrame);
    TEST_ASSERT_EQUAL_UINT32(1u, link.stats().exceptions);
    TEST_ASSERT_EQUAL_UINT8(0x03u, link.lastException());
}

// --- fronteira: a porta nao julga conteudo ---

static void test_casos_traicoeiros_sao_transporte_valido(void) {
    // Angulo congelado com status 0x0011, selftest latchado 0x0009 e heartbeat parado sao
    // quadros PERFEITOS no fio. Quem os reprova e o dominio. Se o fake (ou o adaptador)
    // escondesse esses casos atras de BadFrame, o teste do LinkSupervisor nunca os veria.
    FakeClock clock;
    FakeSensorLink link(clock);
    link.begin();

    SensorSample congelado = FakeSensorLink::goodSample(150, -150, 42);
    congelado.status = static_cast<uint16_t>(kStsDataValid | kStsSclNotResponding);  // 0x0011
    link.replySample(congelado, 12u);
    link.request();
    clock.advanceMs(12u);

    SensorSample out = sentinel();
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Fresh);
    TEST_ASSERT_EQUAL_UINT16(0x0011u, out.status);

    SensorSample selftest = FakeSensorLink::goodSample(150, -150, 42);  // heartbeat parado
    selftest.status = static_cast<uint16_t>(kStsDataValid | kStsSclSelfTestFail);  // 0x0009
    link.replySample(selftest, 12u);
    link.request();
    clock.advanceMs(12u);
    TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Fresh);
    TEST_ASSERT_EQUAL_UINT16(0x0009u, out.status);
    TEST_ASSERT_EQUAL_UINT16(42u, out.heartbeat);
    TEST_ASSERT_EQUAL_UINT32(2u, link.stats().fresh);
}

static void test_estatistica_e_coerente_e_zeravel(void) {
    // bytesTx > 0 com requests == 0 seria estatistica incoerente - foi um dos defeitos do
    // adaptador. Aqui os dois andam juntos.
    FakeClock clock;
    FakeSensorLink link(clock);
    link.begin();
    link.replySample(FakeSensorLink::goodSample(1, 2, 15), 10u);

    for (uint8_t i = 0; i < 3u; ++i) {
        TEST_ASSERT_TRUE(link.request().ok());
        clock.advanceMs(10u);
        SensorSample out = sentinel();
        TEST_ASSERT_TRUE(link.poll(out) == LinkPoll::Fresh);
        clock.advanceMs(40u);
    }

    TEST_ASSERT_EQUAL_UINT32(3u, link.stats().requests);
    TEST_ASSERT_EQUAL_UINT32(3u, link.stats().fresh);
    TEST_ASSERT_EQUAL_UINT32(3u * FakeSensorLink::kRequestLen, link.stats().bytesTx);
    TEST_ASSERT_EQUAL_UINT32(3u * FakeSensorLink::kResponseLen, link.stats().bytesRx);
    TEST_ASSERT_TRUE(link.stats().lastTurnaroundUs == 10000u);

    link.resetStats();
    TEST_ASSERT_EQUAL_UINT32(0u, link.stats().requests);
    TEST_ASSERT_EQUAL_UINT32(0u, link.stats().bytesTx);
    TEST_ASSERT_EQUAL_UINT8(0u, link.lastException());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_numeros_do_enlace_sao_os_da_base_comum);
    RUN_TEST(test_request_antes_de_begin_reprova);
    RUN_TEST(test_begin_e_idempotente);
    RUN_TEST(test_poll_sem_request_e_idle);
    RUN_TEST(test_request_nao_sobrepoe_transacao);
    RUN_TEST(test_resposta_dentro_do_prazo_e_fresh_com_carimbo);
    RUN_TEST(test_quadro_integro_depois_do_prazo_e_timeout_nao_fresh);
    RUN_TEST(test_limite_do_prazo_e_fechado_em_34_e_aberto_em_35);
    RUN_TEST(test_prazo_atravessa_o_wrap_de_2_elevado_a_32);
    RUN_TEST(test_silencio_total_termina_em_timeout_no_fim_do_prazo);
    RUN_TEST(test_um_fresh_por_request_e_depois_idle);
    RUN_TEST(test_falha_de_transmissao_vira_timeout_e_nunca_idle);
    RUN_TEST(test_abort_cancela_e_volta_a_idle);
    RUN_TEST(test_quadro_ruim_so_vira_badframe_no_fim_do_prazo);
    RUN_TEST(test_endereco_e_funcao_errados_contam_framing);
    RUN_TEST(test_excecao_modbus_e_badframe_com_codigo_guardado);
    RUN_TEST(test_casos_traicoeiros_sao_transporte_valido);
    RUN_TEST(test_estatistica_e_coerente_e_zeravel);
    return UNITY_END();
}
