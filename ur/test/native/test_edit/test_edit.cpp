// Testes de dominio do editor de campo (DigitEditor) e da senha de acesso (Password).
//
// Citacoes conferidas por numero de linha em docs/manual-cliente-sui-2026.txt (arquivo bruto).
//
// REQ-PWD-01: senha de 0000 a 9999, padrao de fabrica 1234 (Tabela 1 L131; L99 e L106).
// REQ-PWD-02: tela de login com edicao digito a digito e o digito selecionado piscando (L98 e
//             L101).
// REQ-PWD-03: senha correta abre o Menu de Opcoes (L103).
// REQ-PWD-04: senha incorreta recusa e permite nova tentativa (L103 e L104); ~2 minutos sem
//             tecla voltam ao Modo Normal (L105), e digitar a senha CONTA como tecla.
// REQ-PWD-05: "A nova senha somente passara a ser utilizada nos proximos acessos" (L237).
// REQ-PRG-04: saida do Modo Programacao pelo SAIR (L135) e por timeout de ~2 min (L136).
// REQ-DSP-04: o campo em edicao tem largura fixa (formato +XXX,X de L140) e informa qual
//             caractere pisca (L101, L232).
//
// Decisao A13: efetivacao unica no SAIR; regra unica de cursor (abre no digito mais a direita,
// MENU move para a esquerda, rolagem circular no campo); recusa explicita de valor fora de
// faixa, sem clamp silencioso, NOS DOIS EXTREMOS; bloqueio de senha de 60 s temporario e
// volatil; estado de boot e estado seguro (editor fechado, confirmacao reprovada).
//
// Os prazos sao medidos em TEMPO, com o FakeClock canonico de test/fakes/fake_clock.h, que
// comeca em 0xFFFF0000 e portanto atravessa o wrap de 2^32 ms nos prazos de 60 s e 120 s. Os
// avancos sao IRREGULARES de proposito: uma implementacao que contasse chamadas em vez de
// milissegundos nao passaria.
#include <stddef.h>
#include <string.h>
#include <unity.h>

#include "domain/digit_editor.h"
#include "domain/password.h"
#include "fakes/fake_clock.h"

using domain::AccessResult;
using domain::ConfirmResult;
using domain::DigitEditor;
using domain::DigitFieldSpec;
using domain::Password;
using test::FakeClock;

// Campo angular de Preset (L155 a L157) e de Valor Limite (L213): +XXX,X em decimos de grau,
// faixa -90,0 a +90,0 (Tabela 1 L117, L122). A mensagem de recusa e a de A13.
static const DigitFieldSpec kCampoAngular = {4, 1, true, -900, 900, "FORA DA FAIXA +/-090,0"};

// Campo de senha: 'Senha de acesso:0000' (L98) e 'Edita senha:1234' (L231), sem sinal.
static const DigitFieldSpec kCampoSenha = {4, 0, false, 0, 9999, ""};

// Campo de trim da Auto Calibracao: 'Ajuste 0Vcc:0000' (L172), neutro em 5000 por A14.
static const DigitFieldSpec kCampoTrim = {4, 0, false, 0, 9999, "TRIM FORA DA FAIXA"};

static int code(AccessResult resultado) { return static_cast<int>(resultado); }
static int code(ConfirmResult resultado) { return static_cast<int>(resultado); }

void setUp(void) {}
void tearDown(void) {}

// --- A13: regra unica de cursor ---

static void test_A13_cursor_abre_no_digito_mais_a_direita(void) {
    DigitEditor editor;
    TEST_ASSERT_TRUE(editor.open(kCampoSenha, 0));
    TEST_ASSERT_EQUAL_UINT8(3, editor.cursor());

    TEST_ASSERT_TRUE(editor.open(kCampoAngular, 0));
    TEST_ASSERT_EQUAL_UINT8(3, editor.cursor());
}

