// Implementacao da tela do Modo Normal. Contrato, REQ, citacoes do manual e a lista do que
// esta tela deliberadamente NAO faz estao em domain/ui/normal_screen.h.
//
// Quatro regras de escrita que valem para o arquivo inteiro:
//   1. Nenhuma string de angulo e montada aqui. A leitura sai de Angle::format(), dono unico do
//      formato +/-XXX,X de DSP-02 e do traco de "nao existe leitura"; o offset do Preset, que
//      por A9 vai a +/-1800 decimos e nao cabe na faixa de Angle, sai de
//      ui::PresetWizard::formatDeci(), dono unico daquele mesmo texto. Uma segunda
//      implementacao do formato seria uma segunda oportunidade de divergir do manual - e foi
//      exatamente o que a revisao achou aqui: montar o indicador com Angle::fromDeciDegrees()
//      apagava com "---,-" justamente os offsets grandes, que sao os que precisam ser vistos.
//   2. Nenhuma coordenada de linha e chutada. As alturas vem de IDisplay::lineHeightPx(), as
//      larguras de IDisplay::textWidthPx() e a capacidade da coluna de status de
//      statusRowCapacity(): a porta existe justamente para o dominio alinhar sem adivinhar, e
//      o layout continua legivel se a metrica da fonte mudar. O teste de geometria de
//      test/native/test_normal confere CADA retangulo desenhado contra os limites do painel e
//      contra os outros retangulos do mesmo quadro.
//   3. Nenhuma linha de falha e desenhada em fonte grande. Com a metrica do painel a MENOR das
//      quatro primeiras linhas aprovadas em A7 ("FALHA DO SENSOR", 15 glifos) ja passa dos
//      256 px em fonte grande; um seletor de fonte aqui seria ramo morto, e ramo morto nao se
//      mantem em equipamento de seguranca.
//   4. A coluna de status da tela principal tem CINCO linhas e nao seis. Quando os dois eixos
//      estao em modos de saida diferentes a saida ocupa duas linhas e alguma coisa tem de sair:
//      sai "ENLACE OK", que e acrescimo desta tela, e nunca o indicador de Preset, que e a
//      decisao 1 item 17 e cuja permanencia e o mecanismo de seguranca de DECISIONS.md:2529. A
//      ausencia de "ENLACE OK" nunca e a unica prova de enlace: falha de enlace ROUBA a tela.
#include "domain/ui/normal_screen.h"

#include "domain/parameters.h"
#include "domain/ui/preset_wizard.h"

namespace domain {

namespace {

// Coluna direita da tela principal. A maior linha da coluna ("SAIDA X:MEDICAO", 15 glifos)
// cabe nos 106 px que sobram, e a maior linha da coluna esquerda ("X:+045,0" em fonte grande,
// 144 px) termina antes daqui.
constexpr int16_t kMainStatusX = 150;

// Coluna do valor no detalhe: "+045,0" em fonte grande ocupa 108 px e fecha em 248.
constexpr int16_t kDetailValueX = 140;

constexpr int16_t kMargin = 1;

const char* stateToken(RelayState state) { return (state == RelayState::Signalled) ? "AL" : "--"; }

// Montagem de linha em buffer fixo. Sem heap e sem snprintf: trunca em silencio no cap, que e
// dimensionado para a maior linha do produto.
class Line {
public:
    Line() : len_(0) { buffer_[0] = '\0'; }

    void add(const char* text) {
        if (text == nullptr) {
            return;
        }
        for (uint8_t i = 0; text[i] != '\0' && len_ + 1u < NormalScreen::kLineCap; ++i) {
            buffer_[len_] = text[i];
            ++len_;
        }
        buffer_[len_] = '\0';
    }

    void add(const Angle& angle) {
        char texto[Angle::kTextCap];
        if (angle.format(texto, Angle::kTextCap)) {
            add(texto);
        }
    }

    // A9: o offset do Preset vai a +/-1800 decimos, o DOBRO da faixa de medicao, e por isso NAO
    // passa por Angle. Fora de +/-1800 nao existe valor legitimo, e o campo cai no traco.
    void addPresetOffset(int16_t deci) {
        char texto[ui::PresetWizard::kValueTextCap];
        const bool naFaixa = (deci >= Parameters::kPresetOffsetMinDeci) &&
                             (deci <= Parameters::kPresetOffsetMaxDeci);
        if (naFaixa && ui::PresetWizard::formatDeci(deci, texto, ui::PresetWizard::kValueTextCap)) {
            add(texto);
            return;
        }
        add(Angle::invalid());
    }

