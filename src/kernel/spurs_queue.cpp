/*
 * SPURS queue (cellSpursQueue), PPU side, natively.
 *
 * Drakengard 3 pairs SPU tasks with SPURS queues whose consumer is the game's
 * lifted SPU code, reading the queue straight out of guest memory. The
 * toolkit's HLE has no cellSpursQueuePopBody at all -- the game read an
 * uninitialised stack slot as the popped item and asked malloc for 3.8 GB --
 * and its initializer writes a layout of its own. This file implements the
 * three PPU-side entry points against the queue line the SPU code expects:
 *
 *   _cellSpursQueueInitialize  (spurs, taskset, queue, buffer, size, depth, dir)
 *   cellSpursQueuePushBody     (queue, buffer, isBlocking)
 *   cellSpursQueuePopBody      (queue, buffer, isPeek, isBlocking)
 *
 * The queue line keeps the head and tail at +0x00/+0x04, modulus/depth at
 * +0x0C, item size at +0x10, depth at +0x14, buffer EA at +0x18 and the
 * taskset at +0x70. Indices use libsre's complemented encoding when bit 31 is
 * set; the ring is 2*mod and holds at most mod items.
 *
 * A blocking call retries while the queue is full/empty (ERROR_AGAIN) or
 * contended (ERROR_BUSY) instead of parking on an lv2 event queue that the HLE
 * attach stub never creates.
 */
#include "ppu_recomp.h"
#include "ps3emu/nid.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <thread>
#include <atomic>

extern "C" {
typedef void (*hle_ctx_fn)(ppu_context*);
void ps3_hle_register_ctx(uint32_t nid, const char* name, hle_ctx_fn fn);
void spu_taskset_signal_task(uint32_t taskset_ea, uint32_t taskId);
int spurs_tasksets_on(uint32_t spurs_ea, uint32_t* out, int max);
extern uint8_t* vm_base;
}

enum : uint32_t {
    CELL_SPURS_TASK_ERROR_AGAIN = 0x80410901,
    CELL_SPURS_TASK_ERROR_BUSY = 0x8041090A,
    CELL_SPURS_TASK_ERROR_INVAL = 0x80410902,
    CELL_SPURS_TASK_ERROR_STAT = 0x8041090F,
    CELL_SPURS_TASK_ERROR_ALIGN = 0x80410910,
    CELL_SPURS_TASK_ERROR_NULL_POINTER = 0x80410911,
};

static inline uint32_t q_index(uint32_t v)
{
    return (v & 0x80000000u) ? ~v : v;
}

static uint32_t queue_params(uint32_t q, uint32_t* head, uint32_t* tail, uint32_t* mod, uint32_t* size, uint32_t* buf)
{
    *size = vm_read32(q + 0x10);
    *mod = vm_read32(q + 0x0C);
    if (!*mod) *mod = vm_read32(q + 0x14);
    *buf = (uint32_t)vm_read64(q + 0x18);
    *head = q_index(vm_read32(q + 0x00));
    *tail = q_index(vm_read32(q + 0x04));
    if (!*size || !*mod || !*buf) return CELL_SPURS_TASK_ERROR_INVAL;
    return 0;
}

static uint32_t queue_count(uint32_t head, uint32_t tail, uint32_t mod)
{
    const uint32_t ring = mod * 2u;
    return (tail + ring - head) % ring;
}

