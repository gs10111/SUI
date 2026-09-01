// DE-PURI-DI261924 REV A, folha 1/2 (CN4). Implementacao do adaptador declarado em
// src/adapters/ssd1322_display.h: Decisao 12 e passos 8, 9 e 10 da ORDEM DE BOOT (DECISIONS.md
// Parte 2). O codigo que resolve o hardware - construtor U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI com
// CS/DC/RESET de board_pins, pulso de reset de 10 ms + 120 ms de acomodacao, clearBuffer +
// sendBuffer no fim do init - vem de src/drivers/display_u8g2.cpp do firmware de teste de
// fabrica, ja validado em bancada no env esp32dev-ihm. O que mudou em relacao a ele:
// (a) a pre-reserva do VSPI e o setBusClock(4 MHz) passaram a ser feitos AQUI, antes do
//     u8g2_.begin(), porque o driver de fabrica dependia do main.cpp para isso - e a pre-reserva
//     e feita com board::kDispMiso (IO39, input-only e NC), como o SpiBus do firmware de fabrica
//     ja faz (src/drivers/spi_bus.cpp), NUNCA com MISO = -1, que e o gesto que entrega o IO19
//     (WDI) a matriz do VSPI (ver ORDEM DE BOOT no cabecalho);
// (b) o desenho deixou de ser "escreve e envia" e passou a ser o protocolo clear/draw/present
//     exigido pela porta, sem nada visivel antes do present();
// (c) o autoteste virou os QUATRO padroes que o contrato da porta e o FakeDisplay definem, com
//     Err::Range fora de faixa (o driver de fabrica tinha seis padroes e devolvia Err::Param);
// (d) as metricas de fonte passaram a sair do U8g2 em vez de nao existirem.
#include "adapters/ssd1322_display.h"

#include <Arduino.h>
#include <SPI.h>

#include <driver/gpio.h>

