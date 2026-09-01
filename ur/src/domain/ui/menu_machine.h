// Maquina do Modo Programacao: o portao de senha, o menu de dez itens do manual, os submenus,
// os editores de parametro e o instante unico em que a configuracao editada passa a comandar
// rele. Consome GESTO (KeyGesture) e desenha em IDisplay; nao conhece tecla, pino nem NVS.
//
// Manual SUI-DI141388XY (docs/manual-cliente-sui-2026.txt, citado por numero de linha do
// arquivo bruto; docs/ihm-estados.md secoes 3.3 a 3.5 amarram cada tela ao estado):
//   5.2  L90  - "Mantida pressionada por aproximadamente 3 segundos, permite o acesso ao Modo
//                Programacao": o hold de MENU no Modo Normal abre a tela de senha.
//   5.3  L98  - tela de login literal "Senha de acesso:0000"; L100 com a senha digitada.
//   5.3  L101 - MENU move para o digito a esquerda, UP e DOWN editam, o digito piscando.
//   5.3  L103 - senha correta abre o Menu de Opcoes; senha errada mostra "Senha incorreta!"
//                por alguns segundos e PERMITE nova tentativa (L104).
//   5.3  L105 - ~2 minutos de inatividade voltam ao Modo Normal.
//   5.4  L112 - o menu, na ORDEM LITERAL: "Menu>Voltar   Ajusta Preset   Auto Calibracao
//                Limite 1   Limite 2   Limite 3   Limite 4   Sentido Sensor   Senha   Sair".
//   5.4  L132 - "Sair | Retorna ao Modo Normal de operacao"; L136, timeout de ~2 min.
//   5.6  L148 - submenu literal "Preset>Voltar   Preset X   Preset Y".
//   5.8  L191 e L192 - as duas opcoes de Sentido do Sensor ("Horario" e "Anti-horario");
//                L199 recomenda refazer o Preset e conferir os Limites 1 a 4 depois da troca.
//   5.9  L202 - Limites 1 e 2 sao do eixo X, 3 e 4 do eixo Y (X1, X2, Y1, Y2);
//                L204 a L207, as quatro operacoes de limite; L211, "selecionar o parametro
//                Operacao Limite ou Limite (1 a 4)"; L214 a L217, as quatro telas de Valor
//                Limite; L220, o exemplo programado em +025,0.
//   5.10 L231 - "Edita senha:1234"; L237, a nova senha so vale nos proximos acessos.
//        L183 - "Alteracao bem sucedida!" (sem cedilha e sem til, exatamente como impresso).
//
// DECISAO A13, APROVADA em 2026-08-31 (opcao A), que e o motivo de esta classe existir:
//
// 1. EFETIVACAO EM INSTANTE UNICO NO SAIR. Toda edicao cai num RASCUNHO (a copia interna de
//    Parameters). O agregado ativo - o que o comparador de rele enxerga - so e substituido em
//    UM ponto: a confirmacao da tela de revisao "NOVA CONFIG - CONFIRMA?", alcancada pelos
//    itens Voltar e Sair do nivel 1. Nunca existe valor novo com operacao velha comandando
//    rele, e por isso o par Valor Limite + Operacao Limite e sempre atomico sem que ninguem
//    precise tratar o par como caso especial.
// 2. O TIMEOUT DE 120 s NAO DESCARTA A EDICAO. Ele devolve ao Modo Normal com a configuracao
//    PENDENTE (pendingConfig()), para que a tela principal pisque kMsgConfigPendente. Nenhuma
//    edicao e perdida em silencio, e reentrar no Modo Programacao continua o mesmo rascunho.
//    O prazo vale em TODOS os estados C, D, E e F (T78 de docs/ihm-estados.md), inclusive na
//    tela de revisao e nas telas temporizadas: painel abandonado sempre volta ao Modo Normal.
// 3. BLOQUEIO DE SENHA TEMPORARIO E VOLATIL (5 erros, 60 s), que quem implementa e Password:
//    bloqueio permanente transformaria erro de digitacao num painel de cais em visita de
//    manutencao. Esta classe exibe o estado enquanto ele dura: MenuState::LoginBloqueado NAO
//    tem prazo de mensagem proprio, vive exatamente o tempo de Password::locked() e cai
//    sozinho na tela de login quando o bloqueio expira. Um pisca de 2 s seguido de tela de
//    login normal seria indistinguivel de senha errada, e e o chamado de manutencao que A13
//    quis evitar.
// 4. RECUSA EXPLICITA DE VALOR FORA DE FAIXA, jamais clamp silencioso: o editor pisca a
//    mensagem da spec por kRecusaMs e devolve o cursor ao campo, sem gravar nada.
// 5. REGRA UNICA DE CURSOR (senha, valor limite, preset e trim): o campo abre no digito mais a
//    direita, MENU move para a esquerda, rolagem circular. Quem implementa e DigitEditor.
//
// DECISAO A9, APROVADA (opcao A): A TROCA DE SENTIDO ZERA O OFFSET DO EIXO, com aviso
// obrigatorio de 3 s. O offset de PSET foi calculado contra o sentido antigo
// (offset := P - dir * bruto); mantido apos a inversao, ele desloca os DOIS pontos de atuacao
// do eixo em 2*offset, ate 180,0 graus, sem indicio na tela. Por isso a confirmacao da troca,
// e so quando o valor muda de verdade, zera presetOffsetDeci ANTES de gravar o sentido novo
// (se a gravacao do sentido falhasse depois de zerado, o operador perde a referencia - o
// indicador de PSET some da tela principal, que e a prova visivel exigida por A9 - em vez de
// ficar com atuacao deslocada em silencio) e exibe MenuState::AvisoSentido por kDirWarnMs.
// O aviso e OBRIGATORIO: gesto nenhum o encurta, o que e desvio declarado de T71 de
// docs/ihm-estados.md ("MENU curto ou 5 s"), que e [LACUNA] no proprio documento.
//
// EMENDA 1 a A13 (2026-08-31): a senha esta fora do escopo do MVP de bancada. O portao e
// PARAMETRO de composicao (requirePassword no construtor), no mesmo padrao de
// kRelayFailSafePolarity de A1, e nunca um #ifdef espalhado pelo dominio: binario de producao
// com true, binario de bancada com false, dominio testado nos dois valores. Com o portao
// desarmado qualquer pessoa com acesso as tres teclas frontais muda o ponto de atuacao dos
// quatro reles - aceitavel em bancada, NAO aceitavel em unidade instalada.
//
// GRAVACAO CONFERIDA, NUNCA ANUNCIADA NO ESCURO: todo retorno Status de Parameters e todo bool
// de Password e testado antes de a tela dizer "Alteracao bem sucedida!". Recusa desvia para
// MenuState::FalhaGrav ("Falha de gravacao!", E8 de docs/ihm-estados.md 3.5) sem marcar
// configuracao pendente. Com as faixas de hoje o caminho de recusa e inalcancavel pela IHM
// (DigitEditor ja filtra a faixa antes da confirmacao), e e exatamente por isso que ele esta
// escrito: a mensagem de sucesso de um supervisor de inclinacao nao pode depender desse acaso.
//
// PENDENCIA SO COM ALTERACAO DE VERDADE: a confirmacao que grava o MESMO valor que ja estava
// no rascunho nao marca pendingConfig(). Abrir Operacao Limite ou Sentido do Sensor so para
// consultar e dar o hold nao pode por a tela principal a piscar "CONFIG PENDENTE - REVISAR":
// aviso que aparece sem pendencia treina o operador a ignorar o aviso, e e o mesmo aviso com
// que A13 garante que nenhuma edicao se perde em silencio.
//
// QUATRO ESCOLHAS QUE PRECISAM ESTAR ESCRITAS, porque nenhuma sai do manual:
//
// a. NAVEGACAO CIRCULAR NA LISTA. docs/ihm-estados.md 5.4 registra "se a lista da a volta" como
//    LACUNA aberta (o manual nao diz qual tecla sobe nem se ha volta). Adotada a volta, por
//    coerencia com a rolagem circular que A13 ja aprovou para dentro do campo: com dez itens e
//    tres teclas, chegar em "Sair" e ter de descer nove vezes para voltar a "Voltar" e o tipo
//    de atrito que produz ponteamento em campo. UP sobe (indice anterior), DOWN desce.
//    A lista FECHADA de opcoes de Operacao Limite (E3 de ihm-estados.md) continua SEM volta,
//    como o documento manda - la a volta esconderia o fim da lista.
// b. TEXTO DAS TELAS EM ASCII. O manual imprime "Valor Limite X1(<grau>):+000,0" com o
//    simbolo de grau, e "Auto Calibracao" com til e cedilha; o dominio nao carrega acento
//    nem simbolo de grau, entao a mesma tela sai
//    "Valor Limite X1(graus):+000,0" e "Auto Calibracao". E desvio DECLARADO de grafia, nao de
//    conteudo, e vale para todas as telas deste arquivo. "Alteracao bem sucedida!" e "Senha
//    incorreta!" sao copiadas byte a byte do manual (L183 e L104), inclusive a falta de
//    cedilha e de til que o proprio manual imprime.
// c. O MODO NORMAL NAO E DESENHADO AQUI. Em MenuState::Normal esta classe nao toca no display
//    (ownsDisplay() == false): a tela principal, a falha de comunicacao e o batimento sao de
//    outro modulo. O unico acoplamento e pendingConfig() mais o literal kMsgConfigPendente,
//    que aquele modulo pisca quando ha configuracao pendente por timeout.
// d. A LISTA E DESLIZANTE, DE TRES ENTRADAS. L112 imprime os dez itens do nivel 1 numa linha
//    so; essa linha tem mais de cem caracteres e nao cabe nos 256x64 do SSD1322. A tela
//    mostra o cabecalho mais uma JANELA de tres itens em torno da selecao, com o item
//    selecionado em Inverse - a lista existe e desliza, os nomes e a ordem sao os literais do
//    manual, e o unico desvio e quantos itens cabem por vez. Os submenus tem tres itens e
//    aparecem inteiros.
//
// FRONTEIRA COM OS ASSISTENTES: o Preset (5.6) e a Auto Calibracao (5.7) nao sao executados
// aqui; viram pedido em takeAction(). Enquanto o pedido esta em curso a maquina fica em
// MenuState::Assistente, onde ownsDisplay() e FALSO - dois modulos nunca se declaram donos do
// display ao mesmo tempo - e nao desenha quadro nenhum. Quando o assistente termina, a camada
// de aplicacao chama reclaimDisplay(), que devolve o submenu de origem e FORCA o repinte; sem
// isso a ultima tela do assistente ficaria congelada na frente do operador.
//
// TEMPO: todo prazo passa por Password (inatividade de 120 s e bloqueio de 60 s) ou por
// deadlineReached() de ports/i_clock.h, nunca por "a > b" e nunca por contagem de ciclos.
//
// ESTADO SEGURO: nasce em Normal, sem rascunho pendente, sem editor aberto e SEM PRAZO DE
// MENSAGEM ARMADO. begin() chamado com uma tela temporizada no ar tem de desarmar o prazo:
// prazo sobrevivente venceria dentro do Modo Normal e ressuscitaria o Modo Programacao sem
// passar pelo portao de senha, que e o unico controle de acesso ao setpoint dos quatro reles.
// Nenhum caminho desta classe escreve em rele, em saida analogica ou em NVS: ela troca o
// agregado ATIVO no commit e nada mais. Quem persiste e a camada de aplicacao, olhando
// pendingConfig().
#pragma once

