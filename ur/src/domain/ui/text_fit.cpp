#include "domain/ui/text_fit.h"

namespace domain {
namespace ui {
namespace {

bool cabe(const IDisplay& display, const char* text, TextFont font, int16_t largura) {
    return static_cast<int32_t>(display.textWidthPx(font, text)) <=
           static_cast<int32_t>(largura);
}

}  // namespace

TextFont fontThatFits(const IDisplay& display, const char* text, int16_t availableWidth,
                      TextFont largest) {
    if (text == nullptr || text[0] == '\0' || availableWidth <= 0) {
        return TextFont::Small;
    }
    if (largest == TextFont::Large && cabe(display, text, TextFont::Large, availableWidth)) {
        return TextFont::Large;
    }
    if (largest != TextFont::Small && cabe(display, text, TextFont::Medium, availableWidth)) {
        return TextFont::Medium;
    }
    return TextFont::Small;
}

}  // namespace ui
}  // namespace domain
