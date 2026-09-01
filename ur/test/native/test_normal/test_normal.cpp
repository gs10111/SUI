// Testes de dominio da tela do Modo Normal (NormalScreen).
//
// O que esta preso aqui, com a fonte de cada exigencia:
//
// DSP-01 (manual 4 L64, "o display indica o valor da medicao, inclusive para valores
//   negativos"): os dois eixos aparecem sempre, e quando nao ha leitura o campo mostra o traco
//   de Angle. Um zero no lugar do traco seria uma medicao inventada, e e o defeito que este
//   arquivo existe para impedir.
// DSP-02 (manual 5.5 L131, "sempre expressos em graus, no formato +/-XXX,X, com uma casa
//   decimal fixa"): o texto e conferido byte a byte, com sinal, virgula e uma casa.
// NRM-01 (manual 5.2 L74..L78) e a decisao D3 do briefing: a tela principal mostra X, Y, os
//   quatro limites, o estado do enlace, o modo da saida analogica e a indicacao de Preset.
// NRM-02 (manual 5.2 L83) com D3: a tecla BAIXO nao alterna mais X/Y - ela percorre
//   principal -> detalhe X -> detalhe Y -> principal (T12, T13 e T14 de docs/ihm-estados.md).
// NRM-03 (manual 5.2 L81, T25): MENU mantida ~3 s pede a tela de login, INCLUSIVE com a tela de
//   falha no ar - com o latch de A7 armado essa e a UNICA rota ate o menu de rearme.
// NRM-04 (manual 5.2 L85) com o desvio declarado de 5.3 item 1 de docs/ihm-estados.md: o toque
//   simples em CIMA nao tem funcao e e IGNORADO; so o duplo toque age, como PSET (L152).
// COM-01 (docs/protocolo-rs485.md:629 e manual 7 L297): a perda de comunicacao e sinalizada no
//   painel. O manual promete a mensagem e nao publica o texto; o texto e o da errata.
// A5, aprovada: na falha de enlace os quatro limites vao a alarme, inclusive os programados em
//   Off - e a tela tem de mostrar os quatro em alarme, sem inventar excecao para o canal Off.
// A7, aprovada: quatro estados de falha em duas linhas, sem acentuacao; a segunda linha vira
//   "FALHA TRAVADA - REARMAR NO MENU" quando o latch de flapping pega, e a combinacao
//   link == Ok com latch armado CONTINUA na tela de falha.
// A9 com a decisao 1 item 17: o offset de Preset vai a +/-1800 decimos e usa o formatador do
//   PresetWizard, nao a faixa de medicao de Angle.
// D12 item 8, aprovada: BAIXO mantida 3000 ms pede o autoteste do display.
// D12 item 11, aprovada: batimento de dado fresco, marca de 4x4 px girando por quatro posicoes
//   na caixa de 8x8 px do canto inferior direito, comandada por transacao valida.
//
// GEOMETRIA: nenhuma coordenada deste modulo era provada antes. Agora todo teste de quadro
// termina em verificarQuadro(), que exige de CADA texto desenhado: retangulo inteiro dentro dos
// 256x64 do painel (manual 2.1 L26), nenhuma intersecao com outro texto do mesmo quadro e
// nenhuma intersecao com a caixa do batimento de D12 item 11.
//
// O tempo vem do FakeClock canonico de test/fakes/fake_clock.h, que comeca em 0xFFFF0000: todo
// gesto deste arquivo atravessa o wrap de 2^32 ms. As bordas de tecla sao escritas pelo
// FakeKeypad e viram gesto no KeyGesture ja testado - nenhum gesto e injetado a mao, porque o
// caso que interessa em NRM-04 e justamente o de o duplo toque tambem entregar os dois toques
// curtos que o compoem.
#include <string.h>
#include <unity.h>

#include "domain/ui/normal_screen.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_display.h"
#include "fakes/fake_keypad.h"

using domain::Angle;
using domain::KeyGesture;
using domain::kNormalAxisX;
using domain::kNormalAxisY;
using domain::NormalAnalogMode;
using domain::NormalInput;
using domain::NormalLinkState;
using domain::NormalRequest;
using domain::NormalScreen;
using domain::NormalView;
using test::FakeClock;
using test::FakeDisplay;
using test::FakeKeypad;

namespace {

// Painel de bancada: o FakeDisplay canonico mais duas coisas que ele nao guarda e que a revisao
// mostrou serem justamente as nao provadas - o retangulo do batimento (fillRect) e a injecao de
// falha de desenho, que e o unico jeito de provar o acumulador de lastStatus().
class Painel : public FakeDisplay {
public:
    Painel()
        : rectX_(-1), rectY_(-1), rectW_(0), rectH_(0), rectOn_(false), rects_(0), desenhos_(0),
          falharEm_(0) {}

    Status fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h, bool on) override {
        rectX_ = x;
        rectY_ = y;
        rectW_ = w;
        rectH_ = h;
        rectOn_ = on;
        ++rects_;
        return FakeDisplay::fillRect(x, y, w, h, on);
    }

    // A ordem de falha e contada DENTRO do quadro: clear() abre quadro novo.
    Status clear() override {
        desenhos_ = 0;
        return FakeDisplay::clear();
    }

    Status drawText(int16_t x, int16_t y, const char* text, TextFont font, TextInk ink) override {
        ++desenhos_;
        if (falharEm_ != 0u && desenhos_ >= falharEm_) {
            return (desenhos_ == falharEm_) ? Status(Err::Io) : Status(Err::Range);
        }
        return FakeDisplay::drawText(x, y, text, font, ink);
    }

    void falharNoDesenho(uint8_t ordem) { falharEm_ = ordem; }

    int16_t rectX() const { return rectX_; }
    int16_t rectY() const { return rectY_; }
    uint16_t rectW() const { return rectW_; }
    uint16_t rectH() const { return rectH_; }
    bool rectOn() const { return rectOn_; }
    uint32_t rectCount() const { return rects_; }

private:
    int16_t rectX_;
    int16_t rectY_;
    uint16_t rectW_;
    uint16_t rectH_;
    bool rectOn_;
    uint32_t rects_;
    uint8_t desenhos_;
    uint8_t falharEm_;
};

// Uma UR de bancada inteira: relogio, teclado, reconhecedor de gesto, painel e tela.
struct Bancada {
    FakeClock clock;
    FakeKeypad keypad;
    KeyGesture gesto;
    Painel painel;
    NormalScreen tela;

    Bancada() : clock(), keypad(clock), gesto(keypad, clock), painel(), tela(painel, gesto) {}

    // Um ciclo de IHM: o laco cadencia o reconhecedor, a tela consome o gesto e desenha.
    NormalRequest ciclo(const NormalInput& entrada) {
        gesto.update();
        return tela.update(entrada);
    }
};