#include <stdint.h>

#include "domain/digit_editor.h"
#include "domain/parameters.h"
#include "domain/password.h"
#include "domain/ui/key_gesture.h"
#include "ports/i_clock.h"
#include "ports/i_display.h"

namespace domain {

// Ordem LITERAL de L112. Nao ha item a mais, nao ha item fora de ordem: o valor numerico de
// cada item e a posicao dele na tela do manual.
enum class MenuItem : uint8_t {
    Voltar = 0,
    AjustaPreset = 1,
    AutoCalibracao = 2,
    Limite1 = 3,
    Limite2 = 4,
    Limite3 = 5,
    Limite4 = 6,
    SentidoSensor = 7,
    Senha = 8,
    Sair = 9,
};

enum class MenuState : uint8_t {
    Normal,          // B1: o display e de outro modulo
    Login,           // C1: "Senha de acesso:0000"
    LoginErro,       // C2: "Senha incorreta!"
    LoginBloqueado,  // A13: bloqueio temporario de 60 s, enquanto ele durar
    Menu,            // D1: os dez itens
    SubEixo,         // D2/D3/D5: Voltar / X / Y
    SubLimite,       // D4: Voltar / Valor Limite / Operacao Limite
    EditValor,       // E2
    EditOperacao,    // E3
    EditSentido,     // E4
    AvisoSentido,    // E4b: A9, o aviso obrigatorio de 3 s da troca de sentido
    AvisoPresetZerado,  // aviso obrigatorio de 3 s do "Zerar Preset": mover o zero desloca os
                        // quatro pontos de atuacao, exatamente como a troca de sentido
    EditSenha,       // E5
    Recusa,          // A13: valor fora de faixa, sem clamp silencioso
    GravOk,          // E7: "Alteracao bem sucedida!"
    FalhaGrav,       // E8: "Falha de gravacao!"
    Revisao,         // A13: "NOVA CONFIG - CONFIRMA?"
    Assistente,      // Preset / Auto Calibracao em curso: o display e do assistente
};

// O que esta maquina NAO executa por conta propria: o Preset (5.6) precisa da leitura corrente
// do sensor e a Auto Calibracao (5.7) escreve codigo de DAC. Os dois sao assistentes de outro
// modulo; aqui eles viram PEDIDO, entregue por takeAction(), e a tela do assistente assume.
enum class MenuAction : uint8_t {
    None = 0,
    AjustaPresetX,
    AjustaPresetY,
    AutoCalibracaoX,
    AutoCalibracaoY,
    // Devolve a leitura ao angulo cru nos dois eixos. Nao abre assistente: o menu continua dono
    // do display e mostra o aviso obrigatorio enquanto o composition root grava.
    ZerarPreset,
};

class MenuMachine {
public:
    static constexpr uint8_t kItemCount = 10;
    static constexpr uint8_t kSubItemCount = 3;
    // O submenu de Preset tem um item a mais - "Zerar Preset" - desde 2026-09-01. Os de Auto
    // Calibracao e Sentido continuam com tres. E errata do manual 5.6, que lista tres.
    static constexpr uint8_t kSubItemCountPreset = 4;
    static constexpr uint8_t kLimitOpCount = 4;