    const char* text() const { return buffer_; }

private:
    char buffer_[NormalScreen::kLineCap];
    uint8_t len_;
};

}  // namespace

NormalScreen::NormalScreen(IDisplay& display, KeyGesture& gesture)
    : display_(display), gesture_(gesture), view_(NormalView::Main), lastStatus_(kOk) {}

void NormalScreen::reset() {
    view_ = NormalView::Main;
    gesture_.flush();
}

NormalRequest NormalScreen::update(const NormalInput& in) {
    NormalRequest pedido = NormalRequest::None;
    Gesture gesto{};
    while (pedido == NormalRequest::None && gesture_.takeGesture(gesto)) {
        pedido = apply(gesto);
    }
    render(in);
    return pedido;
}

NormalRequest NormalScreen::apply(const Gesture& gesture) {
    if (gesture.kind == GestureKind::Hold) {
        // NRM-03, manual 5.2 L81 e T25: MENU mantida ~3 s abre o login. O hold chega na borda
        // dos 3000 ms, com a tecla ainda prensada, e por isso a soltura seguinte nao vira toque
        // curto.
        if (gesture.key == Key::Menu) {
            return NormalRequest::OpenLogin;
        }
        // Decisao 12 item 8, aprovada: BAIXO mantida 3000 ms reexecuta o autoteste do display,
        // sem senha. O hold de CIMA continua sem funcao declarada e cai no ignorado de baixo -
        // invariante 6: gesto sem funcao e ignorado, nunca reinterpretado.
        if (gesture.key == Key::Down) {
            return NormalRequest::SelfTest;
        }
        return NormalRequest::None;
    }

    // MAN-5.6-L152 e T15: o PSET e o DUPLO toque em CIMA. Chega depois dos dois toques curtos
    // que o compoem, e e o unico gesto de CIMA com funcao aqui.
    if (gesture.kind == GestureKind::DoubleTap && gesture.key == KeyGesture::kDoubleTapKey) {
        return NormalRequest::Preset;
    }

    // NRM-02 com a decisao D3 do briefing (T12, T13 e T14): BAIXO nao alterna mais X/Y, ela
    // percorre principal -> detalhe X -> detalhe Y -> principal.
    if (gesture.kind == GestureKind::ShortTap && gesture.key == Key::Down) {
        advanceView();
        return NormalRequest::None;
    }

    // NRM-04 e invariante 6: todo o resto - inclusive o toque simples em CIMA - e IGNORADO, e
    // nunca reinterpretado como outro gesto.
    return NormalRequest::None;
}

void NormalScreen::advanceView() {
    switch (view_) {
        case NormalView::Main:
            view_ = NormalView::DetailX;
            return;
        case NormalView::DetailX:
            view_ = NormalView::DetailY;
            return;
        case NormalView::DetailY:
            view_ = NormalView::Main;
            return;
    }
}

void NormalScreen::render(const NormalInput& in) {
    lastStatus_ = kOk;
    keep(display_.clear());

    // COM-01: qualquer estado de enlace diferente de Ok substitui a area de leitura pela
    // mensagem de falha (B7 de docs/ihm-estados.md). A selecao de tela continua valendo por
    // baixo e reaparece intacta quando o enlace volta.
    // A7: o LATCH tambem manda. Com o latch armado a tela continua sendo a de falha mesmo com o
    // enlace ja recuperado, porque os quatro reles continuam em alarme ate o rearme manual e
    // "ENLACE OK" ao lado de quatro canais lendo "AL" nao teria explicacao nem caminho de acao.
    if (in.link != NormalLinkState::Ok || in.linkLatched) {
        renderFault(in);
    } else if (view_ == NormalView::Main) {
        renderMain(in);
    } else {
        renderDetail(in, (view_ == NormalView::DetailX) ? kNormalAxisX : kNormalAxisY);
    }

    renderHeartbeat(in);
    keep(display_.present());
}

void NormalScreen::renderMain(const NormalInput& in) {
    const int16_t alturaGrande = static_cast<int16_t>(display_.lineHeightPx(TextFont::Large));
    const int16_t passo = smallRowHeight();

    // DSP-01 e NRM-01: os DOIS eixos ao mesmo tempo, cada um com a sua identificacao. Leitura
    // ausente sai como o traco de Angle, nunca como zero - um zero seria uma medicao.
    Line eixoX;
    eixoX.add("X:");
    eixoX.add(in.reading[kNormalAxisX]);
    drawAt(kMargin, kMargin, eixoX.text(), TextFont::Large);

    Line eixoY;
    eixoY.add("Y:");
    eixoY.add(in.reading[kNormalAxisY]);
    drawAt(kMargin, static_cast<int16_t>(kMargin + alturaGrande + 2), eixoY.text(), TextFont::Large);

    const bool mesmoModo = sameAnalogMode(in);
    const bool temPreset = in.presetActive[kNormalAxisX] || in.presetActive[kNormalAxisY];
    const uint8_t necessarias =
        static_cast<uint8_t>(2u + (mesmoModo ? 1u : 2u) + (temPreset ? 1u : 0u));
    const bool cabeEnlace = necessarias < statusRowCapacity();

    int16_t linha = 0;

    // Os quatro contatos como o CLP os esta recebendo. Em falha de enlace os quatro leem "AL",
    // inclusive os programados em Off - e A5 aplicada pelo avaliador, nao deduzida aqui.
    Line limitesX;
    limitesX.add(limitLabel(0));
    limitesX.add(":");
    limitesX.add(stateToken(in.limit[0].state));
    limitesX.add(" ");
    limitesX.add(limitLabel(1));
    limitesX.add(":");
    limitesX.add(stateToken(in.limit[1].state));
    drawAt(kMainStatusX, linha, limitesX.text(), TextFont::Small);
    linha = static_cast<int16_t>(linha + passo);

    Line limitesY;
    limitesY.add(limitLabel(2));
    limitesY.add(":");
    limitesY.add(stateToken(in.limit[2].state));
    limitesY.add(" ");
    limitesY.add(limitLabel(3));
    limitesY.add(":");
    limitesY.add(stateToken(in.limit[3].state));
    drawAt(kMainStatusX, linha, limitesY.text(), TextFont::Small);
    linha = static_cast<int16_t>(linha + passo);

    // Invariante 2 de docs/ihm-estados.md: o modo da saida e POR EIXO. Com os dois eixos no
    // mesmo modo cabe uma linha so; quando diferem, cada eixo diz o seu, porque durante a Auto
    // Calibracao a saida de um eixo esta simulada e a do outro continua valendo.
    if (mesmoModo) {
        Line saida;
        saida.add("SAIDA:");
        saida.add(analogText(in.analog[kNormalAxisX]));
        drawAt(kMainStatusX, linha, saida.text(), TextFont::Small);
        linha = static_cast<int16_t>(linha + passo);
    } else {
        for (uint8_t eixo = 0; eixo < kNormalAxisCount; ++eixo) {
            Line saida;
            saida.add("SAIDA ");
            saida.add((eixo == kNormalAxisY) ? "Y" : "X");
            saida.add(":");
            saida.add(analogText(in.analog[eixo]));
            drawAt(kMainStatusX, linha, saida.text(), TextFont::Small);
            linha = static_cast<int16_t>(linha + passo);
        }
    }

    // COM-01 pelo lado saudavel: o operador ve que o enlace esta vivo sem precisar da ausencia
    // de mensagem como prova. E o unico campo que cede lugar quando a coluna enche (regra 4 do
    // cabecalho deste arquivo).
    if (cabeEnlace) {
        drawAt(kMainStatusX, linha, "ENLACE OK", TextFont::Small);
        linha = static_cast<int16_t>(linha + passo);
    }

    // Decisao 1 item 17: enquanto houver offset de Preset, a tela principal diz que a leitura e
    // relativa. Na tela principal cabe a INDICACAO de quais eixos estao deslocados; o valor do
    // deslocamento aparece na tela de detalhe do eixo, que tem largura para ele.
    if (temPreset) {
        renderPresetMark(in, kMainStatusX, linha);
    }
}

void NormalScreen::renderDetail(const NormalInput& in, uint8_t axis) {
    const int16_t passo = smallRowHeight();
    const uint8_t eixo = (axis == kNormalAxisY) ? kNormalAxisY : kNormalAxisX;
    const char* nome = (eixo == kNormalAxisY) ? "Y" : "X";
    const uint8_t primeiroCanal = (eixo == kNormalAxisY) ? 2u : 0u;

    Line cabecalho;
    cabecalho.add("EIXO ");
    cabecalho.add(nome);
    drawAt(kMargin, 0, cabecalho.text(), TextFont::Small);

    Line valor;
    valor.add(in.reading[eixo]);
    drawAt(kDetailValueX, passo, valor.text(), TextFont::Large);

    int16_t linha = passo;
    for (uint8_t i = 0; i < 2u; ++i) {
        const uint8_t canal = static_cast<uint8_t>(primeiroCanal + i);
        Line limite;
        limite.add(limitLabel(canal));
        limite.add(":");
        limite.add(stateToken(in.limit[canal].state));
        limite.add(" ");
        limite.add(in.limit[canal].value);
        drawAt(kMargin, linha, limite.text(), TextFont::Small);
        linha = static_cast<int16_t>(linha + passo);
    }

    // O modo da saida DESTE eixo, e nunca o do outro: e esta a linha que o tecnico de bancada
    // le com o voltimetro na mao.
    Line saida;
    saida.add("SAIDA ");
    saida.add(nome);
    saida.add(":");
    saida.add(analogText(in.analog[eixo]));
    drawAt(kMargin, linha, saida.text(), TextFont::Small);
    linha = static_cast<int16_t>(linha + passo);

    // Decisao 1 item 17 com a faixa de A9: o offset vai a +/-1800 decimos e o texto e o mesmo
    // que PresetWizard imprime na confirmacao do gesto. Fora de +/-1800 nao ha valor legitimo e
    // o campo cai no traco.
    if (in.presetActive[eixo]) {
        Line preset;
        preset.add("PSET ");
        preset.add(nome);
        preset.add(":");
        preset.addPresetOffset(in.presetOffsetDeci[eixo]);
        drawAt(kMargin, linha, preset.text(), TextFont::Small);
    }
}

void NormalScreen::renderFault(const NormalInput& in) {
    const char* linha1 = kTextAwaiting;
    const char* dica = "";
    switch (in.link) {
        case NormalLinkState::CommFault:
            linha1 = kTextCommFault;
            dica = kTextCommFaultHint;
            break;
        case NormalLinkState::SensorFault:
            linha1 = kTextSensorFault;
            dica = kTextSensorFaultHint;
            break;
        case NormalLinkState::Unstable:
            linha1 = kTextUnstable;
            break;
        case NormalLinkState::Awaiting:
        case NormalLinkState::Ok:
            break;
    }

    // A7, aprovada: com o latch armado a SEGUNDA linha e o texto de rearme, e nao a dica de
    // cabo - a acao que resolve deixou de ser inspecionar o cabo e passou a ser rearmar no
    // menu. A primeira linha continua dizendo qual falha travou.
    const char* linha2 = in.linkLatched ? kTextLatched : dica;

    // A7 de novo, na combinacao que so o latch produz: o enlace ja voltou (link == Ok) e o
    // latch continua armado. Nao existe falha corrente para anunciar na primeira linha, entao
    // o texto de rearme sobe para ela e a segunda linha fica vazia.
    if (in.link == NormalLinkState::Ok) {
        linha1 = kTextLatched;
        linha2 = "";
    }

    const int16_t passo = smallRowHeight();

    // O rodape e ancorado no pe da tela: os contatos e o modo da saida sao a informacao de
    // seguranca e nao podem ser empurrados para fora por uma linha de mensagem a mais.
    const int16_t rodapeSaida = static_cast<int16_t>(display_.heightPx() - passo);
    const int16_t rodapeLimites = static_cast<int16_t>(rodapeSaida - passo);

    int16_t linha = 0;
    drawAt(kMargin, linha, linha1, TextFont::Small);
    linha = static_cast<int16_t>(linha + passo);

    if (linha2[0] != '\0') {
        drawAt(kMargin, linha, linha2, TextFont::Small);
    }
    linha = static_cast<int16_t>(linha + passo);

    // Decisao 12 item 12: em falha o campo de valor e substituido pelo traco. Quem apaga o
    // numero e o Angle invalido que o ciclo entrega; a tela nunca inventa uma leitura nem
    // conserva a ultima como se fosse atual.
    Line leitura;
    leitura.add("X:");
    leitura.add(in.reading[kNormalAxisX]);
    leitura.add(" Y:");
    leitura.add(in.reading[kNormalAxisY]);
    drawAt(kMargin, linha, leitura.text(), TextFont::Small);

    // Decisao 1 item 17 e DECISIONS.md:2529: a permanencia do indicador de Preset E o mecanismo
    // de seguranca. MEDICAO INSTAVEL (DECISIONS.md:1733) e condicao rotineira de icamento e nao
    // poe rele nem saida analogica em falha (DECISIONS.md:1738 e :1740 aplicam a falha so em
    // AGUARDANDO, COMUNICACAO e SENSOR): ali a leitura continua valendo e continua sendo
    // relativa ao Preset. Some da tela de falha e o operador perde a unica prova visivel disso.
    if (in.presetActive[kNormalAxisX] || in.presetActive[kNormalAxisY]) {
        renderPresetMark(in, kMainStatusX, linha);
    }

    Line limites;
    for (uint8_t canal = 0; canal < kLimitChannelCount; ++canal) {
        if (canal != 0) {
            limites.add(" ");
        }
        limites.add(limitLabel(canal));
        limites.add(":");
        limites.add(stateToken(in.limit[canal].state));
    }
    drawAt(kMargin, rodapeLimites, limites.text(), TextFont::Small);

    // Aqui o rodape tem a largura inteira do painel, entao os dois eixos cabem na mesma linha
    // quando os modos diferem - ao contrario da coluna estreita da tela principal.
    Line saida;
    if (sameAnalogMode(in)) {
        saida.add("SAIDA:");
        saida.add(analogText(in.analog[kNormalAxisX]));
    } else {
        saida.add("SAIDA X:");
        saida.add(analogText(in.analog[kNormalAxisX]));
        saida.add(" SAIDA Y:");
        saida.add(analogText(in.analog[kNormalAxisY]));
    }
    drawAt(kMargin, rodapeSaida, saida.text(), TextFont::Small);
}

// D12 item 11: marca de 4x4 px girando por quatro posicoes dentro da caixa de 8x8 px do canto
// inferior direito, na ordem horaria. Quem conta transacao valida e a camada de aplicacao; aqui
// so mora a geometria.
void NormalScreen::renderHeartbeat(const NormalInput& in) {
    const int16_t caixaX =
        static_cast<int16_t>(display_.widthPx() - kHeartbeatBoxPx - kMargin);
    const int16_t caixaY =
        static_cast<int16_t>(display_.heightPx() - kHeartbeatBoxPx - kMargin);
    const uint8_t fase = static_cast<uint8_t>(in.heartbeatPhase % kHeartbeatPhases);
    const int16_t dx = (fase == 1u || fase == 2u) ? kHeartbeatMarkPx : 0;
    const int16_t dy = (fase >= 2u) ? kHeartbeatMarkPx : 0;
    keep(display_.fillRect(static_cast<int16_t>(caixaX + dx), static_cast<int16_t>(caixaY + dy),
                           kHeartbeatMarkPx, kHeartbeatMarkPx, true));
}

void NormalScreen::renderPresetMark(const NormalInput& in, int16_t x, int16_t y) {
    Line preset;
    preset.add("PSET:");
    if (in.presetActive[kNormalAxisX]) {
        preset.add("X");
    }
    if (in.presetActive[kNormalAxisY]) {
        preset.add("Y");
    }
    drawAt(x, y, preset.text(), TextFont::Small);
}

void NormalScreen::drawAt(int16_t x, int16_t y, const char* text, TextFont font) {
    keep(display_.drawText(x, y, text, font, TextInk::Normal));
}

int16_t NormalScreen::smallRowHeight() const {
    return static_cast<int16_t>(display_.lineHeightPx(TextFont::Small) + 1);
}

// Quantas linhas de fonte pequena cabem inteiras no painel: a ultima comeca em
// (capacidade-1)*passo e tem de terminar dentro da altura.
uint8_t NormalScreen::statusRowCapacity() const {
    const int16_t altura = static_cast<int16_t>(display_.heightPx());
    const int16_t linha = static_cast<int16_t>(display_.lineHeightPx(TextFont::Small));
    if (altura < linha) {
        return 0;
    }
    return static_cast<uint8_t>((altura - linha) / smallRowHeight() + 1);
}

void NormalScreen::keep(Status status) {
    if (lastStatus_.ok() && status.failed()) {
        lastStatus_ = status;
    }
}

bool NormalScreen::sameAnalogMode(const NormalInput& in) {
    return in.analog[kNormalAxisX] == in.analog[kNormalAxisY];
}

// Rotulos do manual 5.9 L193: os limites 1 e 2 sao do eixo X, os limites 3 e 4 do eixo Y, e o
// operador os le como X1, X2, Y1 e Y2.
const char* NormalScreen::limitLabel(uint8_t index) {
    switch (index) {
        case 0: return "X1";
        case 1: return "X2";
        case 2: return "Y1";
        case 3: return "Y2";
        default: return "??";
    }
}

const char* NormalScreen::analogText(NormalAnalogMode mode) {
    switch (mode) {
        case NormalAnalogMode::Tracking: return "MEDICAO";
        case NormalAnalogMode::Fault: return "FALHA";
        case NormalAnalogMode::Calibrating: return "CALIB";
    }
    return "FALHA";
}

}  // namespace domain
