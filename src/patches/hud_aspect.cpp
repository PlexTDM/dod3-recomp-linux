/*
 * patches/hud_aspect.cpp -- the HUD in a view of another shape than 16:9.
 *
 * With the Aspect Ratio setting (src/ui/settings_menu.cpp) the engine
 * letterboxes the view to the ratio -- 21:9 is 1280x540 in the 1280x720
 * frame, 16:10 is 1152x720 -- and the HUD's canvas is the view's:
 * UGameViewportClient pushes the move to the view's corner onto the
 * canvas's transform stack, and the canvas is the view's size. The HUD lays
 * itself out on 1280x720 whatever the canvas, so it ran off the view. Its
 * markers (enemy gauges, the lock-on, talk icons) are placed on the view
 * itself, by Canvas.Project and the native CalcTransformPosition, and were
 * right.
 *
 * Sqex03GameHUD.DrawHUD hands the port its canvas first and says when it is
 * done (tools/menu_patch.py, bridge 17). In between, the top of the canvas's
 * transform stack is a scale and a move that put the HUD's 1280x720 in the
 * middle of the view, as big as fits: on a wide screen the 16:9 HUD a 16:9
 * screen of the same height would show. The markers' positions are mapped
 * back through it, so that they land where they did. A full-screen
 * rectangle -- a fade, a dimming; the script marks each -- gets a transform
 * that stretches 1280x720 over the whole view. A batch of canvas elements
 * keeps the transform it was made with, matched by the entry's CRC, so each
 * change gives the entry a CRC of its own; the end puts the entry back.
 *
 * The radar is drawn another way: its map and icons are materials that mask
 * themselves to a circle at a fixed place on the screen (they read their own
 * screen position), the place the 16:9 HUD has the radar. It is drawn with
 * the 16:9 HUD's own transform, with a mark in the transform's depth term,
 * and the renderer moves the marked draws to the fitted HUD
 * (rsx_canvas_remap_set): the materials still find themselves where they
 * were made to be.
 *
 * The begin also tells the present the view's shape (the canvas's), so that
 * the frame the HUD is drawn into is shown as a view of that shape: a frame
 * without a scene in it -- the title's last screen once a game has begun, a
 * loading screen -- is one too, its HUD fitted into the view like any other.
 * (A movie is drawn over the whole frame: the present finds its picture
 * among the frame's draws and shows that frame whole.)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ppu_recomp.h"
#include "cpu/eboot.h"   /* the EBOOT version's addresses */
#include "cpu/ppu.h"
#include "patches/hud_aspect.h"

extern "C" void rsx_present_set_content_aspect(float aspect);   /* rsx_draw_engine.h */
extern "C" void rsx_canvas_remap_set(uint32_t mark, float k, float x, float y);

namespace {

/* UCanvas (Engine.Canvas) */
constexpr uint32_t kCanvasClipX = 0x48, kCanvasClipY = 0x4C, kCanvasSizeX = 0x68, kCanvasSizeY = 0x6C,
                   kCanvasFCanvas = 0x70;

/* The canvas's width over its height when it is not 16:9; 0 when it is, or
 * is not a canvas yet. */
float canvas_aspect(uint32_t canvas, int* w, int* h)
{
    *w = canvas ? (int)vm_read32(canvas + kCanvasSizeX) : 0;
    *h = canvas ? (int)vm_read32(canvas + kCanvasSizeY) : 0;
    if (*w <= 0 || *h <= 0 || *w > 1280 || *h > 720) return 0.0f;
    const float ar = (float)*w / (float)*h;
    return fabsf(ar / (16.0f / 9.0f) - 1.0f) < 0.01f ? 0.0f : ar;
}

}  // namespace