    // Quantos itens da lista cabem por vez na janela deslizante (desvio declarado d).
    static constexpr uint8_t kListWindow = 3;

    // Inatividade de ~2 min de L105 e L136, medida por Password.
    static constexpr uint32_t kTimeoutMs = Password::kInactivityMs;

    // "por alguns segundos" (L103) nao tem numero no manual - LACUNA fechada em 2000 ms por
    // docs/ihm-estados.md 1.3 (T_MSG_ERRO_SENHA).
    static constexpr uint32_t kErroSenhaMs = 2000;
    static constexpr uint32_t kGravOkMs = 1500;    // T_MSG_OK
    static constexpr uint32_t kRecusaMs = 2000;    // A13: pisca a recusa e volta ao campo
    static constexpr uint32_t kDirWarnMs = 3000;   // A9: aviso obrigatorio de 3 s
    static constexpr uint32_t kFalhaGravMs = 3000; // E8 de docs/ihm-estados.md 3.5

    static constexpr uint8_t kLineCap = 40;

    // Telas literais. As tres primeiras sao byte a byte do manual.
    static constexpr const char* kMsgSenhaIncorreta = "Senha incorreta!";
    static constexpr const char* kMsgGravOk = "Alteracao bem sucedida!";
    static constexpr const char* kPrefixoLogin = "Senha de acesso:";
    static constexpr const char* kPrefixoEditaSenha = "Edita senha:";
    static constexpr const char* kCabecalhoMenu = "Menu>";
    static constexpr const char* kCabecalhoPreset = "Preset>";
    static constexpr const char* kCabecalhoAutoCal = "Auto Cal>";
    static constexpr const char* kCabecalhoSentido = "Sentido>";
    // A13, tela de revisao e aviso de configuracao pendente. Nao existem no manual.
    static constexpr const char* kMsgRevisao = "NOVA CONFIG - CONFIRMA?";
    static constexpr const char* kMsgConfigPendente = "CONFIG PENDENTE - REVISAR";
    // Bloqueio de 60 s de A13; o manual nao publica tela para ele (LACUNA declarada).
    static constexpr const char* kMsgBloqueado = "Senha bloqueada!";
    // Recusa do campo angular (A13, sub-item do digito das centenas).
    static constexpr const char* kMsgForaDaFaixa = "FORA DA FAIXA +/-090,0";
    // E8 de docs/ihm-estados.md 3.5: gravacao recusada pelo agregado.
    static constexpr const char* kMsgFalhaGrav = "Falha de gravacao!";
    // A9: aviso obrigatorio da troca de sentido, com o eixo e os dois limites a conferir.
    static constexpr const char* kMsgSentidoX = "Sentido X alterado!";
    static constexpr const char* kMsgSentidoY = "Sentido Y alterado!";
    static constexpr const char* kMsgPresetZerado1 = "PRESET ZERADO";
    static constexpr const char* kMsgPresetZerado2 = "confira X1 X2 Y1 Y2";
    static constexpr const char* kMsgPresetZeradoX = "Preset zerado - confira X1 X2";
    static constexpr const char* kMsgPresetZeradoY = "Preset zerado - confira Y1 Y2";

