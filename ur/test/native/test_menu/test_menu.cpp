// Testes de dominio da maquina do Modo Programacao (MenuMachine).
//
// Citacoes conferidas uma a uma por numero de linha em docs/manual-cliente-sui-2026.txt
// (arquivo bruto); docs/ihm-estados.md secoes 3.3 a 3.5 amarram cada tela ao estado.
//
// REQ-PRG-01: acesso ao Modo Programacao e o menu de L112, com os DEZ itens nesta ordem exata:
//             "Menu>Voltar   Ajusta Preset   Auto Calibracao   Limite 1   Limite 2   Limite 3
//             Limite 4   Sentido Sensor   Senha   Sair".
// REQ-PRG-02: navegacao e edicao dos parametros (L108 a L110, L211 a L220, Tabela 1); MENU
//             habilita a edicao do item selecionado (L109 e L212).
// REQ-PRG-03: o Modo Programacao sai sozinho por ~2 min sem tecla (L136), em TODOS os estados
//             C, D, E e F (T78 de docs/ihm-estados.md).
// REQ-PRG-04: saida pelos itens Voltar e Sair (L132) e pelo timeout, com a efetivacao de A13.
// REQ-PWD-01: senha de 0000 a 9999, padrao de fabrica 1234 (Tabela 1 L131).
// REQ-PWD-02: hold de MENU de ~3 s abre "Senha de acesso:0000" (L90 e L98), com o digito
//             selecionado piscando (L101).
// REQ-PWD-03: senha correta abre o Menu de Opcoes (L103).
// REQ-PWD-04: senha errada mostra "Senha incorreta!" (L104) e permite NOVA tentativa (L103).
// REQ-DSP-03: as telas do Modo Programacao sao as do manual, byte a byte. Os literais sao
//             afirmados CRUS neste arquivo, nunca contra a constante que desenhou a tela: um
//             teste que compara a tela com a propria constante nao prova texto nenhum.
// REQ-DSP-04: o digito em edicao e desenhado em Inverse - o "piscando" de L101 e L232,
//             verificavel por FakeDisplay::hasInverse(). Nas listas, o Inverse marca o ITEM
//             selecionado (desvio declarado d do cabecalho de menu_machine.h).
//
// Decisao A13, aprovada: efetivacao em INSTANTE UNICO no SAIR (nenhuma alteracao vale antes de
// sair), tela de revisao "NOVA CONFIG - CONFIRMA?", timeout de 120 s que deixa a configuracao
// PENDENTE em vez de descartar, bloqueio de senha de 60 s temporario, volatil e VISIVEL
// enquanto dura, e recusa explicita de valor fora de faixa sem clamp silencioso.
// Emenda 1 a A13: o portao de senha e PARAMETRO de composicao, e o dominio e testado nos dois
// valores - com requirePassword = false o hold abre o menu direto e o resto do Modo
// Programacao funciona igual, ate a efetivacao.
// Decisao A9, aprovada (opcao A): a troca de Sentido do Sensor ZERA o offset de PSET do eixo e
// exibe aviso obrigatorio de 3 s.
//
// O gesto vem do caminho REAL: FakeKeypad escreve as bordas, KeyGesture reconhece o toque
// curto e o hold de 3000 ms, e so entao MenuMachine recebe o gesto. Nenhum gesto e fabricado
// na mao. O tempo vem do FakeClock canonico de test/fakes/fake_clock.h, que comeca em
// 0xFFFF0000: os prazos de 60 s, 120 s, 1500 ms, 2000 ms e 3000 ms deste arquivo atravessam o
// wrap de 2^32 ms, entao um prazo escrito como "a > b" reprova aqui. Todo prazo e afirmado
// pelo NUMERO da decisao ou do manual, e pela FRONTEIRA: um milissegundo antes nao dispara, no
// prazo exato dispara.
#include <unity.h>

#include "domain/parameters.h"
#include "domain/password.h"
#include "domain/ui/key_gesture.h"
#include "domain/ui/menu_machine.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_display.h"
#include "fakes/fake_keypad.h"

using domain::Angle;
using domain::Axis;
using domain::Gesture;
using domain::KeyGesture;
using domain::LimitId;
using domain::LimitOp;
using domain::MenuAction;
using domain::MenuItem;
using domain::MenuMachine;
using domain::MenuState;
using domain::Parameters;
using domain::Password;
using domain::SensorDir;
using test::FakeClock;
using test::FakeDisplay;
using test::FakeKeypad;

namespace {

// Ordem LITERAL de L112. Um item a mais, um a menos ou fora de ordem reprova aqui.
const char* const kOrdemDoManual[MenuMachine::kItemCount] = {
    "Voltar", "Ajusta Preset", "Auto Calibracao", "Limite 1", "Limite 2",
    "Limite 3", "Limite 4", "Sentido Sensor", "Senha", "Sair",
};

// Os quatro limites: item de menu, etiqueta de eixo (L202), tela de submenu, tela do editor
// com o valor de fabrica da Tabela 2 (L257, L259, L261 e L263) e destino no agregado.
const MenuItem kItemLimite[Parameters::kLimitCount] = {
    MenuItem::Limite1, MenuItem::Limite2, MenuItem::Limite3, MenuItem::Limite4,
};
const LimitId kIdLimite[Parameters::kLimitCount] = {
    LimitId::X1, LimitId::X2, LimitId::Y1, LimitId::Y2,
};
const char* const kCabecalhoLimite[Parameters::kLimitCount] = {
    "Limite 1>", "Limite 2>", "Limite 3>", "Limite 4>",
};
const char* const kItemValorLimite[Parameters::kLimitCount] = {
    "Valor Limite X1", "Valor Limite X2", "Valor Limite Y1", "Valor Limite Y2",
};
const char* const kItemOperacaoLimite[Parameters::kLimitCount] = {
    "Operacao Limite X1", "Operacao Limite X2", "Operacao Limite Y1", "Operacao Limite Y2",
};
const char* const kTelaValorDeFabrica[Parameters::kLimitCount] = {
    "Valor Limite X1(graus):+005,0",
    "Valor Limite X2(graus):+000,0",
    "Valor Limite Y1(graus):+005,0",
    "Valor Limite Y2(graus):+000,0",
};
const char* const kTelaValorProgramado[Parameters::kLimitCount] = {
    "Valor Limite X1(graus):+005,1",
    "Valor Limite X2(graus):+000,2",
    "Valor Limite Y1(graus):+005,3",
    "Valor Limite Y2(graus):+000,4",
};
const int16_t kDeciProgramado[Parameters::kLimitCount] = {51, 2, 53, 4};

struct Bancada {
    FakeClock relogio;
    FakeKeypad teclado;
    FakeDisplay tela;
    Password senha;
    Parameters ativo;
    KeyGesture gestos;
    MenuMachine menu;