NormalInput enlaceSaudavel(int16_t xDeci, int16_t yDeci) {
    NormalInput entrada{};
    entrada.reading[kNormalAxisX] = Angle::fromDeciDegrees(xDeci);
    entrada.reading[kNormalAxisY] = Angle::fromDeciDegrees(yDeci);
    entrada.link = NormalLinkState::Ok;
    entrada.linkLatched = false;
    entrada.analog[kNormalAxisX] = NormalAnalogMode::Tracking;
    entrada.analog[kNormalAxisY] = NormalAnalogMode::Tracking;
    for (uint8_t canal = 0; canal < kLimitChannelCount; ++canal) {
        entrada.limit[canal].state = RelayState::Clear;
        entrada.limit[canal].value = Angle::fromDeciDegrees(static_cast<int16_t>(500 + canal));
    }
    return entrada;
}

// Falha de enlace como o resto do produto a entrega: sem leitura, saida analogica no codigo de
// falha de A2 e os QUATRO contatos sinalizados por A5.
NormalInput enlaceEmFalha(NormalLinkState estado) {
    NormalInput entrada = enlaceSaudavel(0, 0);
    entrada.reading[kNormalAxisX] = Angle::invalid();
    entrada.reading[kNormalAxisY] = Angle::invalid();
    entrada.link = estado;
    entrada.analog[kNormalAxisX] = NormalAnalogMode::Fault;
    entrada.analog[kNormalAxisY] = NormalAnalogMode::Fault;
    for (uint8_t canal = 0; canal < kLimitChannelCount; ++canal) {
        entrada.limit[canal].state = RelayState::Signalled;
    }
    return entrada;
}

void tocar(Bancada& bancada, Key tecla) { bancada.keypad.tap(tecla, 60u); }

// --- geometria do quadro ---

// Painel do produto, manual 2.1 L26. Os literais estao aqui de proposito: comparar o quadro
// contra display_.widthPx() do proprio codigo sob teste deixaria a coluna passar a qualquer
// valor sem reprovar nada.
constexpr int16_t kPainelW = 256;
constexpr int16_t kPainelH = 64;

// D12 item 11: caixa de 8x8 px em (247,55)-(254,62). Nenhum texto pode invadi-la.
constexpr int16_t kBatimentoX = 247;
constexpr int16_t kBatimentoY = 55;
constexpr int16_t kBatimentoPx = 8;

struct Caixa {
    int16_t x0;
    int16_t y0;
    int16_t x1;
    int16_t y1;
};

Caixa caixaDoTexto(const Painel& painel, uint8_t indice) {
    const FakeDisplay::Draw& d = painel.draw(indice);
    const int16_t largura = static_cast<int16_t>(painel.textWidthPx(d.font, d.text));
    const int16_t altura = static_cast<int16_t>(painel.lineHeightPx(d.font));
    return Caixa{d.x, d.y, static_cast<int16_t>(d.x + largura),
                 static_cast<int16_t>(d.y + altura)};
}

bool intersecta(const Caixa& a, const Caixa& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

void verificarQuadro(const Painel& painel) {
    const Caixa batimento{kBatimentoX, kBatimentoY,
                          static_cast<int16_t>(kBatimentoX + kBatimentoPx),
                          static_cast<int16_t>(kBatimentoY + kBatimentoPx)};
    TEST_ASSERT_TRUE(painel.drawCount() > 0u);
    for (uint8_t i = 0; i < painel.drawCount(); ++i) {
        const Caixa a = caixaDoTexto(painel, i);
        const char* texto = painel.draw(i).text;
        TEST_ASSERT_TRUE_MESSAGE(a.x0 >= 0, texto);
        TEST_ASSERT_TRUE_MESSAGE(a.y0 >= 0, texto);
        TEST_ASSERT_TRUE_MESSAGE(a.x1 <= kPainelW, texto);
        TEST_ASSERT_TRUE_MESSAGE(a.y1 <= kPainelH, texto);
        TEST_ASSERT_FALSE_MESSAGE(intersecta(a, batimento), texto);
        for (uint8_t j = static_cast<uint8_t>(i + 1u); j < painel.drawCount(); ++j) {
            TEST_ASSERT_FALSE_MESSAGE(intersecta(a, caixaDoTexto(painel, j)), texto);
        }
    }
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}


static void test_DSP_02_NRM_01_tela_principal_mostra_os_dois_eixos_no_formato_do_manual(void) {
    Bancada bancada;
    const NormalInput entrada = enlaceSaudavel(455, -123);

    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);

    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X:+045,5"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("Y:-012,3"));
    // D3: os quatro limites, o enlace, o modo da saida - tudo na mesma tela, sem alternancia.
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X1:-- X2:--"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("Y1:-- Y2:--"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("ENLACE OK"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("SAIDA:MEDICAO"));
    // Nada conta antes do present(): um quadro apresentado por ciclo.
    TEST_ASSERT_EQUAL_UINT32(1u, bancada.painel.presentCount());
    TEST_ASSERT_EQUAL_UINT32(1u, bancada.painel.clearCount());
    TEST_ASSERT_TRUE(bancada.tela.lastStatus().ok());
    verificarQuadro(bancada.painel);
}

// EMENDA 2, aprovada em 2026-09-01: com o quadro chegando integro e o conteudo recusado, o
// numero medido vai para a tela MARCADO. Esconder um dado que existe nao protege ninguem;
// mostra-lo igual a uma leitura boa seria pior - e o numero plausivel sem aviso.
static void test_EMENDA2_falha_do_sensor_mostra_a_leitura_marcada(void) {
    Bancada bancada;
    NormalInput entrada = enlaceEmFalha(NormalLinkState::SensorFault);
    entrada.unqualified[kNormalAxisX] = Angle::fromDeciDegrees(495);
    entrada.unqualified[kNormalAxisY] = Angle::fromDeciDegrees(9);

    bancada.ciclo(entrada);

    TEST_ASSERT_TRUE_MESSAGE(bancada.painel.shows("+049,5"), "o numero medido tem de aparecer");
    TEST_ASSERT_TRUE_MESSAGE(bancada.painel.shows("+000,9"), "os dois eixos, nao so um");
    TEST_ASSERT_TRUE_MESSAGE(bancada.painel.shows("!"), "e tem de vir marcado");
    TEST_ASSERT_TRUE(bancada.painel.shows("FALHA DO SENSOR"));
    // A marca e SO de display: os quatro contatos continuam em alarme e a saida em falha.
    TEST_ASSERT_TRUE(bancada.painel.shows("AL"));
    verificarQuadro(bancada.painel);
}

static void test_EMENDA2_sem_quadro_nenhum_continua_no_traco(void) {
    // Enlace mudo: nao existe numero medido. A tela nao inventa nem conserva o ultimo valor.
    Bancada bancada;
    NormalInput entrada = enlaceEmFalha(NormalLinkState::CommFault);
    entrada.unqualified[kNormalAxisX] = Angle::invalid();
    entrada.unqualified[kNormalAxisY] = Angle::invalid();

    bancada.ciclo(entrada);

    TEST_ASSERT_TRUE_MESSAGE(bancada.painel.shows("---,-"), "sem quadro, o campo fica no traco");
    TEST_ASSERT_FALSE_MESSAGE(bancada.painel.shows("!"), "sem numero nao ha o que marcar");
    verificarQuadro(bancada.painel);
}

