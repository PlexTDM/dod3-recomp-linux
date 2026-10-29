# Building Drakengard 3 Recompiled natively on Linux

Status: **playable, in progress**. Last updated: 2026-10-29.

This document tracks the native Linux port: the launcher and the game itself,
not the `dod3-setup` installer. The game reaches the title, save selection,
New Game and gameplay with the Vulkan backend; packaging and longer-run
validation are still open.

## Goal

Produce a native Linux executable that:

1. opens the launcher (Play, Mods, DLC, Settings),
2. boots the recompiled game through ps3recomp's Vulkan RSX backend,
3. reads the same `game/` install produced by `dod3-setup`,
4. does not require Wine or Proton at runtime.

The native Linux installer (`dod3-setup`) is separate and already works. See
[`BUILDING-LINUX.md`](BUILDING-LINUX.md).

## Current state

| Part                   | State                                             | Notes                                                                                                                                                                                   |
| ---------------------- | ------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Installer              | Native, works                                     | SDL2 + OpenGL + Dear ImGui, `src/os/linux/setup_linux.cpp`                                                                                                                              |
| Launcher UI            | Written, wired                                    | `src/ui/launcher.cpp` + `src/ui/ui_linux.cpp`; included in the full target                                                                                                              |
| Game runtime           | Native build boots to the title and into gameplay | Vulkan; title and menus at 60 fps; save selection, New Game, stage loading and movement work; stage loads build pipelines on worker threads and hold 25-45 fps while they do            |
| Linux RSX backend      | Opt-in Vulkan, partial                            | `rsx_vulkan_backend.c`; RGBA8 and FP16 HDR colour targets; runtime display mode, window size and V-Sync; `RSX_SCALE` internal resolution (restart for a new scale); toolkit scenes pass |
| Linux full game target | Added, links                                      | `build-native/dod3`, 280 MB                                                                                                                                                             |
| Lifted PPU/SPU sources | Not committed                                     | Generated locally from the player's `EBOOT_101.ELF`                                                                                                                                     |

The Linux game path selects the **Vulkan** backend in `src/main.cpp` and sets
`PS3RECOMP_VK_WINDOW=1` / `PS3RECOMP_VK_GUEST_PROGRAMS=1`. A native build now
opens an SDL2 window (Wayland or X11), presents the selected window/display
size, and can render the register-file engine at an internal resolution scale.
The startup
movie's Edge zlib chunks are inflated natively in `src/patches/edgezlib.cpp`,
and the PPU side of the SPURS queue is implemented natively in
`src/kernel/spurs_queue.cpp`; with `DOD3_LIBSRE_STUB=ON` the game reaches the
title, opens save selection, starts a New Game, loads the first stage and
renders gameplay. The stub is still used by non-zlib LFQueue pushes; a real
decrypted `libsre.prx` remains optional for those paths. Put it at
`ps3recomp/fw_spu/libsre.prx` (or set `-DLIBSRE_PRX=/path/to/libsre.prx`) and
reconfigure with `-DDOD3_LIBSRE_STUB=OFF` to lift the real helpers with
`tools/gen_libsre.py`.

Controller input works through SDL2. Native Linux keyboard and mouse mapping is
not wired yet: `src/input/kbm.cpp` is inert, so use a pad, or the toolkit's
`PAD_SCRIPT` / `PAD_FILE` test inputs.

## Display settings

The Linux Vulkan backend reads these `dod3.ini` / environment values:

| Key           | Values                                 | Applies                               |
| ------------- | -------------------------------------- | ------------------------------------- |
| `RSX_DISPLAY` | `windowed`, `borderless`, `fullscreen` | Live from Graphics Settings           |
| `RSX_WINDOW`  | `1280x720`, `1920x1080`, ...           | Windowed mode, at startup/launcher    |
| `RSX_SCALE`   | `1`, `1.5`, `2`, `3`, `4`              | Internal resolution; restart required |
| `RSX_VSYNC`   | `1` (on), `0` (off)                    | Live from Graphics Settings           |

`RSX_SCALE` enlarges the register-file engine's colour and depth targets (targets
at least `RSX_SCALE_MIN` pixels in both directions, default 200), scales
viewports and scissors, resamples readbacks, and patches WPOS in fragment
shaders. It costs GPU time and memory, so 1.5 is a reasonable first step and 2+
needs a fast GPU. The swapchain follows the window size; the display image
follows `RSX_SCALE` and is blitted to the swapchain, so resizing the window does
not change the internal resolution.

