# Building the Linux setup

The Linux target is `dod3-setup`, the native installer only. It installs the
disc, the 1.01 update and DLC into a folder containing the Windows
`dod3.exe`, and can start the game through a `play.sh` or Wine. It does not
build the recompiled game: the game itself still runs under Proton or Wine.

## Prerequisites

- Linux x86_64
- CMake 3.20 or later
- Ninja
- a C++20 compiler (GCC 11 or later, or Clang 14 or later)
- SDL2 development files
- OpenGL development files
- zlib development files
- Git

Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake ninja sdl2 zlib
```

Debian or Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
    libsdl2-dev libgl1-mesa-dev zlib1g-dev
```

Fedora:

```sh
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config \
    SDL2-devel mesa-libGL-devel zlib-devel
```

The file pickers use `zenity`, `kdialog` or `yad` if one is installed. The
installer itself does not need Wine or Proton; those are only for the game.

## Get the source

From the Linux integration branch:

```sh
git clone --recurse-submodules --branch linux-vulkan-integration \
    https://github.com/PlexTDM/dod3-recomp-linux.git
cd dod3-recomp-linux
```

The `dod3-setup` installer-only target is also available from the upstream
repository's `main` branch.

## Build

The helper script initializes the `ps3recomp` submodule when needed,
configures a release build in `build-installer`, and builds `dod3-setup`:

```sh
tools/build_linux_setup.sh
```

The binary is `build-installer/dod3-setup`. `build-installer/` is generated
and ignored by Git; it is not in the repository.

By hand:

```sh
git submodule update --init --depth 1
cmake -S . -B build-installer -G Ninja \
    -DDOD3_SETUP_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-installer
```

The script takes `--build-dir DIR`, `--debug`, `--clean`, `--run` and
`--base DIR`:

```sh
tools/build_linux_setup.sh --clean
tools/build_linux_setup.sh --run --base /path/to/release
```

## Run

Open the installer:

```sh
./build-installer/dod3-setup
```

With no `--base`, it installs beside `dod3.exe` when one is next to it,
otherwise in `~/.local/share/Drakengard 3 Recompiled` (or
`$XDG_DATA_HOME/Drakengard 3 Recompiled`). To use an existing release
folder:

```sh
./build-installer/dod3-setup \
    --base /path/to/dod3-recomp-v0.4.0-alpha-win64
```

The setup's Play button runs `play.sh` beside `dod3.exe` when it is
executable; otherwise it tries `wine` and `wine64`. For Proton, use the
release's `play.sh` or start the game separately with the same install base:

```sh
cd /path/to/release
./play.sh
```

Install without a window:

```sh
./build-installer/dod3-setup --install --base /path/to/release \
    "/path/Drakengard 3 (USA) (En,Fr).iso" \
    "/path/UP0082-BLUS31197_00-DOD3PATCH0000000-A0101-V0101-PE.pkg"
```

`--check` identifies files without installing anything:

```sh
./build-installer/dod3-setup --check <file>...
```

If the game is installed somewhere other than beside `dod3.exe`, set
`DOD3_INSTALL_BASE` when running it under Proton/Wine.

## Notes

- The setup uses SDL2's X11 or Wayland video driver. On a Wayland desktop,
  set `SDL_VIDEODRIVER=x11` if the native driver has trouble.
- If Add Files does nothing, install `zenity`, `kdialog` or `yad`.
- The native installer is MIT-licensed like the port. The game's files are
  not included; supply your own copy.