    MenuMachine(IDisplay& display, const IClock& clock, Password& password, Parameters& active,
                bool requirePassword);

    MenuMachine(const MenuMachine&) = delete;
    MenuMachine& operator=(const MenuMachine&) = delete;

    // Estado seguro e primeiro quadro. Nao desenha nada: o Modo Normal e de outro modulo.
    void begin();

    // Um gesto ja reconhecido por KeyGesture. Gesto sem funcao no estado corrente e IGNORADO,
    // nunca reinterpretado (invariante 6 de docs/ihm-estados.md secao 6).
    void onGesture(const Gesture& gesture);

    // Prazos (timeout de 120 s e as mensagens temporizadas) e quadro. Uma vez por ciclo.
    void update();

    MenuState state() const { return state_; }

    // Item selecionado no nivel 1. Vale em qualquer estado: e por ele que se entra no submenu.
    MenuItem item() const { return static_cast<MenuItem>(selTop_); }

    // Indice selecionado no nivel corrente (submenu ou lista de opcoes).
    uint8_t subCursor() const { return selSub_; }

    // A13: ha edicao no rascunho que ainda nao comandou rele. A tela principal do Modo Normal
    // pisca kMsgConfigPendente enquanto isto for verdade.
    bool pendingConfig() const { return pending_; }