    explicit Bancada(bool exigeSenha = true)
        : relogio(),
          teclado(relogio),
          tela(),
          senha(relogio),
          ativo(Parameters::factoryDefaults()),
          gestos(teclado, relogio),
          menu(tela, relogio, senha, ativo, exigeSenha) {}
};

// Um ciclo de IHM: reconhece gesto, entrega o que houver e avalia os prazos.
void bombear(Bancada& b) {
    b.gestos.update();
    Gesture gesto{};
    while (b.gestos.takeGesture(gesto)) {
        b.menu.onGesture(gesto);
    }
    b.menu.update();
}

// Toque curto no ritmo de um operador. O intervalo de 500 ms entre gestos e proposital: sem
// ele dois toques seguidos de UP cairiam na janela de 400 ms do duplo toque de PSET.
void toque(Bancada& b, Key tecla) {
    b.relogio.advanceMs(500);
    b.teclado.tap(tecla, 60);
    bombear(b);
}

// Hold de MENU: o gesto dispara NA BORDA dos 3000 ms, com a tecla ainda prensada. Ao voltar,
// o relogio esta exatamente no instante do gesto - e por isso que os prazos de mensagem podem
// ser medidos por fronteira logo depois de um hold.
void hold(Bancada& b) {
    b.relogio.advanceMs(500);
    b.teclado.press(Key::Menu);
    b.relogio.advanceMs(KeyGesture::kHoldMs);
    bombear(b);
    b.teclado.release(Key::Menu);
    bombear(b);
}

void esperar(Bancada& b, uint32_t ms) {
    b.relogio.advanceMs(ms);
    bombear(b);
}

int code(MenuState estado) { return static_cast<int>(estado); }
int code(MenuItem item) { return static_cast<int>(item); }

// Nas listas o Inverse marca a selecao: e assim que o teste distingue "o item aparece na tela"
// de "o item esta selecionado".
const char* selecionado(const Bancada& b) {
    for (uint8_t i = 0; i < b.tela.drawCount(); ++i) {
        if (b.tela.draw(i).ink == TextInk::Inverse) {
            return b.tela.draw(i).text;
        }
    }
    return "";
}

// Digita um valor num campo de 4 digitos que esta em 0000, com o cursor no digito mais a
// direita (regra unica de cursor de A13): UP sobe o digito, MENU move para a esquerda.
void digitarDoZero(Bancada& b, uint16_t valor) {
    uint16_t resto = valor;
    for (uint8_t posicao = 0; posicao < 4; ++posicao) {
        const uint8_t digito = static_cast<uint8_t>(resto % 10u);
        resto = static_cast<uint16_t>(resto / 10u);
        for (uint8_t i = 0; i < digito; ++i) {
            toque(b, Key::Up);
        }
        if (posicao < 3) {
            toque(b, Key::Menu);
        }
    }
}

void entrarNoMenu(Bancada& b) {
    b.menu.begin();
    hold(b);
    digitarDoZero(b, Password::kFactory);
    hold(b);
}

void descerAte(Bancada& b, MenuItem alvo) {
    for (uint8_t i = 0; i < MenuMachine::kItemCount && b.menu.item() != alvo; ++i) {
        toque(b, Key::Down);
    }
}

// Sobe ao nivel 1 a partir de um submenu (selSub_ em 1 ou 2) e volta ao "Menu>".
void voltarAoNivel1(Bancada& b, uint8_t passosAcima) {
    for (uint8_t i = 0; i < passosAcima; ++i) {
        toque(b, Key::Up);
    }
    toque(b, Key::Menu);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- REQ-DSP-03: os literais do contrato, comparados com o literal cru ---

static void test_REQ_DSP_03_constantes_de_tela_sao_os_literais_do_contrato(void) {
    // Manual, byte a byte: L104, L183, L98, L231 e L112.
    TEST_ASSERT_EQUAL_STRING("Senha incorreta!", MenuMachine::kMsgSenhaIncorreta);
    TEST_ASSERT_EQUAL_STRING("Alteracao bem sucedida!", MenuMachine::kMsgGravOk);
    TEST_ASSERT_EQUAL_STRING("Senha de acesso:", MenuMachine::kPrefixoLogin);
    TEST_ASSERT_EQUAL_STRING("Edita senha:", MenuMachine::kPrefixoEditaSenha);
    TEST_ASSERT_EQUAL_STRING("Menu>", MenuMachine::kCabecalhoMenu);
    // Submenus de docs/ihm-estados.md 3.4 (D2 literal de L148; D3 e D5 nao impressos).
    TEST_ASSERT_EQUAL_STRING("Preset>", MenuMachine::kCabecalhoPreset);
    TEST_ASSERT_EQUAL_STRING("Auto Cal>", MenuMachine::kCabecalhoAutoCal);
    TEST_ASSERT_EQUAL_STRING("Sentido>", MenuMachine::kCabecalhoSentido);
    // A13: as duas telas que o manual nao publica e que a decisao fixa palavra por palavra.
    TEST_ASSERT_EQUAL_STRING("NOVA CONFIG - CONFIRMA?", MenuMachine::kMsgRevisao);
    TEST_ASSERT_EQUAL_STRING("CONFIG PENDENTE - REVISAR", MenuMachine::kMsgConfigPendente);
    TEST_ASSERT_EQUAL_STRING("FORA DA FAIXA +/-090,0", MenuMachine::kMsgForaDaFaixa);
    TEST_ASSERT_EQUAL_STRING("Senha bloqueada!", MenuMachine::kMsgBloqueado);
    // E8 de docs/ihm-estados.md 3.5.
    TEST_ASSERT_EQUAL_STRING("Falha de gravacao!", MenuMachine::kMsgFalhaGrav);
    // A9: o aviso obrigatorio da troca de sentido, com os limites do eixo a conferir (L199).
    TEST_ASSERT_EQUAL_STRING("Sentido X alterado!", MenuMachine::kMsgSentidoX);
    TEST_ASSERT_EQUAL_STRING("Sentido Y alterado!", MenuMachine::kMsgSentidoY);
    TEST_ASSERT_EQUAL_STRING("Preset zerado - confira X1 X2", MenuMachine::kMsgPresetZeradoX);
    TEST_ASSERT_EQUAL_STRING("Preset zerado - confira Y1 Y2", MenuMachine::kMsgPresetZeradoY);

    // Os prazos, pelo numero da decisao e do documento, nao pela propria constante.
    TEST_ASSERT_EQUAL_UINT32(120000u, MenuMachine::kTimeoutMs);        // L105 e L136
    TEST_ASSERT_EQUAL_UINT32(2000u, MenuMachine::kErroSenhaMs);        // ihm-estados 1.3
    TEST_ASSERT_EQUAL_UINT32(1500u, MenuMachine::kGravOkMs);           // T_MSG_OK
    TEST_ASSERT_EQUAL_UINT32(2000u, MenuMachine::kRecusaMs);           // A13, opcao C
    TEST_ASSERT_EQUAL_UINT32(3000u, MenuMachine::kDirWarnMs);          // A9, aviso de 3 s
    TEST_ASSERT_EQUAL_UINT32(3000u, MenuMachine::kFalhaGravMs);        // E8
    TEST_ASSERT_EQUAL_UINT32(60000u, Password::kLockoutMs);            // A13, bloqueio de 60 s
    TEST_ASSERT_EQUAL_UINT16(1234u, Password::kFactory);               // Tabela 1, L131
}

// --- REQ-PRG-01: os dez itens, um a um ---

static void test_REQ_PRG_01_os_dez_itens_na_ordem_literal_do_manual(void) {
    Bancada b;
    entrarNoMenu(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));

    for (uint8_t i = 0; i < MenuMachine::kItemCount; ++i) {
        TEST_ASSERT_EQUAL_INT(i, code(b.menu.item()));
        TEST_ASSERT_EQUAL_STRING(kOrdemDoManual[i], MenuMachine::itemName(b.menu.item()));
        TEST_ASSERT_TRUE(b.tela.showsExactly(kOrdemDoManual[i]));
        // A lista esta na tela e o item selecionado e o que aparece em Inverse.
        TEST_ASSERT_EQUAL_STRING(kOrdemDoManual[i], selecionado(b));
        TEST_ASSERT_TRUE(b.tela.showsExactly("Menu>"));
        if (i + 1u < MenuMachine::kItemCount) {
            toque(b, Key::Down);
        }
    }
    // Fecha a conferencia: o decimo item e Sair e nao existe decimo primeiro.
    TEST_ASSERT_EQUAL_INT(code(MenuItem::Sair), code(b.menu.item()));
}

static void test_REQ_PRG_01_lista_deslizante_mostra_os_vizinhos_do_item_selecionado(void) {
    // Desvio declarado (d): a linha unica de L112 nao cabe em 256x64, entao a tela mostra o
    // cabecalho mais uma janela de tres itens. A lista existe e desliza.
    Bancada b;
    entrarNoMenu(b);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Voltar"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Ajusta Preset"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Auto Calibracao"));
    TEST_ASSERT_FALSE(b.tela.showsExactly("Limite 1"));

    descerAte(b, MenuItem::Limite2);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Limite 1"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Limite 2"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Limite 3"));
    TEST_ASSERT_EQUAL_STRING("Limite 2", selecionado(b));
    TEST_ASSERT_FALSE(b.tela.showsExactly("Ajusta Preset"));
}

static void test_REQ_PRG_01_navegacao_circular_com_up_e_down(void) {
    Bancada b;
    entrarNoMenu(b);

    // UP sobe: do primeiro item da a volta para o ultimo.
    TEST_ASSERT_EQUAL_INT(code(MenuItem::Voltar), code(b.menu.item()));
    toque(b, Key::Up);
    TEST_ASSERT_EQUAL_INT(code(MenuItem::Sair), code(b.menu.item()));
    TEST_ASSERT_EQUAL_STRING("Sair", selecionado(b));

    // DOWN desce: do ultimo da a volta para o primeiro.
    toque(b, Key::Down);
    TEST_ASSERT_EQUAL_INT(code(MenuItem::Voltar), code(b.menu.item()));
    TEST_ASSERT_EQUAL_STRING("Voltar", selecionado(b));

    // E a volta inteira devolve o mesmo item.
    for (uint8_t i = 0; i < MenuMachine::kItemCount; ++i) {
        toque(b, Key::Down);
    }
    TEST_ASSERT_EQUAL_INT(code(MenuItem::Voltar), code(b.menu.item()));
}

// --- REQ-PWD: o portao de senha ---

static void test_REQ_PWD_02_hold_de_menu_de_3_s_abre_a_tela_de_senha(void) {
    Bancada b;
    b.menu.begin();
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
    TEST_ASSERT_FALSE(b.menu.ownsDisplay());

    // O manual so da funcao ao MENU do Modo Normal no hold de L90; toque curto nao tem funcao
    // declarada e por isso e IGNORADO (invariante 6 de docs/ihm-estados.md secao 6).
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));

    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Login), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Senha de acesso:0000"));
}

