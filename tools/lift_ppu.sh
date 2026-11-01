#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

PY=.venv/bin/python
[ -x "$PY" ] || PY=python3
LIFTER=ps3recomp/tools/ppu_lifter.py
HOOK_PATCH=patches/sp00nznet-ppu-lifter-hooks.patch
PPU_JOBS=${PPU_JOBS:-2}

if ! grep -Fq 'parser.add_argument("--hook"' "$LIFTER"; then
    (
        cd ps3recomp
        git apply --check "../$HOOK_PATCH"
        git apply "../$HOOK_PATCH"
    )
fi

"$PY" ps3recomp/tools/ppu_loader.py elf/EBOOT_101.ELF -o out/v101/
"$PY" "$LIFTER" elf/EBOOT_101.ELF \
    --functions out/v101/EBOOT_101.functions.json \
    --hle-stubs out/v101/EBOOT_101.imports.json \
    --code-end 0x157fc00 \
    --hook 0x000C1E50 --hook 0x00EE7728 --hook 0x00272E58 \
    --jobs "$PPU_JOBS" \
    -o recompiled_101/