Examples:

```sh
# Windowed 1280x720, native internal resolution, no V-Sync
RSX_DISPLAY=windowed RSX_WINDOW=1280x720 RSX_SCALE=1 RSX_VSYNC=0 \
  DOD3_INSTALL_BASE=game DOD3_LAUNCHER=0 ./build-native/dod3

# Borderless desktop, 1080p internal resolution
RSX_DISPLAY=borderless RSX_SCALE=1.5 \
  DOD3_INSTALL_BASE=game DOD3_LAUNCHER=0 ./build-native/dod3
```

The legacy `PS3RECOMP_VK_FULLSCREEN=1` still selects borderless mode when
`RSX_DISPLAY` is unset. Without a window (`PS3RECOMP_VK_WINDOW=0`), rendering
stays offscreen and `RSX_SCALE` still controls the internal target size.

## Architecture

- **Game code**: `src/` contains the runner, native patches, HLE hooks and
  settings pages.
- **Toolkit**: `ps3recomp/` supplies the PPU/SPU runtime, HLE libraries,
  POSIX compatibility shims, SDL2 audio/input, and RSX backends.
- **Lifted code**: generated from the retail 1.01 `EBOOT_101.ELF` into
  `recompiled_101/` (PPU) and `spu/` (SPU). Both are gitignored and large.
- **Rendering**: Vulkan, loaded through `dlopen("libvulkan.so.1")`. The null
  backend remains the headless fallback.
- **Launcher/installer UI**: `src/ui/launcher.cpp` and `src/ui/wizard.cpp`
  are platform-independent; `src/ui/ui_linux.cpp` provides the SDL2/OpenGL
  window and ImGui backends.
- **Platform layer**: `src/os/linux/setup_linux.cpp` and
  `src/os/linux/proton.cpp` currently belong to the installer target.

## Prerequisites

Tested on Arch Linux x86_64. Other distributions need equivalent packages.

From the repository root, initialize the pinned runtime submodule if it is
not already present:

```sh
git submodule update --init --recursive
```

```sh
sudo pacman -S --needed \
  base-devel cmake ninja git python python-pip \
  sdl2 vulkan-headers vulkan-icd-loader \
  glslang spirv-tools zlib
```

A working Vulkan driver is required. Check it with:

```sh
vulkaninfo --summary
```

The current machine has an NVIDIA driver and Mesa's Vulkan loader. The build
itself only needs the Vulkan headers; `libvulkan` is loaded at run time.

Python packages for the lifter:

```sh
python3 -m venv .venv
.venv/bin/pip install -r ps3recomp/tools/requirements.txt -r tools/requirements.txt
```

## Inputs

The player must supply:

- `game/elf/EBOOT_101.ELF` (26,872,424 bytes, from the 1.01 update),
- `game/game/disc/PS3_GAME/...` and the update/DLC data,
- `game/dod3.exe` only if you keep a Windows release in the same folder for
  Proton; the native build does not need it.

The current install is at `game/` and was produced by `dod3-setup`. It contains
about 16 GB, almost all of it `game/game/disc/PS3_GAME/USRDIR/`.

## Build steps

### 1. Install the game data

If starting from an ISO and update PKG:

```sh
./build-installer/dod3-setup --install --base game \
  "/path/Drakengard 3 (USA) (En,Fr).iso" \
  "/path/UP0082-BLUS31197_00-DOD3PATCH0000000-A0101-V0101-PE.pkg"
```

Or use an existing `game/` folder produced by `dod3-setup`.

### 2. Lift the PPU code

```sh
.venv/bin/python ps3recomp/tools/ppu_loader.py \
  game/elf/EBOOT_101.ELF -o out/v101/

.venv/bin/python ps3recomp/tools/ppu_lifter.py \
  game/elf/EBOOT_101.ELF \
  --functions out/v101/EBOOT_101.functions.json \
  --hle-stubs out/v101/EBOOT_101.imports.json \
  --code-end 0x157fc00 \
  --nonvolatile-locals \
  --hook 0x000C1E50 --hook 0x00EE7728 --hook 0x00272E58 \
  -o recompiled_101/
```

