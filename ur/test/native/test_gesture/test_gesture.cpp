// Testes de dominio do reconhecedor de gesto (KeyGesture).
//
// O que esta preso aqui, com a fonte de cada numero:
//
// MAN-5.2-L81 e MAN-5.4-L101 (docs/ihm-estados.md secao 1.2; arquivo bruto L90 e L110,
//   "aproximadamente 3 segundos"): o hold dispara NA BORDA dos 3000 ms, com a tecla ainda
//   prensada, nao na soltura, nao redispara enquanto a tecla continua prensada, e uma
//   prensagem longa demais NAO vira toque curto quando a tecla e solta.
// MAN-5.6-L152 (arquivo bruto L161, "duplo acionamento da tecla UP (PSET)") com a decisao 1,
//   item 7, APROVADA: duas prensagens de 30 ms a 600 ms cada, intervalo entre a soltura da
//   primeira e a prensagem da segunda de ate 400 ms, gesto inteiro em ate 1600 ms; qualquer
//   borda de MENU ou DOWN dentro da janela anula; um terceiro toque dentro da janela anula,
//   para que repique mecanico nao vire PSET.
// IKeypad::flush() (src/ports/i_keypad.h): a troca de tela ou de modo nao deixa gesto pela
//   metade nem deixa a soltura de uma tecla ja prensada vazar como toque curto.
//
// O tempo vem do FakeClock canonico de test/fakes/fake_clock.h, que comeca em 0xFFFF0000: todo
// gesto deste arquivo atravessa o wrap de 2^32 ms, entao um prazo escrito como "a > b" em vez
// da subtracao unsigned de ports/i_clock.h reprova aqui. As bordas sao escritas pelo
// FakeKeypad, que carimba cada uma com o instante do proprio relogio - nenhum carimbo e
// inventado na mao.
#include <unity.h>

#include "domain/ui/key_gesture.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_keypad.h"

using domain::Gesture;
using domain::GestureKind;
using domain::KeyGesture;
using test::FakeClock;
using test::FakeKeypad;

