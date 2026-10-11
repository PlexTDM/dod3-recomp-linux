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
 *   DOD3_PROMPTS   unset: by player 1's pad (the keyboard's keys with none)
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
#include <mutex>
#include <vector>
#include <zlib.h>
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
    { "keyboard", "keyboard", kPromptPng_keyboard, sizeof kPromptPng_keyboard },
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

/* An 8-bit RGBA, non-interlaced PNG (what tools/prompt_atlas.py writes). */
bool png_rgba(const uint8_t* p, size_t n, std::vector<uint8_t>& out, uint32_t& w, uint32_t& h)
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
        uint32_t w = 0, h = 0;
        if (!png_rgba(kSet[set].png, kSet[set].png_bytes, m, w, h) || w != kAtlasW * kMasterScale ||
            h != kAtlasH * kMasterScale) {
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
    default:
#if defined(_WIN32) || defined(__APPLE__)
        return kKeyboard;           /* nothing plugged in: the keys that stand in for a pad */
#else
        return kOriginal;
#endif
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
