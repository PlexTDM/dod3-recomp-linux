# Building

For the complete native Linux workflow, including installing your game files
before the first build, see [Building and running the native Linux game](BUILDING-LINUX-NATIVE.md).

The Linux port is built in three steps: the game's PPU code is lifted to C++,
the SPU programs are lifted, and the whole is compiled with CMake. Lifting
needs the game's executable, so you need your own copy of the game first.

Building the native recompiled game requires the game's lift and decrypted
`libsre.prx`, as described below.

## Prerequisites

Linux (x86-64):

- GCC 12+ or Clang 14+, CMake 3.20+, Ninja, Python 3.11+, Git and zlib.
- SDL2, Vulkan and glslang development packages (`libsdl2-dev libvulkan-dev
  glslang-dev` on Debian/Ubuntu; `SDL2-devel vulkan-headers glslang-devel` on
  Fedora), plus a Vulkan-capable driver.

Clone the project, then the runtime fork beside it:

```sh
git clone https://github.com/PlexTDM/dod3-recomp-linux.git
cd dod3-recomp-linux
git clone --branch linux-fixes https://github.com/PlexTDM/ps3recomp.git ps3recomp-soyjxck
python3 -m venv .venv
.venv/bin/pip install -r ps3recomp-soyjxck/tools/requirements.txt -r tools/requirements.txt
```

Everything the build needs comes from `ps3recomp-soyjxck/`: the lifters, the
package extractor, `runtime/ppu/ppu_vm_fast.h` and the Vulkan backend. It is
git-ignored, and the port is tested against `linux-fixes` at `ee6d448`. Your
own decrypted `libsre.prx` goes in `fw_spu/`, also git-ignored. The build uses
the venv's Python for the code it generates.

## The game's files

The port is built from the game's 1.01 update: its executable, decrypted, as
`elf/EBOOT_101.ELF` (SHA-256 `ed89e80f2336a948…`, 26,872,424 bytes). The
installer makes it from the update package, and lays out `game/disc` from
your disc or the PSN release's game package at the same time. Any built `dod3` can do that into the
repository root before the first lift -- a release build, or a build from
another machine:

```sh
DOD3_INSTALL_BASE=. path/to/dod3 --install <disc .iso or folder, or PSN game .pkg> <update .pkg> [DLC .pkg ...]
```