static void test_A13_menu_move_para_a_esquerda_e_da_a_volta(void) {
    DigitEditor editor;
    TEST_ASSERT_TRUE(editor.open(kCampoSenha, 0));

    editor.menu();
    TEST_ASSERT_EQUAL_UINT8(2, editor.cursor());
    editor.menu();
    TEST_ASSERT_EQUAL_UINT8(1, editor.cursor());
    editor.menu();
    TEST_ASSERT_EQUAL_UINT8(0, editor.cursor());

    // Rolagem circular dentro do campo: do mais a esquerda volta ao mais a direita.
    editor.menu();
    TEST_ASSERT_EQUAL_UINT8(3, editor.cursor());
}

static void test_A13_up_rola_de_9_para_0(void) {
    DigitEditor editor;
    TEST_ASSERT_TRUE(editor.open(kCampoSenha, 9));
    TEST_ASSERT_EQUAL_INT16(9, editor.value());

    editor.up();
    TEST_ASSERT_EQUAL_INT16(0, editor.value());
}

static void test_A13_down_rola_de_0_para_9(void) {
    DigitEditor editor;
    TEST_ASSERT_TRUE(editor.open(kCampoSenha, 0));

    editor.down();
    TEST_ASSERT_EQUAL_INT16(9, editor.value());
}

static void test_A13_editar_um_digito_nao_mexe_nos_outros(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];
    TEST_ASSERT_TRUE(editor.open(kCampoSenha, 1234));

    editor.menu();
    editor.up();
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("1244", texto);
    TEST_ASSERT_EQUAL_INT16(1244, editor.value());
}

// --- REQ-PWD-02: a senha e digitada digito a digito na tela de login (L101) ---

static void test_REQ_PWD_02_digita_1234_a_partir_de_0000(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];
    TEST_ASSERT_TRUE(editor.open(kCampoSenha, 0));
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("0000", texto);

    for (int i = 0; i < 4; ++i) {
        editor.up();
    }
    editor.menu();
    for (int i = 0; i < 3; ++i) {
        editor.up();
    }
    editor.menu();
    for (int i = 0; i < 2; ++i) {
        editor.up();
    }
    editor.menu();
    editor.up();

    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("1234", texto);
    TEST_ASSERT_EQUAL_INT16(1234, editor.value());
}

// --- A13 e manual L157: nos campos angulares a tecla DOWN e o sinal ---

static void test_A13_down_troca_o_sinal_em_mais_menos_90_graus(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, 900));
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("+090,0", texto);

    editor.down();
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-090,0", texto);
    TEST_ASSERT_EQUAL_INT16(-900, editor.value());
    TEST_ASSERT_TRUE(editor.negative());

    editor.down();
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("+090,0", texto);
    TEST_ASSERT_EQUAL_INT16(900, editor.value());
    TEST_ASSERT_FALSE(editor.negative());
}

static void test_A13_down_no_campo_angular_nao_altera_digito(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, 250));

    editor.down();
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-025,0", texto);
}

static void test_A13_sinal_no_zero_aparece_e_nao_inventa_valor(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, 0));

    editor.down();
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-000,0", texto);
    TEST_ASSERT_EQUAL_INT16(0, editor.value());
    TEST_ASSERT_EQUAL_INT(code(ConfirmResult::Ok), code(editor.confirm()));
}

// --- A13: open() recarrega o SINAL do valor ja gravado ---

static void test_A13_open_carrega_o_sinal_do_valor_gravado(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];

    // Caso normal em campo: reabrir um Valor Limite gravado em -25,0 graus.
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, -250));
    TEST_ASSERT_TRUE(editor.negative());
    TEST_ASSERT_EQUAL_INT16(-250, editor.value());
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-025,0", texto);
    TEST_ASSERT_EQUAL_INT(code(ConfirmResult::Ok), code(editor.confirm()));

    // E o extremo negativo gravado volta intacto.
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, -900));
    TEST_ASSERT_TRUE(editor.negative());
    TEST_ASSERT_EQUAL_INT16(-900, editor.value());
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-090,0", texto);

    // Reabrir com valor positivo tem de LIMPAR o sinal herdado da abertura anterior.
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, 250));
    TEST_ASSERT_FALSE(editor.negative());
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("+025,0", texto);
}

// --- A13: confirmacao recusa fora de faixa, sem clamp silencioso ---

