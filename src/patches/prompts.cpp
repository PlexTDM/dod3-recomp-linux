/*
 * The button prompts, in the glyphs of the pad the player holds.
 *
 * Every button the game draws -- inline in its text (the message tables'
 * control code 1, then the button's number), in the menus' help bar, in the
 * HUD -- is a sprite of one layout, HUD_COMMON.menu.icon_button, over one
 * 256x128 DXT5 atlas of DualShock 3 buttons, HUD_COMMON.Texture.icon_button.
 * The cooker put the same bytes in seven packages (HUD_COMMON, HUD_BATTLE,
 * HUD_RESULT, their French twins and RESULT_DLC), so one hash of its content
 * names it wherever it was loaded from.
 *
 * The renderer asks the port about every texture it uploads
 * (rsx_draw_engine_set_texture_standin). For that atlas the answer is another
 * one with the same 28 rectangles (src/res/prompts/<set>.png, drawn by
 * tools/prompt_atlas.py at four times the size and built in as
 * gen/prompts_data.h). The layout's rectangles reach the GPU as normalised
 * coordinates, so the larger atlas only makes the glyphs sharper; it is
 * handed over at the internal resolution's scale, as the game samples it
 * without mip levels.
 *
 * The keyboard and mouse's atlas is made here, not drawn ahead: the keys are
 * the player's own (src/input/kbm.cpp), so the first key bound to each
 * button is laid into its rectangles from a sheet of key caps
 * (src/res/prompts/keys.png, each tile named in the PNG's "tiles" text) --
 * the four keys of a stick or of the D-pad in a plus, the mouse for the
 * camera. With the port's keyboard off (DOD3_KEYBOARD=0) there are no
 * bindings to draw, and the game's own art stays.
 *
 *   DOD3_PROMPTS   unset: by player 1's pad (the keyboard's keys with none,
 *                  or when the keyboard was touched last)
 *                  ps3 (the game's own art), ps4, ps5, xbox, switch, keyboard
 *   DOD3_PROMPTS_CYCLE=<seconds>   step through every set, to look at them
 *
 * Still the DualShock 3's whatever the setting: the title screen's "Press
 * the START Button" and the cutscenes' "Press START to Skip" (words in
 * hud_title.Texture.font_start and HUD_BATTLE.Texture.demo_skip), the Circle
 * on the dragon's call icon (HUD_BATTLE.Texture.d_icon), the pad pictured on
 * the controller settings page (hud_pause.Texture.icon_option_config), and
 * the few lines of text that name a control in words ("the START button",
 * "the left stick").
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <zlib.h>
#include "input/kbm.h"
#include "patches/prompts.h"
#include "prompts_data.h"   /* gen/: kPromptPng_<set>, from src/res/prompts */
#include "rsx_draw_engine.h"
#include "util.h"

extern "C" int cellPad_host_kind(uint32_t port);   /* ps3recomp/libs/input/cellPad.h, and its values: */
enum { PAD_NONE, PAD_XBOX, PAD_PS3, PAD_PS4, PAD_PS5, PAD_SWITCH, PAD_OTHER };

