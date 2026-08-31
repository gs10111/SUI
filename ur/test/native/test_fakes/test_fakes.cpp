// Prova de contrato dos fakes (LSP).
//
// Os fakes so valem se forem substituiveis pelos adaptadores reais sem que o dominio perceba:
// mesmas pre-condicoes, mesma semantica de erro, mesmos limites. Um fake mais permissivo que o
// alvo faz a suite inteira mentir - o dominio passa no host e falha na placa.
//
// Estes testes prendem os pontos em que o fake poderia ser generoso demais: fila de 16 bordas
// com descarte contado, heldMs valido so em Release, pull-up ausente em DOWN e MENU (que sao
// input-only na placa), nada exibido antes de present(), e verifiable() falso porque o CN4 nao
// tem via de leitura de volta.
#include <unity.h>

#include "fakes/fake_clock.h"
#include "fakes/fake_display.h"
#include "fakes/fake_keypad.h"

using test::FakeClock;
using test::FakeDisplay;
using test::FakeKeypad;

void setUp(void) {}
void tearDown(void) {}

// --- teclado ---

static void test_teclado_entrega_bordas_na_ordem_com_carimbo(void) {
    FakeClock clock(1000u);
    FakeKeypad keypad(clock);

    keypad.press(Key::Menu);
    clock.advanceMs(150u);
    keypad.release(Key::Menu);

    KeyEvent event{};
    TEST_ASSERT_TRUE(keypad.takeEvent(event));
    TEST_ASSERT_TRUE(event.key == Key::Menu);
    TEST_ASSERT_TRUE(event.edge == KeyEdge::Press);
    TEST_ASSERT_EQUAL_UINT32(1000u, event.atMs);

    TEST_ASSERT_TRUE(keypad.takeEvent(event));
    TEST_ASSERT_TRUE(event.edge == KeyEdge::Release);
    TEST_ASSERT_EQUAL_UINT32(1150u, event.atMs);
    TEST_ASSERT_EQUAL_UINT16(150u, event.heldMs);

    TEST_ASSERT_FALSE(keypad.takeEvent(event));
}

static void test_teclado_heldMs_e_zero_no_press(void) {
    // heldMs so tem significado na soltura; no Press tem de ser zero, como no alvo.
    FakeClock clock;
    FakeKeypad keypad(clock);
    keypad.press(Key::Up);

    KeyEvent event{};
    TEST_ASSERT_TRUE(keypad.takeEvent(event));
    TEST_ASSERT_EQUAL_UINT16(0u, event.heldMs);
}

static void test_teclado_conta_descarte_quando_a_fila_enche(void) {
    // Gesto perdido em equipamento de seguranca nao pode ser silencioso.
    FakeClock clock;
    FakeKeypad keypad(clock);

    for (uint8_t i = 0; i < 20u; ++i) {
        keypad.press(Key::Down);
        clock.advanceMs(30u);
        keypad.release(Key::Down);
        clock.advanceMs(30u);
    }

    TEST_ASSERT_EQUAL_UINT8(FakeKeypad::kQueueCap, keypad.queued());
    TEST_ASSERT_TRUE(keypad.droppedEvents() > 0u);
}

static void test_teclado_pressedForMs_anda_com_o_relogio(void) {
    // E o que torna possivel disparar o hold de 3 s NO INSTANTE, com a tecla ainda prensada.
    FakeClock clock;
    FakeKeypad keypad(clock);

    TEST_ASSERT_EQUAL_UINT32(0u, keypad.pressedForMs(Key::Menu));
    keypad.press(Key::Menu);
    clock.advanceMs(3000u);
    TEST_ASSERT_EQUAL_UINT32(3000u, keypad.pressedForMs(Key::Menu));
    TEST_ASSERT_TRUE(keypad.pressed(Key::Menu));

    keypad.release(Key::Menu);
    TEST_ASSERT_EQUAL_UINT32(0u, keypad.pressedForMs(Key::Menu));
    TEST_ASSERT_FALSE(keypad.pressed(Key::Menu));
}

static void test_teclado_mascara_do_boot_nao_drena_a_fila(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    keypad.press(Key::Up);

    TEST_ASSERT_EQUAL_UINT8(1u << static_cast<uint8_t>(Key::Up), keypad.pressedMask());
    TEST_ASSERT_EQUAL_UINT8(1u, keypad.queued());
}

static void test_teclado_espelha_a_ausencia_de_pullup_da_placa(void) {
    // DOWN e MENU sao input-only e ignoram INPUT_PULLUP em silencio: cabo de IHM solto pode
    // ser lido como tecla presa. O fake nao pode esconder isso.
    FakeClock clock;
    FakeKeypad keypad(clock);

    TEST_ASSERT_TRUE(keypad.hasInternalPullup(Key::Up));
    TEST_ASSERT_FALSE(keypad.hasInternalPullup(Key::Down));
    TEST_ASSERT_FALSE(keypad.hasInternalPullup(Key::Menu));
}

static void test_teclado_flush_descarta_bordas_pendentes(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    keypad.tap(Key::Up);
    TEST_ASSERT_TRUE(keypad.queued() > 0u);

    keypad.flush();
    KeyEvent event{};
    TEST_ASSERT_FALSE(keypad.takeEvent(event));
}

// --- display ---