static void test_A13_confirmacao_recusa_1200_decimos_e_nao_grava_900(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, 0));

    // Digita +120,0 graus: centena no digito 0, dezena no digito 1.
    editor.menu();
    editor.menu();
    editor.menu();
    editor.up();
    editor.menu();
    editor.menu();
    editor.menu();
    editor.up();
    editor.up();

    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("+120,0", texto);
    TEST_ASSERT_EQUAL_STRING("FORA DA FAIXA +/-090,0", editor.outOfRangeMessage());

    // confirm() e consulta PURA: nem valor, nem digito, nem cursor mudam na recusa, e chamar
    // duas vezes da o mesmo veredito. Quem grava e o chamador, e so quando o resultado e Ok.
    const uint8_t cursorAntes = editor.cursor();
    TEST_ASSERT_EQUAL_INT(code(ConfirmResult::OutOfRange), code(editor.confirm()));
    TEST_ASSERT_EQUAL_INT(code(ConfirmResult::OutOfRange), code(editor.confirm()));
    TEST_ASSERT_EQUAL_UINT8(cursorAntes, editor.cursor());
    TEST_ASSERT_EQUAL_INT16(1200, editor.value());

    // O que o tecnico digitou continua na tela: nada de 900 gravado em silencio.
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("+120,0", texto);
}

static void test_A13_confirmacao_aceita_os_extremos_e_recusa_um_decimo_alem(void) {
    DigitEditor editor;
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, 900));
    TEST_ASSERT_EQUAL_INT(code(ConfirmResult::Ok), code(editor.confirm()));

    editor.down();
    TEST_ASSERT_EQUAL_INT16(-900, editor.value());
    TEST_ASSERT_EQUAL_INT(code(ConfirmResult::Ok), code(editor.confirm()));

    editor.down();
    editor.up();
    TEST_ASSERT_EQUAL_INT16(901, editor.value());
    TEST_ASSERT_EQUAL_INT(code(ConfirmResult::OutOfRange), code(editor.confirm()));
}

// A fronteira NEGATIVA tem de ser recusada com o mesmo rigor da positiva: sem este teste, uma
// confirmacao que so olhasse spec_.max aceitaria -120,0 graus como setpoint de rele.
static void test_A13_confirmacao_recusa_o_lado_negativo_com_o_mesmo_rigor(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];

    // Um decimo ALEM do extremo negativo: -090,1.
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, -900));
    editor.up();
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-090,1", texto);
    TEST_ASSERT_EQUAL_INT16(-901, editor.value());
    TEST_ASSERT_EQUAL_INT(code(ConfirmResult::OutOfRange), code(editor.confirm()));

    // E bem alem: -190,0.
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, 900));
    editor.down();
    editor.menu();
    editor.menu();
    editor.menu();
    editor.up();
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("-190,0", texto);
    TEST_ASSERT_EQUAL_INT16(-1900, editor.value());
    TEST_ASSERT_EQUAL_INT(code(ConfirmResult::OutOfRange), code(editor.confirm()));
    TEST_ASSERT_EQUAL_STRING("FORA DA FAIXA +/-090,0", editor.outOfRangeMessage());
}

// --- REQ-DSP-04: largura fixa e digito que pisca ---

static void test_REQ_DSP_04_campo_angular_tem_largura_fixa_e_marca_o_digito_que_pisca(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, 900));
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_UINT(6u, static_cast<unsigned>(strlen(texto)));

    TEST_ASSERT_EQUAL_UINT8(5, editor.cursorTextIndex());
    TEST_ASSERT_EQUAL_CHAR('0', texto[editor.cursorTextIndex()]);

    editor.menu();
    TEST_ASSERT_EQUAL_UINT8(3, editor.cursorTextIndex());
    TEST_ASSERT_EQUAL_CHAR('0', texto[editor.cursorTextIndex()]);

    editor.menu();
    TEST_ASSERT_EQUAL_UINT8(2, editor.cursorTextIndex());
    TEST_ASSERT_EQUAL_CHAR('9', texto[editor.cursorTextIndex()]);

    editor.down();
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_UINT(6u, static_cast<unsigned>(strlen(texto)));
}