namespace {

/* icon_button: 256x128, CELL_GCM_TEXTURE_COMPRESSED_DXT45, one level. */
const uint32_t kAtlasW = 256, kAtlasH = 128, kAtlasBytes = 32768;
const uint64_t kAtlasHash = 0xE1FE6B19A377E5DAull;   /* FNV-1a 64 of its bytes */
const uint32_t kMasterScale = 4;                     /* the PNGs: 1024x512 */

enum Set { kOriginal, kPs4, kPs5, kXbox, kSwitch, kKeyboard, kSets };
const struct {
    const char* value;   /* DOD3_PROMPTS */
    const char* name;
    const unsigned char* png;
    size_t png_bytes;
} kSet[kSets] = {
    { "ps3", "PlayStation 3", nullptr, 0 },
    { "ps4", "PlayStation 4", kPromptPng_ps4, sizeof kPromptPng_ps4 },
    { "ps5", "PlayStation 5", kPromptPng_ps5, sizeof kPromptPng_ps5 },
    { "xbox", "Xbox", kPromptPng_xbox, sizeof kPromptPng_xbox },
    { "switch", "Nintendo Switch", kPromptPng_switch, sizeof kPromptPng_switch },
    { "keyboard", "keyboard", nullptr, 0 },   /* made from the bindings: compose_keyboard */
};

std::atomic<int> s_set{ kOriginal };   /* the set shown */
std::atomic<int> s_scale{ 1 };         /* 1, 2 or 4 times the game's atlas */
std::mutex s_mu;                       /* the pixels below */
std::vector<uint8_t> s_master[kSets];  /* the PNG's RGBA, 1024x512 */
bool s_bad[kSets];
std::vector<uint8_t> s_texels[kSets][3];   /* guest A8R8G8B8 at 1x, 2x, 4x: made once, never moved */

uint64_t fnv1a64(const uint8_t* p, size_t n)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* An 8-bit RGBA, non-interlaced PNG (what tools/prompt_atlas.py writes),
 * and its "tiles" text if it has one and it is asked for. */
bool png_rgba(const uint8_t* p, size_t n, std::vector<uint8_t>& out, uint32_t& w, uint32_t& h,
              std::string* tiles = nullptr)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    if (n < 8 || memcmp(p, sig, 8)) return false;
    std::vector<uint8_t> idat;
    w = h = 0;
    for (size_t o = 8; o + 12 <= n;) {
        const uint32_t len = be32(p + o);
        const uint8_t* type = p + o + 4;
        const uint8_t* data = p + o + 8;
        if (len > n - o - 12) return false;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = be32(data);
            h = be32(data + 4);
            if (data[8] != 8 || data[9] != 6 || data[12] != 0) return false;   /* depth, colour type, interlace */
        } else if (!memcmp(type, "IDAT", 4)) {
            idat.insert(idat.end(), data, data + len);
        } else if (tiles && !memcmp(type, "tEXt", 4) && len > 6 && !memcmp(data, "tiles", 6)) {
            tiles->assign((const char*)data + 6, len - 6);
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        o += 12 + (size_t)len;
    }
    if (!w || !h || w > 4096 || h > 4096) return false;
    const size_t stride = (size_t)w * 4;
    std::vector<uint8_t> raw((stride + 1) * h);
    uLongf got = (uLongf)raw.size();
    if (uncompress(raw.data(), &got, idat.data(), (uLong)idat.size()) != Z_OK || got != raw.size()) return false;
    out.assign(stride * h, 0);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* src = raw.data() + (stride + 1) * y;
        uint8_t* row = out.data() + stride * y;
        const uint8_t* up = y ? row - stride : nullptr;
        const int filter = *src++;
        for (size_t x = 0; x < stride; x++) {
            const int a = x >= 4 ? row[x - 4] : 0, b = up ? up[x] : 0, c = (up && x >= 4) ? up[x - 4] : 0;
            int pred = 0;
            switch (filter) {
            case 1: pred = a; break;
            case 2: pred = b; break;
            case 3: pred = (a + b) / 2; break;
            case 4: {
                const int q = a + b - c, pa = abs(q - a), pb = abs(q - b), pc = abs(q - c);
                pred = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                break;
            }
            default: break;
            }
            row[x] = (uint8_t)(src[x] + pred);
        }
    }
    return true;
}

/* ---- the keyboard's atlas, from the bindings ---- */

/* The layout's sprites (HUD_COMMON.menu.icon_button; 12 is a glow on a
 * texture of its own): the cell, and the glyph's box inside it, in the
 * atlas's texels. */