static void test_EMENDA2_leitura_boa_nunca_recebe_a_marca(void) {
    // A marca so existe onde falta credito. Numa leitura valida seria ruido e, pior, ensinaria
    // o operador a ignorar a marca.
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(495, 9);
    entrada.unqualified[kNormalAxisX] = Angle::fromDeciDegrees(495);
    entrada.unqualified[kNormalAxisY] = Angle::fromDeciDegrees(9);

    bancada.ciclo(entrada);

    TEST_ASSERT_TRUE(bancada.painel.shows("+049,5"));
    TEST_ASSERT_FALSE_MESSAGE(bancada.painel.shows("!"), "leitura com credito nao leva marca");
    verificarQuadro(bancada.painel);
}

static void test_DSP_01_leitura_invalida_mostra_o_traco_do_Angle_e_nunca_um_zero(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(0, 0);
    entrada.reading[kNormalAxisX] = Angle::invalid();
    entrada.reading[kNormalAxisY] = Angle::invalid();

    bancada.ciclo(entrada);

    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X:---,-"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("Y:---,-"));
    TEST_ASSERT_FALSE(bancada.painel.shows("000,0"));
    TEST_ASSERT_FALSE(bancada.painel.shows("+0"));
    verificarQuadro(bancada.painel);
}

static void test_DSP_02_o_sinal_e_a_casa_decimal_sao_do_Angle_e_nao_da_tela(void) {
    Bancada bancada;

    bancada.ciclo(enlaceSaudavel(0, -900));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X:+000,0"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("Y:-090,0"));

    bancada.ciclo(enlaceSaudavel(900, -1));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X:+090,0"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("Y:-000,1"));
    verificarQuadro(bancada.painel);
}

static void test_COM_01_falha_de_comunicacao_mostra_o_texto_da_errata_do_item_7(void) {
    Bancada bancada;

    bancada.ciclo(enlaceEmFalha(NormalLinkState::CommFault));

    TEST_ASSERT_TRUE(bancada.painel.showsExactly("FALHA DE COMUNICACAO"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("Verifique cabo RS485 e +5V"));
    // Decisao 12 item 12: em falha o campo de valor e substituido pelo traco.
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X:---,- Y:---,-"));
    // A mensagem SUBSTITUI a area de leitura: nao sobra "ENLACE OK" de tela saudavel.
    TEST_ASSERT_FALSE(bancada.painel.shows("ENLACE OK"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("SAIDA:FALHA"));
    verificarQuadro(bancada.painel);
}

static void test_A5_na_falha_de_enlace_os_quatro_limites_aparecem_em_alarme(void) {
    Bancada bancada;

    bancada.ciclo(enlaceEmFalha(NormalLinkState::CommFault));

    // Inclusive o canal que o cliente programou em Off: A5 desvia de L195 de proposito, e a
    // tela nao pode contar uma historia diferente da que o contato conta ao CLP.
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X1:AL X2:AL Y1:AL Y2:AL"));
}

static void test_A7_latch_de_enlace_mostra_a_segunda_linha_de_rearme(void) {
    Bancada bancada;
    NormalInput entrada = enlaceEmFalha(NormalLinkState::CommFault);
    entrada.linkLatched = true;

    bancada.ciclo(entrada);

    TEST_ASSERT_TRUE(bancada.painel.showsExactly("FALHA TRAVADA - REARMAR NO MENU"));
    // A primeira linha continua dizendo QUAL falha travou.
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("FALHA DE COMUNICACAO"));
    // E a dica de cabo sai: a acao que resolve passou a ser o rearme no menu.
    TEST_ASSERT_FALSE(bancada.painel.shows("Verifique cabo RS485"));
    verificarQuadro(bancada.painel);
}

// A7 (DECISIONS.md:277): o latch trava o estado de falha ate o rearme manual. Enlace de volta e
// latch ainda armado e combinacao LEGITIMA - e a que nasce depois do flapping parar. Com A5 os
// quatro reles continuam em alarme; uma tela dizendo "ENLACE OK" ao lado de quatro canais lendo
// "AL" deixaria o operador sem explicacao e sem caminho de acao.
static void test_A7_enlace_recuperado_com_latch_armado_continua_na_tela_de_rearme(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);
    entrada.linkLatched = true;
    for (uint8_t canal = 0; canal < kLimitChannelCount; ++canal) {
        entrada.limit[canal].state = RelayState::Signalled;
    }

    bancada.ciclo(entrada);

    TEST_ASSERT_TRUE(bancada.painel.showsExactly("FALHA TRAVADA - REARMAR NO MENU"));
    TEST_ASSERT_FALSE(bancada.painel.shows("ENLACE OK"));
    // Nao ha falha corrente para anunciar: a tela nao pode inventar "AGUARDANDO SENSOR".
    TEST_ASSERT_FALSE(bancada.painel.shows("AGUARDANDO SENSOR"));
    TEST_ASSERT_FALSE(bancada.painel.shows("FALHA DE COMUNICACAO"));
    // A leitura corrente continua valendo e continua na tela, com os contatos em alarme.
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X:+045,5 Y:-012,3"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X1:AL X2:AL Y1:AL Y2:AL"));
    verificarQuadro(bancada.painel);

    // Rearmado o latch, a tela principal volta inteira.
    entrada.linkLatched = false;
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("ENLACE OK"));
    TEST_ASSERT_FALSE(bancada.painel.shows("FALHA TRAVADA - REARMAR NO MENU"));
    verificarQuadro(bancada.painel);
}

static void test_A7_os_quatro_estados_de_falha_tem_a_propria_primeira_linha(void) {
    Bancada bancada;

    bancada.ciclo(enlaceEmFalha(NormalLinkState::Awaiting));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("AGUARDANDO SENSOR"));
    TEST_ASSERT_FALSE(bancada.painel.shows("FALHA DE COMUNICACAO"));
    verificarQuadro(bancada.painel);

    bancada.ciclo(enlaceEmFalha(NormalLinkState::SensorFault));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("FALHA DO SENSOR"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("Sensor de inclinacao em falha"));
    verificarQuadro(bancada.painel);

    bancada.ciclo(enlaceEmFalha(NormalLinkState::Unstable));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("MEDICAO INSTAVEL"));
    verificarQuadro(bancada.painel);
}

