/*
 * The UnrealScript VM, as much of it as a replaced native needs.
 *
 * A native's exec thunk is called with its object (r3), the caller's FFrame
 * (r4: Object at +0x14, Code at +0x18) and where the result goes (r5). It
 * evaluates its own arguments out of the caller's bytecode, one FFrame::Step
 * each -- the native for the opcode at Code, from GNatives -- and steps over
 * the EndFunctionParms that closes them. A hook does the same, and may hand
 * the original thunk values it has already evaluated as constants in a few
 * bytes of bytecode of its own.
 */
#pragma once
#include <string.h>
#include "cpu/eboot.h"   /* the EBOOT version's addresses */
#include "cpu/ppu.h"

extern "C" void ps3_indirect_call(ppu_context* ctx);

namespace uscript {

const uint32_t kFrameObject = 0x14, kFrameCode = 0x18;
const uint8_t kIntConst = 0x1D, kFloatConst = 0x1E, kByteConst = 0x24, kEndParms = 0x16;

/* One FFrame::Step: the native for the opcode at Code, with the result
 * written to `result` (guest). GNatives is 8 bytes an opcode: function,
 * this-adjust. */
inline void step(ppu_context* ctx, uint32_t stack, uint32_t result)
{
    const uint32_t obj = vm_read32(stack + kFrameObject);
    const uint32_t code = vm_read32(stack + kFrameCode);
    const uint32_t op = vm_read8(code);
    vm_write32(stack + kFrameCode, code + 1);
    uint32_t fn = vm_read32(DOD3_A_GNATIVES + op * 8);
    const uint32_t self = obj + vm_read32(DOD3_A_GNATIVES + op * 8 + 4);
    if (fn & 1) fn = vm_read32(vm_read32(self) + fn - 1);   /* virtual: the vtable slot */
    ctx->gpr[2] = vm_read32(fn + 4);
    ctx->gpr[3] = self;
    ctx->gpr[4] = stack;
    ctx->gpr[5] = result;
    ctx->ctr = vm_read32(fn);
    ps3_indirect_call(ctx);
    dod3_drain(ctx);
}

/* Evaluate one argument of up to four bytes, in a frame of our own below
 * the caller's: its bytes as the VM stored them, the first one on top. */
inline uint32_t eval(ppu_context* ctx, uint32_t stack)
{
    const uint64_t sp = ctx->gpr[1];
    const uint32_t nsp = ((uint32_t)sp - 0x90u) & ~0xFu;
    vm_write64(nsp, sp);
    vm_write32(nsp + 0x80, 0);
    ctx->gpr[1] = nsp;
    step(ctx, stack, nsp + 0x80);
    ctx->gpr[1] = sp;
    return vm_read32(nsp + 0x80);
}
inline int32_t eval_int(ppu_context* ctx, uint32_t stack) { return (int32_t)eval(ctx, stack); }
inline uint8_t eval_byte(ppu_context* ctx, uint32_t stack) { return (uint8_t)(eval(ctx, stack) >> 24); }
inline float eval_float(ppu_context* ctx, uint32_t stack)
{
    const uint32_t bits = eval(ctx, stack);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

/* Is the next thing in the caller's bytecode the end of the arguments? */
inline bool at_end(uint32_t stack) { return vm_read8(vm_read32(stack + kFrameCode)) == kEndParms; }

inline float read_float(uint32_t ea)
{
    const uint32_t bits = vm_read32(ea);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
inline void write_float(uint32_t ea, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    vm_write32(ea, bits);
}

}  // namespace uscript
