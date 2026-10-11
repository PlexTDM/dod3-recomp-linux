#!/usr/bin/env python3
"""lift_native.py -- a lifted PPU function as native C++, frame in host locals.

    python tools/lift_native.py recompiled_101/ppu_recomp_010.cpp func_00272E58 \
        --name dod3_collision_native -o src/patches/collision_native.cpp

The lifted body (func_<addr>_lifted, as ps3recomp's lifter writes it with
--nonvolatile-locals) is rewritten in two steps:

  1. ps3recomp/tools/ppu_frame_locals.py promotes the frame's own slots --
     callee-saved register saves and restores, the LR word, the compiler's
     spill slots -- to host locals, with its must-define dataflow deciding
     what is safe (a slot read before it is written on some path, or shared
     with a callee through a pointer, stays in memory);
  2. the loads and stores that remain read guest memory directly, without
     the page test (the data is never a mapped register); the floating-point
     helpers are copied from the lifter's preamble, so every result is
     bit-identical to the lifted code, NaNs included; calls stay calls into
     the lifted code.

What stays is the arithmetic, the compares, the branches and the calls. The
output is meant to be checked against the lifted function at run time (a
check mode in the caller) before it replaces it, and compiled with the
lifted translation units' floating-point options (CMakeLists.txt does).

Only the statement shapes the collision test uses are handled; anything else
stops the translation with the offending line, so a new target function
extends this tool rather than silently miscompiling.
"""
import argparse, os, re, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ps3recomp", "tools"))
import ppu_frame_locals  # noqa: E402

ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
ap.add_argument("lifted", help="the lifted translation unit holding the function")
ap.add_argument("func", help="function name, e.g. func_00272E58 (its _lifted body is used)")
ap.add_argument("--name", required=True, help="the native function's name")
ap.add_argument("--lifter", default="ps3recomp/tools/ppu_lifter.py", help="where the FP helpers are copied from")
ap.add_argument("--inline-zero", metavar="CALLEE", default="",
                help="a callee that zeroes r4, stores it to the four words at r3 and returns (the title's "
                     "16-byte clear): its call, with the register-file round trip a lifted call costs, becomes "
                     "that assignment and the four stores")
ap.add_argument("-o", "--out", required=True)
a = ap.parse_args()

src = open(a.lifted, encoding="utf-8", errors="replace").read().split("\n")   # comments carry raw bytes
start = next(i for i, l in enumerate(src) if l.startswith(f"void {a.func}_lifted(ppu_context* ctx)"))
end = next(i for i in range(start + 1, len(src)) if src[i].startswith("}"))
body = [l.strip() for l in src[start + 1:end]]

# --- 1. the frame -------------------------------------------------------------
stats = ppu_frame_locals.Stats()
body = ppu_frame_locals.promote(body, False, False, stats)
if not stats.done:
    sys.exit(f"ppu_frame_locals left the function alone: {stats.bail}")
frame_left = sum(1 for l in body for _ in re.finditer(r"vm_(?:read|write)\d+\(r1 \+", l))

# --- 2. memory and helpers ----------------------------------------------------
lifter = open(a.lifter, encoding="utf-8").read().split("\n")
h0 = next(i for i, l in enumerate(lifter) if l.startswith("/* PPC FP NaN semantics"))
h1 = next(i for i in range(h0, len(lifter)) if lifter[i].startswith("static inline double ppu_frsp"))
h1 = next(i for i in range(h1, len(lifter)) if lifter[i].startswith("}")) + 1
helpers = lifter[h0:h1]

calls = set()
inlined = 0
out = []
for l in body:
    if not l: continue
    if a.inline_zero and f"{a.inline_zero}(ctx);" in l:
        # The callee's whole body, its `li r4, 0` included: the caller's r4 is whatever it last held (0x1F0
        # in the collision test), and r4 is 0 after the call. Without the assignment the 16 bytes were that
        # value four times -- denormals as floats, which a thread with denormals-are-zero set computes with
        # as zero, so the outputs matched on x86 and 0.8% of calls differed on arm64.
        out.append("r4 = (int64_t)(int32_t)(0); st32(r3 + 0x0, (uint32_t)r4); st32(r3 + 0x4, (uint32_t)r4); "
                   "st32(r3 + 0x8, (uint32_t)r4); "
                   f"st32(r3 + 0xC, (uint32_t)r4);   /* {a.inline_zero}, inlined: r4 = 0, four words of it at r3 */")
        inlined += 1
        continue
    for m in re.finditer(r"\b(func_[0-9A-Fa-f]+)\(ctx\)", l):
        calls.add(m.group(1))
    if re.search(r"ps3_indirect_call|ps3_hle_call|lv2_syscall", l):
        sys.exit("a call this tool does not handle: " + l)
    l2 = re.sub(r"\{ uint32_t tmp = vm_read32\(([^)]*)\); float ftmp; memcpy\(&ftmp, &tmp, 4\); f(\d+) = ftmp; \}", r"f\2 = ldf(\1);", l)
    l2 = re.sub(r"\{ float ftmp = \(float\)f(\d+); uint32_t tmp; memcpy\(&tmp, &ftmp, 4\); vm_write32\(([^,]*), tmp\); \}", r"stf(\2, f\1);", l2)
    l2 = re.sub(r"\bvm_read(8|16|32|64)\(", r"ld\1(", l2)
    l2 = re.sub(r"\bvm_write(8|16|32|64)\(", r"st\1(", l2)
    l2 = l2.replace("DRAIN_TRAMPOLINE(ctx);", "dod3_drain(ctx);")   # the port's name for the lifted TUs' macro
    out.append(l2)