static void test_REQ_PWD_03_senha_correta_abre_o_menu(void) {
    Bancada b;
    b.menu.begin();
    hold(b);
    digitarDoZero(b, Password::kFactory);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Senha de acesso:1234"));

    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Menu>"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Voltar"));
}

static void test_REQ_PWD_04_senha_errada_mostra_senha_incorreta_e_permite_nova_tentativa(void) {
    Bancada b;
    b.menu.begin();
    hold(b);
    digitarDoZero(b, 4321);
    hold(b);

    TEST_ASSERT_EQUAL_INT(code(MenuState::LoginErro), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Senha incorreta!"));
    TEST_ASSERT_FALSE(b.tela.showsExactly("Menu>"));

    // "por alguns segundos antes de desaparecer e permitir uma nova tentativa" (L103): 2000 ms
    // de docs/ihm-estados.md 1.3, na fronteira - 1999 ms ainda mostra a recusa.
    esperar(b, 1999);
    TEST_ASSERT_EQUAL_INT(code(MenuState::LoginErro), code(b.menu.state()));
    esperar(b, 1);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Login), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Senha de acesso:0000"));

    // A nova tentativa entra.
    digitarDoZero(b, Password::kFactory);
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));
}

static void test_A13_bloqueio_de_60_s_fica_na_tela_ate_o_bloqueio_expirar(void) {
    // A13: o bloqueio e temporario e volatil, e o operador tem de VER que esta bloqueado -
    // um pisca de 2 s seguido da tela de login e indistinguivel de senha errada.
    Bancada b;
    b.menu.begin();
    hold(b);

    // Cinco submissoes de 0000, todas erradas (a senha vigente e 1234).
    for (uint8_t tentativa = 0; tentativa < Password::kMaxAttempts; ++tentativa) {
        hold(b);
        TEST_ASSERT_EQUAL_INT(code(MenuState::LoginErro), code(b.menu.state()));
        esperar(b, 2000);
    }
    // O bloqueio comeca na quinta recusa; a tela de erro sai e cai NA TELA DE BLOQUEIO.
    const uint32_t bloqueioAte = b.relogio.nowMs() + 60000u - 2000u;
    TEST_ASSERT_EQUAL_INT(code(MenuState::LoginBloqueado), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Senha bloqueada!"));

    // Meio minuto depois ela continua la, sem pisca e sem voltar ao login.
    esperar(b, 30000);
    TEST_ASSERT_EQUAL_INT(code(MenuState::LoginBloqueado), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Senha bloqueada!"));
    TEST_ASSERT_FALSE(b.tela.showsExactly("Senha de acesso:0000"));

    // Fronteira dos 60 s de A13: um milissegundo antes ainda bloqueia.
    esperar(b, bloqueioAte - b.relogio.nowMs() - 1u);
    TEST_ASSERT_EQUAL_INT(code(MenuState::LoginBloqueado), code(b.menu.state()));
    esperar(b, 1);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Login), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Senha de acesso:0000"));

    // E o painel volta a aceitar senha, sem visita de manutencao.
    digitarDoZero(b, Password::kFactory);
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));
}

static void test_A13_bloqueio_recusa_ate_a_senha_certa_enquanto_dura(void) {
    Bancada b;
    b.menu.begin();
    hold(b);
    for (uint8_t tentativa = 0; tentativa < Password::kMaxAttempts; ++tentativa) {
        hold(b);
        esperar(b, 2000);
    }
    TEST_ASSERT_EQUAL_INT(code(MenuState::LoginBloqueado), code(b.menu.state()));

    // Gesto nenhum atravessa a tela de bloqueio: nem toque, nem o hold que submeteria a senha.
    toque(b, Key::Up);
    toque(b, Key::Menu);
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::LoginBloqueado), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Senha bloqueada!"));
}

static void test_A13_emenda_1_sem_senha_o_hold_abre_o_menu_e_programa_igual(void) {
    // Emenda 1 a A13: binario de bancada com requirePassword = false. O portao e parametro de
    // composicao, nao #ifdef - e o dominio e testado nos DOIS valores.
    Bancada b(false);
    b.menu.begin();
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));
    TEST_ASSERT_EQUAL_INT(code(MenuItem::Voltar), code(b.menu.item()));
    TEST_ASSERT_FALSE(b.tela.showsExactly("Senha de acesso:0000"));

    // E o resto do Modo Programacao funciona igual, ate a efetivacao unica do SAIR.
    descerAte(b, MenuItem::Limite4);
    toque(b, Key::Menu);
    toque(b, Key::Down);
    toque(b, Key::Menu);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Valor Limite Y2(graus):+000,0"));
    toque(b, Key::Up);
    hold(b);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Alteracao bem sucedida!"));
    esperar(b, 1500);
    TEST_ASSERT_TRUE(b.menu.pendingConfig());

    voltarAoNivel1(b, 1);
    descerAte(b, MenuItem::Sair);
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Revisao), code(b.menu.state()));
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
    TEST_ASSERT_EQUAL_INT16(1, b.ativo.limitValue(LimitId::Y2).deciDegrees());
}

static void test_A13_emenda_1_com_senha_o_mesmo_hold_para_na_tela_de_login(void) {
    // O outro valor do mesmo parametro, no mesmo gesto: unidade instalada exige o portao.
    Bancada b(true);
    b.menu.begin();
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Login), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Senha de acesso:0000"));
    TEST_ASSERT_FALSE(b.tela.showsExactly("Menu>"));
}

// --- REQ-PRG-03: o timeout de 2 minutos ---

static void test_REQ_PRG_03_timeout_de_120_s_sem_tecla_volta_ao_modo_normal(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Limite1);

    // Um milissegundo antes do prazo de L105/L136 ainda e Modo Programacao.
    esperar(b, 119999);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));

    esperar(b, 1);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
    TEST_ASSERT_FALSE(b.menu.ownsDisplay());
}

static void test_REQ_PRG_04_timeout_na_tela_de_senha_tambem_volta_ao_modo_normal(void) {
    Bancada b;
    b.menu.begin();
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Login), code(b.menu.state()));

    esperar(b, 120000);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
}

static void test_REQ_PRG_03_timeout_vale_na_revisao_e_nas_telas_temporizadas(void) {
    // T78 de docs/ihm-estados.md: os 120 s valem em TODOS os estados C, D, E e F. Painel
    // abandonado em "NOVA CONFIG - CONFIRMA?" nao pode ficar eternamente no Modo Programacao.
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Senha);
    toque(b, Key::Menu);
    toque(b, Key::Up);
    hold(b);
    esperar(b, 1500);
    descerAte(b, MenuItem::Sair);
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Revisao), code(b.menu.state()));

    esperar(b, 120000);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
    // A13: o timeout nao descarta - a edicao continua pendente para revisao.
    TEST_ASSERT_TRUE(b.menu.pendingConfig());
    TEST_ASSERT_EQUAL_UINT16(Password::kFactory, b.senha.effective());

    // Na tela temporizada de gravacao o prazo tambem vale.
    Bancada c;
    entrarNoMenu(c);
    descerAte(c, MenuItem::Senha);
    toque(c, Key::Menu);
    toque(c, Key::Up);
    hold(c);
    TEST_ASSERT_EQUAL_INT(code(MenuState::GravOk), code(c.menu.state()));
    esperar(c, 120000);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(c.menu.state()));

    // E na tela de recusa de A13 tambem.
    Bancada d;
    entrarNoMenu(d);
    descerAte(d, MenuItem::Limite4);
    toque(d, Key::Menu);
    toque(d, Key::Down);
    toque(d, Key::Menu);
    toque(d, Key::Menu);
    toque(d, Key::Menu);
    toque(d, Key::Menu);
    for (uint8_t i = 0; i < 9; ++i) {
        toque(d, Key::Up);
    }
    hold(d);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Recusa), code(d.menu.state()));
    esperar(d, 120000);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(d.menu.state()));
}