Expected output: `recompiled_101/ppu_recomp.h` plus hundreds of `.c`/`.cpp`
files, roughly 550 MB.

### 3. Lift the SPU code

```sh
DOD3_ELF=game/elf/EBOOT_101.ELF tools/lift_spu.sh
```

This writes `spu/images`, `spu/*.c`, and the generated workload registry. It
also needs `tools/make_spu_overlays.py` and the optional `fw/dev_flash` only
for the Sony MP3 decoder path; the native MP3 stand-in does not require
firmware.

### 4. Configure the native build

```sh
cmake -S . -B build-native -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPS3RECOMP_RSX_VULKAN=ON \
  -DDOD3_LIBSRE_STUB=ON
```

`DOD3_LIBSRE_STUB=ON` is enough for the native queue and Edge zlib paths. If
you have a decrypted `libsre.prx`, use `-DDOD3_LIBSRE_STUB=OFF` instead.

### 5. Build

```sh
cmake --build build-native -j2
```

A clean build is expected to take about 30 minutes, mostly the lifted PPU
code. On GCC, each 600k-line lifted translation unit peaks around 5 GB of
anonymous memory even at `-O1`; six parallel jobs OOM-killed a 23 GB machine.
Use `-j2` or `-j3` (or `-j1` under memory pressure), not `-j$(nproc)`. The
lifted TUs default to `-O1` on GCC; override with `-DDOD3_RECOMP_OPT=-O3`
only in a separate build directory.

### 6. Run

Start the launcher:

```sh
DOD3_INSTALL_BASE=game ./build-native/dod3
```

For a direct windowed boot without the launcher:

```sh
PS3RECOMP_VK_WINDOW=1 DOD3_LAUNCHER=0 DOD3_INSTALL_BASE=game ./build-native/dod3
```

For a hands-off run from the title to a New Game, the toolkit's pad script can
press START at 25 s, DOWN at 34 s and CROSS at 36 s:

```sh
PAD_SCRIPT="25:0x0008,34:0x0040,36:0x4000" \
  PS3RECOMP_VK_WINDOW=1 DOD3_LAUNCHER=0 DOD3_INSTALL_BASE=game ./build-native/dod3
```

`0x0008` is START, `0x0040` is DOWN and `0x4000` is CROSS.

Observed on 2026-10-25 with `DOD3_LIBSRE_STUB=ON`:

- Vulkan device: AMD Radeon RX 6700 (RADV NAVI22).
- Window swapchain: 1280x720, 4 images, format 44.
- Title screen: about 30 fps after the first pipeline batch; music and HUD
  title assets load.
- START opens the save-selection screen. Selecting New Game loads the first
  stage and renders gameplay; movement works.
- Stage loading inflated 6000 and then 7000 Edge zlib chunks natively
  (`0` left to the SPU task). The SPURS queue initialized 22 times and the
  game popped from it without a PhysX error.
- New pipelines are built during the stage load; frame rate dips to about
  14-22 fps there, then recovers. The title's own frame limiter holds 30 fps.
- Non-fatal noise: unresolved NIDs `0x45FE2FCE` (boot) and `0x039D70B7`
  (`cellSpursQueueDetachLv2EventQueue`, repeated), and a FIOS scheduler-cache
  "disk full" warning that disables the cache. None stopped boot or gameplay.
- The old shutdown-only unresolved indirect call at `0x3E00F4C1` is gone once
  the PhysX queue initializes successfully.

## Native integration TODOs

### Build system

- [x] Add a full native Linux target in `CMakeLists.txt`; do not stop at
      `DOD3_SETUP_ONLY`.
- [x] Add `src/os/linux/proton.cpp` to `PORT_PLATFORM_SOURCES`; the
      `dod3-setup` main stays in its own target.
- [x] Add `src/ui/wizard.cpp`, `src/ui/launcher.cpp`, `src/ui/look.cpp`,
      `src/ui/setup_shared.cpp`, `src/ui/png.cpp`, and `src/ui/ui_linux.cpp` to
      `SETUP_SOURCES` on Linux.