#ifdef DOD3_FN_CANVAS_PROJECT
namespace {

/* FCanvas::FTransformEntry: an FMatrix (16-byte aligned) and its CRC */
constexpr uint32_t kEntrySize = 80, kEntryCrc = 64;
constexpr float kOffScreen = -10000.0f;   /* CalcTransformPosition's "not on the screen" */
constexpr uint32_t kRemapMark = 0x3A7EDA11u;   /* the radar's transform's depth (about 0.00097): its draws' mark */

float rdf(uint32_t a)
{
    const uint32_t w = vm_read32(a);
    float f;
    memcpy(&f, &w, 4);
    return f;
}

void wrf(uint32_t a, float f)
{
    uint32_t w;
    memcpy(&w, &f, 4);
    vm_write32(a, w);
}

struct Fit {
    bool on;                     /* between the HUD's begin and end, the top entry ours */
    bool full;                   /* a full-screen rectangle's transform */
    bool radar;                  /* the radar's: the 16:9 HUD's, marked */
    bool pause;                  /* the pause screen's: clipped at the HUD's sides */
    uint32_t canvas;             /* the HUD's UCanvas */
    uint32_t stack;              /* its FCanvas's TransformStack: a TArray (data, num, max) */
    int num;                     /* the stack's depth at the begin */
    uint32_t orig[kEntrySize / 4];
    uint32_t clip[2], size[2];   /* the canvas's, put back at the end */
    float base[16];              /* the stack's bottom: the render target's projection */
    float bx, by, bw, bh;        /* the view, in the render target */
    float ox, oy, k;             /* where the HUD's 1280x720 goes, and its scale */
} s;
int s_stack_off = -1;            /* the TransformStack's offset in FCanvas, once found */

/* Each entry of the stack holds the whole transform, the render target's
 * projection included: the bottom one is that projection (a 2D one: 1280x720
 * to -1..1, y down), and UGameViewportClient's push of the move to the view's
 * corner (an absolute one) is that move times it. UE3's matrices take row
 * vectors: the move is the last row, and a = move x base is the base with
 * the move, through the base's scale, added to its last row. */
bool is_2d(const float* m)
{
    static const int zero[] = { 1, 2, 3, 4, 6, 7, 8, 9, 11, 14 };
    for (int i : zero)
        if (m[i] != 0.0f) return false;
    return m[0] != 0.0f && m[5] != 0.0f && m[10] == 1.0f && m[15] == 1.0f;
}

/* Is the top entry the base moved to a point of the render target? */
bool is_moved_base(uint32_t d, int n, float* base, float* x, float* y)
{
    float t[16];
    for (int i = 0; i < 16; i++) {
        base[i] = rdf(d + 4 * i);
        t[i] = rdf(d + (uint32_t)(n - 1) * kEntrySize + 4 * i);
    }
    if (!is_2d(base) || !is_2d(t) || t[0] != base[0] || t[5] != base[5]) return false;
    *x = (t[12] - base[12]) / base[0];
    *y = (t[13] - base[13]) / base[5];
    return *x > -0.5f && *x < 1280.0f && *y > -0.5f && *y < 720.0f;
}

/* The canvas's transform stack, found the first time by its top entry being
 * the move to the view's corner. */
bool find_stack(uint32_t fc, uint32_t* stack, int* num, float* base, float* x, float* y)
{
    auto at = [&](uint32_t off) {
        const uint32_t d = vm_read32(fc + off);
        const int n = (int)vm_read32(fc + off + 4), mx = (int)vm_read32(fc + off + 8);
        if (!d || (d & 3) || n < 2 || n > 32 || mx < n || mx > 4096) return false;
        if (!is_moved_base(d, n, base, x, y)) return false;
        *stack = fc + off;
        *num = n;
        return true;
    };
    if (s_stack_off >= 0) return at((uint32_t)s_stack_off);
    for (uint32_t off = 0; off < 0x100; off += 4)
        if (at(off)) {
            s_stack_off = (int)off;
            fprintf(stderr, "[hud] the canvas's transform stack: FCanvas +%#x\n", off);
            return true;
        }
    return false;
}

/* The top entry, if the stack is at the begin's depth (a push between two
 * statements of the script has been popped again). */
uint32_t top()
{
    const uint32_t d = vm_read32(s.stack);
    return (d && (int)vm_read32(s.stack + 4) == s.num) ? d + (uint32_t)(s.num - 1) * kEntrySize : 0;
}

/* The top entry: a scale by (sx, sy) and a move to (tx, ty), times the base. */
/* The top entry: a scale by (sx, sy) and a move to (tx, ty), times the base.
 * clip_y: the depth made y's place in the HUD's 720 lines (the 2D canvas is
 * drawn without a depth test), so that the hardware clips what is above or
 * below them -- what the HUD keeps parked out of sight there, such as a black
 * bar above the battle HUD. clip_x the same across: a wider view is clipped
 * at the HUD's sides only on the pause screen, which has no markers on the
 * view (in play they reach past the HUD's 1280), and parks a ring and an
 * arrow just outside it on its pages. DOD3_HUD_ZCLIP: 1 (the default) for a
 * depth of 0..1, 2 for -1..1, 0 not to clip. */
void set_top(float sx, float sy, float tx, float ty, uint32_t crc, bool clip_y, bool clip_x = false)
{
    const uint32_t e = top();
    if (!e) return;
    float m[16];
    memcpy(m, s.base, sizeof m);
    m[0] = sx * s.base[0];
    m[5] = sy * s.base[5];
    m[12] = tx * s.base[0] + s.base[12];
    m[13] = ty * s.base[5] + s.base[13];
    static const int zc = [] {
        const char* v = getenv("DOD3_HUD_ZCLIP");
        return v ? atoi(v) : 1;
    }();
    if (clip_y && zc == 1) {   /* z/w in 0..1 */
        m[6] = 1.0f / 720.0f;
        m[10] = 0.0f;
        m[14] = 0.0f;
    } else if (clip_y && zc == 2) {   /* z/w in -1..1 */
        m[6] = 2.0f / 720.0f;
        m[10] = 0.0f;
        m[14] = -1.0f;
    } else if (clip_x && zc == 1) {
        m[2] = 1.0f / 1280.0f;
        m[10] = 0.0f;
        m[14] = 0.0f;
    } else if (clip_x && zc == 2) {
        m[2] = 2.0f / 1280.0f;
        m[10] = 0.0f;
        m[14] = -1.0f;
    }
    for (uint32_t i = 0; i < 16; i++) wrf(e + 4 * i, m[i]);
    vm_write32(e + kEntryCrc, crc);
}

/* The fit; a view narrower than 16:9 leaves room above and below the HUD,
 * which is clipped, a wider one room at the sides, clipped on the pause
 * screen (s.pause). A transform of its own, a CRC of its own. */
void set_fit()
{
    const bool cx = s.pause && s.ox > s.bx + 0.5f;
    set_top(s.k, s.k, s.ox, s.oy, s.orig[kEntryCrc / 4] ^ (cx ? 0x48554432u : 0x48554431u), s.oy > s.by + 0.5f, cx);
}

/* The 16:9 HUD's own transform (the base: the canvas is the target), with the
 * mark for its depth in place of the canvas's z, which the HUD does not use. */
void set_radar()
{
    const uint32_t e = top();
    if (!e) return;
    for (uint32_t i = 0; i < 16; i++) wrf(e + 4 * i, s.base[i]);
    wrf(e + 40, 0.0f);
    vm_write32(e + 56, kRemapMark);
    vm_write32(e + kEntryCrc, s.orig[kEntryCrc / 4] ^ 0x52414452u);
    rsx_canvas_remap_set(kRemapMark, s.k, s.ox, s.oy);
}
void set_fill() { set_top(s.bw / 1280.0f, s.bh / 720.0f, s.bx, s.by, s.orig[kEntryCrc / 4] ^ 0x46554C4Cu, false); }

void begin(uint32_t canvas)
{
    s.on = s.full = s.radar = s.pause = false;
    const uint32_t fc = canvas ? vm_read32(canvas + kCanvasFCanvas) : 0;
    int w, h;
    const float ar = canvas_aspect(canvas, &w, &h), hd = 16.0f / 9.0f;
    if (!fc || ar == 0.0f) return;   /* a 16:9 view: the HUD fits as it is */
    float bx, by;
    if (!find_stack(fc, &s.stack, &s.num, s.base, &bx, &by)) {
        static bool told;
        if (!told) fprintf(stderr, "[hud] a %dx%d view, but no move at the top of its canvas's transforms\n", w, h);
        told = true;
        return;
    }
    s.canvas = canvas;
    const uint32_t e = top();
    for (uint32_t i = 0; i < kEntrySize / 4; i++) s.orig[i] = vm_read32(e + 4 * i);
    s.bx = bx;
    s.by = by;
    s.bw = (float)w;
    s.bh = (float)h;
    if (ar > hd) {   /* wider: the HUD as tall as the view, in its middle */
        s.k = s.bh / 720.0f;
        s.ox = bx + (s.bw - 1280.0f * s.k) / 2;
        s.oy = by;
    } else {         /* narrower: as wide */
        s.k = s.bw / 1280.0f;
        s.ox = bx;
        s.oy = by + (s.bh - 720.0f * s.k) / 2;
    }
    /* The HUD's own code (the native part of it) places some things by the
     * canvas's size; it is told the 1280x720 it was made for. */
    for (uint32_t i = 0; i < 2; i++) {
        s.clip[i] = vm_read32(canvas + kCanvasClipX + 4 * i);
        s.size[i] = vm_read32(canvas + kCanvasSizeX + 4 * i);
    }
    wrf(canvas + kCanvasClipX, 1280.0f);
    wrf(canvas + kCanvasClipY, 720.0f);
    vm_write32(canvas + kCanvasSizeX, 1280);
    vm_write32(canvas + kCanvasSizeY, 720);
    set_fit();
    s.on = true;
    static float told_k;
    if (told_k != s.k)
        fprintf(stderr, "[hud] a %dx%d view at %g,%g: the HUD at %g,%g, x%.4f\n", w, h, bx, by, s.ox, s.oy, s.k);
    told_k = s.k;
}

void end()
{
    if (!s.on) return;
    s.on = s.full = s.radar = s.pause = false;
    if (const uint32_t e = top())
        for (uint32_t i = 0; i < kEntrySize / 4; i++) vm_write32(e + 4 * i, s.orig[i]);
    for (uint32_t i = 0; i < 2; i++) {
        vm_write32(s.canvas + kCanvasClipX + 4 * i, s.clip[i]);
        vm_write32(s.canvas + kCanvasSizeX + 4 * i, s.size[i]);
    }
}

/* A point projected onto the canvas's clip rectangle -- the view's place
 * for it, spread over (cx, cy) -> the one that lands on that place of the
 * view under the fit. */
void map_back(uint32_t a)
{
    const float cx = rdf(s.canvas + kCanvasClipX), cy = rdf(s.canvas + kCanvasClipY);
    if (!(cx > 0.0f && cy > 0.0f)) return;
    const float x = s.bx + rdf(a) * s.bw / cx, y = s.by + rdf(a + 4) * s.bh / cy;
    wrf(a, (x - s.ox) / s.k);
    wrf(a + 4, (y - s.oy) / s.k);
}

/* UCanvas::execProject(FFrame&, RESULT_DECL): the FVector it returns. */
void hook_canvas_project(ppu_context* ctx)
{
    const uint32_t self = (uint32_t)ctx->gpr[3], result = (uint32_t)ctx->gpr[5];
    DOD3_FN_CANVAS_PROJECT(ctx);
    dod3_drain(ctx);
    if (s.on && !s.full && !s.radar && self == s.canvas && result) map_back(result);
}

/* ASqex03GameHUD::CalcTransformPosition: the screen positions of the points
 * registered this frame, a TArray of (x, y). */
void hook_hud_calc_transform(ppu_context* ctx)
{
    const uint32_t hud = (uint32_t)ctx->gpr[3];
    DOD3_FN_HUD_CALC_TRANSFORM(ctx);
    dod3_drain(ctx);
    if (!s.on || s.full || s.radar || vm_read32(hud + DOD3_O_HUD_CANVAS) != s.canvas) return;
    const uint32_t d = vm_read32(hud + DOD3_O_HUD_SCREEN_POS);
    const int n = (int)vm_read32(hud + DOD3_O_HUD_SCREEN_POS + 4);
    for (int i = 0; d && i < n && i < 1024; i++) {
        const uint32_t a = d + 8u * (uint32_t)i;
        if (rdf(a) == kOffScreen && rdf(a + 4) == kOffScreen) continue;
        map_back(a);
    }
}

}  // namespace