// --- REQ-PRG-02 e REQ-DSP: submenus e editores ---

static void test_REQ_PRG_02_menu_habilita_a_edicao_do_valor_limite(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Limite4);

    // MENU curto abre o submenu do limite (L211: "selecionar o parametro Operacao Limite ou
    // Limite (1 a 4)"), que abre em Voltar.
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::SubLimite), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Limite 4>"));
    TEST_ASSERT_EQUAL_STRING("Voltar", selecionado(b));

    toque(b, Key::Down);
    TEST_ASSERT_EQUAL_STRING("Valor Limite Y2", selecionado(b));

    // MENU curto habilita a edicao (L212: "Pressione a tecla MENU para habilitar a edicao").
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::EditValor), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Valor Limite Y2(graus):+000,0"));

    // Com o campo aberto, MENU curto move o cursor (L213) e NAO sai da edicao.
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::EditValor), code(b.menu.state()));
}

static void test_REQ_PRG_02_os_quatro_limites_tem_etiqueta_e_destino_proprios(void) {
    // L214 a L217 imprimem as QUATRO telas de Valor Limite; L202 amarra X1/X2 ao eixo X e
    // Y1/Y2 ao eixo Y. Etiqueta trocada poe o tecnico programando o rele errado, entao cada
    // limite recebe um valor distinto e e conferido no agregado.
    Bancada b;
    entrarNoMenu(b);

    for (uint8_t i = 0; i < Parameters::kLimitCount; ++i) {
        descerAte(b, kItemLimite[i]);
        toque(b, Key::Menu);
        TEST_ASSERT_EQUAL_INT(code(MenuState::SubLimite), code(b.menu.state()));
        TEST_ASSERT_TRUE(b.tela.showsExactly(kCabecalhoLimite[i]));
        TEST_ASSERT_TRUE(b.tela.showsExactly(kItemValorLimite[i]));
        TEST_ASSERT_TRUE(b.tela.showsExactly(kItemOperacaoLimite[i]));

        toque(b, Key::Down);
        toque(b, Key::Menu);
        TEST_ASSERT_EQUAL_INT(code(MenuState::EditValor), code(b.menu.state()));
        // O campo abre no valor CORRENTE do rascunho, que aqui e o de fabrica da Tabela 2.
        TEST_ASSERT_TRUE(b.tela.showsExactly(kTelaValorDeFabrica[i]));

        for (uint8_t passo = 0; passo <= i; ++passo) {
            toque(b, Key::Up);
        }
        TEST_ASSERT_TRUE(b.tela.showsExactly(kTelaValorProgramado[i]));
        hold(b);
        TEST_ASSERT_EQUAL_INT(code(MenuState::GravOk), code(b.menu.state()));
        esperar(b, 1500);
        voltarAoNivel1(b, 1);
    }

    for (uint8_t i = 0; i < Parameters::kLimitCount; ++i) {
        TEST_ASSERT_EQUAL_INT16(kDeciProgramado[i],
                                b.menu.draft().limitValue(kIdLimite[i]).deciDegrees());
    }

    descerAte(b, MenuItem::Sair);
    toque(b, Key::Menu);
    hold(b);
    for (uint8_t i = 0; i < Parameters::kLimitCount; ++i) {
        TEST_ASSERT_EQUAL_INT16(kDeciProgramado[i],
                                b.ativo.limitValue(kIdLimite[i]).deciDegrees());
    }
}

static void test_REQ_PRG_02_operacao_limite_e_submenu_do_limite_n(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Limite2);
    toque(b, Key::Menu);
    toque(b, Key::Down);
    toque(b, Key::Down);
    TEST_ASSERT_EQUAL_STRING("Operacao Limite X2", selecionado(b));

    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::EditOperacao), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Operacao Limite X2:"));
    // Tabela 2 (L258): o Limite 2 (X2) sai de fabrica em Off.
    TEST_ASSERT_TRUE(b.tela.showsExactly("Off (desativado)"));

    // As quatro opcoes de L204 a L207, descendo.
    toque(b, Key::Down);
    TEST_ASSERT_TRUE(b.tela.showsExactly(">= (maior ou igual)"));
    toque(b, Key::Down);
    TEST_ASSERT_TRUE(b.tela.showsExactly("<= (menor ou igual)"));
    toque(b, Key::Down);
    TEST_ASSERT_TRUE(b.tela.showsExactly("+ (modulo)"));
    // Lista FECHADA: no fim ela nao da a volta (docs/ihm-estados.md 3.5, E3).
    toque(b, Key::Down);
    TEST_ASSERT_TRUE(b.tela.showsExactly("+ (modulo)"));

    // E as mesmas quatro subindo: sem o ramo de UP o operador fica preso na ultima opcao de um
    // parametro que comanda rele.
    toque(b, Key::Up);
    TEST_ASSERT_TRUE(b.tela.showsExactly("<= (menor ou igual)"));
    toque(b, Key::Up);
    TEST_ASSERT_TRUE(b.tela.showsExactly(">= (maior ou igual)"));
    toque(b, Key::Up);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Off (desativado)"));
    // No topo tambem nao da a volta.
    toque(b, Key::Up);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Off (desativado)"));
    TEST_ASSERT_FALSE(b.tela.showsExactly("+ (modulo)"));

    toque(b, Key::Down);
    toque(b, Key::Down);
    toque(b, Key::Down);
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::GravOk), code(b.menu.state()));
    // A13: gravou no RASCUNHO, marcou pendencia, e o rele continua com a operacao antiga.
    TEST_ASSERT_TRUE(b.menu.pendingConfig());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(LimitOp::Absolute),
                          static_cast<int>(b.menu.draft().limitOp(LimitId::X2)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(LimitOp::Off),
                          static_cast<int>(b.ativo.limitOp(LimitId::X2)));

    // A alteracao sobrevive ate a efetivacao unica: sem pendencia, o SAIR a jogaria fora em
    // silencio depois de o operador ter lido "Alteracao bem sucedida!".
    esperar(b, 1500);
    voltarAoNivel1(b, 2);
    descerAte(b, MenuItem::Sair);
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Revisao), code(b.menu.state()));
    hold(b);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(LimitOp::Absolute),
                          static_cast<int>(b.ativo.limitOp(LimitId::X2)));
}

static void test_REQ_PRG_02_preset_x_e_y_saem_como_pedido_de_assistente(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::AjustaPreset);
    toque(b, Key::Menu);
    // Submenu literal de L148: "Preset>Voltar   Preset X   Preset Y", inteiro na tela.
    TEST_ASSERT_EQUAL_INT(code(MenuState::SubEixo), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Preset>"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Voltar"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Preset X"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Preset Y"));

    toque(b, Key::Down);
    TEST_ASSERT_EQUAL_STRING("Preset X", selecionado(b));

    MenuAction pedido = MenuAction::None;
    TEST_ASSERT_FALSE(b.menu.takeAction(pedido));
    toque(b, Key::Menu);
    TEST_ASSERT_TRUE(b.menu.takeAction(pedido));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuAction::AjustaPresetX), static_cast<int>(pedido));
    // O pedido e consumido uma unica vez.
    TEST_ASSERT_FALSE(b.menu.takeAction(pedido));

    // Enquanto o assistente desenha, o display NAO e desta maquina e ela nao pinta quadro.
    TEST_ASSERT_EQUAL_INT(code(MenuState::Assistente), code(b.menu.state()));
    TEST_ASSERT_FALSE(b.menu.ownsDisplay());
    const uint32_t quadros = b.tela.presentCount();
    for (uint8_t i = 0; i < 10; ++i) {
        bombear(b);
    }
    TEST_ASSERT_EQUAL_UINT32(quadros, b.tela.presentCount());
    // Gesto tambem e do assistente: nada muda aqui.
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Assistente), code(b.menu.state()));

    // Quando o assistente devolve o display, a maquina REPINTA sozinha - sem isso a ultima
    // tela do assistente ficaria congelada na frente do operador.
    b.menu.reclaimDisplay();
    bombear(b);
    TEST_ASSERT_TRUE(b.tela.presentCount() > quadros);
    TEST_ASSERT_EQUAL_INT(code(MenuState::SubEixo), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.menu.ownsDisplay());
    TEST_ASSERT_TRUE(b.tela.showsExactly("Preset>"));
    TEST_ASSERT_EQUAL_STRING("Preset X", selecionado(b));

    // O eixo Y sai como o pedido do eixo Y, e nao como o do X.
    toque(b, Key::Down);
    TEST_ASSERT_EQUAL_STRING("Preset Y", selecionado(b));
    toque(b, Key::Menu);
    TEST_ASSERT_TRUE(b.menu.takeAction(pedido));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuAction::AjustaPresetY), static_cast<int>(pedido));
    b.menu.reclaimDisplay();
    bombear(b);

    // Voltar do submenu sobe ao nivel 1, nao ao Modo Normal.
    voltarAoNivel1(b, 2);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));
}

