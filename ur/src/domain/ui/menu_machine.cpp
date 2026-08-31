// Implementacao da maquina do Modo Programacao.
// Manual SUI-DI141388XY 5.2 L90, 5.3 L98 a L106, 5.4 L112 a L136, 5.6 L148, 5.8 L188 a L199,
// 5.9 L200 a L220, 5.10 L231 a L237. Decisao A13 (efetivacao unica no SAIR, timeout que nao
// descarta, recusa explicita de valor fora de faixa, bloqueio de senha visivel enquanto dura),
// Decisao A9 (a troca de Sentido zera o offset do eixo com aviso obrigatorio de 3 s) e
// Emenda 1 a A13 (portao de senha como parametro de composicao).
//
// A ordem dos dez itens e o proprio valor do enum MenuItem: nao ha tabela paralela que possa
// divergir da tela, e acrescentar item no meio quebra o teste de ordem antes de quebrar o
// painel do cliente.
//
// Toda edicao escreve em draft_ e NUNCA em active_; a unica atribuicao de active_ no arquivo
// inteiro esta em commitOnExit(). Quem procurar "quando este valor passa a comandar rele"
// encontra um ponto so.
//
// Nenhum retorno de gravacao e descartado: cada Status de Parameters e cada bool de Password
// passa por gravacaoAceita(), que desvia para E8 ("Falha de gravacao!") sem marcar pendencia
// e sem anunciar sucesso.
#include "domain/ui/menu_machine.h"

#include <string.h>

