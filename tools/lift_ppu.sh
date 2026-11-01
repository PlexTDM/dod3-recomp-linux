#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

PY=.venv/bin/python
[ -x "$PY" ] || PY=python3
T=ps3recomp-soyjxck/tools
LIFTER=$T/ppu_lifter.py
PPU_JOBS=${PPU_JOBS:-2}

"$PY" $T/ppu_loader.py elf/EBOOT_101.ELF -o out/v101/
"$PY" "$LIFTER" elf/EBOOT_101.ELF \
    --functions out/v101/EBOOT_101.functions.json \
    --hle-stubs out/v101/EBOOT_101.imports.json \
    --code-end 0x157fc00 \
    --hook 0x000C1E50 --hook 0x00EE7728 --hook 0x00272E58 \
    --jobs "$PPU_JOBS" \
    -o recompiled_101/
