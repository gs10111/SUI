// Padroes de bring-up do display do CN4: continuidade dos 5 sinais e leitura visual do operador.
// Sem MISO: nao existe leitura de volta, todo veredito e visual (folha 1/2).
#include "build_config.h"

#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2

#include "drivers/display_u8g2.h"

namespace {

constexpr const char* kDescriptions[U8g2Display::kPatternCount] = {
    "todos os pixels acesos",
    "todos os pixels apagados",
    "tabuleiro de xadrez de 8 x 8 pixels",
    "texto de identificacao legivel",
    "contraste minimo: imagem quase apagada",
    "contraste maximo: imagem no brilho total",
};

constexpr uint8_t kCheckerBlock = 8;

}  // namespace

U8g2Display::U8g2Display()
    : u8g2_(U8G2_R0, static_cast<uint8_t>(board::kDispCs), static_cast<uint8_t>(board::kDispDc),
            static_cast<uint8_t>(board::kDispReset)),
      ready_(false),
      contrast_(kContrastMax) {}

Status U8g2Display::begin() {
    if (!u8g2_.begin()) {
        ready_ = false;
        return Status(Err::Io);
    }
    ready_ = true;
    u8g2_.setContrast(contrast_);
    u8g2_.clearBuffer();
    u8g2_.sendBuffer();
    return kOk;
}

Status U8g2Display::hardReset() {
    if (board::kDispReset == board::kNoPin) {
        return Status(Err::Param);
    }
    pinMode(static_cast<uint8_t>(board::kDispReset), OUTPUT);
    digitalWrite(static_cast<uint8_t>(board::kDispReset), LOW);
    delay(10);
    digitalWrite(static_cast<uint8_t>(board::kDispReset), HIGH);
    delay(120);
    ready_ = false;
    return kOk;
}

uint16_t U8g2Display::width() const {
    return static_cast<uint16_t>(const_cast<U8g2Display*>(this)->u8g2_.getDisplayWidth());
}

uint16_t U8g2Display::height() const {
    return static_cast<uint16_t>(const_cast<U8g2Display*>(this)->u8g2_.getDisplayHeight());
}

void U8g2Display::fill(bool on) {
    u8g2_.clearBuffer();
    if (on) {
        u8g2_.setDrawColor(1);
        u8g2_.drawBox(0, 0, u8g2_.getDisplayWidth(), u8g2_.getDisplayHeight());
    }
    u8g2_.sendBuffer();
}

void U8g2Display::checkerboard() {
    u8g2_.clearBuffer();
    u8g2_.setDrawColor(1);
    const uint16_t w = static_cast<uint16_t>(u8g2_.getDisplayWidth());
    const uint16_t h = static_cast<uint16_t>(u8g2_.getDisplayHeight());
    for (uint16_t y = 0; y < h; y = static_cast<uint16_t>(y + kCheckerBlock)) {
        for (uint16_t x = 0; x < w; x = static_cast<uint16_t>(x + kCheckerBlock)) {
            const bool on = (((x / kCheckerBlock) + (y / kCheckerBlock)) % 2u) == 0u;
            if (on) {
                u8g2_.drawBox(x, y, kCheckerBlock, kCheckerBlock);
            }
        }
    }
    u8g2_.sendBuffer();
}

Status U8g2Display::showPattern(uint8_t index) {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    if (index >= kPatternCount) {
        return Status(Err::Param);
    }
    switch (index) {
        case 0:
            fill(true);
            break;
        case 1:
            fill(false);
            break;
        case 2:
            checkerboard();
            break;
        case 3:
            return writeText("DE-PURI-DI261924");
        case 4:
            u8g2_.setContrast(kContrastMin);
            contrast_ = kContrastMin;
            fill(true);
            break;
        case 5:
            u8g2_.setContrast(kContrastMax);
            contrast_ = kContrastMax;
            fill(true);
            break;
        default:
            return Status(Err::Param);
    }
    return kOk;
}

const char* U8g2Display::patternDescription(uint8_t index) const {
    return (index < kPatternCount) ? kDescriptions[index] : "?";
}

Status U8g2Display::writeText(const char* text) {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    if (text == nullptr) {
        return Status(Err::Param);
    }
    u8g2_.clearBuffer();
    u8g2_.setDrawColor(1);
    u8g2_.setFont(u8g2_font_ncenB10_tr);
    u8g2_.setCursor(0, 14);
    u8g2_.print("JIG DE TESTE DE FABRICA");
    u8g2_.setCursor(0, 34);
    u8g2_.print(text);
    u8g2_.setCursor(0, 54);
    u8g2_.print("DiEletrons");
    u8g2_.sendBuffer();
    return kOk;
}

Status U8g2Display::setContrast(uint8_t value) {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    contrast_ = value;
    u8g2_.setContrast(value);
    return kOk;
}

Status U8g2Display::off() {
    if (!ready_) {
        return kOk;
    }
    u8g2_.clearBuffer();
    u8g2_.sendBuffer();
    return kOk;
}

#endif