void dod3_hud_aspect(int phase, uint32_t canvas, bool on)
{
    switch (phase) {
    case 1: {
        int w, h;
        const float ar = on ? canvas_aspect(canvas, &w, &h) : 0.0f;
        rsx_present_set_content_aspect(ar);
        static float told = -1.0f;
        static const bool log = getenv("DOD3_ASPECT_LOG") != nullptr;
        if (log && ar != told) fprintf(stderr, "[hud] the view: %.4f (%dx%d canvas)\n", ar, w, h);
        told = ar;
        begin(ar != 0.0f ? canvas : 0);
        break;
    }
    case 0: end(); break;
    case 2:
        if (s.on && !s.full) {
            set_fill();
            s.full = true;
        }
        break;
    case 3:
        if (s.on && s.full) {
            set_fit();
            s.full = false;
        }
        break;
    case 4:
        if (s.on && !s.full && !s.radar) {
            set_radar();
            s.radar = true;
        }
        break;
    case 5:
        if (s.on && s.radar) {
            set_fit();
            s.radar = false;
        }
        break;
    case 7:
    case 8:
        s.pause = phase == 7;
        if (s.on && !s.full && !s.radar) set_fit();
        break;
    }
}

extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));

void dod3_hud_aspect_install()
{
    ppu_register_function(DOD3_A_CANVAS_PROJECT, hook_canvas_project);
    ppu_register_function(DOD3_A_HUD_CALC_TRANSFORM, hook_hud_calc_transform);
}
#else
/* 1.00: the present is told the view's shape; the HUD is left as it is (its
 * markers' hooks are 1.01's). */
void dod3_hud_aspect(int phase, uint32_t canvas, bool on)
{
    int w, h;
    if (phase == 1) rsx_present_set_content_aspect(on ? canvas_aspect(canvas, &w, &h) : 0.0f);
}
void dod3_hud_aspect_install() {}
#endif
