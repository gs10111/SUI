// Testes da sequencia de energizacao da IHM: app::BootSequence - autoteste do display,
// logomarca, autoteste sob demanda e o gesto de Reset de Fabrica.
//
// Fonte de cada numero e de cada texto (manual bruto docs/manual-cliente-sui-2026.txt e
// DECISIONS.md decisao 1, itens 21 a 26, mais decisao 12 itens 6 a 8):
//  - item 21: o gesto NAO bloqueia o boot. main.cpp amostra os tres pinos de tecla no passo 4
//    da ordem de boot e entrega a mascara a begin(); o setup() segue inteiro.
//  - item 22, ASSINATURA DE CABO: se BAIXO ou MENU tambem lerem prensadas na amostragem, o
//    gesto e abortado. Tres teclas prensadas na energizacao e curto de cabo, nao operador.
//  - item 23: ▲ prensada CONTINUAMENTE ate t = 3000 ms contados da ENTRADA do setup(). Por
//    isso begin() recebe o carimbo do passo 4 e nao chama o relogio: qualquer tick que leia a
//    tecla solta anula o gesto.
//  - item 24 (L246): a tela `RESET DE FABRICA`, byte a byte, por no minimo 2000 ms.
//  - item 25 (L247): a execucao so acontece na SOLTURA da tecla, e quem apaga e regrava a
//    Tabela 2 e o composition root, ao ver takeFactoryReset() devolver true.
//  - item 26: ▲ ainda prensada 10000 ms depois da mensagem (t = 13000 ms) ABORTA o reset,
//    exibe `TECLA PRESA` por 3000 ms e o boot segue SEM apagar nada.
//  - decisao 12 itens 6 e 7: 600 ms de autoteste em quatro padroes de 150 ms, mais 600 ms de
//    logomarca. Os 2000 + 1500 ms do rascunho estao mortos: sozinhos violavam o tWD de 1,12 s
//    do STWD100.
//  - decisao 12 item 8: o autoteste sob demanda sai ao primeiro toque em qualquer tecla,
//    DEPOIS que a tecla do proprio gesto for solta, ou por teto de 30000 ms.
//
// A classe e pura: fala com IDisplay, IKeypad e IClock, nao inclui Arduino.h, nao usa ponto
// flutuante e NAO BLOQUEIA - cada tick() faz no maximo um quadro e volta. O tempo vem do
// FakeClock canonico, que comeca em 0xFFFF0000, entao todo prazo daqui atravessa o wrap de
// 2^32 ms.
#include <unity.h>

#include "app/boot_sequence.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_display.h"
#include "fakes/fake_keypad.h"

using app::BootSequence;
using test::FakeClock;
using test::FakeDisplay;
using test::FakeKeypad;

void setUp(void) {}
void tearDown(void) {}

namespace {

// Orcamento tipico do setup() declarado no cabecalho de main.cpp: 231 ms entre a amostragem
// das teclas (passo 4) e a chamada de begin() (fim do passo 13). E exatamente esta defasagem
// que o carimbo de bootAtMs existe para nao cobrar do operador.
constexpr uint32_t kSetupMs = 231;

struct Rig {
    FakeClock clock;
    FakeDisplay display;
    FakeKeypad keypad;
    BootSequence boot;
    uint32_t bootAtMs;

    Rig() : clock(), display(), keypad(clock), boot(display, keypad, clock, "0.1.0"),
            bootAtMs(clock.nowMs()) {}

    // Energiza como o composition root energiza: amostra as teclas na ENTRADA do setup(),
    // gasta o setup inteiro e so entao entrega mascara e carimbo a maquina de estados.
    void power(uint8_t mask) {
        bootAtMs = clock.nowMs();
        clock.advanceMs(kSetupMs);
        boot.begin(bootAtMs, mask);
    }

    // Uma passagem do loop(), que roda a 50 ms.
    void step(uint32_t stepMs = 50) {
        clock.advanceMs(stepMs);
        boot.tick();
    }

    void stepUntilMs(uint32_t absoluteMs) {
        while (static_cast<int32_t>(clock.nowMs() - absoluteMs) < 0) {
            step();
        }
    }
};

}  // namespace

// --- splash ------------------------------------------------------------------------------

