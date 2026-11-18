#include "Ui.h"
#include <cctype>
#include <cstring>

using namespace DirectX;
namespace ui = cfg::ui;

namespace {

// 5x7 ドット文字。row[] の各要素が1行で、下位5bitが左→右のドット (1 = 点灯)
struct Glyph { char c; uint8_t row[ui::kGlyphRows]; };

const Glyph kGlyphs[] = {
    {'A',{0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}},
    {'B',{0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110}},
    {'C',{0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110}},
    {'D',{0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110}},
    {'E',{0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111}},
    {'F',{0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000}},
    {'G',{0b01110,0b10001,0b10000,0b10111,0b10001,0b10001,0b01111}},
    {'H',{0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}},
    {'I',{0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110}},
    {'J',{0b00111,0b00010,0b00010,0b00010,0b00010,0b10010,0b01100}},
    {'K',{0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001}},
    {'L',{0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111}},
    {'M',{0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001}},
    {'N',{0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001}},
    {'O',{0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}},
    {'P',{0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000}},
    {'Q',{0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101}},
    {'R',{0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001}},
    {'S',{0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110}},
    {'T',{0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100}},
    {'U',{0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}},
    {'V',{0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100}},
    {'W',{0b10001,0b10001,0b10001,0b10101,0b10101,0b10101,0b01010}},
    {'X',{0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001}},
    {'Y',{0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100}},
    {'Z',{0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111}},
    {'0',{0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110}},
    {'1',{0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110}},
    {'2',{0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111}},
    {'3',{0b11110,0b00001,0b00001,0b01110,0b00001,0b00001,0b11110}},
    {'4',{0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010}},
    {'5',{0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110}},
    {'6',{0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110}},
    {'7',{0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000}},
    {'8',{0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110}},
    {'9',{0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100}},
    {'.',{0,0,0,0,0,0b01100,0b01100}},
    {':',{0,0b00100,0b00100,0,0b00100,0b00100,0}},
    {'/',{0b00001,0b00010,0b00010,0b00100,0b01000,0b01000,0b10000}},
    {'-',{0,0,0,0b11111,0,0,0}},
    {'!',{0b00100,0b00100,0b00100,0b00100,0b00100,0,0b00100}},
    {'>',{0b10000,0b01000,0b00100,0b00010,0b00100,0b01000,0b10000}},
    {'<',{0b00001,0b00010,0b00100,0b01000,0b00100,0b00010,0b00001}},
    {'%',{0b11001,0b11010,0b00010,0b00100,0b01000,0b01011,0b10011}},
    {'+',{0,0b00100,0b00100,0b11111,0b00100,0b00100,0}},
    {'(',{0b00010,0b00100,0b01000,0b01000,0b01000,0b00100,0b00010}},
    {')',{0b01000,0b00100,0b00010,0b00010,0b00010,0b00100,0b01000}},
    {',',{0,0,0,0,0b01100,0b00100,0b01000}},
};

// 文字 c のグリフを探す。空白・未対応文字は nullptr (何も描かない)
const Glyph* FindGlyph(char c)
{
    c = (char)toupper((unsigned char)c);                 // 小文字は大文字として描く
    for (const Glyph& g : kGlyphs) if (g.c == c) return &g;
    return nullptr;
}

} // namespace

// 画面座標 (左上原点) の矩形 → 単位立方体を拡大して置く
void Ui::Rect(Renderer& r, float x, float y, float w, float h, const XMFLOAT4& c, float z) const
{
    // 正射影は Y が上向きなので、上原点の y を h_ から引いて変換する
    XMMATRIX m = XMMatrixScaling(w, h, ui::kRectThickness)
               * XMMatrixTranslation(x + w * cfg::kHalf, h_ - (y + h * cfg::kHalf), z);
    r.Submit(cube_, m, c);
}

float Ui::TextWidth(const char* s, float pixel) const
{
    const size_t n = strlen(s);
    // 文字数 × 送り幅 - 最後の字間
    return n == 0 ? 0.0f : (float)(n * ui::kGlyphAdvance - (ui::kGlyphAdvance - ui::kGlyphCols)) * pixel;
}

void Ui::Text(Renderer& r, const char* s, float x, float y, float pixel, const XMFLOAT4& c) const
{
    const XMFLOAT4 shadow(0.0f, 0.0f, 0.0f, ui::kShadowAlpha * c.w);
    const float off  = pixel * ui::kShadowOffset;        // 影のずれ
    const float size = pixel * ui::kPixelFill;            // 1ドットの大きさ (隙間を少し空ける)

    // pass 0 = 影 (奥), pass 1 = 本体 (手前)
    for (int pass = 0; pass < 2; ++pass) {
        float cx = x;                                     // 現在の文字の左端
        for (const char* ch = s; *ch; ++ch, cx += ui::kGlyphAdvance * pixel) {
            const Glyph* g = FindGlyph(*ch);
            if (!g) continue;
            for (int row = 0; row < ui::kGlyphRows; ++row)
            for (int col = 0; col < ui::kGlyphCols; ++col) {
                // 左端の列が最上位ビット
                if (!((g->row[row] >> (ui::kGlyphCols - 1 - col)) & 1)) continue;
                if (pass == 0) Rect(r, cx + col * pixel + off, y + row * pixel + off, size, size, shadow, ui::kZShadow);
                else           Rect(r, cx + col * pixel,       y + row * pixel,       size, size, c,      ui::kZText);
            }
        }
    }
}

void Ui::TextCenter(Renderer& r, const char* s, float y, float pixel, const XMFLOAT4& c) const
{
    Text(r, s, (w_ - TextWidth(s, pixel)) * cfg::kHalf, y, pixel, c);
}
