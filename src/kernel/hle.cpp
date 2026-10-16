/*
 * The port's own HLE handlers, registered before the toolkit's.
 *
 * main.cpp calls ps3_load_prx_modules once, after the lifted function table
 * is registered and vm_base is live, before the game runs, and before
 * ppu_hle_init and ppu_sysprx_register: handlers registered here win over
 * theirs. No PRX image is loaded; the SPURS queues run libsre's own code,
 * lifted at build time (tools/gen_libsre.py).
 */

#include "ppu_recomp.h"
#include "ps3emu/nid.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
typedef void (*hle_ctx_fn)(ppu_context*);
void ps3_hle_register_ctx(uint32_t nid, const char* name, hle_ctx_fn fn);
extern uint8_t* vm_base;
void dod3_register_spurs_lfqueue(void);   /* src/kernel/spurs_lfqueue.cpp */
void dod3_register_spurs_queue(void);     /* src/kernel/spurs_queue.cpp */
}

/* cellFsGetFreeSize(path, u32* block_size, u64* free_block_count). The toolkit
 * answers 32 GiB free for any path. Asked about /dev_hdd1, the system cache,
 * that makes the game's FIOS start a disc-data cache there
 * (/dev_hdd1/cache/BLES00000/fios), which it never did here before the call
 * was implemented: the index is written with its header last, and a run that
 * ends by the window closing leaves an index without one, which a release
 * from before the toolkit clears the cache at mount then reads a sector at a
 * time, for good. The disc is a folder on the same drive anyway: the system
 * cache is reported full, and FIOS reads the disc directly, as it always
 * has in this port. Any other path gets the toolkit's answer. */
static void fs_get_free_size(ppu_context* ctx)
{
    const uint32_t path = (uint32_t)ctx->gpr[3], bs = (uint32_t)ctx->gpr[4], fb = (uint32_t)ctx->gpr[5];
    char p[32] = {0};
    for (int i = 0; path && i < 31 && vm_base[path + i]; i++) p[i] = (char)vm_base[path + i];
    const int sys_cache = strncmp(p, "/dev_hdd1", 9) == 0;
    { static int said;
      if (said < 4 && ++said)
          printf("[fs] GetFreeSize('%s'): %s\n", p, sys_cache ? "system cache, reported full" : "32 GiB free"); }
    if (bs) vm_write32(bs, 4096);
    if (fb) vm_write64(fb, sys_cache ? 0 : (32ull << 30) / 4096);
    ctx->gpr[3] = 0;
}

extern "C" void ps3_load_prx_modules(void)
{
    dod3_register_spurs_lfqueue();
    dod3_register_spurs_queue();
    ps3_hle_register_ctx(ps3_compute_nid("cellFsGetFreeSize"), "cellFsGetFreeSize", fs_get_free_size);
}