static void test_REQ_DSP_04_campo_de_senha_nao_tem_sinal_nem_virgula(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];
    TEST_ASSERT_TRUE(editor.open(kCampoSenha, 1234));
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("1234", texto);
    TEST_ASSERT_EQUAL_UINT8(3, editor.cursorTextIndex());
    TEST_ASSERT_EQUAL_CHAR('4', texto[editor.cursorTextIndex()]);
}

static void test_REQ_DSP_04_format_recusa_buffer_curto_sem_escrever(void) {
    DigitEditor editor;
    char texto[4] = {'z', 'z', 'z', 'z'};
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, 250));
    TEST_ASSERT_FALSE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_CHAR('z', texto[0]);
}

// --- A13: a mesma regra serve os quatro campos ---

static void test_A13_mesma_regra_de_cursor_no_trim_da_auto_calibracao(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];
    TEST_ASSERT_TRUE(editor.open(kCampoTrim, 5000));
    TEST_ASSERT_TRUE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_STRING("5000", texto);
    TEST_ASSERT_EQUAL_UINT8(3, editor.cursor());

    editor.up();
    TEST_ASSERT_EQUAL_INT16(5001, editor.value());
}

// --- A13: aberturas impossiveis sao recusadas, nao truncadas ---

static void test_A13_open_recusa_spec_impossivel_e_valor_que_nao_cabe(void) {
    DigitEditor editor;
    const DigitFieldSpec semDigito = {0, 0, false, 0, 0, ""};
    const DigitFieldSpec digitosDemais = {5, 0, false, 0, 9999, ""};
    const DigitFieldSpec soDecimais = {2, 2, false, 0, 99, ""};
    const DigitFieldSpec faixaInvertida = {4, 0, false, 100, 10, ""};

    TEST_ASSERT_FALSE(editor.open(semDigito, 0));
    TEST_ASSERT_FALSE(editor.open(digitosDemais, 0));
    TEST_ASSERT_FALSE(editor.open(soDecimais, 0));
    TEST_ASSERT_FALSE(editor.open(faixaInvertida, 0));

    // 12345 nao cabe em 4 digitos, e senha nao tem sinal.
    TEST_ASSERT_FALSE(editor.open(kCampoSenha, 12345));
    TEST_ASSERT_FALSE(editor.open(kCampoSenha, -1));
}

// Estado seguro: open() reprovado FECHA o editor. Deixa-lo vivo na spec anterior faria a tela
// mostrar o campo antigo, com a faixa antiga, enquanto o tecnico acredita editar outro
// parametro - a falha ignorada tem de virar tela morta, nunca tela errada.
static void test_A13_open_reprovado_fecha_o_editor_em_vez_de_manter_o_campo_anterior(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];
    TEST_ASSERT_TRUE(editor.open(kCampoAngular, 250));
    TEST_ASSERT_EQUAL_INT16(250, editor.value());

    TEST_ASSERT_FALSE(editor.open(kCampoSenha, 12345));

    TEST_ASSERT_FALSE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_INT16(0, editor.value());
    TEST_ASSERT_FALSE(editor.negative());
    TEST_ASSERT_EQUAL_INT(code(ConfirmResult::OutOfRange), code(editor.confirm()));
    TEST_ASSERT_EQUAL_STRING("", editor.outOfRangeMessage());

    // E as teclas nao ressuscitam o campo anterior.
    editor.up();
    editor.down();
    editor.menu();
    TEST_ASSERT_EQUAL_INT16(0, editor.value());
    TEST_ASSERT_FALSE(editor.format(texto, sizeof(texto)));
}