    // O rascunho, para inspecao e para a camada de aplicacao persistir depois do commit.
    const Parameters& draft() const { return draft_; }

    // Em Normal o display pertence ao modulo do Modo Normal; em Assistente, ao assistente.
    bool ownsDisplay() const {
        return state_ != MenuState::Normal && state_ != MenuState::Assistente;
    }

    // Pedido de assistente (Preset / Auto Calibracao). false quando nao ha pedido.
    bool takeAction(MenuAction& out);

    // O assistente terminou e devolve o display: volta ao submenu de origem e forca o repinte.
    // Inerte fora de MenuState::Assistente (o timeout de 120 s pode ter levado ao Modo Normal
    // enquanto o assistente desenhava, e nesse caso quem manda na tela e o Modo Normal).
    void reclaimDisplay();

    // OS ASSISTENTES ESCREVEM NO AGREGADO ATIVO, NAO NO RASCUNHO. Captura de Preset e Auto
    // Calibracao sao gestos fisicos com efeito imediato, e por isso gravam direto - enquanto o
    // menu trabalha numa copia que so volta ao ativo na confirmacao da revisao (A13).
    //
    // Sem esta chamada, editar um limite (que cria a pendencia), entrar num assistente, gravar e
    // depois confirmar o Sair APAGAVA em silencio o que o assistente escreveu: o active_ = draft_
    // da confirmacao restaurava a copia feita ANTES dele. O composition root chama isto ao
    // retomar o display de um assistente.
    //
    // Adota SO os campos que o menu nao edita - offsets de Preset e o par de calibracao. O
    // rascunho de limites, operacoes, sentido e senha fica intocado, senao a adocao descartaria
    // a edicao pendente que ela deveria preservar.
    void adoptExternalChanges();
    uint8_t subItemCount() const;