// Regra 3 do cabecalho de normal_screen.cpp, com o motivo CORRIGIDO em 2026-09-01. O motivo
// escrito antes era largura, e vinha de uma metrica de fake errada: medido contra o u8g2 real,
// "FALHA DO SENSOR" da 212 px em fonte grande e CABERIA nos 256 px do painel.
//
// O que realmente impede e a ALTURA. A tela de falha empilha cinco faixas - titulo, dica ou
// rearme, leitura, rodape de contatos e rodape de saida - e o rodape e ancorado no pe da tela
// porque contato de rele e informacao de seguranca que nao pode ser empurrada para fora. Cinco
// linhas pequenas somam 5*13 = 65 px de passo em 64 px de painel, ja no limite; trocar a
// primeira por uma de 28 px estoura em 16 px e apaga justamente o rodape.
static void test_A7_a_linha_de_falha_e_desenhada_em_fonte_pequena(void) {
    Bancada bancada;

    bancada.ciclo(enlaceEmFalha(NormalLinkState::SensorFault));

    const uint16_t alturaGrande = bancada.painel.lineHeightPx(TextFont::Large);
    const uint16_t alturaPequena = bancada.painel.lineHeightPx(TextFont::Small);
    // titulo grande mais as quatro faixas pequenas que a tela de falha ainda deve
    TEST_ASSERT_TRUE(alturaGrande + 4u * (alturaPequena + 1u) > bancada.painel.heightPx());
    TEST_ASSERT_EQUAL_UINT16(256u, bancada.painel.widthPx());
    bool achou = false;
    for (uint8_t i = 0; i < bancada.painel.drawCount(); ++i) {
        if (strcmp(bancada.painel.draw(i).text, "FALHA DO SENSOR") == 0) {
            achou = true;
            TEST_ASSERT_TRUE(bancada.painel.draw(i).font == TextFont::Small);
        }
    }
    TEST_ASSERT_TRUE(achou);
}

static void test_D3_NRM_02_down_percorre_as_telas_de_detalhe_e_volta(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);
    entrada.limit[0].state = RelayState::Signalled;
    entrada.limit[0].value = Angle::fromDeciDegrees(250);

    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::Main);

    tocar(bancada, Key::Down);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailX);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("EIXO X"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("+045,5"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X1:AL +025,0"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("SAIDA X:MEDICAO"));
    TEST_ASSERT_FALSE(bancada.painel.shows("EIXO Y"));
    verificarQuadro(bancada.painel);

    tocar(bancada, Key::Down);
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailY);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("EIXO Y"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("-012,3"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("Y1:-- +050,2"));
    verificarQuadro(bancada.painel);

    tocar(bancada, Key::Down);
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::Main);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X:+045,5"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("Y:-012,3"));
}

static void test_NRM_04_toque_simples_em_cima_nao_faz_nada_no_modo_normal(void) {
    Bancada bancada;
    const NormalInput entrada = enlaceSaudavel(455, -123);

    bancada.ciclo(entrada);
    const uint8_t desenhosAntes = bancada.painel.drawCount();

    tocar(bancada, Key::Up);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);

    // E continua sem funcao depois que a janela do duplo toque fecha: um toque solto nunca
    // amadurece em PSET.
    bancada.clock.advanceMs(2000u);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);

    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::Main);
    TEST_ASSERT_EQUAL_UINT8(desenhosAntes, bancada.painel.drawCount());
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X:+045,5"));
}

static void test_MAN_5_6_L152_duplo_toque_em_cima_chega_como_pset_e_nao_como_dois_toques(void) {
    Bancada bancada;
    const NormalInput entrada = enlaceSaudavel(455, -123);

    tocar(bancada, Key::Up);
    bancada.clock.advanceMs(120u);
    tocar(bancada, Key::Up);
    // A janela do terceiro toque tem de fechar antes de o gesto ser entregue (decisao 1 item 7).
    bancada.clock.advanceMs(400u);

    uint8_t pedidosDePreset = 0;
    uint8_t pedidosDeLogin = 0;
    for (uint8_t ciclo = 0; ciclo < 4u; ++ciclo) {
        const NormalRequest pedido = bancada.ciclo(entrada);
        if (pedido == NormalRequest::Preset) {
            ++pedidosDePreset;
        }
        if (pedido == NormalRequest::OpenLogin) {
            ++pedidosDeLogin;
        }
    }

    // UM pedido de PSET, e nao dois toques soltos: os dois ShortTap que compoem o gesto foram
    // ignorados por NRM-04 e nao viraram nem tela nem pedido.
    TEST_ASSERT_EQUAL_UINT8(1u, pedidosDePreset);
    TEST_ASSERT_EQUAL_UINT8(0u, pedidosDeLogin);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::Main);
}

static void test_NRM_03_menu_mantida_3_s_pede_a_tela_de_login(void) {
    Bancada bancada;
    const NormalInput entrada = enlaceSaudavel(455, -123);

    bancada.keypad.press(Key::Menu);
    bancada.clock.advanceMs(2999u);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);

    bancada.clock.advanceMs(1u);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::OpenLogin);

    // A soltura da mesma prensagem nao pode virar toque curto na tela seguinte.
    bancada.clock.advanceMs(50u);
    bancada.keypad.release(Key::Menu);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::Main);
}

// T25 de docs/ihm-estados.md ("B7 --> C1 : MENU mantida 3 s") somada a A7: com o latch armado a
// UNICA rota ate o menu de rearme e este hold, feito de dentro da tela de falha. Uma tela que
// devolvesse None enquanto o enlace estivesse ruim trancaria o operador para fora do rearme.
static void test_T25_A7_menu_3_s_na_tela_de_falha_abre_o_login_do_rearme(void) {
    Bancada bancada;
    NormalInput entrada = enlaceEmFalha(NormalLinkState::CommFault);
    entrada.linkLatched = true;

    bancada.keypad.press(Key::Menu);
    bancada.clock.advanceMs(2999u);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);

    bancada.clock.advanceMs(1u);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::OpenLogin);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("FALHA TRAVADA - REARMAR NO MENU"));
}

// Linha B7 da tabela 3.2, coluna "alterna a tela, se houver": a selecao continua andando com a
// falha no ar, e reaparece intacta quando o enlace volta.
static void test_B7_baixo_alterna_a_selecao_com_a_falha_no_ar(void) {
    Bancada bancada;
    const NormalInput falha = enlaceEmFalha(NormalLinkState::CommFault);
    const NormalInput saudavel = enlaceSaudavel(455, -123);

    tocar(bancada, Key::Down);
    bancada.ciclo(falha);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailX);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("FALHA DE COMUNICACAO"));
    TEST_ASSERT_FALSE(bancada.painel.shows("EIXO X"));

    tocar(bancada, Key::Down);
    bancada.ciclo(falha);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailY);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("FALHA DE COMUNICACAO"));

    tocar(bancada, Key::Down);
    bancada.ciclo(falha);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::Main);

    tocar(bancada, Key::Down);
    bancada.ciclo(saudavel);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("EIXO X"));
}