static void test_A13_editor_fechado_ignora_tecla_e_nao_confirma(void) {
    DigitEditor editor;
    char texto[DigitEditor::kTextCap];

    editor.up();
    editor.down();
    editor.menu();

    TEST_ASSERT_FALSE(editor.format(texto, sizeof(texto)));
    TEST_ASSERT_EQUAL_INT16(0, editor.value());
    TEST_ASSERT_EQUAL_INT(code(ConfirmResult::OutOfRange), code(editor.confirm()));

    // No unico estado em que o chamador pede a mensagem, ela nao pode ser ponteiro nulo: o
    // contrato de A13 e "se confirm() != Ok, pisca outOfRangeMessage()", e nao ha excecoes
    // neste projeto para amortecer um nullptr entregue ao IDisplay.
    TEST_ASSERT_NOT_NULL(editor.outOfRangeMessage());
    TEST_ASSERT_EQUAL_STRING("", editor.outOfRangeMessage());
}

// Spec montada pelo chamador sem a mensagem: o acessor blinda, em vez de repassar o nulo.
static void test_A13_spec_sem_mensagem_devolve_texto_vazio_e_nao_nulo(void) {
    DigitEditor editor;
    const DigitFieldSpec semMensagem = {4, 1, true, -900, 900, nullptr};
    TEST_ASSERT_TRUE(editor.open(semMensagem, 900));
    editor.up();
    TEST_ASSERT_EQUAL_INT(code(ConfirmResult::OutOfRange), code(editor.confirm()));
    TEST_ASSERT_NOT_NULL(editor.outOfRangeMessage());
    TEST_ASSERT_EQUAL_STRING("", editor.outOfRangeMessage());
}

// --- REQ-PWD-01: faixa e padrao de fabrica ---

static void test_REQ_PWD_01_padrao_de_fabrica_e_1234_e_a_faixa_e_0000_a_9999(void) {
    FakeClock relogio;
    Password senha(relogio);

    TEST_ASSERT_EQUAL_UINT16(1234, senha.effective());
    TEST_ASSERT_EQUAL_UINT16(Password::kFactory, senha.effective());
    TEST_ASSERT_TRUE(Password::inRange(0));
    TEST_ASSERT_TRUE(Password::inRange(9999));
    TEST_ASSERT_FALSE(Password::inRange(10000));

    TEST_ASSERT_TRUE(senha.load(5678));
    TEST_ASSERT_EQUAL_UINT16(5678, senha.effective());

    // Valor impossivel e recusado e nao derruba a senha vigente.
    TEST_ASSERT_FALSE(senha.load(10000));
    TEST_ASSERT_EQUAL_UINT16(5678, senha.effective());
    TEST_ASSERT_FALSE(senha.stage(10000));
    TEST_ASSERT_FALSE(senha.staged());
}

// --- REQ-PWD-03 e REQ-PWD-04: senha certa, senha errada ---

static void test_REQ_PWD_03_senha_certa_concede_acesso(void) {
    FakeClock relogio;
    Password senha(relogio);

    TEST_ASSERT_EQUAL_INT(code(AccessResult::Granted), code(senha.submit(1234)));
    TEST_ASSERT_EQUAL_UINT8(Password::kMaxAttempts, senha.attemptsLeft());
}

static void test_REQ_PWD_04_senha_errada_nega_e_permite_nova_tentativa(void) {
    FakeClock relogio;
    Password senha(relogio);

    TEST_ASSERT_EQUAL_INT(code(AccessResult::Wrong), code(senha.submit(1233)));
    TEST_ASSERT_EQUAL_UINT8(4, senha.attemptsLeft());
    TEST_ASSERT_FALSE(senha.locked());

    TEST_ASSERT_EQUAL_INT(code(AccessResult::Granted), code(senha.submit(1234)));
    TEST_ASSERT_EQUAL_UINT8(Password::kMaxAttempts, senha.attemptsLeft());
}

// --- A13: bloqueio de 60 s temporario e volatil ---

static void test_A13_cinco_erros_bloqueiam_e_o_bloqueio_recusa_ate_a_senha_certa(void) {
    FakeClock relogio;
    Password senha(relogio);

    for (uint8_t tentativa = 0; tentativa < Password::kMaxAttempts; ++tentativa) {
        TEST_ASSERT_EQUAL_INT(code(AccessResult::Wrong), code(senha.submit(0)));
    }
    TEST_ASSERT_TRUE(senha.locked());
    TEST_ASSERT_EQUAL_UINT8(0, senha.attemptsLeft());
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Locked), code(senha.submit(1234)));
}

