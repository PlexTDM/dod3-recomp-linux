# Building

The port is built in three steps: the game's PPU code is lifted to C++, the
SPU programs are lifted, and the whole is compiled with CMake. Lifting needs
the game's executable, so you need your own copy of the game first.

The Linux installer (`dod3-setup`) does not need the lift; see
[BUILDING-LINUX.md](BUILDING-LINUX.md).

## Prerequisites

Linux x86-64:

- GCC 12+ or Clang 15+, CMake 3.20 or later, Ninja, Python 3.11 or later,
  Git.
- SDL2, zlib, Vulkan headers, and glslang built with SPIRV-Tools (the Vulkan
  backend translates the guest's shaders to HLSL and then to SPIR-V).
- A Vulkan-capable GPU and driver; `vulkaninfo --summary` should list a
  device.
- A CPU with AVX2 (Intel Haswell / AMD Zen or newer).

Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake ninja git python \
    sdl2 vulkan-headers vulkan-icd-loader glslang spirv-tools zlib
```

Debian/Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build pkg-config python3 \
    libsdl2-dev libvulkan-dev glslang-dev spirv-tools zlib1g-dev
```

Then:

```sh
git clone --recurse-submodules https://github.com/soyjxck/dod3-recomp
cd dod3-recomp
python3 -m venv .venv
.venv/bin/pip install -r ps3recomp/tools/requirements.txt -r tools/requirements.txt
```

CMake uses the venv's Python for the code it generates at build time.

## The game's files

The port is built from the game's 1.01 update: its executable, decrypted, as
`elf/EBOOT_101.ELF` (SHA-256 `ed89e80f2336a948…`, 26,872,424 bytes). The
installer makes it from the update package, and lays out `game/disc` from
your disc or the PSN release's game package at the same time. Any built `dod3`
or `dod3-setup` can do that into the repository root before the first lift:

```sh
DOD3_INSTALL_BASE=. path/to/dod3 --install <disc .iso or folder, or PSN game .pkg> <update .pkg> [DLC .pkg ...]
```

Any other decrypted copy of the update's EBOOT.BIN with that hash works too
(RPCS3's *Utilities > Decrypt PS3 Binaries*, for one). `dod3 --check
<file>...` says what the installer makes of a file.

## Lifting the PPU code

```sh
.venv/bin/python ps3recomp/tools/ppu_loader.py elf/EBOOT_101.ELF -o out/v101/
.venv/bin/python ps3recomp/tools/ppu_lifter.py elf/EBOOT_101.ELF \
    --functions out/v101/EBOOT_101.functions.json \
    --hle-stubs out/v101/EBOOT_101.imports.json \
    --code-end 0x157fc00 \
    --nonvolatile-locals \
    --hook 0x000C1E50 --hook 0x00EE7728 --hook 0x00272E58 \
    -o recompiled_101/
```

- `--code-end` is the end of the last executable section, so `.rodata` in the
  R-X segment is never promoted to functions.
- `--nonvolatile-locals` keeps the guest registers in C locals, written back
  to the register file only where they changed and something reads them
  (`ps3recomp/docs/PPU_RECOMP.md`); the port is tuned for it.
- The three `--hook`s are required: those functions are supplied natively
  (the garbage collector and its reachability pass, `src/patches/gc.cpp` and
  `src/patches/gc_native.cpp`; the collision test's timing wrapper,
  `src/cpu/hot.cpp`) and the lifted bodies are emitted as
  `func_<addr>_lifted`. A lift without them fails to link.

The lift takes a few minutes and writes about 600 MB of C++ (14 translation
units). `recompiled_101/` is not in git.

## Lifting the SPU programs

```sh
tools/lift_spu.sh
```

extracts the SPU programs embedded in `elf/EBOOT_101.ELF` (the SPURS tasks,
the raw ShaderPatching job, MultiStream and its DSP plugins) and lifts them
into `spu/`, with the native fast paths hooked in (`--native-hook`). It also
writes the stand-in MP3 decoder image (`tools/make_spu_overlays.py`). Lifts
already in `spu/` are kept, so a second run only redoes what changed.

## Compiling

```sh
cmake -S . -B build-native -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPS3RECOMP_RSX_VULKAN=ON \
    -DDOD3_LIBSRE_STUB=ON
cmake --build build-native -j3
```

A clean build takes about 30 minutes, nearly all of it the lifted translation
units; a header every lifted unit includes (`ppu_recomp.h`, `ppu_vm_fast.h`)
rebuilds all of them. Each lifted TU can need several gigabytes at `-O1`, so
keep `-j` low. `-j2` is safe on a 16 GB machine, `-j3` on 24 GB or more; more
than that has OOM-killed `cc1plus` on the lifted files.

CMake options:

| Option | Default | |
|---|---|---|
| `DOD3_EBOOT` | 101 | the version the lift is from. 100 builds the disc version instead (`elf/EBOOT.ELF`, `recompiled/`, `src/cpu/eboot.h`'s other column); kept for comparison, not maintained |
| `RECOMP_DIR` | `recompiled_101` | the lifted PPU C++ |
| `DOD3_INLINE_VM` | ON | inline the guest-memory fast path into the lifted code |
| `DOD3_SYMBOLS` | ON | debug info on the port's and the runtime's sources, for the profiler and the crash handler |
| `DOD3_LIBSRE_STUB` | ON | stub the non-zlib SPURS LFQueue pushes. Set to OFF with `-DLIBSRE_PRX=/path/to/libsre.prx` only if you have a decrypted `libsre.prx` |
| `LIBSRE_PRX` | the submodule's | the module the SPURS LFQueue push helpers are lifted from at build time (`tools/gen_libsre.py`) |
| `DOD3_RECOMP_OPT` | `-O1` on GCC | extra options for the lifted translation units alone; use a separate build folder |
| `DOD3_SETUP_ONLY` | OFF | build only `dod3-setup`, the Linux installer (see [BUILDING-LINUX.md](BUILDING-LINUX.md)) |

## Running

With no arguments, `dod3` runs the game installed next to it (the layout:
`game/`, `elf/`, `dod3.ini`, `gamedata/`, `cache/` beside the executable), and
opens the installer when it is not installed yet. `DOD3_INSTALL_BASE=<dir>`
points that at another folder -- the repository root, for a developer tree:

```sh
DOD3_INSTALL_BASE=game ./build-native/dod3
```

The developer form takes the executable to run, with the environment it
needs:

```sh
PS3_TITLE="Drakengard 3" PS3_VFS_ROOT=game/game/disc DOD3_FPS=60 \
    ./build-native/dod3 game/elf/EBOOT_101.ELF
tools/run_timed.sh 120 out/run.log        # a bounded run with a log
```

`dod3.ini` is read from the executable's folder (CMake copies the
repository's once into the build folder) or the current one; a variable set
in the environment wins over it.

## Regenerating the committed tables

| File | Tool |
|---|---|
| `src/patches/menu_patch_data_101.h` | `python tools/menu_patch.py --eboot=101 --header=src/patches/menu_patch_data_101.h` (`src/patches/menu_patch_data.h` without `--eboot`, for 1.00) |
| `src/apu/mp3dec_tables.h` | `python tools/gen_mp3_tables.py <minimp3.h> > src/apu/mp3dec_tables.h` |
| `src/apu/mp3_standin.h` | `tools/lift_spu.sh` (through `tools/make_spu_overlays.py standin`) |
| `src/res/prompts/*.png` | `python tools/prompt_atlas.py --font Roboto-Bold.ttf` (the button-prompt atlases and the sheet of key caps the keyboard's is made from; the build embeds them as `gen/prompts_data.h`) |
| `src/patches/collision_native.cpp` | `python tools/lift_native.py <lift>/ppu_recomp_010.cpp func_00272E58 --name dod3_collision_native --inline-zero func_00252A70 -o src/patches/collision_native.cpp`, from a lift made with `PPU_NV_DIRTY=0` in the environment into a folder of its own (`-o <lift>/`): the tool reads the form in which every register is written back at every call, not the one the game is built from. The lifted body may be in another unit (`grep -l func_00272E58_lifted <lift>/*.cpp`) |