namespace adapters {

namespace {

constexpr const char* kPatternText[Ssd1322Display::kPatternCount] = {
    "todos os pixels acesos",
    "moldura fechada",
    "regua de colunas",
    "marcas em 0, 64, 128, 192, 255",
};

constexpr uint16_t kColumnMarkX[Ssd1322Display::kColumnMarkCount] = {0, 64, 128, 192, 255};

constexpr board::Pin kBusPins[] = {board::kDispSclk, board::kDispMosi, board::kDispDc,
                                   board::kDispCs};

// Larguras MEDIDAS com u8g2_GetStrWidth sobre as strings desta IHM (nao estimadas):
//   6x12_tr    5,875 px/glifo  -> "Valor Limite X1(graus):+000,0" = 173 px
//   9x15B_tr   8,875 px/glifo  -> "SAIDA:MEDICAO" = 116 px, "Operacao Limite Y2" = 161 px
//   t0_30b_tr 14,125 px/glifo  -> "X:-180,0" = 113 px, duas linhas empilhadas = 59 de 64 px
// 9x15B foi o maior degrau que ainda deixa a coluna de estado caber ao lado da area de medicao
// e o item de menu mais largo caber nos 256 px. O negrito e proposital: painel monocromatico
// lido de longe num cais.
const uint8_t* fontFor(TextFont font) {
    switch (font) {
        case TextFont::Large: return u8g2_font_t0_30b_tr;
        case TextFont::Medium: return u8g2_font_9x15B_tr;
        case TextFont::Small: break;
    }
    return u8g2_font_6x12_tr;
}

}  // namespace

Ssd1322Display::Ssd1322Display(RearmHook rearmWatchdogPin)
    // U8G2_R2 = 180 graus. O painel do CN4 esta montado invertido na caixa: com R0 a imagem
    // sai de cabeca para baixo para quem le o equipamento de frente. A rotacao e do ADAPTADOR
    // de proposito - o dominio desenha sempre em (0,0) = canto superior esquerdo LOGICO, e nao
    // pode saber como o vidro foi parafusado. Trocar isto inverte a tela inteira, inclusive o
    // marcador de batimento em (247,55) e o deslocamento anti-burn-in.
    : u8g2_(U8G2_R2, static_cast<uint8_t>(board::kDispCs), static_cast<uint8_t>(board::kDispDc),
            static_cast<uint8_t>(board::kDispReset)),
      rearm_(rearmWatchdogPin),
      originDx_(0),
      originDy_(0),
      contrast_(0),
      pattern_(0xFFu),
      contrastSet_(false),
      ready_(false),
      off_(false) {}

void Ssd1322Display::softenBusDrive() const {
    for (uint8_t i = 0; i < (sizeof(kBusPins) / sizeof(kBusPins[0])); ++i) {
        gpio_set_drive_capability(static_cast<gpio_num_t>(kBusPins[i]), GPIO_DRIVE_CAP_0);
    }
}

// IO19 e do adaptador do watchdog. Sem gancho, este adaptador NAO toca no pino: um pinMode local
// nao realinha o nivel com a fase da ISR de 1 kHz (o que o Stwd100Watchdog::rearmPin faz) e
// criaria um segundo dono para o pino que mantem o cachorro vivo.
void Ssd1322Display::rearmWdiPin() const {
    if (rearm_ != nullptr) {
        rearm_();
    }
}

u8g2_uint_t Ssd1322Display::shiftedX(int16_t x) const {
    return static_cast<u8g2_uint_t>(static_cast<int16_t>(x + originDx_));
}

u8g2_uint_t Ssd1322Display::shiftedY(int16_t y) const {
    return static_cast<u8g2_uint_t>(static_cast<int16_t>(y + originDy_));
}

void Ssd1322Display::selectFont(TextFont font) const {
    u8g2_.setFont(fontFor(font));
    u8g2_.setFontRefHeightAll();
    u8g2_.setFontPosTop();
    u8g2_.setFontMode(1);
}

// Caminho unico de init, compartilhado por begin() e por hardReset(). BLOQUEIA ~335 ms: 300 ms
// deles sao os tres delay() do reset de hardware que o proprio U8g2 executa dentro do
// u8g2_.begin() (u8x8_display.c:71-76 com reset_pulse_width_ms = post_reset_wait_ms = 100 em
// u8x8_d_ssd1322.c:249-250), e o resto e o clearDisplay da RAM do controlador mais um quadro.
// Nao aplica contraste por conta propria: brilho e politica do dominio (ver cabecalho).
bool Ssd1322Display::initPanel(bool blankFrame) {
    u8g2_.setBusClock(kBusClockHz);
    const bool started = u8g2_.begin();
    softenBusDrive();
    rearmWdiPin();
    if (!started) {
        return false;
    }
    u8g2_.setPowerSave(off_ ? 1u : 0u);
    if (contrastSet_) {
        u8g2_.setContrast(contrast_);
    }
    u8g2_.setDrawColor(1);
    if (blankFrame) {
        u8g2_.clearBuffer();
    }
    u8g2_.sendBuffer();
    return true;
}

Status Ssd1322Display::begin() {
    // Passo 8 da ordem de boot. MISO real (IO39, input-only e NC), NUNCA -1: com -1 o core
    // instalado poe o IO19 (WDI) em entrada e o prende ao VSPI. Ver ORDEM DE BOOT no cabecalho.
    SPI.begin(board::kDispSclk, board::kDispMiso, board::kDispMosi, board::kNoPin);
    softenBusDrive();
    if (!initPanel(true)) {
        ready_ = false;
        return Status(Err::Io);
    }
    ready_ = true;
    return kOk;
}

// BLOQUEIA ate ~500 ms. Chamada de boot ou de recuperacao explicita, JAMAIS de dentro do ciclo de
// 50 ms (ver CUSTO BLOQUEANTE DECLARADO no cabecalho). Devolve o painel PRONTO: o pulso de reset
// leva o SSD1322 ao POR e apaga a configuracao dele, entao o caminho de init tem de ser repetido
// aqui - sem isso a classe nao teria caminho de volta e todo present() seguinte devolveria
// Err::NotInit para sempre, que e uma pre-condicao que o FakeDisplay nao tem.
Status Ssd1322Display::hardReset() {
    if (board::kDispReset == board::kNoPin) {
        return Status(Err::Param);
    }
    const bool wasReady = ready_;
    ready_ = false;
    pinMode(static_cast<uint8_t>(board::kDispReset), OUTPUT);
    digitalWrite(static_cast<uint8_t>(board::kDispReset), LOW);
    delay(kResetLowMs);
    digitalWrite(static_cast<uint8_t>(board::kDispReset), HIGH);
    delay(kResetSettleMs);
    if (!wasReady) {
        // Nunca houve begin(): so o pulso, como o fake. O passo 9 da ordem de boot ainda vem.
        return kOk;
    }
    if (!initPanel(false)) {
        return Status(Err::Io);
    }
    ready_ = true;
    return kOk;
}

uint16_t Ssd1322Display::widthPx() const {
    return static_cast<uint16_t>(u8g2_.getDisplayWidth());
}

uint16_t Ssd1322Display::heightPx() const {
    return static_cast<uint16_t>(u8g2_.getDisplayHeight());
}

uint8_t Ssd1322Display::lineHeightPx(TextFont font) const {
    selectFont(font);
    const int8_t height = u8g2_.getMaxCharHeight();
    return (height > 0) ? static_cast<uint8_t>(height) : 0u;
}

uint16_t Ssd1322Display::textWidthPx(TextFont font, const char* text) const {
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }
    selectFont(font);
    const uint16_t ink = static_cast<uint16_t>(u8g2_.getUTF8Width(text));
    uint16_t last = 0;
    while (text[last + 1u] != '\0') {
        ++last;
    }
    const uint8_t tailByte = static_cast<uint8_t>(text[last]);
    if (tailByte >= 0x80u) {
        return ink;
    }
    const char tail[2] = {text[last], '\0'};
    const uint16_t tailInk = static_cast<uint16_t>(u8g2_.getUTF8Width(tail));
    const int8_t tailAdvance = u8g2_GetGlyphWidth(u8g2_.getU8g2(), tailByte);
    if (tailInk > ink || tailAdvance <= 0) {
        return ink;
    }
    return static_cast<uint16_t>(ink - tailInk + static_cast<uint16_t>(tailAdvance));
}

