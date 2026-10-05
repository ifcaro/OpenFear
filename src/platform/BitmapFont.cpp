#include "openfear/platform/BitmapFont.h"
#include <algorithm>
#include <string>

extern "C" const unsigned char g_basicfont_bmp[];
extern "C" const unsigned long g_basicfont_bmp_len;

namespace openfear::platform {

namespace {
constexpr int kCols = 12;
// Atlas cell pitch: glyph size + 1px blank gap, so UV rounding at this small
// a glyph size (8x16) bleeds into the gap instead of the neighboring cell.
constexpr int kCellPitchX = BitmapFont::kGlyphWidth + 1;
constexpr int kCellPitchY = BitmapFont::kGlyphHeight + 1;

// Codepoints in atlas order (left-right, top-bottom): space, digits, A-Z,
// punctuation, Western-European uppercase accents. Index * glyph size gives
// the cell. Accents spelled as \U escapes, not literal chars, since
// compilers across our target toolchains disagree on source encoding.
constexpr char32_t kCharset[] = {
    U' ',          U'0', U'1', U'2', U'3',  U'4', U'5', U'6', U'7', U'8', U'9', U'A', U'B',
    U'C',          U'D', U'E', U'F', U'G',  U'H', U'I', U'J', U'K', U'L', U'M', U'N', U'O',
    U'P',          U'Q', U'R', U'S', U'T',  U'U', U'V', U'W', U'X', U'Y', U'Z', U'.', U',',
    U'!',          U'?', U':', U';', U'\'', U'"', U'(', U')', U'-', U'+', U'/', U'%', U'&',
    U'\U000000C0',  // À
    U'\U000000C1',  // Á
    U'\U000000C2',  // Â
    U'\U000000C3',  // Ã
    U'\U000000C4',  // Ä
    U'\U000000C5',  // Å
    U'\U000000C6',  // Æ
    U'\U000000C7',  // Ç
    U'\U000000C8',  // È
    U'\U000000C9',  // É
    U'\U000000CA',  // Ê
    U'\U000000CB',  // Ë
    U'\U000000CC',  // Ì
    U'\U000000CD',  // Í
    U'\U000000CE',  // Î
    U'\U000000CF',  // Ï
    U'\U000000D1',  // Ñ
    U'\U000000D2',  // Ò
    U'\U000000D3',  // Ó
    U'\U000000D4',  // Ô
    U'\U000000D5',  // Õ
    U'\U000000D6',  // Ö
    U'\U000000D8',  // Ø
    U'\U000000D9',  // Ù
    U'\U000000DA',  // Ú
    U'\U000000DB',  // Û
    U'\U000000DC',  // Ü
    U'\U000000DD',  // Ý
    U'\U000000DF',  // ß
    // Spleen (the embedded font) has no Œ/Ÿ glyphs - dropped from this
    // charset to match, not worth hand-drawing 2 extra cells for.
};
constexpr int kCharsetSize = sizeof(kCharset) / sizeof(kCharset[0]);

int GlyphIndex(char32_t cp) {
    // Uppercases ASCII letters automatically (this font has no lowercase)
    // rather than rendering them blank - a caller passing "Checking..."
    // still reads fine as "CHECKING...".
    if (cp >= U'a' && cp <= U'z') cp -= (U'a' - U'A');
    for (int i = 0; i < kCharsetSize; ++i)
        if (kCharset[i] == cp) return i;
    return -1;  // blank cell
}

// Decodes one UTF-8 codepoint at *p, advances *p past it. Invalid/truncated
// sequences decode to a replacement codepoint and advance 1 byte, so a bad
// byte can't stall the loop.
char32_t DecodeUtf8(const char*& p) {
    const unsigned char c0 = static_cast<unsigned char>(*p);
    if (c0 < 0x80) {
        ++p;
        return c0;
    }
    int extra = 0;
    char32_t cp = 0;
    if ((c0 & 0xE0) == 0xC0) {
        extra = 1;
        cp = c0 & 0x1F;
    } else if ((c0 & 0xF0) == 0xE0) {
        extra = 2;
        cp = c0 & 0x0F;
    } else if ((c0 & 0xF8) == 0xF0) {
        extra = 3;
        cp = c0 & 0x07;
    } else {
        ++p;
        return 0xFFFD;
    }
    const char* q = p + 1;
    for (int i = 0; i < extra; ++i) {
        const unsigned char cn = static_cast<unsigned char>(q[i]);
        if ((cn & 0xC0) != 0x80) {
            ++p;
            return 0xFFFD;  // truncated sequence - bail, advance 1 byte only
        }
        cp = (cp << 6) | (cn & 0x3F);
    }
    p = q + extra;
    return cp;
}
}  // namespace

bool BitmapFont::Load(IRenderer& renderer) {
    tex_ = renderer.loadBitmapFromMemory(g_basicfont_bmp, static_cast<size_t>(g_basicfont_bmp_len));
    if (!tex_) return false;
    sprite_ = renderer.createSprite(tex_);
    return true;
}

void BitmapFont::Unload(IRenderer& renderer) {
    if (tex_) {
        renderer.destroyTexture(tex_);
        tex_ = 0;
    }
    sprite_ = {};
}

void BitmapFont::DrawText(IRenderer& renderer, int x, int y, const char* text, uint8_t r, uint8_t g,
                          uint8_t b) const {
    if (!tex_ || !text) return;
    int cursor_x = x;
    const char* p = text;
    while (*p) {
        const char32_t cp = DecodeUtf8(p);
        const int index = GlyphIndex(cp);
        if (index >= 0) {
            const int src_x = (index % kCols) * kCellPitchX;
            const int src_y = (index / kCols) * kCellPitchY;
            renderer.drawSpriteTinted(sprite_, cursor_x, y, src_x, src_y, kGlyphWidth, kGlyphHeight,
                                      r, g, b);
        }
        cursor_x += kGlyphWidth + kCharSpacing;
    }
}

void BitmapFont::DrawTextWrapped(IRenderer& renderer, int x, int y, const char* text, int max_width,
                                 uint8_t r, uint8_t g, uint8_t b) const {
    if (!text) return;
    const int chars_per_line = std::max(1, max_width / (kGlyphWidth + kCharSpacing));
    const char* p = text;
    while (*p) {
        const char* line_start = p;
        // Decodes (not just skips) up to chars_per_line codepoints so the cut always lands on
        // a codepoint boundary, never mid-sequence inside a multi-byte UTF-8 character.
        for (int count = 0; *p && count < chars_per_line; ++count) DecodeUtf8(p);
        const std::string line(line_start, static_cast<size_t>(p - line_start));
        DrawText(renderer, x, y, line.c_str(), r, g, b);
        y += kGlyphHeight + 4;
    }
}

int BitmapFont::TextWidth(const char* text) const {
    if (!text) return 0;
    int count = 0;
    const char* p = text;
    while (*p) {
        DecodeUtf8(p);
        ++count;
    }
    // No trailing spacing after the last glyph - matches DrawText's actual
    // cursor advance pattern (kCharSpacing only sits BETWEEN characters).
    return count > 0 ? count * kGlyphWidth + (count - 1) * kCharSpacing : 0;
}

}  // namespace openfear::platform