// Decisao 12 item 8, aprovada: BAIXO mantida 3000 ms pede o autoteste do display, sem senha. O
// hold de CIMA continua sem funcao (invariante 6 de docs/ihm-estados.md) e o de MENU continua
// sendo o login - a checagem de QUAL tecla e o que separa "reexecutar um padrao de tela" de
// "abrir a tela que da acesso aos setpoints de rele".
static void test_D12_item8_hold_de_baixo_pede_o_autoteste_e_o_de_cima_e_ignorado(void) {
    Bancada bancada;
    const NormalInput entrada = enlaceSaudavel(455, -123);

    bancada.keypad.press(Key::Down);
    bancada.clock.advanceMs(2999u);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);
    bancada.clock.advanceMs(1u);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::SelfTest);
    // O gesto de autoteste nao mexe na selecao de tela.
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::Main);
    bancada.clock.advanceMs(50u);
    bancada.keypad.release(Key::Down);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::Main);

    // CIMA mantida nao abre nada: gesto sem funcao declarada e ignorado, nunca reinterpretado.
    bancada.keypad.press(Key::Up);
    bancada.clock.advanceMs(3000u);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);
    bancada.clock.advanceMs(3000u);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);
    bancada.keypad.release(Key::Up);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);
}

// O login continua sendo o hold de MENU mesmo com outra tecla ja prensada: e o caso do operador
// que apoia a mao no painel. O que decide e a tecla do gesto, nao "houve um hold".
static void test_NRM_03_menu_3_s_com_outra_tecla_prensada_ainda_abre_o_login(void) {
    Bancada bancada;
    const NormalInput entrada = enlaceSaudavel(455, -123);

    bancada.keypad.press(Key::Up);
    bancada.clock.advanceMs(10u);
    bancada.keypad.press(Key::Menu);
    bancada.clock.advanceMs(3000u);

    // CIMA prensada ha mais tempo dispara o proprio hold, que e ignorado; MENU dispara o login.
    uint8_t pedidosDeLogin = 0;
    for (uint8_t ciclo = 0; ciclo < 3u; ++ciclo) {
        if (bancada.ciclo(entrada) == NormalRequest::OpenLogin) {
            ++pedidosDeLogin;
        }
    }
    TEST_ASSERT_EQUAL_UINT8(1u, pedidosDeLogin);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::Main);
}

static void test_COM_01_a_falha_rouba_a_tela_e_a_selecao_volta_intacta(void) {
    Bancada bancada;
    const NormalInput saudavel = enlaceSaudavel(455, -123);

    tocar(bancada, Key::Down);
    bancada.ciclo(saudavel);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailX);

    bancada.ciclo(enlaceEmFalha(NormalLinkState::CommFault));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("FALHA DE COMUNICACAO"));
    TEST_ASSERT_FALSE(bancada.painel.shows("EIXO X"));
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailX);

    bancada.ciclo(saudavel);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("EIXO X"));
    TEST_ASSERT_FALSE(bancada.painel.shows("FALHA DE COMUNICACAO"));
}

static void test_D1_item17_preset_ativo_aparece_na_principal_e_com_valor_no_detalhe(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);
    entrada.presetActive[kNormalAxisX] = true;
    entrada.presetOffsetDeci[kNormalAxisX] = -120;

    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("PSET:X"));
    verificarQuadro(bancada.painel);

    tocar(bancada, Key::Down);
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("PSET X:-012,0"));
    verificarQuadro(bancada.painel);

    // Sem offset nao ha indicacao nenhuma: leitura absoluta nao se anuncia.
    entrada.presetActive[kNormalAxisX] = false;
    bancada.ciclo(entrada);
    TEST_ASSERT_FALSE(bancada.painel.shows("PSET"));
}

// A9 permite offset de ate +/-1800 decimos, o DOBRO da faixa de medicao. O texto e o mesmo que
// PresetWizard::formatIndicator() imprime, e nao a faixa de Angle: montar este campo com
// Angle::fromDeciDegrees() apagava com "---,-" justamente os offsets grandes.
static void test_D1_item17_A9_offset_de_120_graus_aparece_e_nao_vira_traco(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);
    entrada.presetActive[kNormalAxisY] = true;
    entrada.presetOffsetDeci[kNormalAxisY] = 1200;

    tocar(bancada, Key::Down);
    bancada.ciclo(entrada);
    tocar(bancada, Key::Down);
    bancada.ciclo(entrada);

    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailY);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("PSET Y:+120,0"));
    TEST_ASSERT_FALSE(bancada.painel.shows("---,-"));
    verificarQuadro(bancada.painel);

    // O extremo negativo de A9 tambem e um valor legitimo.
    entrada.presetOffsetDeci[kNormalAxisY] = -1800;
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("PSET Y:-180,0"));

    // Fora de +/-1800 nao existe offset legitimo: o campo cai no traco em vez de imprimir um
    // numero que nenhuma decisao autoriza.
    entrada.presetOffsetDeci[kNormalAxisY] = 1900;
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("PSET Y:---,-"));
    verificarQuadro(bancada.painel);
}

// Decisao 1 item 17 com DECISIONS.md:2529: a permanencia do indicador e o mecanismo de
// seguranca. MEDICAO INSTAVEL nao poe rele nem saida em falha (DECISIONS.md:1738 e :1740): a
// leitura continua valendo, continua relativa ao Preset, e o operador precisa continuar vendo
// isso.
static void test_D1_item17_indicador_de_preset_permanece_na_tela_de_falha(void) {
    Bancada bancada;
    NormalInput entrada = enlaceEmFalha(NormalLinkState::Unstable);
    entrada.reading[kNormalAxisX] = Angle::fromDeciDegrees(455);
    entrada.reading[kNormalAxisY] = Angle::fromDeciDegrees(-123);
    entrada.analog[kNormalAxisX] = NormalAnalogMode::Tracking;
    entrada.analog[kNormalAxisY] = NormalAnalogMode::Tracking;
    entrada.presetActive[kNormalAxisX] = true;
    entrada.presetOffsetDeci[kNormalAxisX] = -120;
    // DECISIONS.md:1738 e :1740 aplicam a falha so em AGUARDANDO, COMUNICACAO e SENSOR: em
    // MEDICAO INSTAVEL os contatos e a saida analogica continuam como o avaliador os deixou.
    for (uint8_t canal = 0; canal < kLimitChannelCount; ++canal) {
        entrada.limit[canal].state = RelayState::Clear;
    }

    bancada.ciclo(entrada);

    TEST_ASSERT_TRUE(bancada.painel.showsExactly("MEDICAO INSTAVEL"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("PSET:X"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X1:-- X2:-- Y1:-- Y2:--"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("SAIDA:MEDICAO"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("X:+045,5 Y:-012,3"));
    verificarQuadro(bancada.painel);

    // E no estado mais grave tambem: o marcador nao depende de qual falha esta no ar.
    NormalInput comFalha = enlaceEmFalha(NormalLinkState::CommFault);
    comFalha.presetActive[kNormalAxisY] = true;
    comFalha.presetOffsetDeci[kNormalAxisY] = 300;
    bancada.ciclo(comFalha);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("PSET:Y"));
    verificarQuadro(bancada.painel);
}

// Invariante 2 de docs/ihm-estados.md: o wizard de Auto Calibracao escreve codigo de DAC direto
// e SO no eixo em calibracao; o outro eixo continua rastreando o angulo real. O rotulo por eixo
// e o que o tecnico de bancada le com o voltimetro na mao.
static void test_invariante2_modo_da_saida_analogica_e_por_eixo(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);
    entrada.analog[kNormalAxisX] = NormalAnalogMode::Calibrating;
    entrada.analog[kNormalAxisY] = NormalAnalogMode::Tracking;

    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("SAIDA X:CALIB"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("SAIDA Y:MEDICAO"));
    TEST_ASSERT_FALSE(bancada.painel.showsExactly("SAIDA:MEDICAO"));
    TEST_ASSERT_FALSE(bancada.painel.showsExactly("SAIDA:CALIB"));
    verificarQuadro(bancada.painel);

    tocar(bancada, Key::Down);
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailX);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("SAIDA X:CALIB"));
    TEST_ASSERT_FALSE(bancada.painel.shows("MEDICAO"));
    verificarQuadro(bancada.painel);

    tocar(bancada, Key::Down);
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailY);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("SAIDA Y:MEDICAO"));
    TEST_ASSERT_FALSE(bancada.painel.shows("CALIB"));
    verificarQuadro(bancada.painel);

    // Na tela de falha o rodape tem a largura inteira: os dois eixos cabem na mesma linha.
    NormalInput falha = enlaceEmFalha(NormalLinkState::SensorFault);
    falha.analog[kNormalAxisX] = NormalAnalogMode::Calibrating;
    bancada.ciclo(falha);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("SAIDA X:CALIB SAIDA Y:FALHA"));
    verificarQuadro(bancada.painel);

    // Os tres valores do enum tem rotulo proprio: nenhum cai no "FALHA" de fim de funcao.
    Bancada outra;
    NormalInput calibrando = enlaceSaudavel(0, 0);
    calibrando.analog[kNormalAxisX] = NormalAnalogMode::Calibrating;
    calibrando.analog[kNormalAxisY] = NormalAnalogMode::Calibrating;
    outra.ciclo(calibrando);
    TEST_ASSERT_TRUE(outra.painel.showsExactly("SAIDA:CALIB"));
    outra.ciclo(enlaceEmFalha(NormalLinkState::CommFault));
    TEST_ASSERT_TRUE(outra.painel.showsExactly("SAIDA:FALHA"));
    outra.ciclo(enlaceSaudavel(0, 0));
    TEST_ASSERT_TRUE(outra.painel.showsExactly("SAIDA:MEDICAO"));
}

