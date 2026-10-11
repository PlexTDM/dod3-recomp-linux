/*
 * Hot lifted functions, hooked (lifted with --hook <addr>; the lifted body is
 * func_<addr>_lifted): a native version where there is one, a timing wrapper
 * around whichever runs, and a check mode that runs both and compares.
 *
 *   func_00272E58  the float-heavy collision test that dominates the game
 *                  thread when scenery breaks up (32-37% of it there; 9-12%
 *                  in an ordinary battle). Native: src/patches/
 *                  collision_native.cpp, generated from the lifted body by
 *                  tools/lift_native.py.
 *
 * DOD3_COLLISION_NATIVE=1 (default) the native one, 0 the lifted one, check
 * runs the lifted one and then the native one on the same input and compares
 * the nine words they write and r3 (the first differences are printed, then
 * a count every 100,000 calls). A call that differs is run through the
 * lifted one once more: the second run starts from the memory the first left
 * (its frame, and whatever another thread wrote meanwhile), not from the
 * call's own, so where the lifted code then gives the native answer the
 * difference is the rerun's and not the native code's. The count says how
 * many were of each kind.
 *
 * DOD3_HOT_LOG=1 prints, every 5 s, the calls per second and the cycles per
 * call, so a code-generation change can be measured on the function it
 * targets rather than on the whole frame.
 */
#include "cpu/ppu.h"
#include "cpu/eboot.h"   /* the EBOOT version's addresses */
#include "cpu/cycles.h"
#include "util.h"
#include "ppu_vm_fast.h"  /* g_ppu_vm_page, for the check mode's diagnostic */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dod3_collision_native(ppu_context* ctx);   /* src/patches/collision_native.cpp */

namespace {
struct HotStat {
    const char* name;
    uint64_t calls, cycles;
};
HotStat s_hot[] = { { "collision test", 0, 0 } };

int hot_log(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("DOD3_HOT_LOG");
        on = (e && *e != '0') ? 1 : 0;
    }
    return on;
}

}  // namespace

/* -1 until the first call reads DOD3_COLLISION_NATIVE; main.cpp's A/B switch
 * "collnative" sets it to 0 or 1 in a run. */
extern "C" int g_dod3_collision_native = -1;