static void test_D12_o_splash_percorre_os_quatro_padroes_e_termina_em_1200_ms(void) {
    // 600 ms de autoteste em quatro padroes de 150 ms, mais 600 ms de logomarca. A tarefa ctrl
    // esta polando a sensora e comandando os quatro reles durante todo esse tempo.
    Rig rig;
    rig.power(0);
    TEST_ASSERT_TRUE(rig.boot.stage() == BootSequence::Stage::SelfTest);
    TEST_ASSERT_TRUE(rig.boot.ownsDisplay());

    uint8_t vistos = 0;
    uint8_t ultimo = 0xFF;
    for (uint16_t i = 0; i < 12u; ++i) {
        rig.step();
        if (rig.boot.stage() == BootSequence::Stage::SelfTest &&
            rig.display.lastPattern() != ultimo) {
            ultimo = rig.display.lastPattern();
            ++vistos;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(vistos >= 4u, "os quatro padroes de bancada tem de aparecer");
    TEST_ASSERT_TRUE(rig.boot.stage() == BootSequence::Stage::Logo);
    TEST_ASSERT_TRUE(rig.display.shows(BootSequence::kTextBrand));

    rig.stepUntilMs(rig.bootAtMs + kSetupMs + BootSequence::kSelfTestMs + BootSequence::kLogoMs +
                    50u);
    TEST_ASSERT_TRUE(rig.boot.finished());
    TEST_ASSERT_FALSE(rig.boot.ownsDisplay());
    TEST_ASSERT_FALSE(rig.boot.takeFactoryReset());
}

// --- gesto de Reset de Fabrica -----------------------------------------------------------

static void test_D1_item22_mascara_de_tres_teclas_aborta_o_gesto(void) {
    // Assinatura de cabo: tres teclas prensadas na energizacao e curto, nao operador. O gesto
    // nem chega a ser armado, entao segurar UP por 3 s nao apaga nada.
    Rig rig;
    rig.keypad.press(Key::Up);
    rig.keypad.press(Key::Down);
    rig.keypad.press(Key::Menu);
    rig.power(static_cast<uint8_t>(BootSequence::kMaskUp | BootSequence::kMaskDown |
                                   BootSequence::kMaskMenu));
    TEST_ASSERT_FALSE(rig.boot.resetArmed());

    rig.stepUntilMs(rig.bootAtMs + 4000u);
    TEST_ASSERT_TRUE(rig.boot.finished());
    TEST_ASSERT_FALSE(rig.boot.takeFactoryReset());
    TEST_ASSERT_FALSE(rig.display.showsExactly(BootSequence::kTextFactoryReset));
}

static void test_D1_item22_menu_junto_com_UP_tambem_aborta(void) {
    Rig rig;
    rig.keypad.press(Key::Up);
    rig.keypad.press(Key::Menu);
    rig.power(static_cast<uint8_t>(BootSequence::kMaskUp | BootSequence::kMaskMenu));
    TEST_ASSERT_FALSE(rig.boot.resetArmed());
    rig.stepUntilMs(rig.bootAtMs + 4000u);
    TEST_ASSERT_FALSE(rig.boot.takeFactoryReset());
}

static void test_D1_item23_a_janela_de_3000_ms_conta_da_ENTRADA_do_setup(void) {
    // O carimbo e o do passo 4, nao o do fim do setup(). Se a origem fosse o fim do setup(), o
    // operador teria de segurar UP por 3,23 s no tipico e ate 3,97 s com NVS virgem - e a
    // classe estaria medindo uma janela cuja origem nao e a origem da amostragem que a arma.
    Rig rig;
    rig.keypad.press(Key::Up);
    rig.power(BootSequence::kMaskUp);
    TEST_ASSERT_TRUE(rig.boot.resetArmed());

    // Um pouco antes dos 3000 ms contados da entrada do setup(): a tela ainda nao apareceu.
    rig.stepUntilMs(rig.bootAtMs + BootSequence::kResetHoldMs - 100u);
    TEST_ASSERT_FALSE(rig.display.showsExactly(BootSequence::kTextFactoryReset));

    rig.stepUntilMs(rig.bootAtMs + BootSequence::kResetHoldMs + 60u);
    TEST_ASSERT_TRUE(rig.boot.stage() == BootSequence::Stage::ResetMessage);
    TEST_ASSERT_TRUE(rig.display.showsExactly(BootSequence::kTextFactoryReset));
}

static void test_D1_item23_soltura_de_UP_antes_dos_3000_ms_anula_o_gesto(void) {
    // A continuidade e verificada a cada tick por pressedForMs(), sem passar por bordas de
    // fila: qualquer tick que leia a tecla solta anula. Prensar de novo nao rearma.
    Rig rig;
    rig.keypad.press(Key::Up);
    rig.power(BootSequence::kMaskUp);

    rig.stepUntilMs(rig.bootAtMs + 1000u);
    rig.keypad.release(Key::Up);
    rig.step();
    TEST_ASSERT_FALSE(rig.boot.resetArmed());

    rig.keypad.press(Key::Up);
    rig.stepUntilMs(rig.bootAtMs + 5000u);
    TEST_ASSERT_FALSE(rig.boot.takeFactoryReset());
    TEST_ASSERT_FALSE(rig.display.showsExactly(BootSequence::kTextFactoryReset));
}

static void test_D1_item24_item25_a_tela_fica_2000_ms_e_o_reset_sai_na_SOLTURA(void) {
    Rig rig;
    rig.keypad.press(Key::Up);
    rig.power(BootSequence::kMaskUp);
    rig.stepUntilMs(rig.bootAtMs + BootSequence::kResetHoldMs + 60u);
    TEST_ASSERT_TRUE(rig.boot.stage() == BootSequence::Stage::ResetMessage);

    // Enquanto a tecla nao for solta nada e executado, por mais que a tela ja tenha cumprido
    // os 2000 ms: item 25, "so na SOLTURA da tecla".
    rig.stepUntilMs(rig.bootAtMs + BootSequence::kResetHoldMs + BootSequence::kResetMessageMs +
                    500u);
    TEST_ASSERT_TRUE(rig.boot.stage() == BootSequence::Stage::ResetMessage);
    TEST_ASSERT_FALSE(rig.boot.takeFactoryReset());

    const uint32_t soltouEm = rig.clock.nowMs();
    rig.keypad.release(Key::Up);
    rig.step();
    // A mensagem ja tinha cumprido os 2000 ms, entao a soltura fecha o gesto no tick seguinte.
    TEST_ASSERT_TRUE(rig.boot.finished());
    TEST_ASSERT_TRUE(elapsedMs(soltouEm, rig.clock.nowMs()) <= 60u);

    // takeFactoryReset() e um consumo unico: o composition root apaga e regrava a Tabela 2 UMA
    // vez, nunca a cada passagem do loop().
    TEST_ASSERT_TRUE(rig.boot.takeFactoryReset());
    TEST_ASSERT_FALSE(rig.boot.takeFactoryReset());
    TEST_ASSERT_FALSE(rig.boot.takeFactoryReset());
}

static void test_D1_item24_a_mensagem_nao_sai_antes_dos_2000_ms(void) {
    Rig rig;
    rig.keypad.press(Key::Up);
    rig.power(BootSequence::kMaskUp);
    rig.stepUntilMs(rig.bootAtMs + BootSequence::kResetHoldMs + 60u);
    const uint32_t mensagemEm = rig.clock.nowMs();

    rig.keypad.release(Key::Up);
    rig.stepUntilMs(mensagemEm + BootSequence::kResetMessageMs - 200u);
    TEST_ASSERT_TRUE(rig.boot.stage() == BootSequence::Stage::ResetMessage);
    TEST_ASSERT_FALSE(rig.boot.finished());

    rig.stepUntilMs(mensagemEm + BootSequence::kResetMessageMs + 60u);
    TEST_ASSERT_TRUE(rig.boot.finished());
    TEST_ASSERT_TRUE(rig.boot.takeFactoryReset());
}

static void test_D1_item26_tecla_presa_aos_13000_ms_aborta_sem_apagar_nada(void) {
    // Tecla soldada, cabo em curto ou operador que esqueceu o dedo: o firmware nao pode apagar
    // a Tabela 2 por causa disso. Exibe `TECLA PRESA` por 3000 ms e o boot segue.
    Rig rig;
    rig.keypad.press(Key::Up);
    rig.power(BootSequence::kMaskUp);

    rig.stepUntilMs(rig.bootAtMs + BootSequence::kStuckKeyDeadlineMs - 200u);
    TEST_ASSERT_TRUE(rig.boot.stage() == BootSequence::Stage::ResetMessage);

    rig.stepUntilMs(rig.bootAtMs + BootSequence::kStuckKeyDeadlineMs + 60u);
    TEST_ASSERT_TRUE(rig.boot.stage() == BootSequence::Stage::StuckKey);
    TEST_ASSERT_TRUE(rig.display.showsExactly(BootSequence::kTextStuckKey));
    TEST_ASSERT_FALSE(rig.boot.resetArmed());

    const uint32_t presaEm = rig.clock.nowMs();
    rig.stepUntilMs(presaEm + BootSequence::kStuckKeyMessageMs + 60u);
    TEST_ASSERT_TRUE(rig.boot.finished());

    // O ponto do teste: NADA foi apagado.
    TEST_ASSERT_FALSE(rig.boot.takeFactoryReset());
}

// --- autoteste sob demanda ---------------------------------------------------------------

static void test_D12_item8_o_autoteste_sob_demanda_so_sai_depois_que_a_tecla_e_solta(void) {
    // O gesto que abre o autoteste e BAIXO mantida 3000 ms. Se qualquer tecla prensada
    // encerrasse a tela, ela fecharia no mesmo instante em que abriu, com o dedo do operador
    // ainda no botao.
    Rig rig;
    rig.power(0);
    rig.stepUntilMs(rig.bootAtMs + 2000u);
    TEST_ASSERT_TRUE(rig.boot.finished());

    rig.keypad.press(Key::Down);  // a tecla do proprio gesto, ainda prensada
    rig.boot.beginOnDemand();
    TEST_ASSERT_TRUE(rig.boot.ownsDisplay());

    for (uint16_t i = 0; i < 20u; ++i) {
        rig.step();
    }
    TEST_ASSERT_TRUE_MESSAGE(rig.boot.ownsDisplay(),
                             "com a tecla do gesto ainda prensada a tela nao pode fechar");

    rig.keypad.release(Key::Down);
    rig.step();
    TEST_ASSERT_TRUE(rig.boot.ownsDisplay());

    rig.keypad.press(Key::Menu);
    rig.step();
    TEST_ASSERT_TRUE(rig.boot.finished());
}

static void test_D12_item8_o_autoteste_sob_demanda_tem_teto_de_30000_ms(void) {
    Rig rig;
    rig.power(0);
    rig.stepUntilMs(rig.bootAtMs + 2000u);

    rig.keypad.press(Key::Down);
    rig.boot.beginOnDemand();
    const uint32_t abriuEm = rig.clock.nowMs();

    rig.stepUntilMs(abriuEm + BootSequence::kOnDemandCeilingMs - 200u);
    TEST_ASSERT_TRUE(rig.boot.ownsDisplay());
    rig.stepUntilMs(abriuEm + BootSequence::kOnDemandCeilingMs + 60u);
    TEST_ASSERT_TRUE(rig.boot.finished());
}

static void test_o_autoteste_sob_demanda_nunca_arma_o_reset_de_fabrica(void) {
    // beginOnDemand() e chamado do Modo Normal, sem senha. Se ele herdasse o candidato a reset
    // do boot, uma tecla presa em operacao apagaria a Tabela 2.
    Rig rig;
    rig.keypad.press(Key::Up);
    rig.power(BootSequence::kMaskUp);
    TEST_ASSERT_TRUE(rig.boot.resetArmed());

    rig.boot.beginOnDemand();
    TEST_ASSERT_FALSE(rig.boot.resetArmed());

    rig.stepUntilMs(rig.clock.nowMs() + 20000u);
    TEST_ASSERT_FALSE(rig.boot.takeFactoryReset());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_D12_o_splash_percorre_os_quatro_padroes_e_termina_em_1200_ms);
    RUN_TEST(test_D1_item22_mascara_de_tres_teclas_aborta_o_gesto);
    RUN_TEST(test_D1_item22_menu_junto_com_UP_tambem_aborta);
    RUN_TEST(test_D1_item23_a_janela_de_3000_ms_conta_da_ENTRADA_do_setup);
    RUN_TEST(test_D1_item23_soltura_de_UP_antes_dos_3000_ms_anula_o_gesto);
    RUN_TEST(test_D1_item24_item25_a_tela_fica_2000_ms_e_o_reset_sai_na_SOLTURA);
    RUN_TEST(test_D1_item24_a_mensagem_nao_sai_antes_dos_2000_ms);
    RUN_TEST(test_D1_item26_tecla_presa_aos_13000_ms_aborta_sem_apagar_nada);
    RUN_TEST(test_D12_item8_o_autoteste_sob_demanda_so_sai_depois_que_a_tecla_e_solta);
    RUN_TEST(test_D12_item8_o_autoteste_sob_demanda_tem_teto_de_30000_ms);
    RUN_TEST(test_o_autoteste_sob_demanda_nunca_arma_o_reset_de_fabrica);
    return UNITY_END();
}