- [x] Add ImGui's `imgui_impl_sdl2.cpp` and `imgui_impl_opengl3.cpp` on Linux.
- [x] Link SDL2, OpenGL, `dl`, `pthread`, and zlib for the full target.
- [x] Pass `-DPS3RECOMP_RSX_VULKAN=ON` and ensure Vulkan headers are found.
- [x] Ensure glslang is found so guest shaders can be translated to SPIR-V.
- [x] Keep generated `recompiled_101/`, `spu/`, `out/`, and `build-native/`
      out of git.

### Launcher and setup flow

- [x] Include `ui/ui.h` in `src/main.cpp`.
- [x] On Linux, when no ELF argument is given, use `DOD3_INSTALL_BASE` or the
      executable directory as the install base.
- [x] If the install is not ready, call `run_installer(base)`.
- [x] Otherwise call `run_launcher_loop(base)` and boot the game on `kPlay`.
- [x] Honor `DOD3_LAUNCHER=0` (no Shift detection on Linux yet).
- [x] Make the installer's Play button launch the native binary, not Proton,
      when built natively.
- [ ] Verify `install-desktop.sh` and the desktop entry point at the native
      binary.

### Renderer

- [x] Replace the Linux null backend selection in `src/main.cpp` with
      `rsx_vulkan_backend_init/present/pump_messages`.
- [x] Default `PS3RECOMP_VK_GUEST_PROGRAMS=1` for the game (guest shaders are
      required for correct visuals).
- [x] Add FP16 HDR colour-target render passes (the title binds format 10,
      `R16G16B16A16_SFLOAT`) and blit them to the RGBA8 display image.
- [x] Accept `VK_INCOMPLETE` when a Wayland driver exposes more than 64
      surface formats; this unblocked the Wayland swapchain.
- [x] Read `RSX_DISPLAY` / `RSX_WINDOW` / `RSX_VSYNC`, recreate the swapchain
      on resize or a live display/V-Sync change, and implement `RSX_SCALE`
      internal-resolution targets, scaled viewports/scissors, WPOS variants and
      readback resampling. `RSX_SCALE` applies at the next start.
- [ ] Verify the draw-engine path (`PS3RECOMP_RSX_ENGINE=dispatch`).
- [ ] Add/verify cube-map support; Drakengard 3 may use environment maps.
- [ ] Add/verify vertex-texture support.
- [ ] Check depth snapshots, MRT, depth textures, render-to-texture, and
      occlusion queries against the game's effects.
- [x] Check the synchronous Vulkan submit path for performance. Done in place:
      two frames in flight, one submission a frame, async pipeline builds,
      uploads recorded into the frame (see the 2026-10-26 log entry).
- [ ] Tune the buffer pool and anisotropy defaults for the Vulkan path.

### Runtime and platform

- [x] Audit the old `_WIN32` blocks in `src/main.cpp`; the platform branches
      were removed and the Linux behavior is the only path.
- [x] Verify controller input (SDL2 `cellPad`), save loading and the New Game
      flow.
- [ ] Add native Linux keyboard and mouse mapping in `src/input/kbm.cpp`
      through SDL2.
- [ ] Verify SDL2 audio (`cellAudio`) under gameplay.
- [ ] Verify POSIX thread/memory/filesystem shims under the game's load.
- [x] Verify save data load and New Game.
- [ ] Verify trophies, DLC and mods paths under `game/`.
- [x] Verify the native MP3 stand-in (title and stage music).
- [ ] Verify the `gamedata/dev_flash` layout.
- [ ] Exercise the native SPURS queue push path; 0 PPU pushes were observed in
      the title and first-stage runs.
- [ ] Verify shutdown/exit paths and log file locations.

### Validation

- [x] Native binary links and opens a Vulkan window.
- [x] Get past the startup movie with native Edge zlib and the native SPURS
      queue.
- [x] Boot to the title screen.
- [x] Reach the first playable area and move around.
- [ ] Verify longer gameplay stability and performance after pipeline warm-up.
- [ ] Complete the first chapter and the heavy destruction scene.
- [ ] Check saves, settings, launcher round-trip, and fullscreen/windowed modes.
- [ ] Run the toolkit's Vulkan CI scenes locally.

### Packaging

- [ ] Document runtime dependencies (SDL2, Vulkan loader/driver, glslang not
      needed at runtime).
- [ ] Decide whether to ship a binary, a build script, or both.
- [ ] Add a native Linux release/CI job once the build is stable.

