# Build and run the native Linux game

This guide builds the native Linux game executable, `dod3`, from the
Drakengard 3 1.01 update. It uses your own game files and does not download or
include any game or PlayStation firmware data.

For the CMake options, the run forms and the committed tables, see
[BUILDING.md](BUILDING.md).

## Requirements

- 64-bit Linux, GCC 12+ or Clang 14+, CMake 3.20+, Ninja, Python 3.11+ and Git.
- SDL2, Vulkan, glslang and zlib development packages, plus a Vulkan-capable
  driver. On Arch Linux:

  ```sh
  sudo pacman -S --needed base-devel cmake ninja python git zlib sdl2 \
      vulkan-headers vulkan-icd-loader glslang
  ```

- A CPU with AVX2, FMA and BMI2 for the default x86-64-v3 build.
- About 20 GB of free space for the installed game files and generated build
  outputs, in addition to the source ISO and update package.
- The US Drakengard 3 disc as a decrypted ISO or folder, and the genuine 1.01
  update package. Optional DLC packages can be added during installation.
- Your own decrypted `libsre.prx` from your PS3 firmware's `dev_flash`: a plain
  ELF64 big-endian PPC64 PRX (ELF type `0xFFA4`, machine `0x0015`), not the
  SELF-wrapped `SCE` firmware file. Put it at `fw_spu/libsre.prx`, or pass its
  path to CMake as shown below. Firmware files are not included; see
  [ps3recomp's firmware notes](https://github.com/PlexTDM/ps3recomp/blob/linux-fixes/docs/FIRMWARE_LLE.md).
- A checkout of the `linux-fixes` branch of
  [PlexTDM/ps3recomp](https://github.com/PlexTDM/ps3recomp): the runtime and
  Vulkan backend the game is built from, and the lifting tools.

## Prepare the source

Clone the project, then the runtime checkout beside it:

```sh
git clone https://github.com/PlexTDM/dod3-recomp-linux.git
cd dod3-recomp-linux
git clone --branch linux-fixes https://github.com/PlexTDM/ps3recomp.git ps3recomp-soyjxck
python3 -m venv .venv
.venv/bin/python -m pip install -r ps3recomp-soyjxck/tools/requirements.txt \
    -r tools/requirements.txt
```

`ps3recomp-soyjxck/` is git-ignored on purpose: it is a separate source
checkout, not part of this repository. Stay on `linux-fixes`; the port is
tested against `042e13a5`. Everything the build needs comes from it: the
lifters, the package extractor, `runtime/ppu/ppu_vm_fast.h` and the Vulkan
backend.

## Install your game files

The game files have to be in place before the first lift, so use the
installer-only target, which needs none of the game's code:

```sh
tools/build_linux_setup.sh
```

Install to the repository root:

```sh
./build-installer/dod3-setup --install --base "$PWD" \
    "/path/Drakengard 3 (USA) (En,Fr).iso" \
    "/path/UP0082-BLUS31197_00-DOD3PATCH0000000-A0101-V0101-PE.pkg"
```

The installer writes the game layout under `game/` and the decrypted 1.01
executable under `elf/EBOOT_101.ELF`. The ISO must be decrypted; ordinary
encrypted disc images are refused. Add optional DLC package paths after the
update package, and replace `--base "$PWD"` to use another folder.
`--check <file>...` identifies files without installing them.

## Lift the game code

```sh
sh tools/lift_ppu.sh
tools/lift_spu.sh
```

The first lifts the PPU code from the 1.01 executable, the second the SPU
programs and their native fast-path hooks. They create `recompiled_101/`,
`spu/` and `out/`, none of it committed. The PPU helper defaults to two worker
processes to limit memory use; `PPU_JOBS=1` lowers it further.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPS3RECOMP_DIR="$PWD/ps3recomp-soyjxck" \
    -DLIBSRE_PRX="$PWD/fw_spu/libsre.prx"
cmake --build build
```

The result is `build/dod3`, rendering through the runtime's Vulkan backend.
The build sizes itself to the machine (`DOD3_COMPILE_JOBS`), so no `--parallel`
is needed; see [BUILDING.md](BUILDING.md) for the options.

## Run

The installer put the game in the repository root, so run it with the same
base:

```sh
DOD3_INSTALL_BASE=. build/dod3
```

To keep the game in `$XDG_DATA_HOME/dod3-recomp` (or
`~/.local/share/dod3-recomp`) instead, install there after building with
`build/dod3 --install <disc-or-folder> <update.pkg> [DLC.pkg ...]`, then run
`build/dod3` with no variable set.

There is no graphical launcher: the game's own Settings pages cover the
options, and `dod3.ini` in the install directory the rest.

## Frame capture

`PS3RECOMP_FRAME_GRAB=<prefix>` writes the next presented frame to
`<prefix>.ppm` when the file `<prefix>.req` exists; this is what
`tools/autoplay.sh` uses to find the battle HUD. For a hands-off chapter 1 run
with frame times:

```sh
tools/live_bench.sh mytest 120 AUTOPLAY_FORWARD=1 AUTOPLAY_COMBAT=1
```

The log is `out/bench/mytest.log`; the latest grabbed frame is
`out/bench/mytest.grab.ppm`.