namespace {

constexpr uint8_t kColhidosCap = 16;

struct Colheita {
    Gesture item[kColhidosCap];
    uint8_t count;
};

Colheita colher(KeyGesture& gesto) {
    Colheita colheita{};
    Gesture saida{};
    while (colheita.count < kColhidosCap && gesto.takeGesture(saida)) {
        colheita.item[colheita.count] = saida;
        ++colheita.count;
    }
    return colheita;
}

uint8_t contar(const Colheita& colheita, GestureKind tipo) {
    uint8_t total = 0;
    for (uint8_t i = 0; i < colheita.count; ++i) {
        if (colheita.item[i].kind == tipo) {
            ++total;
        }
    }
    return total;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_D1_item7_toque_curto_puro_entrega_um_unico_gesto(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    const uint32_t inicio = clock.nowMs();
    keypad.press(Key::Menu);
    clock.advanceMs(60u);
    keypad.release(Key::Menu);
    gesto.update();

    const Colheita colheita = colher(gesto);
    TEST_ASSERT_EQUAL_UINT8(1u, colheita.count);
    TEST_ASSERT_TRUE(colheita.item[0].kind == GestureKind::ShortTap);
    TEST_ASSERT_TRUE(colheita.item[0].key == Key::Menu);
    TEST_ASSERT_EQUAL_UINT32(inicio + 60u, colheita.item[0].atMs);
}

static void test_D1_item7_toque_curto_vale_de_30_ms_a_600_ms(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    keypad.tap(Key::Down, 29u);
    keypad.tap(Key::Down, 30u);
    keypad.tap(Key::Down, 600u);
    keypad.tap(Key::Down, 601u);
    gesto.update();

    const Colheita colheita = colher(gesto);
    TEST_ASSERT_EQUAL_UINT8(2u, colheita.count);
    TEST_ASSERT_EQUAL_UINT8(2u, contar(colheita, GestureKind::ShortTap));
}

static void test_MAN_5_4_L101_hold_dispara_em_3000_ms_e_nao_em_2999(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    const uint32_t inicio = clock.nowMs();
    keypad.press(Key::Menu);
    clock.advanceMs(2999u);
    gesto.update();
    TEST_ASSERT_EQUAL_UINT8(0u, colher(gesto).count);

    clock.advanceMs(1u);
    gesto.update();

    const Colheita colheita = colher(gesto);
    TEST_ASSERT_EQUAL_UINT8(1u, colheita.count);
    TEST_ASSERT_TRUE(colheita.item[0].kind == GestureKind::Hold);
    TEST_ASSERT_TRUE(colheita.item[0].key == Key::Menu);
    TEST_ASSERT_EQUAL_UINT32(inicio + 3000u, colheita.item[0].atMs);
    TEST_ASSERT_TRUE(keypad.pressed(Key::Menu));
}

static void test_MAN_5_4_L101_hold_nao_redispara_com_a_tecla_ainda_prensada(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    keypad.press(Key::Menu);
    clock.advanceMs(3000u);
    gesto.update();
    TEST_ASSERT_EQUAL_UINT8(1u, colher(gesto).count);

    for (uint8_t volta = 0; volta < 20u; ++volta) {
        clock.advanceMs(500u);
        gesto.update();
        TEST_ASSERT_EQUAL_UINT8(0u, colher(gesto).count);
    }

    keypad.release(Key::Menu);
    gesto.update();
    TEST_ASSERT_EQUAL_UINT8(0u, colher(gesto).count);
}

static void test_D1_item7_toque_longo_demais_nao_vira_toque_curto_na_soltura(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    keypad.press(Key::Menu);
    clock.advanceMs(800u);
    keypad.release(Key::Menu);
    gesto.update();

    TEST_ASSERT_EQUAL_UINT8(0u, colher(gesto).count);
}

static void test_MAN_5_6_L152_duplo_toque_valido_entrega_o_pset(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    keypad.press(Key::Up);
    clock.advanceMs(60u);
    keypad.release(Key::Up);
    clock.advanceMs(200u);
    keypad.press(Key::Up);
    clock.advanceMs(60u);
    keypad.release(Key::Up);
    const uint32_t segundaSoltura = clock.nowMs();
    gesto.update();

    const Colheita antes = colher(gesto);
    TEST_ASSERT_EQUAL_UINT8(2u, contar(antes, GestureKind::ShortTap));
    TEST_ASSERT_EQUAL_UINT8(0u, contar(antes, GestureKind::DoubleTap));
    TEST_ASSERT_TRUE(gesto.doubleTapPending());

    clock.advanceMs(400u);
    gesto.update();

    const Colheita depois = colher(gesto);
    TEST_ASSERT_EQUAL_UINT8(1u, depois.count);
    TEST_ASSERT_TRUE(depois.item[0].kind == GestureKind::DoubleTap);
    TEST_ASSERT_TRUE(depois.item[0].key == Key::Up);
    TEST_ASSERT_EQUAL_UINT32(segundaSoltura, depois.item[0].atMs);
    TEST_ASSERT_FALSE(gesto.doubleTapPending());
}

static void test_D1_item7_duplo_toque_com_intervalo_de_401_ms_e_recusado(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    keypad.tap(Key::Up, 60u);
    clock.advanceMs(401u);
    keypad.tap(Key::Up, 60u);
    clock.advanceMs(2000u);
    gesto.update();

    const Colheita colheita = colher(gesto);
    TEST_ASSERT_EQUAL_UINT8(2u, contar(colheita, GestureKind::ShortTap));
    TEST_ASSERT_EQUAL_UINT8(0u, contar(colheita, GestureKind::DoubleTap));
    TEST_ASSERT_FALSE(gesto.doubleTapPending());
}

static void test_D1_item7_duplo_toque_com_primeiro_toque_de_601_ms_e_recusado(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    keypad.tap(Key::Up, 601u);
    clock.advanceMs(100u);
    keypad.tap(Key::Up, 60u);
    clock.advanceMs(2000u);
    gesto.update();

    const Colheita colheita = colher(gesto);
    TEST_ASSERT_EQUAL_UINT8(1u, contar(colheita, GestureKind::ShortTap));
    TEST_ASSERT_EQUAL_UINT8(0u, contar(colheita, GestureKind::DoubleTap));
    TEST_ASSERT_FALSE(gesto.doubleTapPending());
}

static void test_D1_item7_terceiro_toque_na_janela_anula_o_pset(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    keypad.tap(Key::Up, 60u);
    clock.advanceMs(200u);
    keypad.tap(Key::Up, 60u);
    gesto.update();
    TEST_ASSERT_TRUE(gesto.doubleTapPending());
    (void)colher(gesto);

    clock.advanceMs(100u);
    keypad.press(Key::Up);
    gesto.update();
    TEST_ASSERT_FALSE(gesto.doubleTapPending());

    clock.advanceMs(60u);
    keypad.release(Key::Up);
    clock.advanceMs(3000u);
    gesto.update();

    const Colheita colheita = colher(gesto);
    TEST_ASSERT_EQUAL_UINT8(0u, contar(colheita, GestureKind::DoubleTap));
}

static void test_D1_item7_borda_de_menu_no_meio_anula_o_duplo_toque(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    keypad.tap(Key::Up, 60u);
    clock.advanceMs(100u);
    keypad.tap(Key::Menu, 60u);
    clock.advanceMs(100u);
    keypad.tap(Key::Up, 60u);
    clock.advanceMs(1000u);
    gesto.update();

    const Colheita colheita = colher(gesto);
    TEST_ASSERT_EQUAL_UINT8(0u, contar(colheita, GestureKind::DoubleTap));
    TEST_ASSERT_FALSE(gesto.doubleTapPending());
}

static void test_D1_item7_borda_de_down_no_meio_anula_o_duplo_toque(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    keypad.tap(Key::Up, 60u);
    clock.advanceMs(100u);
    keypad.press(Key::Down);
    clock.advanceMs(50u);
    keypad.press(Key::Up);
    clock.advanceMs(60u);
    keypad.release(Key::Up);
    keypad.release(Key::Down);
    clock.advanceMs(1000u);
    gesto.update();

    const Colheita colheita = colher(gesto);
    TEST_ASSERT_EQUAL_UINT8(0u, contar(colheita, GestureKind::DoubleTap));
    TEST_ASSERT_FALSE(gesto.doubleTapPending());
}

static void test_IKEYPAD_flush_nao_deixa_duplo_toque_pela_metade(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    keypad.tap(Key::Up, 60u);
    gesto.update();
    (void)colher(gesto);

    gesto.flush();

    clock.advanceMs(200u);
    keypad.tap(Key::Up, 60u);
    clock.advanceMs(2000u);
    gesto.update();

    const Colheita colheita = colher(gesto);
    TEST_ASSERT_EQUAL_UINT8(0u, contar(colheita, GestureKind::DoubleTap));
    TEST_ASSERT_FALSE(gesto.doubleTapPending());
}

static void test_IKEYPAD_flush_descarta_o_duplo_toque_ja_completo_e_a_fila_da_porta(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    keypad.tap(Key::Up, 60u);
    clock.advanceMs(200u);
    keypad.tap(Key::Up, 60u);
    gesto.update();
    TEST_ASSERT_TRUE(gesto.doubleTapPending());

    keypad.tap(Key::Menu, 60u);
    gesto.flush();
    TEST_ASSERT_EQUAL_UINT8(0u, keypad.queued());

    clock.advanceMs(3000u);
    gesto.update();

    TEST_ASSERT_EQUAL_UINT8(0u, colher(gesto).count);
    TEST_ASSERT_FALSE(gesto.doubleTapPending());
}

static void test_IKEYPAD_flush_impede_que_a_soltura_da_tecla_prensada_vire_toque_curto(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    keypad.press(Key::Menu);
    clock.advanceMs(60u);
    gesto.update();
    TEST_ASSERT_EQUAL_UINT8(0u, colher(gesto).count);

    gesto.flush();

    clock.advanceMs(60u);
    keypad.release(Key::Menu);
    gesto.update();

    TEST_ASSERT_EQUAL_UINT8(0u, colher(gesto).count);
}

static void test_D1_item7_ciclo_atrasado_nao_desloca_o_instante_do_hold(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);
    KeyGesture gesto(keypad, clock);

    const uint32_t inicio = clock.nowMs();
    keypad.press(Key::Menu);
    clock.advanceMs(5000u);
    gesto.update();

    const Colheita colheita = colher(gesto);
    TEST_ASSERT_EQUAL_UINT8(1u, colheita.count);
    TEST_ASSERT_TRUE(colheita.item[0].kind == GestureKind::Hold);
    TEST_ASSERT_EQUAL_UINT32(inicio + 3000u, colheita.item[0].atMs);
}

static void test_D1_item7_soltura_sem_prensagem_conhecida_nao_gera_gesto(void) {
    FakeClock clock;
    FakeKeypad keypad(clock);

    keypad.press(Key::Up);
    keypad.flush();

    KeyGesture gesto(keypad, clock);
    clock.advanceMs(60u);
    keypad.release(Key::Up);
    gesto.update();

    TEST_ASSERT_EQUAL_UINT8(0u, colher(gesto).count);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_D1_item7_toque_curto_puro_entrega_um_unico_gesto);
    RUN_TEST(test_D1_item7_toque_curto_vale_de_30_ms_a_600_ms);
    RUN_TEST(test_MAN_5_4_L101_hold_dispara_em_3000_ms_e_nao_em_2999);
    RUN_TEST(test_MAN_5_4_L101_hold_nao_redispara_com_a_tecla_ainda_prensada);
    RUN_TEST(test_D1_item7_toque_longo_demais_nao_vira_toque_curto_na_soltura);
    RUN_TEST(test_MAN_5_6_L152_duplo_toque_valido_entrega_o_pset);
    RUN_TEST(test_D1_item7_duplo_toque_com_intervalo_de_401_ms_e_recusado);
    RUN_TEST(test_D1_item7_duplo_toque_com_primeiro_toque_de_601_ms_e_recusado);
    RUN_TEST(test_D1_item7_terceiro_toque_na_janela_anula_o_pset);
    RUN_TEST(test_D1_item7_borda_de_menu_no_meio_anula_o_duplo_toque);
    RUN_TEST(test_D1_item7_borda_de_down_no_meio_anula_o_duplo_toque);
    RUN_TEST(test_IKEYPAD_flush_nao_deixa_duplo_toque_pela_metade);
    RUN_TEST(test_IKEYPAD_flush_descarta_o_duplo_toque_ja_completo_e_a_fila_da_porta);
    RUN_TEST(test_IKEYPAD_flush_impede_que_a_soltura_da_tecla_prensada_vire_toque_curto);
    RUN_TEST(test_D1_item7_ciclo_atrasado_nao_desloca_o_instante_do_hold);
    RUN_TEST(test_D1_item7_soltura_sem_prensagem_conhecida_nao_gera_gesto);
    return UNITY_END();
}