// O bloqueio expira por TEMPO decorrido, nao por numero de consultas: o relogio anda em passos
// irregulares que somam 59999 ms, e so o milissegundo 60000 destrava.
static void test_A13_bloqueio_expira_sozinho_em_60_s(void) {
    FakeClock relogio;
    Password senha(relogio);

    for (uint8_t tentativa = 0; tentativa < Password::kMaxAttempts; ++tentativa) {
        senha.submit(0);
    }
    TEST_ASSERT_TRUE(senha.locked());

    static const uint32_t kPassos[] = {1u, 999u, 20000u, 3u, 38996u};
    uint32_t somado = 0;
    for (size_t i = 0; i < sizeof(kPassos) / sizeof(kPassos[0]); ++i) {
        relogio.advanceMs(kPassos[i]);
        somado += kPassos[i];
        TEST_ASSERT_TRUE(senha.locked());
        TEST_ASSERT_EQUAL_UINT8(0, senha.attemptsLeft());
    }
    TEST_ASSERT_EQUAL_UINT32(Password::kLockoutMs - 1u, somado);
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Locked), code(senha.submit(1234)));

    relogio.advanceMs(1);
    TEST_ASSERT_FALSE(senha.locked());
    TEST_ASSERT_EQUAL_UINT8(Password::kMaxAttempts, senha.attemptsLeft());
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Granted), code(senha.submit(1234)));
    TEST_ASSERT_EQUAL_UINT8(Password::kMaxAttempts, senha.attemptsLeft());
}

static void test_A13_bloqueio_e_volatil_e_nao_sobrevive_ao_boot(void) {
    FakeClock relogio;
    Password senha(relogio);
    for (uint8_t tentativa = 0; tentativa < Password::kMaxAttempts; ++tentativa) {
        senha.submit(0);
    }
    TEST_ASSERT_TRUE(senha.locked());

    // Religou: o bloqueio vive so na RAM, entao o equipamento volta destravado.
    Password aposBoot(relogio);
    TEST_ASSERT_FALSE(aposBoot.locked());
    TEST_ASSERT_EQUAL_UINT8(Password::kMaxAttempts, aposBoot.attemptsLeft());
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Granted), code(aposBoot.submit(1234)));
}

// --- REQ-PWD-04 e REQ-PRG-04: timeout de 2 minutos sem tecla (L105 e L136) ---

static void test_REQ_PWD_04_timeout_de_120_s_sem_tecla(void) {
    FakeClock relogio;
    Password senha(relogio);

    // Passos irregulares somando 119999 ms: o prazo e TEMPO, nao contagem de ciclos.
    static const uint32_t kPassos[] = {1u, 37u, 4000u, 55u, 63907u, 51999u};
    uint32_t somado = 0;
    for (size_t i = 0; i < sizeof(kPassos) / sizeof(kPassos[0]); ++i) {
        relogio.advanceMs(kPassos[i]);
        somado += kPassos[i];
        TEST_ASSERT_FALSE(senha.timedOut());
    }
    TEST_ASSERT_EQUAL_UINT32(Password::kInactivityMs - 1u, somado);

    relogio.advanceMs(1);
    TEST_ASSERT_TRUE(senha.timedOut());
}

static void test_REQ_PRG_04_qualquer_tecla_rearma_o_timeout(void) {
    FakeClock relogio;
    Password senha(relogio);

    relogio.advanceMs(Password::kInactivityMs - 1u);
    TEST_ASSERT_FALSE(senha.timedOut());
    senha.noteActivity();

    static const uint32_t kPassos[] = {7u, 119000u, 991u, 1u};
    uint32_t somado = 0;
    for (size_t i = 0; i < sizeof(kPassos) / sizeof(kPassos[0]); ++i) {
        relogio.advanceMs(kPassos[i]);
        somado += kPassos[i];
        TEST_ASSERT_FALSE(senha.timedOut());
    }
    TEST_ASSERT_EQUAL_UINT32(Password::kInactivityMs - 1u, somado);

    relogio.advanceMs(1);
    TEST_ASSERT_TRUE(senha.timedOut());
}