Any other decrypted copy of the update's EBOOT.BIN with that hash works too
(RPCS3's *Utilities > Decrypt PS3 Binaries*, for one). `dod3 --check
<file>...` says what the installer makes of a file.

## Lifting the PPU code

```sh
sh tools/lift_ppu.sh
```

- `--code-end` is the end of the last executable section, so `.rodata` in the
  R-X segment is never promoted to functions.
- The helper applies the port's small compatibility patch to enable the three
  `--hook`s required for native replacements: the garbage collector and its
  reachability pass (`src/patches/gc.cpp`, `src/patches/gc_native.cpp`) and
  the collision test's timing wrapper (`src/cpu/hot.cpp`). It emits the lifted
  bodies as `func_<addr>_lifted`; a lift without these hooks fails to link.

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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPS3RECOMP_DIR="$PWD/ps3recomp-soyjxck" \
    -DLIBSRE_PRX="$PWD/fw_spu/libsre.prx"
cmake --build build
```

The build selects the Vulkan backend of the runtime in `PS3RECOMP_DIR`
(the fork's `linux-fixes` branch). Use
`-DPS3RECOMP_RSX_VULKAN=OFF` only for a headless build. glslang is needed to
translate the game's guest shaders.

A clean build is nearly all lifted translation units: 20-45 MB of generated
C++ each, several minutes and several GiB of compiler memory apiece. The build
caps how many compile at once to what the machine can hold
(`DOD3_COMPILE_JOBS` below) and builds them at `-O2` (`DOD3_RECOMP_OPT`), so
plain `cmake --build build` needs no `--parallel`, and more cores than the
pool size will not make it faster. The largest unit takes about 10 minutes
with GCC, and a header every lifted unit includes (`ppu_recomp.h`,
`ppu_vm_fast.h`) rebuilds all of them. Clang builds it in about two thirds of
the time and half the memory; configure a fresh folder with
`-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` (`clang++`, not
`clang`: only the C++ driver links libstdc++), and keep the same compiler in a
build folder. Stop any build already running before starting another: two
ninja processes in one folder overcommit the machine, and the kernel kills
compilers. On repeat builds, ccache and lld are worth configuring once:

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld
```

A build folder keeps `RECOMP_DIR` in its cache: a folder configured for another
lift must be removed, or given `-DRECOMP_DIR=` explicitly (the scripts do).

Options:

| Option | Default | |
|---|---|---|
| `DOD3_EBOOT` | 101 | the version the lift is from. 100 builds the disc version instead (`elf/EBOOT.ELF`, `recompiled/`, `src/cpu/eboot.h`'s other column); kept for comparison, not maintained |
| `RECOMP_DIR` | `recompiled_101` | the lifted PPU C++ |
| `DOD3_X86_LEVEL` | x86-64-v3 | the lifted code's x86 level; empty for the baseline. The default needs AVX2, FMA and BMI2, and the executable checks for them at start |
| `DOD3_INLINE_VM` | ON | inline the guest-memory fast path into the lifted code |
| `DOD3_RECOMP_OPT` | `-O2` | optimization of the lifted PPU sources. Compile time is what it changes, not memory: the largest unit takes 13.5 minutes at `-O3`, 10 at `-O2` and 5.3 at `-O1` with GCC, and game performance moves the other way |
| `DOD3_COMPILE_JOBS` | 0 (automatic) | lifted units compiled at once (the `dod3_compile` job pool), sized from the lift's largest unit, the machine's RAM and the compiler; a positive number pins it. Raise it if the machine has RAM to spare, lower it if the build swaps |
| `LIBSRE_PRX` | `fw_spu/libsre.prx` | the module the SPURS queue paths are lifted from at build time (`tools/gen_libsre.py`) |

The replay tool (`rsx_replay`, for the rendering regression) is not part of
the default target: `cmake --build build --target rsx_replay`.

## Running

Running `dod3` starts the game. The data directory defaults to
`$XDG_DATA_HOME/dod3-recomp` or `~/.local/share/dod3-recomp`;
`DOD3_INSTALL_BASE=<dir>` selects another, such as the repository root:

```sh
DOD3_INSTALL_BASE=. build/dod3
build/dod3 --install <decrypted-disc-or-folder> <update.pkg> [DLC.pkg ...]
```

The developer form takes the executable to run, with the environment it
needs:

```sh
PS3_TITLE="Drakengard 3" PS3_VFS_ROOT=game/disc DOD3_FPS=60 build/dod3 elf/EBOOT_101.ELF
tools/run_timed.sh 120 out/run.log        # a bounded run with a log
```

`dod3.ini` is read from the install directory; a variable set in the
environment wins over it.

## Packaging a release

`tools/package_linux.sh` writes `dist/dod3-recomp-<version>-linux-x86_64.tar.gz`:
the executable, the default `dod3.ini`, the player guide and the licences.
Nothing of the game goes in it -- players run `dod3 --install` in the folder
the archive unpacks to. It refuses a binary that needs more than glibc,
libstdc++, zlib and SDL2, and prints the newest glibc it asks for.

```sh
bash tools/package_linux.sh v0.5.0              # build/dod3
bash tools/package_linux.sh v0.5.0 build-linux  # another build folder
```

## Regenerating the committed tables

| File | Tool |
|---|---|
| `src/patches/menu_patch_data_101.h` | `python tools/menu_patch.py --eboot=101 --header=src/patches/menu_patch_data_101.h` (`src/patches/menu_patch_data.h` without `--eboot`, for 1.00) |
| `src/apu/mp3dec_tables.h` | `python tools/gen_mp3_tables.py <minimp3.h> > src/apu/mp3dec_tables.h` |
| `src/apu/mp3_standin.h` | `tools/lift_spu.sh` (through `tools/make_spu_overlays.py standin`) |
| `src/res/prompts/*.png` | `python tools/prompt_atlas.py --font Roboto-Bold.ttf` (the button-prompt atlases and the sheet of key caps the keyboard's is made from; the build embeds them as `gen/prompts_data.h`) |
| `src/patches/collision_native.cpp` | `python tools/lift_native.py <lift>/ppu_recomp_010.cpp func_00272E58 --name dod3_collision_native --inline-zero func_00252A70 -o src/patches/collision_native.cpp`, from a lift made with `PPU_NV_DIRTY=0` in the environment into a folder of its own (`-o <lift>/`): the tool reads the form in which every register is written back at every call, not the one the game is built from. The lifted body may be in another unit (`grep -l func_00272E58_lifted <lift>/*.cpp`) |

## The Linux installer (`dod3-setup`)

`-DDOD3_SETUP_ONLY=ON` builds `dod3-setup` alone: a GUI installer for the
Windows release, run under Wine or Proton, that needs none of the game's code.
It is what a machine without a lift uses to lay out the game files, and it is
not part of the native game.

```sh
tools/build_linux_setup.sh            # build-installer/dod3-setup
tools/build_linux_setup.sh --run --base /path/to/release
./build-installer/dod3-setup --check <file>...
```

`tools/build_linux_setup.sh` takes `--build-dir DIR`, `--debug`, `--clean`,
`--run` and `--base DIR`. The file pickers use `zenity`, `kdialog` or `yad`
when one is installed.