static void test_display_nao_mostra_nada_antes_do_present(void) {
    // Dominio que desenha e esquece o present() tem de reprovar.
    FakeDisplay display;
    display.clear();
    display.drawText(0, 0, "Senha incorreta!", TextFont::Large, TextInk::Normal);

    TEST_ASSERT_FALSE(display.shows("Senha incorreta!"));
    TEST_ASSERT_EQUAL_UINT8(0u, display.drawCount());

    display.present();
    TEST_ASSERT_TRUE(display.showsExactly("Senha incorreta!"));
    TEST_ASSERT_EQUAL_UINT8(1u, display.drawCount());
}

static void test_display_quadro_novo_substitui_o_anterior(void) {
    FakeDisplay display;
    display.clear();
    display.drawText(0, 0, "RESET DE FABRICA", TextFont::Large, TextInk::Normal);
    display.present();
    TEST_ASSERT_TRUE(display.shows("RESET DE FABRICA"));

    display.clear();
    display.drawText(0, 0, "+045,0", TextFont::Large, TextInk::Normal);
    display.present();
    TEST_ASSERT_FALSE(display.shows("RESET DE FABRICA"));
    TEST_ASSERT_TRUE(display.showsExactly("+045,0"));
}

static void test_display_distingue_texto_exato_de_fragmento(void) {
    FakeDisplay display;
    display.clear();
    display.drawText(0, 0, "Senha incorreta!", TextFont::Small, TextInk::Normal);
    display.present();

    TEST_ASSERT_TRUE(display.shows("Senha"));
    TEST_ASSERT_FALSE(display.showsExactly("Senha"));
    TEST_ASSERT_TRUE(display.showsExactly("Senha incorreta!"));
}

static void test_display_registra_o_digito_piscando_como_inverso(void) {
    FakeDisplay display;
    display.clear();
    display.drawText(0, 0, "1234", TextFont::Large, TextInk::Normal);
    display.present();
    TEST_ASSERT_FALSE(display.hasInverse());

    display.clear();
    display.drawText(0, 0, "123", TextFont::Large, TextInk::Normal);
    display.drawText(40, 0, "4", TextFont::Large, TextInk::Inverse);
    display.present();
    TEST_ASSERT_TRUE(display.hasInverse());
    TEST_ASSERT_TRUE(display.inkOf("4") == TextInk::Inverse);
}

static void test_display_recusa_deslocamento_fora_de_dois_pixels(void) {
    FakeDisplay display;
    TEST_ASSERT_TRUE(display.setOrigin(2, -2).ok());
    TEST_ASSERT_TRUE(display.setOrigin(3, 0).failed());
    TEST_ASSERT_TRUE(display.setOrigin(0, -3).failed());
    TEST_ASSERT_EQUAL_INT8(2, display.originDx());
    TEST_ASSERT_EQUAL_INT8(-2, display.originDy());
}

static void test_display_recusa_padrao_de_autoteste_inexistente(void) {
    FakeDisplay display;
    TEST_ASSERT_TRUE(display.showPattern(0).ok());
    TEST_ASSERT_TRUE(display.showPattern(display.patternCount()).failed());
    TEST_ASSERT_EQUAL_UINT8(0u, display.lastPattern());
}

static void test_display_nao_e_verificavel(void) {
    // Sem MISO no CN4 nao ha como provar que o painel respondeu. Nenhuma decisao de rele pode
    // depender desta porta, e o fake nao pode sugerir o contrario.
    FakeDisplay display;
    TEST_ASSERT_FALSE(display.verifiable());
    TEST_ASSERT_TRUE(display.begin().ok());
    TEST_ASSERT_FALSE(display.verifiable());
}

static void test_display_recusa_texto_nulo(void) {
    FakeDisplay display;
    TEST_ASSERT_TRUE(display.drawText(0, 0, nullptr, TextFont::Small, TextInk::Normal).failed());
}

static void test_display_geometria_bate_com_o_painel_do_manual(void) {
    FakeDisplay display;
    TEST_ASSERT_EQUAL_UINT16(256u, display.widthPx());
    TEST_ASSERT_EQUAL_UINT16(64u, display.heightPx());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_teclado_entrega_bordas_na_ordem_com_carimbo);
    RUN_TEST(test_teclado_heldMs_e_zero_no_press);
    RUN_TEST(test_teclado_conta_descarte_quando_a_fila_enche);
    RUN_TEST(test_teclado_pressedForMs_anda_com_o_relogio);
    RUN_TEST(test_teclado_mascara_do_boot_nao_drena_a_fila);
    RUN_TEST(test_teclado_espelha_a_ausencia_de_pullup_da_placa);
    RUN_TEST(test_teclado_flush_descarta_bordas_pendentes);
    RUN_TEST(test_display_nao_mostra_nada_antes_do_present);
    RUN_TEST(test_display_quadro_novo_substitui_o_anterior);
    RUN_TEST(test_display_distingue_texto_exato_de_fragmento);
    RUN_TEST(test_display_registra_o_digito_piscando_como_inverso);
    RUN_TEST(test_display_recusa_deslocamento_fora_de_dois_pixels);
    RUN_TEST(test_display_recusa_padrao_de_autoteste_inexistente);
    RUN_TEST(test_display_nao_e_verificavel);
    RUN_TEST(test_display_recusa_texto_nulo);
    RUN_TEST(test_display_geometria_bate_com_o_painel_do_manual);
    return UNITY_END();
}