// L105 conta INATIVIDADE, e digitar a senha e atividade: sem o carimbo dentro de submit(), o
// tecnico que erra aos 119 s e reenvia cai no Modo Normal no meio da tentativa.
static void test_REQ_PWD_04_tentativa_de_senha_conta_como_atividade(void) {
    FakeClock relogio;
    Password senha(relogio);

    relogio.advanceMs(Password::kInactivityMs - 1u);
    TEST_ASSERT_FALSE(senha.timedOut());
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Wrong), code(senha.submit(1233)));

    relogio.advanceMs(60000u);
    TEST_ASSERT_FALSE(senha.timedOut());
    relogio.advanceMs(59999u);
    TEST_ASSERT_FALSE(senha.timedOut());
    relogio.advanceMs(1);
    TEST_ASSERT_TRUE(senha.timedOut());

    // A senha CERTA tambem carimba, e o prazo volta a contar do zero a partir dela.
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Granted), code(senha.submit(1234)));
    TEST_ASSERT_FALSE(senha.timedOut());
    relogio.advanceMs(Password::kInactivityMs - 1u);
    TEST_ASSERT_FALSE(senha.timedOut());
    relogio.advanceMs(1);
    TEST_ASSERT_TRUE(senha.timedOut());
}

// --- REQ-PWD-05 e A13: a nova senha so vale no acesso seguinte (L237) ---

static void test_REQ_PWD_05_senha_antiga_ainda_acessa_logo_depois_da_troca(void) {
    FakeClock relogio;
    Password senha(relogio);

    TEST_ASSERT_TRUE(senha.stage(5678));
    TEST_ASSERT_TRUE(senha.staged());

    // Acesso feito logo depois da troca: e a senha antiga que vale (L237).
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Granted), code(senha.submit(1234)));
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Wrong), code(senha.submit(5678)));
    TEST_ASSERT_EQUAL_UINT16(1234, senha.effective());
}

static void test_REQ_PRG_04_a_efetivacao_da_senha_acontece_no_sair(void) {
    FakeClock relogio;
    Password senha(relogio);
    TEST_ASSERT_TRUE(senha.stage(5678));

    senha.commitOnExit();

    TEST_ASSERT_FALSE(senha.staged());
    TEST_ASSERT_EQUAL_UINT16(5678, senha.effective());
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Granted), code(senha.submit(5678)));
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Wrong), code(senha.submit(1234)));
}

// commitOnExit() e o UNICO ponto de efetivacao de A13, e por isso e a unica funcao que precisa
// ser inerte no estado neutro: sem a guarda, todo SAIR sem troca de senha gravaria o valor
// encostado - que no boot e o de fabrica - e a senha do painel voltaria sozinha para 1234.
static void test_A13_sair_sem_edicao_nao_mexe_na_senha_vigente(void) {
    FakeClock relogio;
    Password senha(relogio);
    TEST_ASSERT_TRUE(senha.load(5678));

    senha.commitOnExit();

    TEST_ASSERT_FALSE(senha.staged());
    TEST_ASSERT_EQUAL_UINT16(5678, senha.effective());
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Granted), code(senha.submit(5678)));
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Wrong), code(senha.submit(Password::kFactory)));

    // Idempotente: dois SAIR seguidos nao repoem a senha de fabrica.
    senha.commitOnExit();
    senha.commitOnExit();
    TEST_ASSERT_EQUAL_UINT16(5678, senha.effective());
    TEST_ASSERT_EQUAL_INT(code(AccessResult::Wrong), code(senha.submit(Password::kFactory)));

    // E depois de uma efetivacao de verdade, o SAIR seguinte tambem tem de ser inerte.
    TEST_ASSERT_TRUE(senha.stage(4321));
    senha.commitOnExit();
    TEST_ASSERT_EQUAL_UINT16(4321, senha.effective());
    TEST_ASSERT_TRUE(senha.load(5678));
    senha.commitOnExit();
    TEST_ASSERT_EQUAL_UINT16(5678, senha.effective());
}