static void test_REQ_PRG_02_auto_calibracao_x_e_y_saem_como_pedido_de_assistente(void) {
    // docs/ihm-estados.md 3.4, D3: "Auto Cal>Voltar   Auto Calibracao X   Auto Calibracao Y",
    // os dois parametros distintos da Tabela 1 (L119 e L120).
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::AutoCalibracao);
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::SubEixo), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Auto Cal>"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Voltar"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Auto Calibracao X"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Auto Calibracao Y"));

    MenuAction pedido = MenuAction::None;
    toque(b, Key::Down);
    TEST_ASSERT_EQUAL_STRING("Auto Calibracao X", selecionado(b));
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Assistente), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.menu.takeAction(pedido));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuAction::AutoCalibracaoX),
                          static_cast<int>(pedido));

    b.menu.reclaimDisplay();
    bombear(b);
    toque(b, Key::Down);
    TEST_ASSERT_EQUAL_STRING("Auto Calibracao Y", selecionado(b));
    toque(b, Key::Menu);
    TEST_ASSERT_TRUE(b.menu.takeAction(pedido));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuAction::AutoCalibracaoY),
                          static_cast<int>(pedido));
    TEST_ASSERT_FALSE(b.menu.takeAction(pedido));
}

static void test_A9_troca_de_sentido_zera_o_offset_de_preset_e_avisa_por_3_s(void) {
    // A9, opcao A: o offset foi calculado contra o sentido antigo (offset := P - dir * bruto).
    // Mantido apos a inversao, ele desloca os DOIS pontos de atuacao do eixo em 2*offset.
    Bancada b;
    TEST_ASSERT_TRUE(b.ativo.setPresetOffset(Axis::X, 300).ok());
    entrarNoMenu(b);
    descerAte(b, MenuItem::SentidoSensor);
    toque(b, Key::Menu);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Sentido>"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Sentido Sensor X"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Sentido Sensor Y"));
    toque(b, Key::Down);
    TEST_ASSERT_EQUAL_STRING("Sentido Sensor X", selecionado(b));

    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::EditSentido), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Sentido Sensor X:"));
    // Tabela 2, L264: o eixo X sai de fabrica em Horario (manual 5.8, L191).
    TEST_ASSERT_TRUE(b.tela.showsExactly("Horario"));

    // Lista de duas opcoes, que anda nos DOIS sentidos: sem o ramo de UP nao ha volta de
    // Anti-horario para Horario.
    toque(b, Key::Down);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Anti-horario"));
    toque(b, Key::Up);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Horario"));
    toque(b, Key::Up);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Horario"));
    toque(b, Key::Down);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Anti-horario"));

    hold(b);
    // A9: aviso obrigatorio, com o eixo e os dois limites a conferir (L199).
    const uint32_t avisoDesde = b.relogio.nowMs();
    TEST_ASSERT_EQUAL_INT(code(MenuState::AvisoSentido), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Sentido X alterado!"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Preset zerado - confira X1 X2"));
    TEST_ASSERT_FALSE(b.tela.showsExactly("Alteracao bem sucedida!"));

    // O rascunho tem o sentido novo E o offset zerado; o agregado que comanda rele nao mudou.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorDir::CounterClockwise),
                          static_cast<int>(b.menu.draft().sensorDir(Axis::X)));
    TEST_ASSERT_EQUAL_INT16(0, b.menu.draft().presetOffsetDeci(Axis::X));
    TEST_ASSERT_TRUE(b.menu.pendingConfig());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorDir::Clockwise),
                          static_cast<int>(b.ativo.sensorDir(Axis::X)));
    TEST_ASSERT_EQUAL_INT16(300, b.ativo.presetOffsetDeci(Axis::X));

    // Aviso de 3 s de A9, na fronteira: 2999 ms ainda mostra, e gesto nenhum o encurta.
    toque(b, Key::Menu);
    toque(b, Key::Down);
    TEST_ASSERT_EQUAL_INT(code(MenuState::AvisoSentido), code(b.menu.state()));
    esperar(b, avisoDesde + 3000u - 1u - b.relogio.nowMs());
    TEST_ASSERT_EQUAL_INT(code(MenuState::AvisoSentido), code(b.menu.state()));
    esperar(b, 1);
    TEST_ASSERT_EQUAL_INT(code(MenuState::SubEixo), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Sentido>"));

    // Efetivacao unica: so no SAIR o eixo passa a ler invertido, e ja sem offset velho.
    voltarAoNivel1(b, 1);
    descerAte(b, MenuItem::Sair);
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Revisao), code(b.menu.state()));
    hold(b);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorDir::CounterClockwise),
                          static_cast<int>(b.ativo.sensorDir(Axis::X)));
    TEST_ASSERT_EQUAL_INT16(0, b.ativo.presetOffsetDeci(Axis::X));
    // O outro eixo nao foi tocado.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorDir::Clockwise),
                          static_cast<int>(b.ativo.sensorDir(Axis::Y)));
}

static void test_A9_sentido_do_eixo_y_avisa_com_os_limites_do_eixo_y(void) {
    Bancada b;
    TEST_ASSERT_TRUE(b.ativo.setPresetOffset(Axis::X, 200).ok());
    TEST_ASSERT_TRUE(b.ativo.setPresetOffset(Axis::Y, -450).ok());
    entrarNoMenu(b);
    descerAte(b, MenuItem::SentidoSensor);
    toque(b, Key::Menu);
    toque(b, Key::Down);
    toque(b, Key::Down);
    TEST_ASSERT_EQUAL_STRING("Sentido Sensor Y", selecionado(b));
    toque(b, Key::Menu);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Sentido Sensor Y:"));
    toque(b, Key::Down);
    hold(b);

    TEST_ASSERT_EQUAL_INT(code(MenuState::AvisoSentido), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Sentido Y alterado!"));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Preset zerado - confira Y1 Y2"));
    TEST_ASSERT_EQUAL_INT16(0, b.menu.draft().presetOffsetDeci(Axis::Y));
    // O eixo que ninguem tocou mantem o offset: A9 zera o offset DO EIXO trocado, so ele.
    TEST_ASSERT_EQUAL_INT16(200, b.menu.draft().presetOffsetDeci(Axis::X));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorDir::Clockwise),
                          static_cast<int>(b.menu.draft().sensorDir(Axis::X)));
}

static void test_REQ_DSP_04_digito_em_edicao_desenhado_em_inverse(void) {
    Bancada b;
    b.menu.begin();
    hold(b);
    // Login: o digito selecionado "permanecera piscando" (L101) - Inverse no quadro.
    TEST_ASSERT_TRUE(b.tela.showsExactly("Senha de acesso:0000"));
    TEST_ASSERT_TRUE(b.tela.hasInverse());

    digitarDoZero(b, Password::kFactory);
    hold(b);
    // Menu: nao ha campo em edicao; o unico Inverse e o ITEM selecionado da lista.
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));
    TEST_ASSERT_EQUAL_STRING("Voltar", selecionado(b));

    // "Edita senha:1234" (L231), abrindo no valor CORRENTE, com o digito piscando (L232).
    descerAte(b, MenuItem::Senha);
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::EditSenha), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Edita senha:1234"));
    TEST_ASSERT_TRUE(b.tela.hasInverse());

    // A tela de mensagem nao tem campo em edicao nem lista: nada de Inverse sobrando do
    // quadro anterior.
    toque(b, Key::Up);
    hold(b);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Alteracao bem sucedida!"));
    TEST_ASSERT_FALSE(b.tela.hasInverse());
}

static void test_REQ_DSP_03_telas_do_modo_programacao_sao_literais_do_manual(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Limite4);
    toque(b, Key::Menu);
    toque(b, Key::Down);
    toque(b, Key::Menu);

    // L217: "Valor Limite Y2(<grau>):+000,0" - em ASCII, "(graus)".
    TEST_ASSERT_TRUE(b.tela.showsExactly("Valor Limite Y2(graus):+000,0"));

    // L220 traz o mesmo campo programado em +025,0: MENU move o cursor para a esquerda e UP
    // sobe o digito (regra unica de cursor de A13).
    toque(b, Key::Menu);
    for (uint8_t i = 0; i < 5; ++i) {
        toque(b, Key::Up);
    }
    toque(b, Key::Menu);
    toque(b, Key::Up);
    toque(b, Key::Up);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Valor Limite Y2(graus):+025,0"));

    // L183: "Alteracao bem sucedida!", sem cedilha e sem til, como o manual imprime.
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::GravOk), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Alteracao bem sucedida!"));

    // A mensagem some sozinha no prazo T_MSG_OK e devolve ao nivel de origem, o submenu do
    // Limite 4. Na fronteira: 1499 ms ainda mostra a confirmacao.
    esperar(b, 1499);
    TEST_ASSERT_EQUAL_INT(code(MenuState::GravOk), code(b.menu.state()));
    esperar(b, 1);
    TEST_ASSERT_EQUAL_INT(code(MenuState::SubLimite), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Limite 4>"));
}

