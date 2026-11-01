#!/bin/bash
# Package a Linux release: dist/dod3-recomp-<version>-linux-x86_64.tar.gz,
# and the same tree as a .zip.
#
#   bash tools/package_linux.sh v0.5.0-alpha [build folder]
#
# Contents: dod3, the default dod3.ini, the player guide as README.md, and
# the licences. Nothing from the game: players install their own files with
# dod3 --install, into the folder the archive unpacks to.
#
# glslang and SPIRV-Tools: where the distribution ships them static (Ubuntu's
# glslang-dev) they are linked in; where it ships them shared only (Arch,
# Fedora) they are copied into lib/, which dod3's RPATH looks in first. What
# dod3 needs from the system either way is glibc, libstdc++, zlib and SDL2
# (sdl2, or sdl2-compat as SteamOS has it), plus a Vulkan-capable driver at
# run time. The libstdc++ is the system's on purpose: a bundled one older than
# the system's breaks Mesa's drivers, which load into the same process. Build
# in the oldest distribution the release is for (tools: Ubuntu 24.04, glibc
# 2.39) and check build/dod3 is the one you mean to ship. The second argument
# picks another build folder.
set -euo pipefail
cd "$(dirname "$0")/.."
ver=${1:?usage: package_linux.sh <version> [build folder]}
build=${2:-build}
runtime=${PS3RECOMP_DIR:-ps3recomp-soyjxck}
name=dod3-recomp-$ver-linux-x86_64
out=dist/$name
rm -rf "$out" "dist/$name.tar.gz" "dist/$name.zip"
mkdir -p "$out"

cp "$build/dod3" "$out/"
# the default settings as committed (a local dod3.ini may hold your own)
git show HEAD:dod3.ini > "$out/dod3.ini"
# Our own version of the title's post-processing composite (Post-Processing
# Quality, High, and the HDR highlight plane: src/main.cpp finds it beside
# the executable).
mkdir -p "$out/shaders/override"
cp shaders/override/*.hlsl "$out/shaders/override/"
cp docs/PLAYING.md "$out/README.md"
cp LICENSE "$out/LICENSE.txt"
cp THIRD_PARTY_NOTICES.md "$out/THIRD_PARTY_NOTICES.md"
cp "$runtime/LICENSE" "$out/LICENSE-ps3recomp.txt"
cp third_party/imgui/LICENSE.txt "$out/LICENSE-imgui.txt"
cp third_party/fonts/inter/LICENSE.txt "$out/LICENSE-inter.txt"

# What the executable links: the system's own (glibc, libstdc++, zlib, SDL2)
# and the shader translators, which travel with the release (below). Nothing
# else may be needed, or the release only runs where that library happens to
# be installed.
allowed='^(libc\.so\.6|libm\.so\.6|libstdc\+\+\.so\.6|libgcc_s\.so\.1|libz\.so\.1|libSDL2-2\.0\.so\.0|ld-linux-x86-64\.so\.2|libdl\.so\.2|libpthread\.so\.0)$'
bundled='^lib(glslang|SPIRV)'
needed=$(readelf -d "$out/dod3" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
bad=''; ship=''
for lib in $needed; do
  echo "$lib" | grep -Eq "$allowed" && continue
  echo "$lib" | grep -Eq "$bundled" && { ship="$ship $lib"; continue; }
  bad="$bad $lib"
done
if [ -n "$bad" ]; then
  echo "package_linux: dod3 needs libraries a player may not have:"
  for lib in $bad; do echo "  $lib"; done
  exit 1
fi
echo "package_linux: links $(echo $needed | tr '\n' ' ')"

# glslang and SPIRV-Tools: Ubuntu's glslang-dev ships static libraries and the
# binary takes them in (nothing to do here), Arch's and Fedora's only shared
# ones, so those travel in lib/ beside dod3 (CMakeLists.txt puts $ORIGIN/lib
# in its RPATH). Each one's own dependencies are checked as well.
for lib in $ship; do
  src=$(ldd "$out/dod3" | sed -n "s/^[[:space:]]*$lib => \(.*\) (0x.*/\1/p")
  if [ -z "$src" ] || [ ! -f "$src" ]; then echo "package_linux: cannot find $lib"; exit 1; fi
  mkdir -p "$out/lib"
  cp -L "$src" "$out/lib/$lib"
  for dep in $(readelf -d "$src" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
    echo "$dep" | grep -Eq "$allowed|$bundled" ||
      { echo "package_linux: $lib needs $dep, which the package lacks"; exit 1; }
  done
done
if [ -n "$ship" ]; then echo "package_linux: ships$ship in lib/"; fi
# The newest glibc symbol version it asks for: the oldest glibc it runs on.
glibc=$(objdump -T "$out/dod3" | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -t. -k2,2n -u | tail -1)
echo "package_linux: needs $glibc or newer"

# Nothing the player supplies, and no build leftovers.
for bad in EBOOT.ELF EBOOT_101.ELF EBOOT.BIN flashMP3.pic PARAM.SFO keys.txt guest_programs.bin libsre.prx; do
  if find "$out" -iname "$bad" | grep -q .; then echo "package_linux: $bad in the package"; exit 1; fi
done

tar -C dist -czf "dist/$name.tar.gz" "$name"
# The same tree as a .zip: upstream ships Linux as a tar.gz and Windows and
# macOS as zips, and a .zip unpacks on every desktop without a word.
python3 - "$name" <<'PY'
import os, sys, zipfile
name = sys.argv[1]
with zipfile.ZipFile(f"dist/{name}.zip", "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for root, dirs, files in os.walk(f"dist/{name}"):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(root, f)
            z.write(p, os.path.relpath(p, "dist"))
PY
ls -la "dist/$name.tar.gz" "dist/$name.zip"
(cd dist && sha256sum "$name.tar.gz" "$name.zip" && cd "$name" && sha256sum dod3)