static void test_A13_timeout_nao_apaga_a_edicao_pendente(void) {
    FakeClock relogio;
    Password senha(relogio);
    TEST_ASSERT_TRUE(senha.stage(5678));

    relogio.advanceMs(Password::kInactivityMs);
    TEST_ASSERT_TRUE(senha.timedOut());

    // A13 opcao A: o timeout deixa a configuracao PENDENTE para revisao, nao descarta em
    // silencio; e continua sendo a senha antiga que autoriza o acesso ate o SAIR.
    TEST_ASSERT_TRUE(senha.staged());
    TEST_ASSERT_EQUAL_UINT16(1234, senha.effective());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_A13_cursor_abre_no_digito_mais_a_direita);
    RUN_TEST(test_A13_menu_move_para_a_esquerda_e_da_a_volta);
    RUN_TEST(test_A13_up_rola_de_9_para_0);
    RUN_TEST(test_A13_down_rola_de_0_para_9);
    RUN_TEST(test_A13_editar_um_digito_nao_mexe_nos_outros);
    RUN_TEST(test_REQ_PWD_02_digita_1234_a_partir_de_0000);
    RUN_TEST(test_A13_down_troca_o_sinal_em_mais_menos_90_graus);
    RUN_TEST(test_A13_down_no_campo_angular_nao_altera_digito);
    RUN_TEST(test_A13_sinal_no_zero_aparece_e_nao_inventa_valor);
    RUN_TEST(test_A13_open_carrega_o_sinal_do_valor_gravado);
    RUN_TEST(test_A13_confirmacao_recusa_1200_decimos_e_nao_grava_900);
    RUN_TEST(test_A13_confirmacao_aceita_os_extremos_e_recusa_um_decimo_alem);
    RUN_TEST(test_A13_confirmacao_recusa_o_lado_negativo_com_o_mesmo_rigor);
    RUN_TEST(test_REQ_DSP_04_campo_angular_tem_largura_fixa_e_marca_o_digito_que_pisca);
    RUN_TEST(test_REQ_DSP_04_campo_de_senha_nao_tem_sinal_nem_virgula);
    RUN_TEST(test_REQ_DSP_04_format_recusa_buffer_curto_sem_escrever);
    RUN_TEST(test_A13_mesma_regra_de_cursor_no_trim_da_auto_calibracao);
    RUN_TEST(test_A13_open_recusa_spec_impossivel_e_valor_que_nao_cabe);
    RUN_TEST(test_A13_open_reprovado_fecha_o_editor_em_vez_de_manter_o_campo_anterior);
    RUN_TEST(test_A13_editor_fechado_ignora_tecla_e_nao_confirma);
    RUN_TEST(test_A13_spec_sem_mensagem_devolve_texto_vazio_e_nao_nulo);
    RUN_TEST(test_REQ_PWD_01_padrao_de_fabrica_e_1234_e_a_faixa_e_0000_a_9999);
    RUN_TEST(test_REQ_PWD_03_senha_certa_concede_acesso);
    RUN_TEST(test_REQ_PWD_04_senha_errada_nega_e_permite_nova_tentativa);
    RUN_TEST(test_A13_cinco_erros_bloqueiam_e_o_bloqueio_recusa_ate_a_senha_certa);
    RUN_TEST(test_A13_bloqueio_expira_sozinho_em_60_s);
    RUN_TEST(test_A13_bloqueio_e_volatil_e_nao_sobrevive_ao_boot);
    RUN_TEST(test_REQ_PWD_04_timeout_de_120_s_sem_tecla);
    RUN_TEST(test_REQ_PRG_04_qualquer_tecla_rearma_o_timeout);
    RUN_TEST(test_REQ_PWD_04_tentativa_de_senha_conta_como_atividade);
    RUN_TEST(test_REQ_PWD_05_senha_antiga_ainda_acessa_logo_depois_da_troca);
    RUN_TEST(test_REQ_PRG_04_a_efetivacao_da_senha_acontece_no_sair);
    RUN_TEST(test_A13_sair_sem_edicao_nao_mexe_na_senha_vigente);
    RUN_TEST(test_A13_timeout_nao_apaga_a_edicao_pendente);
    return UNITY_END();
}