namespace domain {
namespace {

// Telas de uma e de duas linhas (editores, mensagens e o aviso de A9).
constexpr int16_t kLinha1Y = 20;
constexpr int16_t kLinha2Y = 44;

// Lista deslizante: cabecalho e a janela de tres entradas (desvio declarado d do cabecalho).
constexpr int16_t kListaCabecalhoY = 2;
constexpr int16_t kListaItemY[MenuMachine::kListWindow] = {18, 32, 46};

// Campo de senha: "Senha de acesso:0000" (L98) e "Edita senha:1234" (L231), quatro digitos sem
// sinal, faixa 0000 a 9999 da Tabela 1 (L131).
const DigitFieldSpec kCampoSenha = {4, 0, false, 0, 9999, ""};

// Campo angular de Valor Limite (L214 a L217): +XXX,X em decimos de grau, -90,0 a +90,0.
const DigitFieldSpec kCampoAngular = {4, 1, true, Angle::kMinDeciDeg, Angle::kMaxDeciDeg,
                                      MenuMachine::kMsgForaDaFaixa};

const char* const kNomeItem[MenuMachine::kItemCount] = {
    "Voltar", "Ajusta Preset", "Auto Calibracao", "Limite 1", "Limite 2",
    "Limite 3", "Limite 4", "Sentido Sensor", "Senha", "Sair",
};

// docs/ihm-estados.md 3.4: "Limite 1>Voltar   Valor Limite X1   Operacao Limite X1".
const char* const kCabecalhoLimite[Parameters::kLimitCount] = {
    "Limite 1>", "Limite 2>", "Limite 3>", "Limite 4>",
};

// Limites 1 e 2 sao do eixo X, 3 e 4 do eixo Y (manual 5.9, L202, e Tabela 1 L121 a L128).
const char* const kEtiquetaLimite[Parameters::kLimitCount] = {"X1", "X2", "Y1", "Y2"};

// Submenu de eixo: L148 imprime o de Preset; os de Auto Calibracao e Sentido do Sensor seguem
// a mesma forma (docs/ihm-estados.md 3.4, D3 e D5).
const char* const kItemPreset[MenuMachine::kSubItemCount] = {"Voltar", "Preset X", "Preset Y"};
const char* const kItemAutoCal[MenuMachine::kSubItemCount] = {"Voltar", "Auto Calibracao X",
                                                              "Auto Calibracao Y"};
const char* const kItemSentido[MenuMachine::kSubItemCount] = {"Voltar", "Sentido Sensor X",
                                                              "Sentido Sensor Y"};

// docs/ihm-estados.md 3.5, E3: simbolo mais extensao. ">=" e "<=" em ASCII no lugar dos
// simbolos de maior-ou-igual e menor-ou-igual do manual (L204 a L207): desvio de grafia.
const char* const kNomeOperacao[MenuMachine::kLimitOpCount] = {
    "Off (desativado)", ">= (maior ou igual)", "<= (menor ou igual)", "+ (modulo)",
};

// Manual 5.8 L191 e L192 e Tabela 2 L264/L265, sem acento.
const char* const kNomeSentido[2] = {"Horario", "Anti-horario"};

uint8_t appendTo(char* dst, uint8_t cap, uint8_t len, const char* src) {
    if (dst == nullptr || src == nullptr || cap == 0) {
        return len;
    }
    uint8_t escrito = len;
    while (*src != '\0' && (escrito + 1u) < cap) {
        dst[escrito] = *src;
        ++escrito;
        ++src;
    }
    dst[escrito] = '\0';
    return escrito;
}

}  // namespace

MenuMachine::MenuMachine(IDisplay& display, const IClock& clock, Password& password,
                         Parameters& active, bool requirePassword)
    : display_(display),
      clock_(clock),
      password_(password),
      active_(active),
      draft_(active),
      editor_(),
      state_(MenuState::Normal),
      backTo_(MenuState::Normal),
      editReturn_(MenuState::Menu),
      subKind_(SubKind::Preset),
      action_(MenuAction::None),
      msgSinceMs_(clock.nowMs()),
      msgSpanMs_(0),
      selTop_(0),
      selSub_(0),
      opSel_(0),
      dirSel_(0),
      axis_(Axis::X),
      line_(),
      recusaMsg_(""),
      requirePassword_(requirePassword),
      pending_(false),
      dirty_(false) {
    line_[0] = '\0';
}

void MenuMachine::begin() {
    // Estado seguro INTEIRO: prazo de mensagem desarmado junto com o estado. Prazo que
    // sobrevivesse ao begin() venceria dentro do Modo Normal e executaria state_ = backTo_,
    // ressuscitando o Modo Programacao sem passar pelo portao de senha.
    state_ = MenuState::Normal;
    backTo_ = MenuState::Normal;
    editReturn_ = MenuState::Menu;
    msgSinceMs_ = clock_.nowMs();
    msgSpanMs_ = 0;
    draft_ = active_;
    pending_ = false;
    action_ = MenuAction::None;
    selTop_ = 0;
    selSub_ = 0;
    dirty_ = false;
    password_.noteActivity();
}

const char* MenuMachine::itemName(MenuItem menuItem) {
    const uint8_t indice = static_cast<uint8_t>(menuItem);
    return (indice < kItemCount) ? kNomeItem[indice] : "";
}

const char* MenuMachine::limitOpName(LimitOp op) {
    const uint8_t indice = static_cast<uint8_t>(op);
    return (indice < kLimitOpCount) ? kNomeOperacao[indice] : "";
}

const char* MenuMachine::sensorDirName(SensorDir dir) {
    return kNomeSentido[static_cast<uint8_t>(dir) & 1u];
}

bool MenuMachine::takeAction(MenuAction& out) {
    if (action_ == MenuAction::None) {
        return false;
    }
    out = action_;
    action_ = MenuAction::None;
    return true;
}

void MenuMachine::reclaimDisplay() {
    if (state_ != MenuState::Assistente) {
        return;
    }
    password_.noteActivity();
    state_ = MenuState::SubEixo;
    dirty_ = true;
}

uint8_t MenuMachine::stepCircular(uint8_t current, uint8_t count, bool forward) {
    if (count == 0) {
        return 0;
    }
    return forward ? static_cast<uint8_t>((current + 1u) % count)
                   : static_cast<uint8_t>((current + count - 1u) % count);
}

LimitId MenuMachine::currentLimit() const {
    const uint8_t indice = static_cast<uint8_t>(selTop_ - static_cast<uint8_t>(MenuItem::Limite1));
    return static_cast<LimitId>(indice < Parameters::kLimitCount ? indice : 0u);
}

// --- entrada de gesto ---

void MenuMachine::onGesture(const Gesture& gesture) {
    password_.noteActivity();
    switch (state_) {
        case MenuState::Normal: onNormal(gesture); break;
        case MenuState::Login: onLogin(gesture); break;
        case MenuState::Menu: onMenu(gesture); break;
        case MenuState::SubEixo: onSubEixo(gesture); break;
        case MenuState::SubLimite: onSubLimite(gesture); break;
        case MenuState::EditValor: onEditValor(gesture); break;
        case MenuState::EditOperacao: onEditOperacao(gesture); break;
        case MenuState::EditSentido: onEditSentido(gesture); break;
        case MenuState::EditSenha: onEditSenha(gesture); break;
        case MenuState::Revisao: onRevisao(gesture); break;
        // Telas temporizadas, bloqueio e assistente: gesto IGNORADO, nunca reinterpretado
        // (invariante 6 de docs/ihm-estados.md secao 6; o aviso de A9 e obrigatorio e nao
        // encurta, e durante o assistente o teclado e do assistente).
        case MenuState::LoginErro:
        case MenuState::LoginBloqueado:
        case MenuState::Recusa:
        case MenuState::GravOk:
        case MenuState::FalhaGrav:
        case MenuState::AvisoSentido:
        case MenuState::Assistente: break;
    }
    if (dirty_) {
        render();
        dirty_ = false;
    }
}

void MenuMachine::update() {
    // O timeout de ~2 min (L105 e L136) vale em TODOS os estados C, D, E e F, inclusive nas
    // telas temporizadas e na tela de revisao. A13: ele NAO descarta o rascunho.
    if (state_ != MenuState::Normal && password_.timedOut()) {
        toNormal();
    } else if (state_ == MenuState::LoginBloqueado) {
        // A13: a tela de bloqueio dura o bloqueio inteiro e sai sozinha quando ele expira.
        if (!password_.locked()) {
            openLogin();
        }
    } else if (msgSpanMs_ != 0 && deadlineReached(msgSinceMs_, clock_.nowMs(), msgSpanMs_)) {
        msgSpanMs_ = 0;
        if (backTo_ == MenuState::Login) {
            // L104: a mensagem "desaparece e permite uma nova tentativa"; o campo reabre em
            // 0000, como L98 imprime (docs/ihm-estados.md 3.3 registra a lacuna).
            openLogin();
        } else {
            state_ = backTo_;
            dirty_ = true;
        }
    }
    if (dirty_) {
        render();
        dirty_ = false;
    }
}

void MenuMachine::onNormal(const Gesture& gesture) {
    // L90: MENU mantida ~3 s abre o Modo Programacao. Emenda 1 a A13: com o portao desarmado
    // o menu abre direto, sem tela de senha.
    if (gesture.kind == GestureKind::Hold && gesture.key == Key::Menu) {
        if (requirePassword_) {
            openLogin();
        } else {
            openMenu();
        }
    }
}

void MenuMachine::onLogin(const Gesture& gesture) {
    if (gesture.key == Key::Menu && gesture.kind == GestureKind::Hold) {
        const AccessResult resultado = password_.submit(static_cast<uint16_t>(editor_.value()));
        if (resultado == AccessResult::Granted) {
            openMenu();
        } else if (resultado == AccessResult::Wrong) {
            showMessage(MenuState::LoginErro, MenuState::Login, kErroSenhaMs);
        } else {
            // Bloqueado: openLogin() e o unico ponto que decide entre campo de senha e tela de
            // bloqueio, para que as duas entradas nao possam divergir.
            openLogin();
        }
        return;
    }
    if (gesture.kind != GestureKind::ShortTap) {
        return;
    }
    // L101: MENU move para o digito a esquerda, UP e DOWN editam o digito piscando.
    switch (gesture.key) {
        case Key::Menu: editor_.menu(); break;
        case Key::Up: editor_.up(); break;
        case Key::Down: editor_.down(); break;
    }
    dirty_ = true;
}

void MenuMachine::onMenu(const Gesture& gesture) {
    if (gesture.kind != GestureKind::ShortTap) {
        return;
    }
    switch (gesture.key) {
        case Key::Up:
            selTop_ = stepCircular(selTop_, kItemCount, false);
            dirty_ = true;
            break;
        case Key::Down:
            selTop_ = stepCircular(selTop_, kItemCount, true);
            dirty_ = true;
            break;
        case Key::Menu: openItem(); break;
    }
}

void MenuMachine::onSubEixo(const Gesture& gesture) {
    if (gesture.kind != GestureKind::ShortTap) {
        return;
    }
    if (gesture.key == Key::Up || gesture.key == Key::Down) {
        selSub_ = stepCircular(selSub_, kSubItemCount, gesture.key == Key::Down);
        dirty_ = true;
        return;
    }
    if (selSub_ == 0) {
        state_ = MenuState::Menu;
        dirty_ = true;
        return;
    }
    const Axis eixo = (selSub_ == 1) ? Axis::X : Axis::Y;
    switch (subKind_) {
        case SubKind::Preset:
            action_ = (eixo == Axis::X) ? MenuAction::AjustaPresetX : MenuAction::AjustaPresetY;
            // O display passa a ser do assistente ate reclaimDisplay().
            state_ = MenuState::Assistente;
            break;
        case SubKind::AutoCal:
            action_ =
                (eixo == Axis::X) ? MenuAction::AutoCalibracaoX : MenuAction::AutoCalibracaoY;
            state_ = MenuState::Assistente;
            break;
        case SubKind::Sentido: openSentido(eixo); break;
    }
}

void MenuMachine::onSubLimite(const Gesture& gesture) {
    if (gesture.kind != GestureKind::ShortTap) {
        return;
    }
    if (gesture.key == Key::Up || gesture.key == Key::Down) {
        selSub_ = stepCircular(selSub_, kSubItemCount, gesture.key == Key::Down);
        dirty_ = true;
        return;
    }
    // L218 e L219: o valor primeiro, a operacao depois.
    switch (selSub_) {
        case 0:
            state_ = MenuState::Menu;
            dirty_ = true;
            break;
        case 1: openValor(); break;
        default: openOperacao(); break;
    }
}

void MenuMachine::onEditValor(const Gesture& gesture) {
    if (gesture.key == Key::Menu && gesture.kind == GestureKind::Hold) {
        if (editor_.confirm() != ConfirmResult::Ok) {
            // A13: recusa explicita, sem clamp silencioso; nada e gravado e o campo volta.
            recusaMsg_ = editor_.outOfRangeMessage();
            showMessage(MenuState::Recusa, MenuState::EditValor, kRecusaMs);
            return;
        }
        const LimitId alvo = currentLimit();
        const int16_t novo = editor_.value();
        if (novo != draft_.limitValue(alvo).deciDegrees()) {
            if (!gravacaoAceita(draft_.setLimitValue(alvo, Angle::fromDeciDegrees(novo)))) {
                return;
            }
            pending_ = true;
        }
        gravado();
        return;
    }
    if (gesture.kind != GestureKind::ShortTap) {
        return;
    }
    switch (gesture.key) {
        case Key::Menu: editor_.menu(); break;
        case Key::Up: editor_.up(); break;
        case Key::Down: editor_.down(); break;
    }
    dirty_ = true;
}

void MenuMachine::onEditOperacao(const Gesture& gesture) {
    if (gesture.key == Key::Menu && gesture.kind == GestureKind::Hold) {
        const LimitId alvo = currentLimit();
        const LimitOp escolhida = static_cast<LimitOp>(opSel_);
        if (escolhida != draft_.limitOp(alvo)) {
            if (!gravacaoAceita(draft_.setLimitOp(alvo, escolhida))) {
                return;
            }
            pending_ = true;
        }
        gravado();
        return;
    }
    if (gesture.kind != GestureKind::ShortTap) {
        return;
    }
    // Lista FECHADA, sem volta (docs/ihm-estados.md 3.5, E3).
    if (gesture.key == Key::Up && opSel_ > 0) {
        --opSel_;
        dirty_ = true;
    } else if (gesture.key == Key::Down && (opSel_ + 1u) < kLimitOpCount) {
        ++opSel_;
        dirty_ = true;
    }
}

void MenuMachine::onEditSentido(const Gesture& gesture) {
    if (gesture.key == Key::Menu && gesture.kind == GestureKind::Hold) {
        const SensorDir escolhido = static_cast<SensorDir>(dirSel_);
        if (escolhido == draft_.sensorDir(axis_)) {
            gravado();
            return;
        }
        // A9: o offset de PSET foi calculado contra o sentido antigo. Zera-se o offset ANTES
        // de gravar o sentido novo, para que uma recusa deixe o eixo sem referencia (o
        // indicador de PSET some, prova visivel exigida por A9) em vez de deixar os dois
        // pontos de atuacao deslocados em 2*offset sem indicio na tela.
        if (!gravacaoAceita(draft_.setPresetOffset(axis_, 0))) {
            return;
        }
        if (!gravacaoAceita(draft_.setSensorDir(axis_, escolhido))) {
            return;
        }
        pending_ = true;
        showMessage(MenuState::AvisoSentido, MenuState::SubEixo, kDirWarnMs);
        return;
    }
    if (gesture.kind != GestureKind::ShortTap) {
        return;
    }
    if (gesture.key == Key::Up && dirSel_ > 0) {
        dirSel_ = 0;
        dirty_ = true;
    } else if (gesture.key == Key::Down && dirSel_ == 0) {
        dirSel_ = 1;
        dirty_ = true;
    }
}

void MenuMachine::onEditSenha(const Gesture& gesture) {
    if (gesture.key == Key::Menu && gesture.kind == GestureKind::Hold) {
        const uint16_t nova = static_cast<uint16_t>(editor_.value());
        const uint16_t anterior = draft_.password();
        // L237: a nova senha so vale nos PROXIMOS acessos. Ela fica encostada em Password e
        // no rascunho; a sessao corrente continua valendo e a senha antiga continua abrindo o
        // painel ate a efetivacao unica do SAIR.
        if (nova != anterior) {
            if (!gravacaoAceita(draft_.setPassword(nova))) {
                return;
            }
            if (!password_.stage(nova)) {
                const Status restauro = draft_.setPassword(anterior);
                (void)restauro;
                showMessage(MenuState::FalhaGrav, editReturn_, kFalhaGravMs);
                return;
            }
            pending_ = true;
        }
        gravado();
        return;
    }
    if (gesture.kind != GestureKind::ShortTap) {
        return;
    }
    switch (gesture.key) {
        case Key::Menu: editor_.menu(); break;
        case Key::Up: editor_.up(); break;
        case Key::Down: editor_.down(); break;
    }
    dirty_ = true;
}

void MenuMachine::onRevisao(const Gesture& gesture) {
    if (gesture.key == Key::Menu && gesture.kind == GestureKind::Hold) {
        commitOnExit();
        return;
    }
    if (gesture.kind == GestureKind::ShortTap && gesture.key == Key::Menu) {
        // Desistiu de confirmar agora: volta ao menu com a edicao ainda pendente.
        state_ = MenuState::Menu;
        dirty_ = true;
    }
}

// --- transicoes ---

void MenuMachine::openLogin() {
    if (password_.locked()) {
        // A13: enquanto o bloqueio de 60 s dura, a tela e a do bloqueio - nao a de login.
        state_ = MenuState::LoginBloqueado;
        backTo_ = MenuState::Login;
        msgSpanMs_ = 0;
        dirty_ = true;
        return;
    }
    editor_.open(kCampoSenha, 0);
    state_ = MenuState::Login;
    msgSpanMs_ = 0;
    dirty_ = true;
}

void MenuMachine::openMenu() {
    // Sem pendencia, o rascunho parte do que esta ativo; com pendencia, A13 manda continuar a
    // mesma edicao que o timeout deixou para revisar.
    if (!pending_) {
        draft_ = active_;
    }
    selTop_ = 0;
    selSub_ = 0;
    state_ = MenuState::Menu;
    msgSpanMs_ = 0;
    dirty_ = true;
}

void MenuMachine::openItem() {
    switch (static_cast<MenuItem>(selTop_)) {
        // L132: "Sair | Retorna ao Modo Normal". Voltar faz o mesmo neste nivel
        // (docs/ihm-estados.md 5.3, contradicao 6).
        case MenuItem::Voltar:
        case MenuItem::Sair: requestExit(); break;
        case MenuItem::AjustaPreset: openSubEixo(SubKind::Preset); break;
        case MenuItem::AutoCalibracao: openSubEixo(SubKind::AutoCal); break;
        case MenuItem::Limite1:
        case MenuItem::Limite2:
        case MenuItem::Limite3:
        case MenuItem::Limite4: openSubLimite(); break;
        case MenuItem::SentidoSensor: openSubEixo(SubKind::Sentido); break;
        case MenuItem::Senha: openSenha(); break;
    }
}

void MenuMachine::openSubEixo(SubKind kind) {
    subKind_ = kind;
    selSub_ = 0;
    state_ = MenuState::SubEixo;
    dirty_ = true;
}

void MenuMachine::openSubLimite() {
    selSub_ = 0;
    state_ = MenuState::SubLimite;
    dirty_ = true;
}

void MenuMachine::openValor() {
    editReturn_ = MenuState::SubLimite;
    editor_.open(kCampoAngular, draft_.limitValue(currentLimit()).deciDegrees());
    state_ = MenuState::EditValor;
    dirty_ = true;
}

void MenuMachine::openOperacao() {
    editReturn_ = MenuState::SubLimite;
    opSel_ = static_cast<uint8_t>(draft_.limitOp(currentLimit()));
    state_ = MenuState::EditOperacao;
    dirty_ = true;
}

void MenuMachine::openSentido(Axis axis) {
    axis_ = axis;
    editReturn_ = MenuState::SubEixo;
    dirSel_ = static_cast<uint8_t>(draft_.sensorDir(axis));
    state_ = MenuState::EditSentido;
    dirty_ = true;
}

void MenuMachine::openSenha() {
    editReturn_ = MenuState::Menu;
    editor_.open(kCampoSenha, static_cast<int16_t>(draft_.password()));
    state_ = MenuState::EditSenha;
    dirty_ = true;
}

void MenuMachine::requestExit() {
    if (pending_) {
        state_ = MenuState::Revisao;
        dirty_ = true;
        return;
    }
    toNormal();
}

void MenuMachine::commitOnExit() {
    // A13: ESTE e o instante unico em que a configuracao editada passa a comandar rele.
    active_ = draft_;
    password_.commitOnExit();
    pending_ = false;
    toNormal();
}

void MenuMachine::showMessage(MenuState msgState, MenuState backTo, uint32_t spanMs) {
    state_ = msgState;
    backTo_ = backTo;
    msgSinceMs_ = clock_.nowMs();
    msgSpanMs_ = spanMs;
    dirty_ = true;
}

void MenuMachine::gravado() { showMessage(MenuState::GravOk, editReturn_, kGravOkMs); }

bool MenuMachine::gravacaoAceita(Status status) {
    if (status.ok()) {
        return true;
    }
    // O agregado recusou (seletor inexistente ou valor fora de faixa): E8, sem pendencia e sem
    // anuncio de sucesso.
    showMessage(MenuState::FalhaGrav, editReturn_, kFalhaGravMs);
    return false;
}

void MenuMachine::toNormal() {
    state_ = MenuState::Normal;
    msgSpanMs_ = 0;
    dirty_ = false;
}

// --- desenho ---

void MenuMachine::drawLine(int16_t y, const char* text) {
    display_.drawText(0, y, text, TextFont::Small, TextInk::Normal);
}

void MenuMachine::drawList(const char* header, const char* const* items, uint8_t count,
                           uint8_t sel) {
    uint8_t inicio = 0;
    if (count > kListWindow) {
        inicio = (sel > 0u) ? static_cast<uint8_t>(sel - 1u) : 0u;
        const uint8_t maxInicio = static_cast<uint8_t>(count - kListWindow);
        if (inicio > maxInicio) {
            inicio = maxInicio;
        }
    }
    drawLine(kListaCabecalhoY, header);
    for (uint8_t i = 0; i < kListWindow && (inicio + i) < count; ++i) {
        const uint8_t indice = static_cast<uint8_t>(inicio + i);
        display_.drawText(0, kListaItemY[i], items[indice], TextFont::Small,
                          (indice == sel) ? TextInk::Inverse : TextInk::Normal);
    }
}

void MenuMachine::drawEditLine(int16_t y, const char* text, uint8_t inverseIndex) {
    display_.drawText(0, y, text, TextFont::Small, TextInk::Normal);
    const size_t comprimento = strlen(text);
    if (inverseIndex >= comprimento) {
        return;
    }
    char cabeca[kLineCap];
    uint8_t i = 0;
    for (; i < inverseIndex && (i + 1u) < kLineCap; ++i) {
        cabeca[i] = text[i];
    }
    cabeca[i] = '\0';
    const char digito[2] = {text[inverseIndex], '\0'};
    // REQ-DSP-04: o caractere em edicao e desenhado em Inverse por cima da linha inteira.
    display_.drawText(static_cast<int16_t>(display_.textWidthPx(TextFont::Small, cabeca)), y,
                      digito, TextFont::Small, TextInk::Inverse);
}

uint8_t MenuMachine::buildFieldLine(const char* prefix) {
    line_[0] = '\0';
    const uint8_t prefixo = appendTo(line_, kLineCap, 0, prefix);
    char campo[DigitEditor::kTextCap];
    if (editor_.format(campo, DigitEditor::kTextCap)) {
        appendTo(line_, kLineCap, prefixo, campo);
    }
    return prefixo;
}

void MenuMachine::render() {
    // Em Normal e durante o assistente o display e de outro modulo: nem clear(), nem present().
    if (!ownsDisplay()) {
        return;
    }
    display_.clear();
    switch (state_) {
        case MenuState::Normal:
        case MenuState::Assistente: break;

        case MenuState::Login: {
            const uint8_t prefixo = buildFieldLine(kPrefixoLogin);
            drawEditLine(kLinha1Y, line_,
                         static_cast<uint8_t>(prefixo + editor_.cursorTextIndex()));
            break;
        }

        case MenuState::LoginErro: drawLine(kLinha1Y, kMsgSenhaIncorreta); break;
        case MenuState::LoginBloqueado: drawLine(kLinha1Y, kMsgBloqueado); break;

        // L112: a lista, com o item selecionado em Inverse (desvio declarado d).
        case MenuState::Menu: drawList(kCabecalhoMenu, kNomeItem, kItemCount, selTop_); break;

        case MenuState::SubEixo: {
            const char* const* itens = kItemPreset;
            const char* cabecalho = kCabecalhoPreset;
            if (subKind_ == SubKind::AutoCal) {
                itens = kItemAutoCal;
                cabecalho = kCabecalhoAutoCal;
            } else if (subKind_ == SubKind::Sentido) {
                itens = kItemSentido;
                cabecalho = kCabecalhoSentido;
            }
            drawList(cabecalho, itens, kSubItemCount, selSub_);
            break;
        }

        case MenuState::SubLimite: {
            const uint8_t limite = static_cast<uint8_t>(currentLimit());
            char linhaValor[kLineCap];
            char linhaOperacao[kLineCap];
            linhaValor[0] = '\0';
            uint8_t n = appendTo(linhaValor, kLineCap, 0, "Valor Limite ");
            appendTo(linhaValor, kLineCap, n, kEtiquetaLimite[limite]);
            linhaOperacao[0] = '\0';
            n = appendTo(linhaOperacao, kLineCap, 0, "Operacao Limite ");
            appendTo(linhaOperacao, kLineCap, n, kEtiquetaLimite[limite]);
            const char* itens[kSubItemCount] = {kNomeItem[0], linhaValor, linhaOperacao};
            drawList(kCabecalhoLimite[limite], itens, kSubItemCount, selSub_);
            break;
        }

        case MenuState::EditValor: {
            // L214 a L217: "Valor Limite X1(<grau>):+000,0", em ASCII "(graus)".
            char prefixo[kLineCap];
            prefixo[0] = '\0';
            uint8_t n = appendTo(prefixo, kLineCap, 0, "Valor Limite ");
            n = appendTo(prefixo, kLineCap, n, kEtiquetaLimite[static_cast<uint8_t>(currentLimit())]);
            n = appendTo(prefixo, kLineCap, n, "(graus):");
            const uint8_t base = buildFieldLine(prefixo);
            drawEditLine(kLinha1Y, line_, static_cast<uint8_t>(base + editor_.cursorTextIndex()));
            break;
        }

        case MenuState::EditOperacao: {
            line_[0] = '\0';
            uint8_t n = appendTo(line_, kLineCap, 0, "Operacao Limite ");
            n = appendTo(line_, kLineCap, n, kEtiquetaLimite[static_cast<uint8_t>(currentLimit())]);
            n = appendTo(line_, kLineCap, n, ":");
            drawLine(kLinha1Y, line_);
            drawLine(kLinha2Y, kNomeOperacao[opSel_]);
            break;
        }

        case MenuState::EditSentido: {
            line_[0] = '\0';
            uint8_t n = appendTo(line_, kLineCap, 0, "Sentido Sensor ");
            n = appendTo(line_, kLineCap, n, (axis_ == Axis::X) ? "X" : "Y");
            n = appendTo(line_, kLineCap, n, ":");
            drawLine(kLinha1Y, line_);
            drawLine(kLinha2Y, kNomeSentido[dirSel_ & 1u]);
            break;
        }

        // A9: aviso obrigatorio de 3 s, com o eixo e os dois limites a conferir (L199).
        case MenuState::AvisoSentido:
            drawLine(kLinha1Y, (axis_ == Axis::X) ? kMsgSentidoX : kMsgSentidoY);
            drawLine(kLinha2Y, (axis_ == Axis::X) ? kMsgPresetZeradoX : kMsgPresetZeradoY);
            break;

        case MenuState::EditSenha: {
            const uint8_t prefixo = buildFieldLine(kPrefixoEditaSenha);
            drawEditLine(kLinha1Y, line_,
                         static_cast<uint8_t>(prefixo + editor_.cursorTextIndex()));
            break;
        }

        case MenuState::Recusa: drawLine(kLinha1Y, recusaMsg_); break;
        case MenuState::GravOk: drawLine(kLinha1Y, kMsgGravOk); break;
        case MenuState::FalhaGrav: drawLine(kLinha1Y, kMsgFalhaGrav); break;
        case MenuState::Revisao: drawLine(kLinha1Y, kMsgRevisao); break;
    }
    display_.present();
}

}  // namespace domain