    static const char* itemName(MenuItem menuItem);
    static const char* limitOpName(LimitOp op);
    static const char* sensorDirName(SensorDir dir);

private:
    enum class SubKind : uint8_t { Preset, AutoCal, Sentido };

    // --- gestos por estado ---
    void onNormal(const Gesture& gesture);
    void onLogin(const Gesture& gesture);
    void onMenu(const Gesture& gesture);
    void onSubEixo(const Gesture& gesture);
    void onSubLimite(const Gesture& gesture);
    void onEditValor(const Gesture& gesture);
    void onEditOperacao(const Gesture& gesture);
    void onEditSentido(const Gesture& gesture);
    void onEditSenha(const Gesture& gesture);
    void onRevisao(const Gesture& gesture);

    // --- transicoes ---
    void openLogin();
    void openMenu();
    void openItem();
    void openSubEixo(SubKind kind);
    void openSubLimite();
    void openValor();
    void openOperacao();
    void openSentido(Axis axis);
    void openSenha();
    void requestExit();
    void commitOnExit();
    void showMessage(MenuState msgState, MenuState backTo, uint32_t spanMs);
    void gravado();
    // false quando o agregado recusou a gravacao: ja deixou E8 no ar e o chamador desiste.
    bool gravacaoAceita(Status status);
    void toNormal();

    LimitId currentLimit() const;
    static uint8_t stepCircular(uint8_t current, uint8_t count, bool forward);

    // --- desenho ---
    void render();
    void drawLine(int16_t y, const char* text, TextFont font);
    TextFont contentFont(const char* text) const;
    // Cabecalho mais a janela de kListWindow entradas em torno da selecao, com o item
    // selecionado em Inverse (desvio declarado d).
    void drawList(const char* header, const char* const* items, uint8_t count, uint8_t sel);
    // Desenha a linha inteira em Normal e repinta em Inverse o caractere do cursor: e assim
    // que o digito em edicao "pisca" (REQ-DSP-04). Indice fora da linha nao pinta nada.
    void drawEditLine(int16_t y, const char* text, uint8_t inverseIndex, TextFont font);
    // Monta prefixo + campo formatado em line_ e devolve o comprimento do prefixo, que e o
    // deslocamento do cursor do editor dentro da linha inteira.
    uint8_t buildFieldLine(const char* prefix);

    IDisplay& display_;
    const IClock& clock_;
    Password& password_;
    Parameters& active_;
    Parameters draft_;
    DigitEditor editor_;

    MenuState state_;
    MenuState backTo_;
    MenuState editReturn_;
    SubKind subKind_;
    MenuAction action_;

    uint32_t msgSinceMs_;
    uint32_t msgSpanMs_;

    uint8_t selTop_;
    uint8_t selSub_;
    uint8_t opSel_;
    uint8_t dirSel_;
    Axis axis_;

    char line_[kLineCap];
    const char* recusaMsg_;

    bool requirePassword_;
    bool pending_;
    bool dirty_;
};

}  // namespace domain