namespace {
int collision_mode(void)
{
    if (g_dod3_collision_native < 0) {
        const char* e = getenv("DOD3_COLLISION_NATIVE");
        g_dod3_collision_native = !e ? 1 : !strcmp(e, "check") ? 2 : atoi(e) ? 1 : 0;
    }
    return g_dod3_collision_native;
}

void hot_report(void)
{
    static uint64_t last = 0;
    const uint64_t now = dod3_now_us();
    if (!last) {
        last = now;
        return;
    }
    if (now - last < 5000000) return;
    const double secs = (now - last) / 1e6;
    last = now;
    for (HotStat& h : s_hot) {
        if (!h.calls) continue;
        fprintf(stderr, "[hot] %s: %.0f calls/s, %.0f cycles each, %.1f Mcycles/s (%s)\n", h.name, h.calls / secs,
                (double)h.cycles / (double)h.calls, h.cycles / secs / 1e6,
                collision_mode() == 1 ? "native" : collision_mode() == 2 ? "check" : "lifted");
        h.calls = 0;
        h.cycles = 0;
    }
}

/* The test writes through two pointers passed on the stack: six words at the
 * one at sp+0x84 and three at the one at sp+0x7C (the caller's parameter
 * area; sp is r1 at entry). Both are read after the lifted run and after the
 * native run and compared, with r3, the result. */
void collision_check(ppu_context* ctx)
{
    static uint64_t calls = 0, bad = 0, rerun = 0, shown = 0;
    const uint64_t sp = ctx->gpr[1];
    const uint32_t out_a = vm_read32(sp + 0x84), out_b = vm_read32(sp + 0x7C);
    ppu_context saved;
    memcpy(&saved, ctx, sizeof saved);

    DOD3_FN_COLLISION_TEST_LIFTED(ctx);
    dod3_drain(ctx);
    uint32_t la[6], lb[3];
    for (int i = 0; i < 6; i++) la[i] = vm_read32(out_a + 4u * i);
    for (int i = 0; i < 3; i++) lb[i] = vm_read32(out_b + 4u * i);
    const uint64_t lr3 = ctx->gpr[3];

    memcpy(ctx, &saved, sizeof saved);
    dod3_collision_native(ctx);
    uint32_t na[6], nb[3];
    for (int i = 0; i < 6; i++) na[i] = vm_read32(out_a + 4u * i);
    for (int i = 0; i < 3; i++) nb[i] = vm_read32(out_b + 4u * i);

    calls++;
    const bool differ = memcmp(la, na, sizeof la) || memcmp(lb, nb, sizeof lb) || lr3 != ctx->gpr[3];
    if (differ) {
        bad++;
        /* The lifted one again, from where the native one started (see the head
         * of the file); then the native run's registers and words are put back. */
        ppu_context native;
        memcpy(&native, ctx, sizeof native);
        memcpy(ctx, &saved, sizeof saved);
        DOD3_FN_COLLISION_TEST_LIFTED(ctx);
        dod3_drain(ctx);
        uint32_t ra[6], rb[3];
        for (int i = 0; i < 6; i++) ra[i] = vm_read32(out_a + 4u * i);
        for (int i = 0; i < 3; i++) rb[i] = vm_read32(out_b + 4u * i);
        const uint64_t rr3 = ctx->gpr[3];
        const bool again = !memcmp(ra, na, sizeof ra) && !memcmp(rb, nb, sizeof rb) && rr3 == native.gpr[3];
        if (again) rerun++;
        memcpy(ctx, &native, sizeof native);
        for (int i = 0; i < 6; i++) vm_write32(out_a + 4u * i, na[i]);
        for (int i = 0; i < 3; i++) vm_write32(out_b + 4u * i, nb[i]);
        if (shown < 8) {
            shown++;
            fprintf(stderr, "[collision-check] call %llu differs%s: r3 %llu/%llu;", (unsigned long long)calls,
                    again ? " (not when the lifted one is run again)" : "", (unsigned long long)lr3,
                    (unsigned long long)ctx->gpr[3]);
            for (int i = 0; i < 6; i++)
                if (la[i] != na[i]) fprintf(stderr, " a[%d] %08X/%08X", i, la[i], na[i]);
            for (int i = 0; i < 3; i++)
                if (lb[i] != nb[i]) fprintf(stderr, " b[%d] %08X/%08X", i, lb[i], nb[i]);
            /* The pointer arguments and the page flags they land on: a page
             * without the fast-path flag reads through the slow path in the
             * lifted code and directly in the native one. */
            fprintf(stderr, "\n[collision-check]   args");
            for (int r = 3; r <= 10; r++) {
                const uint32_t p = (uint32_t)saved.gpr[r];
                fprintf(stderr, " r%d=%08X/%u", r, p, (unsigned)g_ppu_vm_page[p >> 16]);
            }
            for (int o = 0x74; o <= 0x84; o += 8) {
                const uint32_t p = vm_read32(sp + o);
                fprintf(stderr, " [sp+%X]=%08X/%u", o, p, (unsigned)g_ppu_vm_page[p >> 16]);
            }
            fprintf(stderr, "\n");
        }
    }
    if (calls % 100000 == 0)
        fprintf(stderr, "[collision-check] %llu calls, %llu differ, %llu of them not when the lifted one is run again\n",
                (unsigned long long)calls, (unsigned long long)bad, (unsigned long long)rerun);
}
}  // namespace

void DOD3_FN_COLLISION_TEST(ppu_context* ctx)
{
    const int mode = collision_mode();
    if (!hot_log()) {
        if (mode == 1) dod3_collision_native(ctx);
        else if (mode == 2) collision_check(ctx);
        else { DOD3_FN_COLLISION_TEST_LIFTED(ctx); dod3_drain(ctx); }
        return;
    }
    const uint64_t c0 = dod3_cycles();
    if (mode == 1) dod3_collision_native(ctx);
    else if (mode == 2) collision_check(ctx);
    else { DOD3_FN_COLLISION_TEST_LIFTED(ctx); dod3_drain(ctx); }
    s_hot[0].cycles += dod3_cycles() - c0;
    s_hot[0].calls++;
    hot_report();
}