static void queue_initialize(ppu_context* ctx)
{
    const uint32_t spurs = (uint32_t)ctx->gpr[3];
    const uint32_t taskset = (uint32_t)ctx->gpr[4];
    const uint32_t q = (uint32_t)ctx->gpr[5];
    const uint32_t buf = (uint32_t)ctx->gpr[6];
    const uint32_t size = (uint32_t)ctx->gpr[7];
    const uint32_t depth = (uint32_t)ctx->gpr[8];
    const uint32_t dir = (uint32_t)ctx->gpr[9];

    uint32_t rc = 0;
    if (!q || !buf)
        rc = CELL_SPURS_TASK_ERROR_NULL_POINTER;
    else if ((q & 0x7F) || (buf & 0xF))
        rc = CELL_SPURS_TASK_ERROR_ALIGN;
    else if (!size || size > 0x4000 || (size & 0xF) || !depth || depth > 0x7FFF)
        rc = CELL_SPURS_TASK_ERROR_INVAL;
    if (rc) {
        ctx->gpr[3] = (int64_t)(int32_t)rc;
        fprintf(stderr, "[spurs-queue] init invalid q=0x%08X buf=0x%08X size=%u depth=%u -> 0x%08X\n",
                q, buf, size, depth, rc);
        return;
    }

    for (uint32_t o = 0; o < 128; o += 8) vm_write64(q + o, 0);
    vm_write32(q + 0x0C, depth);
    vm_write32(q + 0x10, size);
    vm_write32(q + 0x14, depth);
    vm_write64(q + 0x18, buf);
    vm_write64(q + 0x70, taskset ? taskset : spurs);
    memset(vm_base + buf, 0, (size_t)size * depth);

    fprintf(stderr, "[spurs-queue] init taskset=0x%08X q=0x%08X buf=0x%08X size=%u depth=%u dir=%u -> 0\n",
            taskset, q, buf, size, depth, dir);
    ctx->gpr[3] = 0;
}