// Regra 4 do cabecalho de normal_screen.cpp: quando a coluna de status enche, quem cede lugar e
// "ENLACE OK", que e acrescimo desta tela, e nunca o indicador de Preset, que e decisao
// aprovada. A ausencia de "ENLACE OK" nunca e a unica prova de enlace: falha ROUBA a tela.
static void test_D1_item17_indicador_de_preset_tem_prioridade_sobre_enlace_ok(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);
    entrada.analog[kNormalAxisX] = NormalAnalogMode::Calibrating;
    entrada.analog[kNormalAxisY] = NormalAnalogMode::Tracking;
    entrada.presetActive[kNormalAxisX] = true;
    entrada.presetActive[kNormalAxisY] = true;
    entrada.presetOffsetDeci[kNormalAxisX] = -120;
    entrada.presetOffsetDeci[kNormalAxisY] = 250;

    bancada.ciclo(entrada);

    TEST_ASSERT_TRUE(bancada.painel.showsExactly("PSET:XY"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("SAIDA X:CALIB"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("SAIDA Y:MEDICAO"));
    TEST_ASSERT_FALSE(bancada.painel.shows("ENLACE OK"));
    verificarQuadro(bancada.painel);

    // Com um modo so, as cinco linhas comportam tudo.
    entrada.analog[kNormalAxisX] = NormalAnalogMode::Tracking;
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("ENLACE OK"));
    TEST_ASSERT_TRUE(bancada.painel.showsExactly("PSET:XY"));
    verificarQuadro(bancada.painel);
}

// D12 item 11, aprovada: marca de 4x4 px girando por quatro posicoes na caixa de 8x8 px de
// (247,55)-(254,62), comandada por TRANSACAO VALIDA (DECISIONS.md:2362). Marcador parado e o
// unico sinal de painel congelado exibindo dado plausivel, que e o modo de falha mais perigoso
// de uma IHM de seguranca.
static void test_D12_item11_batimento_gira_por_transacao_valida(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);

    const int16_t xEsperado[4] = {247, 251, 251, 247};
    const int16_t yEsperado[4] = {55, 55, 59, 59};

    for (uint8_t fase = 0; fase < 8u; ++fase) {
        entrada.heartbeatPhase = fase;
        bancada.ciclo(entrada);
        TEST_ASSERT_EQUAL_UINT32(fase + 1u, bancada.painel.rectCount());
        TEST_ASSERT_EQUAL_INT16(xEsperado[fase % 4u], bancada.painel.rectX());
        TEST_ASSERT_EQUAL_INT16(yEsperado[fase % 4u], bancada.painel.rectY());
        TEST_ASSERT_EQUAL_UINT16(4u, bancada.painel.rectW());
        TEST_ASSERT_EQUAL_UINT16(4u, bancada.painel.rectH());
        TEST_ASSERT_TRUE(bancada.painel.rectOn());
        verificarQuadro(bancada.painel);
    }

    // Duas fases consecutivas nunca desenham no mesmo lugar: um contador preso salta aos olhos.
    for (uint8_t fase = 0; fase < 4u; ++fase) {
        const uint8_t proxima = static_cast<uint8_t>((fase + 1u) % 4u);
        TEST_ASSERT_FALSE(xEsperado[fase] == xEsperado[proxima] &&
                          yEsperado[fase] == yEsperado[proxima]);
    }

    // E o batimento esta nas TRES telas, inclusive na de falha - painel congelado durante a
    // falha e exatamente o caso em que o operador precisa saber que o firmware parou.
    const uint32_t antesDoDetalhe = bancada.painel.rectCount();
    tocar(bancada, Key::Down);
    bancada.ciclo(entrada);
    TEST_ASSERT_EQUAL_UINT32(antesDoDetalhe + 1u, bancada.painel.rectCount());
    bancada.ciclo(enlaceEmFalha(NormalLinkState::CommFault));
    TEST_ASSERT_EQUAL_UINT32(antesDoDetalhe + 2u, bancada.painel.rectCount());
}

// O painel nao e canal de seguranca (invariante 5): a falha de desenho e RETIDA em lastStatus()
// e nao muda nada do resto. A primeira falha e a que fica - a segunda nao apaga a origem.
static void test_IDISPLAY_lastStatus_retem_a_primeira_falha_de_desenho(void) {
    Bancada bancada;
    const NormalInput entrada = enlaceSaudavel(455, -123);

    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.tela.lastStatus().ok());

    bancada.painel.falharNoDesenho(2u);
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.tela.lastStatus().failed());
    TEST_ASSERT_TRUE(bancada.tela.lastStatus().err == Err::Io);

    // O quadro seguinte comeca limpo: lastStatus() e do ULTIMO quadro, nao da vida inteira.
    bancada.painel.falharNoDesenho(0u);
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.tela.lastStatus().ok());
}