## Progress log

### 2026-10-26

Playability pass on the Vulkan backend. The hitching and the stage-load
freezes came from the frame submission, not from the guest, and the depth
format had no stencil.

- **Two frames in flight.** A present ended with a fence wait, so the walker
  and the GPU took turns: `RSX_VK_BATCH_STATS` measured 11-12 ms a frame spent
  inside `vkWaitForFences` in play, with about 9 ms of recording either side.
  The display blit is now recorded into the frame's own command buffer, the
  submit carries the acquire/signal semaphores, and the present is queued
  without waiting; the next frame records into the other of two slots
  (`VK_SLOTS`), each with its own command buffer, fence and staging arenas, so
  a frame waits on the fence of the one two before it. Wait time in play is
  down to ~1.5 ms a frame.
- **Staging arenas reset per frame.** They used to reset only when one
  overflowed, which ended the batch, waited and reallocated 32-128 MB in the
  middle of play; the constants arena overflowed every ~40 frames and the
  vertex arena every ~75. They reset when the frame's slot comes round again.
  `arena 0.0` in the stats; a stage load's worst frame fell from 2.9 s to
  under 0.2 s.
- **Async pipeline builds on Linux.** The engine's worker-thread pipeline build
  was compiled in for Windows and macOS only, so on Linux every new material
  compiled on the FIFO walker: 215 pipelines of 60-100 ms, 121 of them inside
  one frame of the first stage's load (9.7 s in that frame alone). Linux uses
  the workers now. The Vulkan create calls a worker makes take a mutex
  (`s_eng_vk_mu`, held for the call, not for the translate around it), and the
  worker builds the triangle-list variants eagerly, so the walker only binds.
  The same load now runs at 25-45 fps instead of 5-17.
- **Texture uploads ride the frame.** An uploaded level used to get its own
  staging buffer, a submit and a full device wait; in play the engine uploads a
  few textures a frame (fire, water, HUD animations), and those waits were most
  of the walker's time. They stage into the frame's upload arena and are
  recorded into the frame's command buffer: an upload is a memcpy and one copy
  command.
- **Depth and stencil.** The depth format is `D32_SFLOAT_S8_UINT` now (it was
  `D32_SFLOAT`), the same thing `D32_FLOAT_S8X24` is on D3D12 and
  `Depth32Float_Stencil8` on Metal, so the title's stencil tests stop being
  inert. Depth snapshots copy the 32-bit depth aspect unchanged. Stencil
  clears clear the aspects that were asked for, and both render-pass builders
  load and store the stencil.