const struct {
    int16_t cx, cy, cw, ch, x0, y0, x1, y1;
} kSprite[29] = {
    { 32, 31, 31, 32, 3, 4, 28, 29 },  { 0, 31, 31, 32, 3, 4, 28, 29 },   { 32, 0, 31, 32, 3, 4, 28, 29 },
    { 0, 0, 31, 32, 3, 4, 28, 29 },    { 0, 63, 45, 25, 5, 3, 40, 22 },   { 0, 89, 45, 38, 5, 9, 40, 28 },
    { 63, 1, 45, 25, 5, 3, 40, 22 },   { 63, 27, 45, 38, 5, 9, 40, 28 },  { 46, 66, 31, 28, 1, 1, 29, 27 },
    { 79, 66, 36, 28, 1, 2, 35, 27 },  { 50, 97, 26, 26, 2, 2, 24, 24 },  { 76, 97, 26, 26, 2, 2, 24, 24 },
    { 0, 0, 0, 0, 0, 0, 0, 0 },        { 109, 0, 34, 33, 5, 5, 28, 28 },  { 143, 0, 33, 33, 5, 5, 28, 28 },
    { 177, 0, 33, 33, 5, 5, 28, 28 },  { 147, 64, 30, 29, 1, 1, 28, 28 }, { 115, 64, 30, 29, 1, 1, 28, 28 },
    { 147, 33, 30, 29, 1, 1, 28, 28 }, { 115, 33, 30, 29, 1, 1, 28, 28 }, { 102, 97, 26, 26, 2, 2, 24, 24 },
    { 128, 97, 26, 26, 2, 2, 24, 24 }, { 181, 35, 26, 26, 3, 4, 22, 22 }, { 180, 61, 18, 16, 3, 2, 15, 14 },
    { 224, 94, 31, 32, 1, 2, 30, 31 }, { 192, 94, 31, 32, 1, 2, 30, 31 }, { 159, 94, 31, 31, 1, 2, 30, 31 },
    { 224, 63, 31, 32, 1, 2, 30, 31 }, { 180, 78, 18, 16, 3, 2, 15, 14 },
};

struct Tile {
    int x, y, w, h;   /* in the sheet's pixels */
};
enum { kPlain, kPressed, kLit };

/* One tile into a box of the master (pixels; the tile keeps its shape and
 * sits in the middle): each texel the average of what it covers, the alpha
 * weighted in, and where nothing opaque is under it the plain average -- the
 * edge colour the sheet carries there. */
void put_tile(std::vector<uint8_t>& m, const std::vector<uint8_t>& sheet, uint32_t sheet_w, const Tile& t, float bx0,
              float by0, float bx1, float by1, int mode)
{
    const uint32_t mw = kAtlasW * kMasterScale, mh = kAtlasH * kMasterScale;
    const float k =
        (bx1 - bx0) / (float)t.w < (by1 - by0) / (float)t.h ? (bx1 - bx0) / (float)t.w : (by1 - by0) / (float)t.h;
    const float dw = (float)t.w * k, dh = (float)t.h * k;
    const int ox = (int)((bx0 + bx1 - dw) / 2 + 0.5f), oy = (int)((by0 + by1 - dh) / 2 + 0.5f);
    const int w = (int)(dw + 0.5f), h = (int)(dh + 0.5f);
    if (w < 1 || h < 1) return;
    const float sx = (float)t.w / (float)w, sy = (float)t.h / (float)h;   /* sheet pixels a texel */
    for (int j = 0; j < h; j++) {
        const float v0 = (float)j * sy, v1 = v0 + sy;
        for (int i = 0; i < w; i++) {
            const float u0 = (float)i * sx, u1 = u0 + sx;
            float a = 0, pr = 0, pg = 0, pb = 0, r = 0, g = 0, b = 0, area = 0;
            for (int y = (int)v0; y < t.h && (float)y < v1; y++) {
                const float wy = ((float)(y + 1) < v1 ? (float)(y + 1) : v1) - ((float)y > v0 ? (float)y : v0);
                for (int x = (int)u0; x < t.w && (float)x < u1; x++) {
                    const float wx = ((float)(x + 1) < u1 ? (float)(x + 1) : u1) - ((float)x > u0 ? (float)x : u0);
                    const uint8_t* s = &sheet[((size_t)(t.y + y) * sheet_w + (size_t)(t.x + x)) * 4];
                    const float wt = wx * wy;
                    area += wt;
                    a += wt * s[3];
                    pr += wt * s[0] * s[3];
                    pg += wt * s[1] * s[3];
                    pb += wt * s[2] * s[3];
                    r += wt * s[0];
                    g += wt * s[1];
                    b += wt * s[2];
                }
            }
            const int px = ox + i, py = oy + j;
            if (area <= 0 || px < 0 || py < 0 || px >= (int)mw || py >= (int)mh) continue;
            float c[4] = { a > 0 ? pr / a : r / area, a > 0 ? pg / a : g / area, a > 0 ? pb / a : b / area, a / area };
            if (mode == kPressed) {          /* the pale one the game shows as a button goes down */
                for (int q = 0; q < 3; q++) c[q] += (255.0f - c[q]) * 0.45f;
                c[3] *= 0.9f;
            } else if (mode == kLit) {       /* the D-pad's lit arms */
                c[0] *= 0.42f;
                c[2] *= 0.9f;
            }
            uint8_t* d = &m[((size_t)py * mw + (size_t)px) * 4];
            for (int q = 0; q < 4; q++) d[q] = (uint8_t)(c[q] < 0 ? 0 : c[q] > 255 ? 255 : c[q] + 0.5f);
        }
    }
}