# --- write -------------------------------------------------------------------
w = []
w.append(f"/* {a.func} ({os.path.basename(a.lifted)}) as native C++: generated by tools/lift_native.py -- do not edit.")
w.append(" * The frame's slots are host locals (ppu_frame_locals.py), memory is read directly,")
w.append(" * the arithmetic is the lifter's own (bit-identical). See the tool's docstring. */")
w.append('#include "cpu/ppu.h"')
w.append('#include "ppu_vm_fast.h"   /* vm_base */')
w.append("#include <math.h>")
w.append("#include <stdint.h>")
w.append("#include <string.h>")
w.append("")
if calls:
    w.append(f"/* calls into the lifted code (declared by ppu_recomp.h): {', '.join(sorted(calls))} */")
w.append("")
w.append("namespace {")
w.append("#if defined(__clang__)")
w.append('#pragma clang diagnostic ignored "-Wunused-function"   /* the helper set is the lifter\'s whole one */')
w.append("#endif")
w.append("#if defined(__GNUC__) || defined(__clang__)")
w.append("#define PPU_UNLIKELY(x) __builtin_expect(!!(x), 0)")
w.append("#else")
w.append("#define PPU_UNLIKELY(x) (x)")
w.append("#endif")
w += helpers
w.append("")
w.append("inline uint8_t  ld8(uint64_t ea)  { return vm_base[(uint32_t)ea]; }")
w.append("inline uint16_t ld16(uint64_t ea) { uint16_t v; memcpy(&v, vm_base + (uint32_t)ea, 2); return __builtin_bswap16(v); }")
w.append("inline uint32_t ld32(uint64_t ea) { uint32_t v; memcpy(&v, vm_base + (uint32_t)ea, 4); return __builtin_bswap32(v); }")
w.append("inline uint64_t ld64(uint64_t ea) { uint64_t v; memcpy(&v, vm_base + (uint32_t)ea, 8); return __builtin_bswap64(v); }")
w.append("inline void st8(uint64_t ea, uint8_t v)   { vm_base[(uint32_t)ea] = v; }")
w.append("inline void st16(uint64_t ea, uint16_t v) { v = __builtin_bswap16(v); memcpy(vm_base + (uint32_t)ea, &v, 2); }")
w.append("inline void st32(uint64_t ea, uint32_t v) { v = __builtin_bswap32(v); memcpy(vm_base + (uint32_t)ea, &v, 4); }")
w.append("inline void st64(uint64_t ea, uint64_t v) { v = __builtin_bswap64(v); memcpy(vm_base + (uint32_t)ea, &v, 8); }")
w.append("inline double ldf(uint64_t ea) { const uint32_t v = ld32(ea); float f; memcpy(&f, &v, 4); return f; }")
w.append("inline void stf(uint64_t ea, double d) { const float f = (float)d; uint32_t v; memcpy(&v, &f, 4); st32(ea, v); }")
w.append("}  // namespace")
w.append("")
w.append(f"void {a.name}(ppu_context* ctx)")
w.append("{")
for l in out:
    if re.match(r"^loc_[0-9A-Fa-f]+:$", l): w.append(l)
    else: w.append("    " + l)
w.append("}")
open(a.out, "w", encoding="utf-8", newline="\n").write("\n".join(w) + "\n")
print(f"{a.out}: {len(out)} statements; frame: {stats.saves} callee-save and {stats.spills} spill slots promoted "
      f"({stats.accesses} accesses), {frame_left} frame accesses left in memory; calls kept: {', '.join(sorted(calls)) or 'none'}"
      f"{f'; {inlined} call(s) inlined' if inlined else ''}")