- Measured on the i5-10400 / RX 6700 machine, save loaded from the title, in
  borderless 1920x1080 at `RSX_SCALE = 1.5`: the first stage's load went from
  5-17 fps with multi-second stalls to 25-45 fps; the cathedral fight went from
  40 to 45-57 fps; the outdoor path runs at 30-33 fps, where the GPU wait is
  near zero (the guest's own CPU cost). The old binary did not reach gameplay
  within 200 s from the same save, so no in-play A/B was possible in that
  window.

### 2026-10-25

- Confirmed the installer and Windows/Proton game are working.
- Inspected the existing Linux pieces:
  - `src/ui/ui_linux.cpp` provides SDL2 + OpenGL + ImGui.
  - `src/ui/launcher.cpp` is platform-independent.
  - `ps3recomp` has POSIX shims and an opt-in Vulkan RSX backend.
  - `src/main.cpp` still selects the null backend on Linux.
  - `CMakeLists.txt` builds only `dod3-setup` on Linux.
- Verified the local toolchain: CMake 4.4.4, Ninja 1.13.2, Python 3.14.8,
  SDL2, Vulkan headers/loader, glslang, and spirv-tools are installed.
- Verified the game inputs: `game/elf/EBOOT_101.ELF` is 26,872,424 bytes and
  `game/game/disc` is present.
- Created `.venv` and installed `ps3recomp/tools/requirements.txt` plus
  `tools/requirements.txt`.
- Ran `ppu_loader.py`: 30,138 unique functions, 252 firmware imports.
- Ran `ppu_lifter.py`: 53,426 functions, 14 source chunks, 589 MB,
  `recompiled_101/ppu_recomp.h` written.
- Ran `tools/lift_spu.sh`: 44 SPU images, 30 MB, `spu_workloads.c` and
  `spu_overlays.c` written.
- Configured `build-native` with `PS3RECOMP_RSX_VULKAN=ON`; Vulkan headers and
  guest-program glslang support were found.
- Started the native build. It stops at `gen/libsre_lifted.cpp`: the build
  expects `ps3recomp/fw_spu/libsre.prx`, a decrypted firmware module the
  repository does not ship. Added `DOD3_LIBSRE_STUB=ON` as a compile-only
  fallback while the real module is obtained; at that point the SPURS queue
  and LFQueue paths failed at run time with the stub.
- Added the full Linux target to `CMakeLists.txt` and wired the Linux
  launcher/installer flow plus the Vulkan backend in `src/main.cpp`.
- First full build attempt OOM-killed `cc1plus` at `-j6`: each lifted PPU
  translation unit peaks around 4 GB. Restarted with `-j2`.
- Added `-mssse3 -msse4.1` to the lifted PPU sources (and
  `collision_native.cpp`); GCC rejected the `VSLDOI`/`_mm_alignr_epi8` path
  without the ISA flags.
- Defaulted lifted PPU TUs to `-O1` on non-MSVC builds; `-O3` made each TU
  both slower and no smaller in memory. `DOD3_RECOMP_OPT` overrides it.
- Completed the first full link: `build-native/dod3`, 280 MB.
- Fixed the Wayland swapchain: `vkGetPhysicalDeviceSurfaceFormatsKHR`
  returns `VK_INCOMPLETE` when the driver exposes more than the 64-entry
  local array; accept the truncated list instead of dropping to headless.
- Added FP16 (`R16G16B16A16_SFLOAT`) and FP32 colour-target render passes and
  per-format framebuffer selection. The title's format-10 HDR targets now
  create successfully; `vk_eng_present` blits them to the RGBA8 display.
- Ran the native binary from `game/`: 1280x720 window, ~30 fps for 5 s, then
  the FIFO stops at `[usercmd] #256` and the run freezes.
- Verified the startup Bink video renders (“Now Loading...” surface dump) and
  tied the freeze to its Edge zlib LFQueue push, not to presentation or the
  RSX shader path.
- Proved the freeze cause: `lfq_push_body` calls the compile-only
  `dod3_libsre_call` stub, which always returns `CELL_SYNC_ERROR_AGAIN`, so
  `_cellSpursLFQueueGetPushPointer2` never returns a slot. The queue is empty;
  the push retries forever while the rest of the guest idles. Supplying
  `libsre.prx` and rebuilding with `DOD3_LIBSRE_STUB=OFF` is the next step.
- Superseded that next step: `src/patches/edgezlib.cpp` now inflates Edge zlib
  chunks natively and completes the request's counter and event flag. The
  startup movie's 1934 chunks complete without the SPU task.
- Implemented the PPU side of `cellSpursQueue` natively in
  `src/kernel/spurs_queue.cpp` (initialize, push, pop, blocking retry). PhysX
  queue creation now succeeds; the game reaches the title screen at 30 fps.
- Verified START opens the save-selection screen, New Game loads the first
  stage, and movement works. Stage loading inflated 6000/7000 chunks natively
  and built new pipelines (about 14-22 fps during the load).
- Removed the temporary `LFQ_TRACE` instrumentation and updated the native
  queue comments and this document.

## Known risks

- The Vulkan backend is intentionally conservative and incomplete. Missing
  cube maps or vertex textures may make some scenes render incorrectly.
- In play the renderer now keeps the GPU busy while the walker records the
  next frame, so what is left is mostly the guest's own CPU cost: on the
  i5-10400 test machine the outdoor path sits at ~30 fps with a GPU wait near
  zero, where the cathedral fight reaches 45-57.
- The engine's scissored depth/stencil clear (`clear_depth_stencil_rect`, which
  this title uses to pack several shadow maps into one depth target) is still
  not implemented on Vulkan, so a packed target is cleared whole.
- The lifted code is large and compiler/resource intensive.
- The native path is younger than the Proton release; some game-specific
  fixes may still assume the older renderer.
- Native does not automatically mean faster than Proton. The first goal is
  correctness and integration; performance comes after.