static void test_IDISPLAY_falha_de_desenho_nao_engole_o_pedido_da_tecla(void) {
    Bancada bancada;
    const NormalInput entrada = enlaceSaudavel(455, -123);

    bancada.painel.falharNoDesenho(1u);
    bancada.keypad.press(Key::Menu);
    bancada.clock.advanceMs(3000u);

    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::OpenLogin);
    TEST_ASSERT_TRUE(bancada.tela.lastStatus().failed());
}

// O cabecalho de normal_screen.h promete: todos os gestos que nao geram pedido sao consumidos
// no MESMO ciclo. Com um gesto por ciclo, um toque enfileirado atras de outro so agiria ciclos
// depois e a fila de 8 do KeyGesture poderia encher.
static void test_update_drena_todos_os_gestos_sem_pedido_no_mesmo_ciclo(void) {
    Bancada bancada;
    const NormalInput entrada = enlaceSaudavel(455, -123);

    tocar(bancada, Key::Down);
    bancada.clock.advanceMs(200u);
    tocar(bancada, Key::Down);
    bancada.clock.advanceMs(500u);

    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailY);
    TEST_ASSERT_EQUAL_UINT32(0u, bancada.gesto.droppedGestures());
}

// A outra metade da mesma promessa: o gesto POSTERIOR ao que gerou pedido nao e consumido junto,
// fica na fila para o proximo ciclo.
static void test_update_deixa_na_fila_o_gesto_posterior_ao_pedido(void) {
    Bancada bancada;
    const NormalInput entrada = enlaceSaudavel(455, -123);

    bancada.keypad.press(Key::Menu);
    bancada.clock.advanceMs(3000u);
    tocar(bancada, Key::Down);

    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::OpenLogin);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::Main);

    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailX);
}

static void test_IKEYPAD_reset_volta_a_principal_e_nao_deixa_gesto_atravessar(void) {
    Bancada bancada;
    const NormalInput entrada = enlaceSaudavel(455, -123);

    tocar(bancada, Key::Down);
    bancada.ciclo(entrada);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::DetailX);

    // Volta de outro modo com uma tecla ainda prensada: a soltura nao pode virar toque curto.
    bancada.keypad.press(Key::Down);
    bancada.tela.reset();
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::Main);

    bancada.clock.advanceMs(60u);
    bancada.keypad.release(Key::Down);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);
    TEST_ASSERT_TRUE(bancada.tela.view() == NormalView::Main);
}

// --- LAYOUT (2026-09-01): a coluna de estado sobe de fonte quando cabe ----------------------
//
// Escolha do bigboss: a area de medicao (X:/Y:) NAO muda - duas linhas de 28 px ja ocupam 59
// dos 64 px do painel e nao ha para onde crescer. Quem cresce e a coluna da direita, que e o
// que o operador le de longe para saber o estado dos quatro contatos.
//
// A coluna so sobe quando TUDO cabe: se qualquer linha estourar a largura disponivel ou as
// linhas necessarias nao couberem na altura, a coluna inteira volta para a fonte pequena. Nunca
// se esconde uma linha de estado de rele para caber uma fonte maior - o inverso do que o
// operador precisa num supervisor de seguranca.

static void test_layout_coluna_de_estado_sobe_de_fonte_com_os_dois_eixos_no_mesmo_modo(void) {
    Bancada bancada;

    TEST_ASSERT_TRUE(bancada.ciclo(enlaceSaudavel(455, -123)) == NormalRequest::None);

    // a medicao continua na fonte grande
    TEST_ASSERT_TRUE(bancada.painel.fontOf("X:+045,5") == TextFont::Large);
    TEST_ASSERT_TRUE(bancada.painel.fontOf("Y:-012,3") == TextFont::Large);
    // e a coluna da direita subiu
    TEST_ASSERT_TRUE(bancada.painel.fontOf("X1:-- X2:--") == TextFont::Medium);
    TEST_ASSERT_TRUE(bancada.painel.fontOf("Y1:-- Y2:--") == TextFont::Medium);
    verificarQuadro(bancada.painel);
}

static void test_layout_coluna_de_estado_volta_para_a_fonte_pequena_quando_nao_cabe(void) {
    // Durante a Auto Calibracao um eixo fica em CALIB e o outro segue em MEDICAO: a coluna
    // passa a precisar de "SAIDA X:..." e "SAIDA Y:...", que nao cabem na largura em Medium.
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);
    entrada.analog[kNormalAxisX] = NormalAnalogMode::Calibrating;
    entrada.analog[kNormalAxisY] = NormalAnalogMode::Tracking;

    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);

    TEST_ASSERT_TRUE(bancada.painel.shows("SAIDA X:"));
    TEST_ASSERT_TRUE(bancada.painel.shows("SAIDA Y:"));
    TEST_ASSERT_TRUE(bancada.painel.fontOf("X1:-- X2:--") == TextFont::Small);
    TEST_ASSERT_TRUE(bancada.painel.fontOf("SAIDA X:") == TextFont::Small);
    verificarQuadro(bancada.painel);
}

static void test_layout_coluna_de_estado_nao_invade_a_area_de_medicao(void) {
    // O X da coluna sai da largura REAL da fonte grande, e nao de um numero escrito a mao: se
    // a fonte grande mudar, a coluna anda junto em vez de montar em cima do angulo.
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(-1800, 1800);
    entrada.presetActive[kNormalAxisX] = true;

    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);

    const int16_t fimDoAngulo = static_cast<int16_t>(
        bancada.painel.xOf("X:-180,0") +
        bancada.painel.textWidthPx(TextFont::Large, "X:-180,0"));
    TEST_ASSERT_TRUE(bancada.painel.xOf("X1:-- X2:--") > fimDoAngulo);
    verificarQuadro(bancada.painel);
}

// --- TELAS DEDICADAS AO EIXO (detalhe X / detalhe Y) -----------------------------------------
//
// Pedido do bigboss em 2026-09-01: crescer tambem as telas de eixo. Vale aqui a mesma regra da
// coluna de estado - a fonte maior NUNCA custa informacao. O orcamento e apertado: cabecalho
// pequeno de 12 px mais N linhas de conteudo de 16 px em 64 px de painel deixa N = 3. Sao
// exatamente as tres que a tela sempre tem (dois limites e o modo da saida); a quarta, o
// indicador de PSET, so aparece com offset ligado - e ai a tela inteira volta para a fonte
// pequena em vez de esconder o indicador, que e a prova visivel de que a leitura e relativa.

static void test_layout_tela_de_eixo_cresce_quando_nao_ha_preset(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);

    bancada.ciclo(entrada);
    tocar(bancada, Key::Down);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);

    TEST_ASSERT_TRUE(bancada.painel.shows("EIXO X"));
    TEST_ASSERT_TRUE(bancada.painel.fontOf("EIXO X") == TextFont::Small);
    TEST_ASSERT_TRUE(bancada.painel.fontOf("X1:") == TextFont::Medium);
    TEST_ASSERT_TRUE(bancada.painel.fontOf("SAIDA X:") == TextFont::Medium);
    verificarQuadro(bancada.painel);
}