/* The keyboard and mouse's atlas: the first key bound to each button in the
 * button's rectangles. Under s_mu. */
bool compose_keyboard(std::vector<uint8_t>& m)
{
    std::vector<uint8_t> sheet;
    std::string text;
    uint32_t sw = 0, sh = 0;
    if (!png_rgba(kPromptPng_keys, sizeof kPromptPng_keys, sheet, sw, sh, &text)) return false;
    std::map<std::string, Tile> tiles;
    for (size_t p = 0; p < text.size();) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        char name[32];
        Tile t;
        if (sscanf(text.substr(p, e - p).c_str(), "%31s %d %d %d %d", name, &t.x, &t.y, &t.w, &t.h) == 5 && t.x >= 0 &&
            t.y >= 0 && t.w > 0 && t.h > 0 && (uint32_t)(t.x + t.w) <= sw && (uint32_t)(t.y + t.h) <= sh)
            tiles[name] = t;
        p = e + 1;
    }
    auto tile = [&](const char* name) -> const Tile* {
        auto it = tiles.find(name ? name : "Unbound");
        if (it == tiles.end()) it = tiles.find("Unbound");
        return it == tiles.end() ? nullptr : &it->second;
    };
    m.assign((size_t)kAtlasW * kMasterScale * kAtlasH * kMasterScale * 4, 0);
    const float S = (float)kMasterScale;
    /* a sprite's box, grown by gx and gy texels each way but never out of its cell */
    auto box = [&](int sprite, float gx, float gy, float* b) {
        const auto& s = kSprite[sprite];
        float x0 = s.x0 - gx, y0 = s.y0 - gy, x1 = s.x1 + gx, y1 = s.y1 + gy;
        if (x0 < 0.5f) x0 = 0.5f;
        if (y0 < 0.5f) y0 = 0.5f;
        if (x1 > s.cw - 0.5f) x1 = s.cw - 0.5f;
        if (y1 > s.ch - 0.5f) y1 = s.ch - 0.5f;
        b[0] = (s.cx + x0) * S;
        b[1] = (s.cy + y0) * S;
        b[2] = (s.cx + x1) * S;
        b[3] = (s.cy + y1) * S;
    };
    auto key = [&](int sprite, const char* name, float gx, float gy, int mode) {
        float b[4];
        box(sprite, gx, gy, b);
        if (const Tile* t = tile(name)) put_tile(m, sheet, sw, *t, b[0], b[1], b[2], b[3], mode);
    };
    /* four keys in a plus: up, down, left, right; `lit` a mask of them */
    auto plus = [&](int sprite, Dod3Bind up, float grow, int lit) {
        float b[4];
        box(sprite, grow, grow, b);
        const float k = (b[2] - b[0]) / 3, cx = (b[0] + b[2]) / 2, cy = (b[1] + b[3]) / 2;
        static const int dx[4] = { 0, 0, -1, 1 }, dy[4] = { -1, 1, 0, 0 };
        for (int d = 0; d < 4; d++)
            if (const Tile* t = tile(dod3_kbm_key((Dod3Bind)(up + d)))) {
                const float x = cx + dx[d] * k, y = cy + dy[d] * k;
                put_tile(m, sheet, sw, *t, x - k / 2, y - k / 2, x + k / 2, y + k / 2, (lit >> d) & 1 ? kLit : kPlain);
            }
    };
    static const struct {
        Dod3Bind bind;
        int small, pressed, large;
    } face[4] = {
        { kBindCircle, 0, 16, 24 }, { kBindCross, 1, 17, 25 }, { kBindTriangle, 2, 18, 26 }, { kBindSquare, 3, 19, 27 }
    };
    for (const auto& f : face) {
        const char* k = dod3_kbm_key(f.bind);
        key(f.small, k, 2.5f, 0, kPlain);      /* a wide key may use the cell's width */
        key(f.pressed, k, 1, 0, kPressed);
        key(f.large, k, 1, 0, kPlain);
    }
    key(4, dod3_kbm_key(kBindL1), 0, 2, kPlain);
    key(5, dod3_kbm_key(kBindL2), 0, 2, kPlain);
    key(6, dod3_kbm_key(kBindR1), 0, 2, kPlain);
    key(7, dod3_kbm_key(kBindR2), 0, 2, kPlain);
    key(8, dod3_kbm_key(kBindStart), 0, 0, kPlain);
    key(9, dod3_kbm_key(kBindSelect), 0, 0, kPlain);
    plus(10, kBindMoveUp, 1, 0);
    if (dod3_kbm_mouse_look())
        key(11, "MouseMove", 1, 1, kPlain);
    else
        plus(11, kBindLookUp, 1, 0);
    plus(13, kBindUp, 2, 0);
    plus(14, kBindUp, 2, 1 | 2);
    plus(15, kBindUp, 2, 4 | 8);
    key(20, dod3_kbm_key(kBindL3), 1, 1, kPlain);
    key(21, dod3_kbm_key(kBindR3), 1, 1, kPlain);
    key(22, "Star", 0, 0, kPlain);
    key(23, "Registered", 0, 0, kPlain);
    key(28, "Copyright", 0, 0, kPlain);
    return true;
}

