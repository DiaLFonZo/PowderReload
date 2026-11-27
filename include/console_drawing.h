#pragma once

#include <TFT_eSPI.h>
#include "console_art.h"

namespace ConsoleDrawing {
template <size_t Count>
const ConsoleArt::Glyph* findGlyph(const ConsoleArt::Glyph (&font)[Count], char character) {
    for (const auto& glyph : font) {
        if (glyph.character == character) return &glyph;
    }
    return nullptr;
}

template <size_t Count>
int textWidth(const ConsoleArt::Glyph (&font)[Count], const char* label) {
    int width = 0;
    for (; *label; ++label) {
        const auto* glyph = findGlyph(font, *label);
        if (glyph) width += glyph->advance;
    }
    return width;
}

template <size_t Count>
void drawText(TFT_eSprite& canvas, const ConsoleArt::Glyph (&font)[Count], const uint8_t* pixels,
              const char* label, int x, int baseline, uint16_t ink = TFT_WHITE) {
    for (; *label; ++label) {
        const auto* glyph = findGlyph(font, *label);
        if (!glyph) continue;
        for (int y = 0; y < glyph->height; ++y) {
            for (int column = 0; column < glyph->width; ++column) {
                const int index = y * glyph->width + column;
                const uint8_t packed = pixels[glyph->offset + index / 2];
                const uint8_t level = (index % 2 ? packed & 15 : packed >> 4);
                if (!level) continue;
                const uint16_t shade = level;
                const uint16_t color = ((((ink >> 11) * shade) / 15) << 11) |
                    (((((ink >> 5) & 63) * shade) / 15) << 5) | (((ink & 31) * shade) / 15);
                canvas.drawPixel(x + glyph->left + column, baseline + glyph->top + y, color);
            }
        }
        x += glyph->advance;
    }
}

inline void background(TFT_eSprite& canvas) {
    canvas.fillSprite(TFT_BLACK);
    for (const auto& span : ConsoleArt::Background) {
        canvas.drawFastHLine(span.x, span.y, span.length, span.color);
    }
}

// Big reading with its unit, centred.
inline void reading(TFT_eSprite& canvas, const char* value, int width, int baseline = 133,
                    uint16_t ink = TFT_WHITE) {
    using namespace ConsoleArt;
    const int numberWidth = textWidth(Large, value);
    const int left = (width - numberWidth - textWidth(Unit, "gr") - 6) / 2;
    drawText(canvas, Large, LargePixels, value, left, baseline, ink);
    drawText(canvas, Unit, UnitPixels, "gr", left + numberWidth + 6, baseline, ink);
}
}