// --- A13: a efetivacao unica no SAIR ---

static void test_A13_efetivacao_unica_no_sair_nada_vale_antes_de_sair(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Limite4);
    toque(b, Key::Menu);
    toque(b, Key::Down);
    toque(b, Key::Menu);

    // Programa +025,0 no Valor Limite Y2 e confirma com o hold de 3 s.
    toque(b, Key::Menu);
    for (uint8_t i = 0; i < 5; ++i) {
        toque(b, Key::Up);
    }
    toque(b, Key::Menu);
    toque(b, Key::Up);
    toque(b, Key::Up);
    hold(b);
    esperar(b, 1500);

    // A ALTERACAO NAO VALE AINDA: o rascunho tem 250 decimos, o agregado que comanda rele
    // continua com o valor de fabrica.
    TEST_ASSERT_TRUE(b.menu.pendingConfig());
    TEST_ASSERT_EQUAL_INT16(250, b.menu.draft().limitValue(LimitId::Y2).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(Parameters::kDefaultLimitOffDeci,
                            b.ativo.limitValue(LimitId::Y2).deciDegrees());

    // Sobe ao nivel 1 e escolhe Sair (L132).
    voltarAoNivel1(b, 1);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));
    descerAte(b, MenuItem::Sair);
    toque(b, Key::Menu);

    // A13: tela de revisao antes de qualquer coisa comandar rele; ate aqui, nada mudou.
    TEST_ASSERT_EQUAL_INT(code(MenuState::Revisao), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("NOVA CONFIG - CONFIRMA?"));
    TEST_ASSERT_EQUAL_INT16(Parameters::kDefaultLimitOffDeci,
                            b.ativo.limitValue(LimitId::Y2).deciDegrees());

    // O hold de 3 s na revisao e o INSTANTE UNICO da efetivacao.
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
    TEST_ASSERT_FALSE(b.menu.pendingConfig());
    TEST_ASSERT_EQUAL_INT16(250, b.ativo.limitValue(LimitId::Y2).deciDegrees());
}

static void test_REQ_PRG_04_sair_sem_edicao_nao_mostra_revisao_e_nao_regrava(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Sair);
    toque(b, Key::Menu);

    // Sem edicao encostada nao ha o que revisar nem o que efetivar.
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
    TEST_ASSERT_FALSE(b.menu.pendingConfig());
    TEST_ASSERT_EQUAL_UINT16(Password::kFactory, b.senha.effective());
    TEST_ASSERT_EQUAL_INT16(Parameters::kDefaultLimitDeci,
                            b.ativo.limitValue(LimitId::X1).deciDegrees());
}

static void test_REQ_PRG_04_voltar_do_nivel_1_sai_como_sair(void) {
    // T33 e a contradicao 6 de docs/ihm-estados.md 5.3: no nivel 1, Voltar e Sair fazem a
    // mesma coisa. Sem isso o operador seleciona Voltar, aperta MENU e nada acontece.
    Bancada b;
    entrarNoMenu(b);
    TEST_ASSERT_EQUAL_INT(code(MenuItem::Voltar), code(b.menu.item()));
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
    TEST_ASSERT_FALSE(b.menu.pendingConfig());

    // E com edicao pendente, Voltar tambem passa pela revisao de A13.
    Bancada c;
    entrarNoMenu(c);
    descerAte(c, MenuItem::Senha);
    toque(c, Key::Menu);
    toque(c, Key::Up);
    hold(c);
    esperar(c, 1500);
    TEST_ASSERT_TRUE(c.menu.pendingConfig());
    descerAte(c, MenuItem::Voltar);
    TEST_ASSERT_EQUAL_INT(code(MenuItem::Voltar), code(c.menu.item()));
    toque(c, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Revisao), code(c.menu.state()));
    TEST_ASSERT_TRUE(c.tela.showsExactly("NOVA CONFIG - CONFIRMA?"));
}

static void test_A13_recusa_valor_fora_de_faixa_sem_clamp_silencioso(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Limite4);
    toque(b, Key::Menu);
    toque(b, Key::Down);
    toque(b, Key::Menu);

    // Leva o campo a +900,0 graus, muito alem dos +/-90,0 da Tabela 1.
    toque(b, Key::Menu);
    toque(b, Key::Menu);
    toque(b, Key::Menu);
    for (uint8_t i = 0; i < 9; ++i) {
        toque(b, Key::Up);
    }
    TEST_ASSERT_TRUE(b.tela.showsExactly("Valor Limite Y2(graus):+900,0"));

    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Recusa), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("FORA DA FAIXA +/-090,0"));
    // Nem o rascunho recebeu 900 grampeado: recusa nao grava nada.
    TEST_ASSERT_EQUAL_INT16(Parameters::kDefaultLimitOffDeci,
                            b.menu.draft().limitValue(LimitId::Y2).deciDegrees());
    TEST_ASSERT_FALSE(b.menu.pendingConfig());

    // A mensagem pisca por 2000 ms (A13) e devolve o campo, com o valor digitado ainda la para
    // correcao. Na fronteira: 1999 ms ainda e a recusa.
    esperar(b, 1999);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Recusa), code(b.menu.state()));
    esperar(b, 1);
    TEST_ASSERT_EQUAL_INT(code(MenuState::EditValor), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Valor Limite Y2(graus):+900,0"));
}

static void test_A13_timeout_nao_descarta_a_edicao_e_deixa_config_pendente(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Limite4);
    toque(b, Key::Menu);
    toque(b, Key::Down);
    toque(b, Key::Menu);
    toque(b, Key::Menu);
    for (uint8_t i = 0; i < 5; ++i) {
        toque(b, Key::Up);
    }
    hold(b);
    esperar(b, 1500);
    TEST_ASSERT_EQUAL_INT16(50, b.menu.draft().limitValue(LimitId::Y2).deciDegrees());

    // 120 s sem tecla: volta ao Modo Normal SEM descartar e SEM efetivar.
    esperar(b, 120000);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.menu.pendingConfig());
    TEST_ASSERT_EQUAL_INT16(50, b.menu.draft().limitValue(LimitId::Y2).deciDegrees());
    TEST_ASSERT_EQUAL_INT16(Parameters::kDefaultLimitOffDeci,
                            b.ativo.limitValue(LimitId::Y2).deciDegrees());

    // Reentrar continua a MESMA edicao, para revisar - e nao um rascunho novo.
    hold(b);
    digitarDoZero(b, Password::kFactory);
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));
    TEST_ASSERT_EQUAL_INT16(50, b.menu.draft().limitValue(LimitId::Y2).deciDegrees());

    descerAte(b, MenuItem::Sair);
    toque(b, Key::Menu);
    hold(b);
    TEST_ASSERT_EQUAL_INT16(50, b.ativo.limitValue(LimitId::Y2).deciDegrees());
}

static void test_REQ_PWD_05_senha_nova_so_vale_depois_do_sair(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Senha);
    toque(b, Key::Menu);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Edita senha:1234"));

    // 1234 -> 1235, um toque de UP no digito mais a direita, que e onde o campo abre.
    toque(b, Key::Up);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Edita senha:1235"));
    hold(b);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Alteracao bem sucedida!"));
    TEST_ASSERT_TRUE(b.menu.pendingConfig());

    // L237: "a nova senha somente passara a ser utilizada nos proximos acessos". Antes do
    // SAIR quem abre o painel continua sendo a senha antiga.
    TEST_ASSERT_EQUAL_UINT16(Password::kFactory, b.senha.effective());
    esperar(b, 1500);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));

    descerAte(b, MenuItem::Sair);
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Revisao), code(b.menu.state()));
    TEST_ASSERT_EQUAL_UINT16(Password::kFactory, b.senha.effective());

    hold(b);
    TEST_ASSERT_EQUAL_UINT16(1235, b.senha.effective());
    TEST_ASSERT_EQUAL_UINT16(1235, b.ativo.password());

    // E o proximo acesso e com a senha nova: a antiga nao entra mais.
    hold(b);
    digitarDoZero(b, Password::kFactory);
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::LoginErro), code(b.menu.state()));
    esperar(b, 2000);
    digitarDoZero(b, 1235);
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));
}