static uint32_t queue_push(ppu_context* ctx, bool blocking)
{
    const uint32_t q = (uint32_t)ctx->gpr[3];
    const uint32_t data = (uint32_t)ctx->gpr[4];
    if (!q || !data) return CELL_SPURS_TASK_ERROR_NULL_POINTER;
    if (q & 0x7F) return CELL_SPURS_TASK_ERROR_ALIGN;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10000);
    for (unsigned spins = 0;; spins++) {
        uint32_t head, tail, mod, size, buf;
        uint32_t rc = queue_params(q, &head, &tail, &mod, &size, &buf);
        if (rc) return rc;
        if (queue_count(head, tail, mod) < mod) {
            const uint32_t slot = tail % mod;
            memcpy(vm_base + buf + (size_t)slot * size, vm_base + data, size);
            const uint32_t next = (tail + 1u) % (mod * 2u);
            vm_write32(q + 0x04, next);
            static std::atomic<unsigned> n{0};
            const unsigned k = n.fetch_add(1, std::memory_order_relaxed);
            if (k < 16 || (k % 1000) == 0)
                fprintf(stderr, "[spurs-queue] push q=0x%08X slot=%u size=%u head=%u tail=%u -> 0\n",
                        q, slot, size, head, next);
            return 0;
        }
        if (!blocking) return CELL_SPURS_TASK_ERROR_AGAIN;
        if (std::chrono::steady_clock::now() >= deadline) {
            fprintf(stderr, "[spurs-queue] push q=0x%08X timed out head=%u tail=%u mod=%u\n", q, head, tail, mod);
            return CELL_SPURS_TASK_ERROR_AGAIN;
        }
        if (spins < 8) std::this_thread::yield();
        else std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

static uint32_t queue_pop(ppu_context* ctx, bool peek, bool blocking)
{
    const uint32_t q = (uint32_t)ctx->gpr[3];
    const uint32_t data = (uint32_t)ctx->gpr[4];
    if (!q || !data) return CELL_SPURS_TASK_ERROR_NULL_POINTER;
    if (q & 0x7F) return CELL_SPURS_TASK_ERROR_ALIGN;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10000);
    for (unsigned spins = 0;; spins++) {
        uint32_t head, tail, mod, size, buf;
        uint32_t rc = queue_params(q, &head, &tail, &mod, &size, &buf);
        if (rc) return rc;
        if (queue_count(head, tail, mod) != 0) {
            const uint32_t slot = head % mod;
            memcpy(vm_base + data, vm_base + buf + (size_t)slot * size, size);
            const uint32_t next = peek ? head : (head + 1u) % (mod * 2u);
            if (!peek) vm_write32(q + 0x00, next);
            static std::atomic<unsigned> n{0};
            const unsigned k = n.fetch_add(1, std::memory_order_relaxed);
            if (k < 16 || (k % 1000) == 0)
                fprintf(stderr, "[spurs-queue] pop q=0x%08X slot=%u size=%u head=%u tail=%u peek=%u -> 0\n",
                        q, slot, size, next, tail, peek ? 1u : 0u);
            return 0;
        }
        if (!blocking) return CELL_SPURS_TASK_ERROR_AGAIN;
        if (std::chrono::steady_clock::now() >= deadline) {
            fprintf(stderr, "[spurs-queue] pop q=0x%08X timed out head=%u tail=%u mod=%u\n", q, head, tail, mod);
            return CELL_SPURS_TASK_ERROR_AGAIN;
        }
        if (spins < 8) std::this_thread::yield();
        else std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

static void queue_push_body(ppu_context* ctx)
{
    const uint32_t rc = queue_push(ctx, (uint32_t)ctx->gpr[5] != 0);
    ctx->gpr[3] = (int64_t)(int32_t)rc;
}

static void queue_pop_body(ppu_context* ctx)
{
    const uint32_t rc = queue_pop(ctx, (uint32_t)ctx->gpr[5] != 0, (uint32_t)ctx->gpr[6] != 0);
    ctx->gpr[3] = (int64_t)(int32_t)rc;
}

/* ---- hooks the lifted libsre calls (see tools/gen_libsre.py) ------------- */

/* _cellSpursSendSignal(taskset, taskId) */
extern "C" void dod3_spurs_send_signal(ppu_context* ctx)
{
    spu_taskset_signal_task((uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4]);
    ctx->gpr[3] = 0;
}

/* libsre 0x010134B8 (spurs, u32* taskset_out, wid): the taskset running as
 * workload `wid`. The HLE gives every taskset workload id 0, so the id cannot
 * be trusted to index anything; with more than one taskset on the instance
 * the first is the best answer available, and it is reported once. */
extern "C" void dod3_spurs_workload_taskset(ppu_context* ctx)
{
    const uint32_t spurs = (uint32_t)ctx->gpr[3], out = (uint32_t)ctx->gpr[4];
    const uint32_t wid = (uint32_t)ctx->gpr[5];
    uint32_t ts[16];
    const int n = spurs_tasksets_on(spurs, ts, 16);
    if (!n) {
        ctx->gpr[3] = (int64_t)(int32_t)0x80410711u;
        return;
    }   /* SRCH */
    if (n > 1) {
        static int s_once = 0;
        if (!s_once++)
            fprintf(stderr, "[spurs-queue] workload %u on spurs 0x%08X: %d tasksets, using the first\n", wid, spurs, n);
    }
    if (out) vm_write32(out, ts[0]);
    ctx->gpr[3] = 0;
}

/* sysPrxForUser _sys_memcpy(dst, src, n) -> dst, within guest memory. */
extern "C" void dod3_libsre_memcpy(ppu_context* ctx)
{
    const uint32_t dst = (uint32_t)ctx->gpr[3], src = (uint32_t)ctx->gpr[4];
    const uint32_t n = (uint32_t)ctx->gpr[5];
    memmove(vm_base + dst, vm_base + src, n);
}

extern "C" void dod3_register_spurs_queue(void)
{
    ps3_hle_register_ctx(ps3_compute_nid("_cellSpursQueueInitialize"), "_cellSpursQueueInitialize", queue_initialize);
    ps3_hle_register_ctx(ps3_compute_nid("cellSpursQueuePushBody"), "cellSpursQueuePushBody", queue_push_body);
    ps3_hle_register_ctx(ps3_compute_nid("cellSpursQueuePopBody"), "cellSpursQueuePopBody", queue_pop_body);
}