/* The set's atlas at 1, 2 or 4 times the game's, as guest A8R8G8B8 texels:
 * the master averaged down with its alpha weighted in. A texel with nothing
 * opaque under it keeps the plain average, the edge colour the PNG carries
 * there for the bilinear taps that reach it. Under s_mu. */
const std::vector<uint8_t>* texels(int set, uint32_t scale)
{
    const int slot = scale >= 4 ? 2 : scale >= 2 ? 1 : 0;
    std::vector<uint8_t>& out = s_texels[set][slot];
    if (!out.empty()) return &out;
    if (s_bad[set]) return nullptr;
    std::vector<uint8_t>& m = s_master[set];
    if (m.empty()) {
        uint32_t w = kAtlasW * kMasterScale, h = kAtlasH * kMasterScale;
        const bool ok = set == kKeyboard && dod3_kbm_present()
                            ? compose_keyboard(m)
                            : kSet[set].png && png_rgba(kSet[set].png, kSet[set].png_bytes, m, w, h);
        if (!ok || w != kAtlasW * kMasterScale || h != kAtlasH * kMasterScale) {
            fprintf(stderr, "[prompts] the %s atlas does not decode\n", kSet[set].name);
            m.clear();
            s_bad[set] = true;
            return nullptr;
        }
    }
    const uint32_t f = kMasterScale >> slot;   /* master texels per texel, each way */
    const uint32_t w = kAtlasW * kMasterScale / f, h = kAtlasH * kMasterScale / f, mw = kAtlasW * kMasterScale;
    out.resize((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t sa = 0, pr = 0, pg = 0, pb = 0, r = 0, g = 0, b = 0;
            for (uint32_t j = 0; j < f; j++)
                for (uint32_t i = 0; i < f; i++) {
                    const uint8_t* s = &m[((size_t)(y * f + j) * mw + (x * f + i)) * 4];
                    sa += s[3];
                    pr += s[0] * s[3];
                    pg += s[1] * s[3];
                    pb += s[2] * s[3];
                    r += s[0];
                    g += s[1];
                    b += s[2];
                }
            uint8_t* d = &out[((size_t)y * w + x) * 4];
            d[0] = (uint8_t)(sa / (f * f));
            d[1] = (uint8_t)(sa ? pr / sa : r / (f * f));
            d[2] = (uint8_t)(sa ? pg / sa : g / (f * f));
            d[3] = (uint8_t)(sa ? pb / sa : b / (f * f));
        }
    return &out;
}

int standin(void*, uint32_t fmt, uint32_t w, uint32_t h, uint32_t levels, const uint8_t* bytes, uint32_t span,
            rsx_texture_standin* out)
{
    (void)levels;
    const int set = s_set.load(std::memory_order_relaxed);
    if (set == kOriginal) return 0;
    if (w != kAtlasW || h != kAtlasH || span != kAtlasBytes || (fmt & 0x9Fu) != 0x88u) return 0;
    if (fnv1a64(bytes, span) != kAtlasHash) return 0;
    const uint32_t scale = (uint32_t)s_scale.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(s_mu);
    const std::vector<uint8_t>* t = texels(set, scale);
    if (!t) return 0;
    out->bytes = t->data();
    out->fmt = 0x85u | 0x20u;   /* CELL_GCM_TEXTURE_A8R8G8B8, linear rows */
    out->w = kAtlasW * scale;
    out->h = kAtlasH * scale;
    out->levels = 1;
    out->pitch = out->w * 4;
    return 1;
}

/* The set DOD3_PROMPTS asks for; unset or unknown, the one for player 1's pad. */
int wanted_set()
{
    if (const char* e = getenv("DOD3_PROMPTS"))
        for (int i = 0; i < kSets; i++)
            if (!strcmp(e, kSet[i].value)) return i;
    switch (cellPad_host_kind(0)) {
    case PAD_PS3: return kOriginal;
    case PAD_PS4: return kPs4;
    case PAD_PS5: return kPs5;
    case PAD_SWITCH: return kSwitch;
    case PAD_XBOX:
    case PAD_OTHER: return kXbox;   /* what a pad of no known family is laid out as */
    default:   /* nothing plugged in, or the keyboard touched last: the keys */
        return dod3_kbm_present() ? kKeyboard : kOriginal;
    }
}

} // namespace