static void test_A13_revisao_pode_ser_adiada_sem_perder_a_edicao(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Senha);
    toque(b, Key::Menu);
    toque(b, Key::Up);
    hold(b);
    esperar(b, 1500);

    descerAte(b, MenuItem::Sair);
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Revisao), code(b.menu.state()));

    // Toque curto na revisao volta ao menu com a edicao ainda pendente: nada e efetivado e
    // nada e descartado.
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Menu), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.menu.pendingConfig());
    TEST_ASSERT_EQUAL_UINT16(Password::kFactory, b.senha.effective());
}

static void test_A13_confirmacao_sem_alteracao_nao_marca_config_pendente(void) {
    // Aviso de configuracao pendente que aparece sem configuracao pendente treina o operador a
    // ignorar o aviso - e e o mesmo aviso com que A13 garante que nada se perde em silencio.
    Bancada b;
    TEST_ASSERT_TRUE(b.ativo.setPresetOffset(Axis::X, 300).ok());
    entrarNoMenu(b);

    // Valor Limite: abre e confirma o proprio valor.
    descerAte(b, MenuItem::Limite4);
    toque(b, Key::Menu);
    toque(b, Key::Down);
    toque(b, Key::Menu);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Valor Limite Y2(graus):+000,0"));
    hold(b);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Alteracao bem sucedida!"));
    TEST_ASSERT_FALSE(b.menu.pendingConfig());
    esperar(b, 1500);

    // Operacao Limite: consulta e confirma o que ja estava.
    toque(b, Key::Down);
    toque(b, Key::Menu);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Off (desativado)"));
    hold(b);
    TEST_ASSERT_FALSE(b.menu.pendingConfig());
    esperar(b, 1500);
    voltarAoNivel1(b, 2);

    // Sentido do Sensor: confirmar sem trocar NAO zera o offset de PSET e nao dispara o aviso
    // de A9 - zerar a referencia de quem so foi consultar seria perda silenciosa.
    descerAte(b, MenuItem::SentidoSensor);
    toque(b, Key::Menu);
    toque(b, Key::Down);
    toque(b, Key::Menu);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Horario"));
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::GravOk), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Alteracao bem sucedida!"));
    TEST_ASSERT_FALSE(b.menu.pendingConfig());
    TEST_ASSERT_EQUAL_INT16(300, b.menu.draft().presetOffsetDeci(Axis::X));
    esperar(b, 1500);
    voltarAoNivel1(b, 1);

    // Senha: confirmar a mesma senha nao encosta senha nenhuma em Password.
    descerAte(b, MenuItem::Senha);
    toque(b, Key::Menu);
    TEST_ASSERT_TRUE(b.tela.showsExactly("Edita senha:1234"));
    hold(b);
    TEST_ASSERT_FALSE(b.menu.pendingConfig());
    TEST_ASSERT_FALSE(b.senha.staged());
    esperar(b, 1500);

    // Sem pendencia de verdade, o SAIR vai direto ao Modo Normal, sem tela de revisao.
    descerAte(b, MenuItem::Sair);
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
}

static void test_A13_gesto_em_tela_temporizada_e_ignorado(void) {
    // Invariante 6 de docs/ihm-estados.md secao 6: gesto sem funcao declarada e IGNORADO,
    // nunca reinterpretado. E aqui que um gesto vaza de uma tela para a seguinte.
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Limite4);
    toque(b, Key::Menu);
    toque(b, Key::Down);
    toque(b, Key::Menu);
    toque(b, Key::Up);
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::GravOk), code(b.menu.state()));

    // Em "Alteracao bem sucedida!" nada anda: nem o item do nivel 1, nem o estado.
    const int itemAntes = code(b.menu.item());
    toque(b, Key::Down);
    toque(b, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::GravOk), code(b.menu.state()));
    TEST_ASSERT_EQUAL_INT(itemAntes, code(b.menu.item()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Alteracao bem sucedida!"));
    esperar(b, 500);
    TEST_ASSERT_EQUAL_INT(code(MenuState::SubLimite), code(b.menu.state()));

    // Na recusa de A13 o campo tambem nao anda: o valor digitado tem de voltar intacto para
    // correcao.
    Bancada c;
    entrarNoMenu(c);
    descerAte(c, MenuItem::Limite4);
    toque(c, Key::Menu);
    toque(c, Key::Down);
    toque(c, Key::Menu);
    toque(c, Key::Menu);
    toque(c, Key::Menu);
    toque(c, Key::Menu);
    for (uint8_t i = 0; i < 9; ++i) {
        toque(c, Key::Up);
    }
    hold(c);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Recusa), code(c.menu.state()));
    toque(c, Key::Up);
    toque(c, Key::Up);
    toque(c, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Recusa), code(c.menu.state()));
    TEST_ASSERT_TRUE(c.tela.showsExactly("FORA DA FAIXA +/-090,0"));
    TEST_ASSERT_FALSE(c.menu.pendingConfig());
    esperar(c, 500);
    TEST_ASSERT_EQUAL_INT(code(MenuState::EditValor), code(c.menu.state()));
    TEST_ASSERT_TRUE(c.tela.showsExactly("Valor Limite Y2(graus):+900,0"));

    // E na recusa de senha (C2), idem.
    Bancada d;
    d.menu.begin();
    hold(d);
    digitarDoZero(d, 4321);
    hold(d);
    TEST_ASSERT_EQUAL_INT(code(MenuState::LoginErro), code(d.menu.state()));
    toque(d, Key::Up);
    toque(d, Key::Menu);
    TEST_ASSERT_EQUAL_INT(code(MenuState::LoginErro), code(d.menu.state()));
    TEST_ASSERT_TRUE(d.tela.showsExactly("Senha incorreta!"));
}

static void test_A13_begin_desarma_o_prazo_de_mensagem_e_nao_ressuscita_o_menu(void) {
    // ESTADO SEGURO: prazo de mensagem que sobrevivesse ao begin() venceria dentro do Modo
    // Normal e executaria state_ = backTo_, ressuscitando o Modo Programacao sem passar pelo
    // portao de senha - o unico controle de acesso ao setpoint dos quatro reles.
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Senha);
    toque(b, Key::Menu);
    toque(b, Key::Up);
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::GravOk), code(b.menu.state()));

    b.menu.begin();
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
    TEST_ASSERT_FALSE(b.menu.pendingConfig());

    esperar(b, 1500);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));
    TEST_ASSERT_FALSE(b.menu.ownsDisplay());
    esperar(b, 2000);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Normal), code(b.menu.state()));

    // E o unico caminho de volta ao Modo Programacao continua sendo o portao de senha.
    hold(b);
    TEST_ASSERT_EQUAL_INT(code(MenuState::Login), code(b.menu.state()));
}

