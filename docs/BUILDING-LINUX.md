# Building the Linux setup

The Linux target is `dod3-setup`, the native installer only. It installs the
disc, the 1.01 update and DLC into a folder containing the Windows
`dod3.exe`, and can start the game through a `play.sh` or Wine. It does not
build the recompiled game: the game itself still runs under Proton or Wine.

# TLDR

1. run

```bash
./tools/build_linux_setup.sh
```

2. download latest windows build from github releases, and extract them to ./game folder.
3. run

```bash
./build-installer/dod3-setup
```

4. select downloaded game files on 2nd screen and continue with the installation.
5. run

```bash
./game/play.sh
```

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
installer does not need Wine or Proton

## Get the source

From the branch this page is on:

```sh
git clone --recurse-submodules --branch linux-setup \
    https://github.com/PlexTDM/dod3-recomp.git
cd dod3-recomp
```

Once the branch is merged, clone the upstream repository instead and use
`main`.

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

## Install and play

The setup installs game data only. `dod3.exe` comes from the Windows
release; the setup cannot start the game until that release is in the
install folder.

### Install into the Windows release (recommended)

Extract the Windows release somewhere, then point the setup at it:

```sh
./build-installer/dod3-setup \
    --base /path/to/dod3-recomp-v0.4.0-alpha-win64
```

Add the disc ISO and the 1.01 update PKG (and DLC packages if you have
them) in the window, then press Install. The setup makes the executable
from the update, so the 1.01 path does not need the keys page.

For a headless install, pass the same files to `--install`:

```sh
./build-installer/dod3-setup --install \
    --base /path/to/dod3-recomp-v0.4.0-alpha-win64 \
    "/path/Drakengard 3 (USA) (En,Fr).iso" \
    "/path/UP0082-BLUS31197_00-DOD3PATCH0000000-A0101-V0101-PE.pkg"
```

### Install into the source tree

A setup run from a source build defaults to `<repo>/game`, which is
gitignored. Put the Windows release there first. If you have a fresh
release archive, extract it into `game/`, dropping the archive's
top-level release folder:

```sh
mkdir -p game
bsdtar -xf /path/to/dod3-recomp-v0.4.0-alpha-win64.zip \
    -C game --strip-components 1
./build-installer/dod3-setup
```

Do not use `unzip` or `7z` for this archive: they keep the Windows
backslashes as literal filename characters, producing flattened names
such as `dod3-recomp-v0.4.0-alpha-win64\dod3.exe` at the top of
`game/`. If that happens, remove those entries and re-extract with
`bsdtar`, or move an already-extracted release folder as below.

If you already have a release folder, move it instead of copying the
14 GB install:

```sh
rmdir game          # only if the earlier run left it empty
mv /path/to/dod3-recomp-v0.4.0-alpha-win64 game
./build-installer/dod3-setup
```

The layout is then `game/dod3.exe`, `game/play.sh`, and
`game/game/disc/...`. This keeps the install out of `~/.local/share`
while working on the port. If `game/` already has data, use `--base`
pointing at that folder instead. If you installed the desktop entry
before moving the release, re-run `./install-desktop.sh` from the new
folder.

If the setup says:

```text
no dod3.exe in the install folder
```

the game data was installed, but the Windows release is not in that
folder. Put the release there or re-run with `--base` pointing at it.

### Default folder

With no `--base`, the setup chooses:

1. `DOD3_INSTALL_BASE`, if set.
2. Beside `dod3.exe`, when run from a release folder.
3. `<repo>/game`, when run from a build in the source tree.
4. `$XDG_DATA_HOME/Drakengard 3 Recompiled`, or
   `~/.local/share/Drakengard 3 Recompiled`.

`--base DIR` overrides all of these.

### Proton

The setup finds the Proton builds in Steam's `compatibilitytools.d` folder
(GE-Proton, cachyos, DW-Proton and others) and in `steamapps/common`, and
shows the one Play will use on the Welcome page and on the Proton page.
Pick another build there; the choice is saved as `proton.conf` in the
install folder and used by the setup's Play button. `DOD3_PROTON` sets a
build when nothing has been picked.

From a terminal:

```sh
./build-installer/dod3-setup --proton-list
./build-installer/dod3-setup --proton /path/to/GE-Proton
./build-installer/dod3-setup --proton auto
```

### Play

The setup's Play button starts `play.sh` beside `dod3.exe` with the chosen
Proton when it is executable; otherwise it starts the chosen Proton
directly, or tries `wine` and `wine64`. To play from a terminal:

```sh
cd /path/to/install
./play.sh
```

`play.sh` finds GE-Proton, cachyos or Proton and uses
`~/.local/share/dod3-recomp/proton` as the prefix. It takes the build from
`DOD3_PROTON` when it is set; the setup sets it from `proton.conf` when it
starts the game. Started by hand, `play.sh` does not read `proton.conf`:
pass the saved build with `DOD3_PROTON="$(cat proton.conf)" ./play.sh`. To
skip the launcher:

```sh
DOD3_LAUNCHER=0 ./play.sh
```

Install GE-Proton into Steam's `compatibilitytools.d` folder, or point
`DOD3_PROTON` at a Proton folder. If your release has no `play.sh`, the
Play button starts the chosen Proton directly, or falls back to Wine;
install `wine` or `wine64` on `PATH` for that:

```sh
DOD3_PROTON=/path/to/GE-Proton ./play.sh
```

The game is installed in the folder the setup printed. If you used
`--base`, use that folder; if you used the source-tree default, it is
`<repo>/game`.

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