static void test_layout_tela_de_eixo_encolhe_para_nao_esconder_o_preset(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);
    entrada.presetActive[kNormalAxisX] = true;
    entrada.presetOffsetDeci[kNormalAxisX] = 120;

    bancada.ciclo(entrada);
    tocar(bancada, Key::Down);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);

    // a quarta linha continua no ar - e ela que diz que a leitura e relativa
    TEST_ASSERT_TRUE(bancada.painel.shows("PSET X:"));
    TEST_ASSERT_TRUE(bancada.painel.fontOf("X1:") == TextFont::Small);
    verificarQuadro(bancada.painel);
}

static void test_layout_valor_grande_do_eixo_nao_monta_sobre_as_linhas(void) {
    // O X do numero grande sai da largura MEDIDA, e nao de uma constante: com a fonte das
    // linhas mudando, um X fixo deixaria as duas se encontrarem no meio da tela.
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(-900, 900);

    bancada.ciclo(entrada);
    tocar(bancada, Key::Down);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);

    TEST_ASSERT_TRUE(bancada.painel.shows("-090,0"));
    TEST_ASSERT_TRUE(bancada.painel.fontOf("-090,0") == TextFont::Large);
    verificarQuadro(bancada.painel);
}

static void test_layout_tela_do_eixo_y_segue_a_mesma_regra(void) {
    Bancada bancada;
    NormalInput entrada = enlaceSaudavel(455, -123);

    bancada.ciclo(entrada);
    tocar(bancada, Key::Down);
    bancada.ciclo(entrada);
    tocar(bancada, Key::Down);
    TEST_ASSERT_TRUE(bancada.ciclo(entrada) == NormalRequest::None);

    TEST_ASSERT_TRUE(bancada.painel.shows("EIXO Y"));
    TEST_ASSERT_TRUE(bancada.painel.fontOf("Y1:") == TextFont::Medium);
    TEST_ASSERT_TRUE(bancada.painel.fontOf("SAIDA Y:") == TextFont::Medium);
    verificarQuadro(bancada.painel);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_EMENDA2_falha_do_sensor_mostra_a_leitura_marcada);
    RUN_TEST(test_EMENDA2_sem_quadro_nenhum_continua_no_traco);
    RUN_TEST(test_EMENDA2_leitura_boa_nunca_recebe_a_marca);
    RUN_TEST(test_DSP_02_NRM_01_tela_principal_mostra_os_dois_eixos_no_formato_do_manual);
    RUN_TEST(test_EMENDA2_falha_do_sensor_mostra_a_leitura_marcada);
    RUN_TEST(test_EMENDA2_sem_quadro_nenhum_continua_no_traco);
    RUN_TEST(test_EMENDA2_leitura_boa_nunca_recebe_a_marca);
    RUN_TEST(test_DSP_01_leitura_invalida_mostra_o_traco_do_Angle_e_nunca_um_zero);
    RUN_TEST(test_DSP_02_o_sinal_e_a_casa_decimal_sao_do_Angle_e_nao_da_tela);
    RUN_TEST(test_COM_01_falha_de_comunicacao_mostra_o_texto_da_errata_do_item_7);
    RUN_TEST(test_A5_na_falha_de_enlace_os_quatro_limites_aparecem_em_alarme);
    RUN_TEST(test_A7_latch_de_enlace_mostra_a_segunda_linha_de_rearme);
    RUN_TEST(test_A7_enlace_recuperado_com_latch_armado_continua_na_tela_de_rearme);
    RUN_TEST(test_A7_os_quatro_estados_de_falha_tem_a_propria_primeira_linha);
    RUN_TEST(test_A7_a_linha_de_falha_e_desenhada_em_fonte_pequena);
    RUN_TEST(test_layout_coluna_de_estado_sobe_de_fonte_com_os_dois_eixos_no_mesmo_modo);
    RUN_TEST(test_layout_coluna_de_estado_volta_para_a_fonte_pequena_quando_nao_cabe);
    RUN_TEST(test_layout_coluna_de_estado_nao_invade_a_area_de_medicao);
    RUN_TEST(test_layout_tela_de_eixo_cresce_quando_nao_ha_preset);
    RUN_TEST(test_layout_tela_de_eixo_encolhe_para_nao_esconder_o_preset);
    RUN_TEST(test_layout_valor_grande_do_eixo_nao_monta_sobre_as_linhas);
    RUN_TEST(test_layout_tela_do_eixo_y_segue_a_mesma_regra);
    RUN_TEST(test_D3_NRM_02_down_percorre_as_telas_de_detalhe_e_volta);
    RUN_TEST(test_NRM_04_toque_simples_em_cima_nao_faz_nada_no_modo_normal);
    RUN_TEST(test_MAN_5_6_L152_duplo_toque_em_cima_chega_como_pset_e_nao_como_dois_toques);
    RUN_TEST(test_NRM_03_menu_mantida_3_s_pede_a_tela_de_login);
    RUN_TEST(test_T25_A7_menu_3_s_na_tela_de_falha_abre_o_login_do_rearme);
    RUN_TEST(test_B7_baixo_alterna_a_selecao_com_a_falha_no_ar);
    RUN_TEST(test_D12_item8_hold_de_baixo_pede_o_autoteste_e_o_de_cima_e_ignorado);
    RUN_TEST(test_NRM_03_menu_3_s_com_outra_tecla_prensada_ainda_abre_o_login);
    RUN_TEST(test_COM_01_a_falha_rouba_a_tela_e_a_selecao_volta_intacta);
    RUN_TEST(test_D1_item17_preset_ativo_aparece_na_principal_e_com_valor_no_detalhe);
    RUN_TEST(test_D1_item17_A9_offset_de_120_graus_aparece_e_nao_vira_traco);
    RUN_TEST(test_D1_item17_indicador_de_preset_permanece_na_tela_de_falha);
    RUN_TEST(test_invariante2_modo_da_saida_analogica_e_por_eixo);
    RUN_TEST(test_D1_item17_indicador_de_preset_tem_prioridade_sobre_enlace_ok);
    RUN_TEST(test_D12_item11_batimento_gira_por_transacao_valida);
    RUN_TEST(test_IDISPLAY_lastStatus_retem_a_primeira_falha_de_desenho);
    RUN_TEST(test_IDISPLAY_falha_de_desenho_nao_engole_o_pedido_da_tecla);
    RUN_TEST(test_update_drena_todos_os_gestos_sem_pedido_no_mesmo_ciclo);
    RUN_TEST(test_update_deixa_na_fila_o_gesto_posterior_ao_pedido);
    RUN_TEST(test_IKEYPAD_reset_volta_a_principal_e_nao_deixa_gesto_atravessar);
    return UNITY_END();
}
