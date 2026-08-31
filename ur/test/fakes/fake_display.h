// Display de teste: guarda o que foi desenhado no quadro corrente para que o teste possa
// afirmar o texto LITERAL das telas do manual.
//
// Guarda o texto, e nao o framebuffer de pixels, porque a assercao que interessa e "a tela
// mostra 'Senha incorreta!'", nao "o pixel (37, 12) esta aceso". Renderizar glifo no fake
// so acrescentaria uma fonte de divergencia entre fake e alvo sem provar nada a mais.
//
// Duas regras do alvo que este fake preserva de proposito, porque sao onde o dominio erra:
// 1. Nada conta como exibido antes de present(). O que o teste inspeciona e o ULTIMO quadro
//    apresentado, nao o rascunho em andamento. Um dominio que desenha e esquece o present()
//    tem de reprovar.
// 2. verifiable() e false. O CN4 nao tem via de leitura de volta, entao nenhuma decisao de
//    rele pode depender desta porta - e o fake nao pode sugerir o contrario.
#pragma once

#include <stdint.h>
#include <string.h>

#include "ports/i_display.h"

namespace test {

class FakeDisplay : public IDisplay {
public:
    static constexpr uint16_t kWidthPx = 256;
    static constexpr uint16_t kHeightPx = 64;
    static constexpr uint8_t kMaxDraws = 24;
    static constexpr uint8_t kTextCap = 48;

    struct Draw {
        int16_t x;
        int16_t y;
        char text[kTextCap];
        TextFont font;
        TextInk ink;
    };

    FakeDisplay()
        : drafting_(0), shown_(0), presents_(0), clears_(0), contrast_(0),
          originDx_(0), originDy_(0), pattern_(0xFF), begun_(false), off_(false) {}

    // --- IDisplay ---

    Status begin() override {
        begun_ = true;
        return kOk;
    }

    Status hardReset() override { return kOk; }

    uint16_t widthPx() const override { return kWidthPx; }
    uint16_t heightPx() const override { return kHeightPx; }

    uint8_t lineHeightPx(TextFont font) const override {
        return (font == TextFont::Large) ? 28u : 12u;
    }

    uint16_t textWidthPx(TextFont font, const char* text) const override {
        const uint16_t perGlyph = (font == TextFont::Large) ? 18u : 7u;
        return static_cast<uint16_t>(perGlyph * len(text));
    }

    Status clear() override {
        drafting_ = 0;
        ++clears_;
        return kOk;
    }

    Status drawText(int16_t x, int16_t y, const char* text, TextFont font,
                    TextInk ink) override {
        if (text == nullptr) {
            return Status(Err::Param);
        }
        if (drafting_ >= kMaxDraws) {
            return Status(Err::Range);
        }
        Draw& d = draft_[drafting_];
        d.x = x;
        d.y = y;
        d.font = font;
        d.ink = ink;
        uint8_t i = 0;
        for (; i + 1u < kTextCap && text[i] != '\0'; ++i) {
            d.text[i] = text[i];
        }
        d.text[i] = '\0';
        ++drafting_;
        return kOk;
    }

    Status fillRect(int16_t, int16_t, uint16_t, uint16_t, bool) override { return kOk; }
    Status drawFrame(int16_t, int16_t, uint16_t, uint16_t) override { return kOk; }

    Status present() override {
        for (uint8_t i = 0; i < drafting_; ++i) {
            frame_[i] = draft_[i];
        }
        shown_ = drafting_;
        ++presents_;
        return kOk;
    }

    Status setOrigin(int8_t dx, int8_t dy) override {
        if (dx < -2 || dx > 2 || dy < -2 || dy > 2) {
            return Status(Err::Range);
        }
        originDx_ = dx;
        originDy_ = dy;
        return kOk;
    }

    Status setContrast(uint8_t value) override {
        contrast_ = value;
        return kOk;
    }

    Status off() override {
        off_ = true;
        return kOk;
    }

    uint8_t patternCount() const override { return 4; }

    const char* patternDescription(uint8_t index) const override {
        switch (index) {
            case 0: return "todos os pixels acesos";
            case 1: return "moldura fechada";
            case 2: return "regua de colunas";
            case 3: return "marcas em 0, 64, 128, 192, 255";
            default: return "";
        }
    }

    Status showPattern(uint8_t index) override {
        if (index >= patternCount()) {
            return Status(Err::Range);
        }
        pattern_ = index;
        return kOk;
    }

    bool verifiable() const override { return false; }
    const char* driverName() const override { return "FakeDisplay"; }

    // --- inspecao pelo teste: sempre sobre o ultimo quadro APRESENTADO ---

    uint8_t drawCount() const { return shown_; }
    const Draw& draw(uint8_t index) const { return frame_[index]; }

    bool shows(const char* text) const { return find(text) < shown_; }

    // Sinaliza texto que apareceu como fragmento de outro: "Senha" dentro de "Senha
    // incorreta!" nao prova que a tela de login esta no ar.
    bool showsExactly(const char* text) const {
        for (uint8_t i = 0; i < shown_; ++i) {
            if (equal(frame_[i].text, text)) {
                return true;
            }
        }
        return false;
    }

    TextInk inkOf(const char* text) const {
        const uint8_t i = find(text);
        return (i < shown_) ? frame_[i].ink : TextInk::Normal;
    }

    // Um digito piscando e desenhado em Inverse: e assim que o teste prova REQ-DSP-04.
    bool hasInverse() const {
        for (uint8_t i = 0; i < shown_; ++i) {
            if (frame_[i].ink == TextInk::Inverse) {
                return true;
            }
        }
        return false;
    }

    uint32_t presentCount() const { return presents_; }
    uint32_t clearCount() const { return clears_; }
    uint8_t contrast() const { return contrast_; }
    int8_t originDx() const { return originDx_; }
    int8_t originDy() const { return originDy_; }
    uint8_t lastPattern() const { return pattern_; }
    bool begun() const { return begun_; }
    bool isOff() const { return off_; }

    void resetCounters() {
        presents_ = 0;
        clears_ = 0;
    }

private:
    static uint8_t len(const char* text) {
        uint8_t n = 0;
        while (text != nullptr && text[n] != '\0' && n < 255u) {
            ++n;
        }
        return n;
    }

    static bool equal(const char* a, const char* b) {
        return (a != nullptr) && (b != nullptr) && (strcmp(a, b) == 0);
    }

    uint8_t find(const char* text) const {
        if (text == nullptr) {
            return shown_;
        }
        for (uint8_t i = 0; i < shown_; ++i) {
            if (strstr(frame_[i].text, text) != nullptr) {
                return i;
            }
        }
        return shown_;
    }

    Draw draft_[kMaxDraws];
    Draw frame_[kMaxDraws];
    uint8_t drafting_;
    uint8_t shown_;
    uint32_t presents_;
    uint32_t clears_;
    uint8_t contrast_;
    int8_t originDx_;
    int8_t originDy_;
    uint8_t pattern_;
    bool begun_;
    bool off_;
};

}  // namespace test
