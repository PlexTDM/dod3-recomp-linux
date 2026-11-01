# Build and run the native Linux game

This guide builds the native Linux game executable from the Drakengard 3
1.01 update. It is separate from [the Linux setup-only guide](BUILDING-LINUX.md),
which builds an installer for the Windows release and does not build the game.

The build uses your own game files. It does not download or include any game
or PlayStation firmware data.

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
- Your own decrypted `libsre.prx` from your PS3 firmware's `dev_flash`.
  It must be a plain decrypted ELF64 big-endian PPC64 PRX (ELF type `0xFFA4`,
  machine `0x0015`), not the SELF-wrapped `SCE` firmware file. Firmware files
  are not included; see
  [ps3recomp's firmware notes](https://github.com/PlexTDM/ps3recomp/blob/linux-fixes/docs/FIRMWARE_LLE.md)
  for requirements. Put it at `fw_spu/libsre.prx`, or pass its path to CMake
  as shown below.
- A checkout of the `linux-fixes` branch of
  [PlexTDM/ps3recomp](https://github.com/PlexTDM/ps3recomp). This is the
  runtime and Vulkan backend the native game is built from: colour-target
  snapshots, depth-as-colour snapshots, HDR/FP16 targets, batched submits,
  and the `<prefix>.req` frame grab the tools drive. It also supplies the
  lifting tools, the package extractor and `runtime/ppu/ppu_vm_fast.h`.

## Prepare the source and Python tools

Clone the project, then the runtime fork beside it:

```sh
git clone https://github.com/PlexTDM/dod3-recomp-linux.git
cd dod3-recomp-linux
git clone --branch linux-fixes https://github.com/PlexTDM/ps3recomp.git ps3recomp-soyjxck
python3 -m venv .venv
.venv/bin/python -m pip install -r ps3recomp-soyjxck/tools/requirements.txt \
    -r tools/requirements.txt
```

`ps3recomp-soyjxck/` is git-ignored on purpose: it is a separate source
checkout, not part of this repository. If you already have one, make sure it
is on `linux-fixes` (tested at `16ef065`); nothing else is checked out
alongside the project.

`tools/lift_ppu.sh` and `tools/lift_spu.sh` use the tools in `ps3recomp/`.
If those tools predate the `--hook` and `--native-hook` options, the helpers
apply `patches/sp00nznet-ppu-lifter-hooks.patch` and
`patches/sp00nznet-spu-native-hooks.patch` automatically.

## Install the supplied game files

The Linux setup target can install the disc and update before the native game
has been built. If `build-installer/dod3-setup` is not present, build it first:

```sh
tools/build_linux_setup.sh
```

For the game-files folder used in this setup, install to the repository root:

```sh
GAME_FILES=/home/unnamed/codes/drakengardpc/game-files
./build-installer/dod3-setup --install --base "$PWD" \
    "$GAME_FILES/Drakengard 3 (USA) (En,Fr).iso" \
    "$GAME_FILES/UP0082-BLUS31197_00-DOD3PATCH0000000-A0101-V0101-PE.pkg"
```

To use another location, replace `--base "$PWD"` with `--base /path/to/game-data`.
The installer writes the game layout under `game/` and the decrypted 1.01
executable under `elf/EBOOT_101.ELF` in that base directory. The ISO must be
decrypted; ordinary encrypted disc images are refused. Add any optional DLC
package paths after the update package. Check the supplied files without
installing them with:

```sh
./build-installer/dod3-setup --check "$GAME_FILES/Drakengard 3 (USA) (En,Fr).iso" \
    "$GAME_FILES/UP0082-BLUS31197_00-DOD3PATCH0000000-A0101-V0101-PE.pkg"
```

## Lift the game code

Lift the PPU code from the 1.01 executable:

```sh
sh tools/lift_ppu.sh
```

The helper applies the small compatibility patch needed for this port's
native function hooks, then runs the PPU loader and lifter. Next lift the SPU
programs and generate their native fast-path hooks:

```sh
tools/lift_spu.sh
```

These commands create local generated files in `out/`, `recompiled_101/` and
`spu/`; they are not committed. The PPU helper defaults to two worker
processes to limit memory use. Set `PPU_JOBS=1` if the machine is memory
constrained, or choose a larger worker count if it has more available memory.

## Build

Configure against the runtime fork and point CMake at your decrypted
`libsre.prx`:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPS3RECOMP_DIR="$PWD/ps3recomp-soyjxck" \
    -DLIBSRE_PRX="$PWD/fw_spu/libsre.prx"
cmake --build build
```

The result is `build/dod3`. CMake uses the runtime's Vulkan renderer by
default. The build limits itself to what the machine can hold:
`DOD3_COMPILE_JOBS` sets the `dod3_compile` job pool from the lift's largest
translation unit and the machine's RAM, so no `--parallel` is needed. Raise
it if the machine has memory to spare, lower it if the build swaps.

The build cache records the runtime selection:

```sh
grep PS3RECOMP_DIR build/CMakeCache.txt
# PS3RECOMP_DIR:UNINITIALIZED=/path/to/dod3-recomp-linux/ps3recomp-soyjxck
```

### Which `ps3recomp` is used

One checkout is involved:

- `ps3recomp-soyjxck/` (the fork's `linux-fixes` branch): the runtime, RSX
  draw engine and Vulkan backend compiled into `build/dod3`. Pass it as
  `PS3RECOMP_DIR`.

The renderer work is that branch's own code, so a checkout of `linux-fixes`
is all the build needs: no patch is applied at configure time. The project
used to carry the upstream v0.17.0 toolkit as a submodule; it was removed
when the fork took over every job it had (lifters, package extractor,
`ppu_vm_fast.h`), leaving only `libsre.prx`, which now lives in `fw_spu/`.

## Renderer fixes that make the effects work

The native Vulkan path needs the following changes. They are why the game's
shadows, light shafts, bloom and post-process appear instead of a flat,
blurry or white layer.

This checkout's `dod3.ini` currently sets `DOD3_MOTION_BLUR = 0` and
`DOD3_POSTFX = 0`, which switch off motion blur and depth of field, bloom and
colour grading. Remove or change those lines to see those effects; they are
not renderer fixes.

- **Colour-target snapshots (`color_snapshot`).** Drakengard 3's
  post-process runs in place in the back buffer: it samples the same colour
  target it is writing. Vulkan forbids that, and the old path fell back to
  stale guest bytes, which showed up as white/flashing frames and a blurry
  post-process layer. `vk_eng_color_snapshot` copies an owned colour target
  to a sampleable twin before the draw and binds the twin. This is the
  Vulkan implementation of the runtime's `color_snapshot` op
  (`ps3recomp-soyjxck`, `linux-fixes`).
- **Depth-as-colour snapshots (`depth_snapshot_rgba8`).**
  `vk_eng_depth_snapshot_rgba8` reads the D32 depth target back and packs it
  as `R = depth[15:8]`, `G = depth[7:0]`, `B = stencil`,
  `A = depth[23:16]` for passes that sample depth through an RGBA8 view.
  Shadow and depth-based effects need this; without it they read stale or
  dummy data.
- **HDR/FP16 colour targets.** The render pass and pipeline cache key on
  `(colour format, target count)` instead of assuming RGBA8, so the game's
  `R16G16B16A16F` HDR targets, bloom and depth-of-field passes are created
  instead of dropped. Colour targets are checked for
  `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT` before use.
- **Surface views over textures.** `vk_eng_surface_view` resolves both
  `VK_ENG_COLOR` and `VK_ENG_TEXTURE`, so effects that sample a texture alias
  of a render target get a valid view.
- **Blur-chain scale floor (`RSX_SCALE_MIN=200`).** The 322x182 targets are
  the light-shaft and bloom blur chains: a few taps per pass, spaced for a
  picture that soft. Scaling them with `RSX_SCALE` makes every tap visible as
  a ladder or grid, which is the blurry layer artifact above the original
  resolution. `src/main.cpp` sets `RSX_SCALE_MIN=200` so those targets keep
  the game's own size; the environment and `dod3.ini` still override it
  (commit `7714438` in this repository).
- **Light-shaft saturation.** `RSX_FP_SAT_ALPHA=1a9b74dc1afc2a84` clamps the
  point-light shaft mask, so the village interior's smoky doorway and windows
  do not blow out white.
- **Cached readback and pipelined draws.** `vk_create_host_buffer_cached`
  selects `HOST_CACHED` (and coherent where available) readback memory, with
  `vk_flush`/`vk_invalidate` around transfers; this cut the 1280x720 depth
  pack from about 176 ms to 1.8 ms. Draws are recorded into 64 independent
  command-buffer slots and waited on only when a slot is reused. Readback,
  snapshots, clears, present, release and uploads call
  `vk_eng_slots_wait_all()` first.
- **Wayland swapchain.** The surface-format list is sized from the count
  returned by Vulkan and accepts `VK_INCOMPLETE`; the old 64-entry fixed
  array was too short for Wayland's format/colour-space pairs.
- **Cleared depth targets.** `eng_zdepth_snapshot` and `eng_zdepth_packed`
  accept a cleared depth target before its first writing draw. Before that
  change, a shadow projection that ran before its casters' depth pass sampled
  stale guest bytes and could show a dark rectangle. The fix is smoke-tested
  in chapter 1; visual verification in the green-canyon area is still
  pending.

## Frame capture

`PS3RECOMP_FRAME_GRAB=<prefix>` writes the next presented frame to
`<prefix>.ppm` when the file `<prefix>.req` exists. This is what
`tools/autoplay.sh` uses to find the battle HUD. For a hands-off chapter 1
run and frame times:

```sh
tools/live_bench.sh mytest 120 AUTOPLAY_FORWARD=1 AUTOPLAY_COMBAT=1
```

The log is `out/bench/mytest.log`; the latest grabbed frame is
`out/bench/mytest.grab.ppm`.

## Run

The earlier installer step put the game in the repository root. Run the
native game with that same install base:

```sh
DOD3_INSTALL_BASE=. build/dod3
```

If you chose another `--base` directory, set `DOD3_INSTALL_BASE` to that
same directory. To use the normal user-data location instead, install after
building with `build/dod3 --install <disc-or-folder> <update.pkg> [DLC.pkg
...]`, then run `build/dod3`. By default those files and saves are stored
under `$XDG_DATA_HOME/dod3-recomp`, or `~/.local/share/dod3-recomp` when
`XDG_DATA_HOME` is unset.

The native Linux game does not include a graphical launcher; in-game options
are available in its Settings pages, and other settings can be edited in
`dod3.ini` in the install directory.