namespace {

// --- LAYOUT: parte fixa no canto, conteudo grande -------------------------------------------
//
// Pedido do bigboss em 2026-09-01: fontes maiores e, ao entrar no menu, a parte FIXA vai para o
// canto superior esquerdo para sobrar tela para as opcoes. A regra que sai disso e verificavel:
// cabecalho de lista em Small colado em (0,0); item de lista, opcao escolhida e mensagem em
// Medium.
//
// A TELA DO EDITOR NUMERICO NAO SOBE DE FONTE, e isso e deliberado: "Valor Limite X1(graus):
// +000,0" tem 29 caracteres e so cabe nos 256 px em Small. Parti-la em rotulo pequeno mais
// campo grande contraria REQ-DSP-03, que exige a linha literal do manual - e mudanca de manual,
// nao de layout. Fica registrado aqui para quem for propor a errata.
//
// O invariante geometrico e o que impede a regra de virar promessa: com fonte maior, uma linha
// que antes cabia passa a vazar do painel ou a montar em cima da outra, e nenhum teste de
// conteudo enxerga isso - a string continua "aparecendo".

struct CaixaMenu {
    int16_t x0;
    int16_t y0;
    int16_t x1;
    int16_t y1;
};

CaixaMenu caixaMenuDoTexto(const FakeDisplay& painel, uint8_t indice) {
    const FakeDisplay::Draw& d = painel.draw(indice);
    const int16_t largura = static_cast<int16_t>(painel.textWidthPx(d.font, d.text));
    const int16_t altura = static_cast<int16_t>(painel.lineHeightPx(d.font));
    return CaixaMenu{d.x, d.y, static_cast<int16_t>(d.x + largura),
                     static_cast<int16_t>(d.y + altura)};
}

bool intersectaMenu(const CaixaMenu& a, const CaixaMenu& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

// O digito em Inverse e desenhado POR CIMA da linha, de proposito (REQ-DSP-04): e o unico par
// que pode se sobrepor, e so quando um dos dois e Inverse.
void verificarQuadroMenu(const FakeDisplay& painel) {
    TEST_ASSERT_TRUE(painel.drawCount() > 0u);
    for (uint8_t i = 0; i < painel.drawCount(); ++i) {
        const CaixaMenu a = caixaMenuDoTexto(painel, i);
        const char* texto = painel.draw(i).text;
        TEST_ASSERT_TRUE_MESSAGE(a.x0 >= 0, texto);
        TEST_ASSERT_TRUE_MESSAGE(a.y0 >= 0, texto);
        TEST_ASSERT_TRUE_MESSAGE(a.x1 <= 256, texto);
        TEST_ASSERT_TRUE_MESSAGE(a.y1 <= 64, texto);
        for (uint8_t j = static_cast<uint8_t>(i + 1u); j < painel.drawCount(); ++j) {
            if (painel.draw(i).ink == TextInk::Inverse ||
                painel.draw(j).ink == TextInk::Inverse) {
                continue;
            }
            TEST_ASSERT_FALSE_MESSAGE(intersectaMenu(a, caixaMenuDoTexto(painel, j)), texto);
        }
    }
}

}  // namespace

static void test_layout_cabecalho_fixo_fica_pequeno_no_canto_superior_esquerdo(void) {
    Bancada b;
    entrarNoMenu(b);

    TEST_ASSERT_TRUE(b.tela.showsExactly("Menu>"));
    TEST_ASSERT_TRUE(b.tela.fontOf("Menu>") == TextFont::Small);
    TEST_ASSERT_EQUAL_INT16(0, b.tela.xOf("Menu>"));
    TEST_ASSERT_EQUAL_INT16(0, b.tela.yOf("Menu>"));
    verificarQuadroMenu(b.tela);
}

static void test_layout_itens_da_lista_usam_a_fonte_maior(void) {
    Bancada b;
    entrarNoMenu(b);

    TEST_ASSERT_TRUE(b.tela.fontOf("Voltar") == TextFont::Medium);
    TEST_ASSERT_TRUE(b.tela.fontOf("Ajusta Preset") == TextFont::Medium);
    // O primeiro item comeca logo ABAIXO do cabecalho de 12 px, e nao numa terceira faixa
    // desperdicada: e este numero que devolve tela para as opcoes.
    TEST_ASSERT_TRUE(b.tela.yOf("Voltar") >= 12);
    TEST_ASSERT_TRUE(b.tela.yOf("Voltar") <= 16);
    verificarQuadroMenu(b.tela);
}

static void test_layout_submenu_de_limite_tambem_cabe_com_a_fonte_maior(void) {
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Limite4);
    toque(b, Key::Menu);

    TEST_ASSERT_TRUE(b.tela.fontOf("Limite 4>") == TextFont::Small);
    TEST_ASSERT_EQUAL_INT16(0, b.tela.yOf("Limite 4>"));
    // "Operacao Limite Y2" e o item mais largo da IHM: 18 caracteres em Medium.
    TEST_ASSERT_TRUE(b.tela.fontOf("Operacao Limite Y2") == TextFont::Medium);
    verificarQuadroMenu(b.tela);
}

static void test_layout_editor_numerico_continua_literal_do_manual_e_dentro_da_tela(void) {
    // REQ-DSP-03: a linha do manual e uma so, com rotulo e valor juntos. 29 caracteres so cabem
    // nos 256 px em Small - por isso este e o unico texto da IHM que NAO sobe de fonte.
    Bancada b;
    entrarNoMenu(b);
    descerAte(b, MenuItem::Limite4);
    toque(b, Key::Menu);
    toque(b, Key::Down);
    toque(b, Key::Menu);

    TEST_ASSERT_EQUAL_INT(code(MenuState::EditValor), code(b.menu.state()));
    TEST_ASSERT_TRUE(b.tela.showsExactly("Valor Limite Y2(graus):+000,0"));
    TEST_ASSERT_TRUE(b.tela.fontOf("Valor Limite Y2(graus):+000,0") == TextFont::Small);
    verificarQuadroMenu(b.tela);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_REQ_DSP_03_constantes_de_tela_sao_os_literais_do_contrato);
    RUN_TEST(test_REQ_PRG_01_os_dez_itens_na_ordem_literal_do_manual);
    RUN_TEST(test_REQ_PRG_01_lista_deslizante_mostra_os_vizinhos_do_item_selecionado);
    RUN_TEST(test_REQ_PRG_01_navegacao_circular_com_up_e_down);
    RUN_TEST(test_REQ_PWD_02_hold_de_menu_de_3_s_abre_a_tela_de_senha);
    RUN_TEST(test_REQ_PWD_03_senha_correta_abre_o_menu);
    RUN_TEST(test_REQ_PWD_04_senha_errada_mostra_senha_incorreta_e_permite_nova_tentativa);
    RUN_TEST(test_A13_bloqueio_de_60_s_fica_na_tela_ate_o_bloqueio_expirar);
    RUN_TEST(test_A13_bloqueio_recusa_ate_a_senha_certa_enquanto_dura);
    RUN_TEST(test_A13_emenda_1_sem_senha_o_hold_abre_o_menu_e_programa_igual);
    RUN_TEST(test_A13_emenda_1_com_senha_o_mesmo_hold_para_na_tela_de_login);
    RUN_TEST(test_REQ_PRG_03_timeout_de_120_s_sem_tecla_volta_ao_modo_normal);
    RUN_TEST(test_REQ_PRG_04_timeout_na_tela_de_senha_tambem_volta_ao_modo_normal);
    RUN_TEST(test_REQ_PRG_03_timeout_vale_na_revisao_e_nas_telas_temporizadas);
    RUN_TEST(test_REQ_PRG_02_menu_habilita_a_edicao_do_valor_limite);
    RUN_TEST(test_REQ_PRG_02_os_quatro_limites_tem_etiqueta_e_destino_proprios);
    RUN_TEST(test_REQ_PRG_02_operacao_limite_e_submenu_do_limite_n);
    RUN_TEST(test_REQ_PRG_02_preset_x_e_y_saem_como_pedido_de_assistente);
    RUN_TEST(test_REQ_PRG_02_auto_calibracao_x_e_y_saem_como_pedido_de_assistente);
    RUN_TEST(test_A9_troca_de_sentido_zera_o_offset_de_preset_e_avisa_por_3_s);
    RUN_TEST(test_A9_sentido_do_eixo_y_avisa_com_os_limites_do_eixo_y);
    RUN_TEST(test_REQ_DSP_04_digito_em_edicao_desenhado_em_inverse);
    RUN_TEST(test_REQ_DSP_03_telas_do_modo_programacao_sao_literais_do_manual);
    RUN_TEST(test_A13_efetivacao_unica_no_sair_nada_vale_antes_de_sair);
    RUN_TEST(test_REQ_PRG_04_sair_sem_edicao_nao_mostra_revisao_e_nao_regrava);
    RUN_TEST(test_REQ_PRG_04_voltar_do_nivel_1_sai_como_sair);
    RUN_TEST(test_A13_recusa_valor_fora_de_faixa_sem_clamp_silencioso);
    RUN_TEST(test_A13_timeout_nao_descarta_a_edicao_e_deixa_config_pendente);
    RUN_TEST(test_REQ_PWD_05_senha_nova_so_vale_depois_do_sair);
    RUN_TEST(test_A13_revisao_pode_ser_adiada_sem_perder_a_edicao);
    RUN_TEST(test_A13_confirmacao_sem_alteracao_nao_marca_config_pendente);
    RUN_TEST(test_A13_gesto_em_tela_temporizada_e_ignorado);
    RUN_TEST(test_A13_begin_desarma_o_prazo_de_mensagem_e_nao_ressuscita_o_menu);
    RUN_TEST(test_layout_cabecalho_fixo_fica_pequeno_no_canto_superior_esquerdo);
    RUN_TEST(test_layout_itens_da_lista_usam_a_fonte_maior);
    RUN_TEST(test_layout_submenu_de_limite_tambem_cabe_com_a_fonte_maior);
    RUN_TEST(test_layout_editor_numerico_continua_literal_do_manual_e_dentro_da_tela);
    return UNITY_END();
}
