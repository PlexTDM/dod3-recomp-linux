/*
 * Loading in the frame limiter's sleep.
 *
 * A chapter's packages come in through Unreal's async loader, which
 * UObject::StaticTick runs once a frame for 5 ms at most -- and for less: it
 * gives the frame up whenever the read it needs next has not come back,
 * though here that takes a fraction of a millisecond. The rest of the frame
 * the game thread sleeps in the limiter. So a loading screen lasted a number
 * of frames, not an amount of work: 265 for the first chapter's, which is
 * 4.5 s at 60 fps, 2.9 s at a 120 cap and 9 s at the console's 30. (A larger
 * time limit in StaticTick changed nothing: it is the giving up that paces
 * it, and this build's loader has no "use the full time" argument.)
 *
 * While no scene is drawn -- a loading screen, a menu, a movie: a few dozen
 * draws a frame, where a scene is hundreds -- and packages are pending, the
 * limiter's sleep goes to the loader instead: ProcessAsyncLoading again, for
 * what is left of the sleep, as often as it gives up. It runs on the game
 * thread at the top of FEngineLoop::Tick, where nothing is ticking (as the
 * deferred purge of gc.cpp does), with the guest registers saved around it,
 * and the sleep is shortened by what it took: the frame keeps its time.
 * Without a frame cap, or in a frame over its budget, there is no sleep and
 * nothing changes.
 *
 * DOD3_LOAD_IDLE=0 turns it off; =2 does it with a scene on screen as well
 * (levels streaming in during play; not the default, being untried beyond
 * the first chapter). DOD3_LOAD_IDLE_LOG=1 says what each second of it did.
 */
#include "cpu/ppu.h"
#include "cpu/eboot.h"   /* the EBOOT version's addresses */
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <thread>

extern "C" uint32_t g_rsx_engine_frame;            /* rsx_draw_engine.c: the present count */
extern "C" volatile unsigned g_rsx_frame_draws;    /* and the draws of the last frame presented */

void dod3_load_in_limiter(ppu_context* ctx, uint64_t* usec)
{
#if defined(DOD3_FN_PROCESS_ASYNC_LOADING) && defined(DOD3_A_GOBJ_ASYNC_PACKAGES)
    static int mode = -1, log = 0;
    if (mode < 0) {
        const char* e = getenv("DOD3_LOAD_IDLE");
        mode = e && *e ? atoi(e) : 1;
        log = getenv("DOD3_LOAD_IDLE_LOG") ? 1 : 0;
    }
    if (mode <= 0) return;
    /* Three frames in a row without a scene start it; one frame of a scene
     * ends it. */
    static uint32_t last_frame = 0xFFFFFFFFu, quiet;
    const uint32_t frame = g_rsx_engine_frame;
    if (frame != last_frame) {
        last_frame = frame;
        quiet = g_rsx_frame_draws < 300 ? quiet + 1 : 0;
    }
    if (mode == 1 && quiet < 3) return;
    if (!vm_read32(DOD3_A_GOBJ_ASYNC_PACKAGES + 4)) return;
    /* A millisecond stays a sleep: the limiter's own margin, and the loader
     * ends a step after its limit, not at it. */
    if (*usec < 2000) return;
    const uint64_t t0 = dod3_now_us();
    const uint64_t deadline = t0 + (*usec > 50000 ? 50000 : *usec) - 1000;

    const ppu_context saved = *ctx;
    unsigned calls = 0;
    for (uint64_t now = t0; now + 300 < deadline && vm_read32(DOD3_A_GOBJ_ASYNC_PACKAGES + 4);) {
        ctx->gpr[1] = (saved.gpr[1] - 0x400) & ~0xFull;
        ctx->gpr[3] = 1;                         /* bUseTimeLimit */
        ctx->gpr[4] = 0;
        ctx->gpr[5] = 0;                         /* ExcludeType: NAME_None */
        ctx->fpr[1] = (double)(float)((double)(deadline - now) / 1e6);
        DOD3_FN_PROCESS_ASYNC_LOADING(ctx);
        dod3_drain(ctx);
        *ctx = saved;
        calls++;
        /* It gave up for a read: a moment for that to come back. */
        const uint64_t until = dod3_now_us() + 100;
        do { std::this_thread::yield(); now = dod3_now_us(); } while (now < until);
    }
    const uint64_t spent = dod3_now_us() - t0;
    *usec = spent >= *usec ? 0 : *usec - spent;

    if (log) {
        static uint64_t since, sum;
        static unsigned n, ncalls;
        n++; sum += spent; ncalls += calls;
        if (!since) since = t0;
        if (t0 - since >= 1000000) {
            fprintf(stderr, "[load] in the limiter's sleep: %u frames a second, %.1f ms and %.1f calls a frame\n",
                    n, (double)sum / 1000.0 / n, (double)ncalls / n);
            since = t0; sum = 0; n = ncalls = 0;
        }
    }
#else
    (void)ctx; (void)usec;
#endif
}