void dod3_prompts_poll(bool now)
{
    static std::atomic<uint64_t> next{ 0 };
    const uint64_t t = dod3_now_us();
    if (!now && t < next.load(std::memory_order_relaxed)) return;
    next.store(t + 250000, std::memory_order_relaxed);

    int set = wanted_set();
    static int cycle = -1;
    if (cycle < 0) {
        const char* e = getenv("DOD3_PROMPTS_CYCLE");
        cycle = e ? atoi(e) : 0;
    }
    if (cycle > 0) set = (int)(t / 1000000ull / (uint64_t)cycle % kSets);

    /* The atlas is sampled without mip levels: no larger than the picture
     * it is drawn into needs. */
    const char* e = getenv("RSX_SCALE");
    const double rs = e ? atof(e) : 1.0;
    const int scale = rs > 2.0 ? 4 : rs > 1.0 ? 2 : 1;

    if (set == s_set.load() && scale == s_scale.load()) return;
    const bool said = set != s_set.load();
    s_set.store(set);
    s_scale.store(scale);
    rsx_draw_engine_texture_standins_changed();
    if (said) fprintf(stderr, "[prompts] button prompts: %s\n", kSet[set].name);
}

void dod3_prompts_install()
{
    rsx_draw_engine_set_texture_standin(standin, nullptr);
    dod3_prompts_poll(true);
}