Status Ssd1322Display::clear() {
    u8g2_.setDrawColor(1);
    u8g2_.clearBuffer();
    return kOk;
}

Status Ssd1322Display::drawText(int16_t x, int16_t y, const char* text, TextFont font,
                                TextInk ink) {
    if (text == nullptr) {
        return Status(Err::Param);
    }
    selectFont(font);
    const u8g2_uint_t px = shiftedX(x);
    const u8g2_uint_t py = shiftedY(y);
    if (ink == TextInk::Inverse) {
        const int8_t height = u8g2_.getMaxCharHeight();
        const uint16_t box = textWidthPx(font, text);
        selectFont(font);
        if (box > 0 && height > 0) {
            u8g2_.setDrawColor(1);
            u8g2_.drawBox(px, py, box, static_cast<u8g2_uint_t>(height));
        }
        u8g2_.setDrawColor(0);
        u8g2_.drawUTF8(px, py, text);
        u8g2_.setDrawColor(1);
        return kOk;
    }
    u8g2_.setDrawColor(1);
    u8g2_.drawUTF8(px, py, text);
    return kOk;
}

Status Ssd1322Display::fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h, bool on) {
    if (w == 0u || h == 0u) {
        return kOk;
    }
    u8g2_.setDrawColor(on ? 1u : 0u);
    u8g2_.drawBox(shiftedX(x), shiftedY(y), w, h);
    u8g2_.setDrawColor(1);
    return kOk;
}

Status Ssd1322Display::drawFrame(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    if (w == 0u || h == 0u) {
        return kOk;
    }
    u8g2_.setDrawColor(1);
    u8g2_.drawFrame(shiftedX(x), shiftedY(y), w, h);
    return kOk;
}

Status Ssd1322Display::present() {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    u8g2_.sendBuffer();
    return kOk;
}

Status Ssd1322Display::setOrigin(int8_t dx, int8_t dy) {
    if (dx < -kOriginLimitPx || dx > kOriginLimitPx || dy < -kOriginLimitPx ||
        dy > kOriginLimitPx) {
        return Status(Err::Range);
    }
    originDx_ = dx;
    originDy_ = dy;
    return kOk;
}

Status Ssd1322Display::setContrast(uint8_t value) {
    contrast_ = value;
    contrastSet_ = true;
    if (ready_) {
        u8g2_.setContrast(value);
    }
    return kOk;
}

// Um comando de power save, sem trafego de framebuffer: o SSD1322 em power save ja apaga o
// painel, e mandar 8192 bytes de zeros aqui seria um segundo bloqueio de 16,4 ms num metodo que a
// porta nao declara como bloqueante, alem de destruir o quadro corrente - que o FakeDisplay
// preserva. O estado apagado e registrado mesmo sem begin(), como no fake.
Status Ssd1322Display::off() {
    off_ = true;
    if (ready_) {
        u8g2_.setPowerSave(1);
    }
    return kOk;
}

const char* Ssd1322Display::patternDescription(uint8_t index) const {
    return (index < kPatternCount) ? kPatternText[index] : "";
}

Status Ssd1322Display::showPattern(uint8_t index) {
    if (index >= kPatternCount) {
        return Status(Err::Range);
    }
    const u8g2_uint_t w = u8g2_.getDisplayWidth();
    const u8g2_uint_t h = u8g2_.getDisplayHeight();
    u8g2_.setDrawColor(1);
    u8g2_.clearBuffer();
    switch (index) {
        case 0:
            u8g2_.drawBox(0, 0, w, h);
            break;
        case 1:
            u8g2_.drawFrame(0, 0, w, h);
            break;
        case 2:
            u8g2_.drawHLine(0, static_cast<u8g2_uint_t>(h - 1u), w);
            for (u8g2_uint_t x = 0; x < w; x = static_cast<u8g2_uint_t>(x + kRulerStepPx)) {
                u8g2_.drawVLine(x, static_cast<u8g2_uint_t>(h - kRulerTickPx), kRulerTickPx);
            }
            break;
        case 3:
            for (uint8_t i = 0; i < kColumnMarkCount; ++i) {
                if (kColumnMarkX[i] < w) {
                    u8g2_.drawVLine(static_cast<u8g2_uint_t>(kColumnMarkX[i]), 0, h);
                }
            }
            break;
        default:
            return Status(Err::Range);
    }
    pattern_ = index;
    return kOk;
}

}  // namespace adapters
