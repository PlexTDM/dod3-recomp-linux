#!/bin/bash
# Build the Linux installer (dod3-setup) from this tree.
#
#   tools/build_linux_setup.sh [--build-dir DIR] [--debug] [--clean]
#                              [--run] [--base DIR]
#
# This builds the native installer only, not the recompiled game. The
# ps3recomp submodule is initialized when it is missing. The binary is
# printed at the end; --run starts it, optionally with --base.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

build_dir=build-installer
build_type=Release
clean=0
run=0
base=

usage() {
    cat <<'EOF'
usage: tools/build_linux_setup.sh [options]

  --build-dir DIR  build in DIR instead of build-installer
  --debug          build with CMAKE_BUILD_TYPE=Debug
  --clean          remove the build folder before configuring
  --run            start dod3-setup after building
  --base DIR       pass --base DIR to --run
  -h, --help       show this help
EOF
}

die() { echo "build_linux_setup: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir)
            [ $# -ge 2 ] || die "--build-dir needs a folder"
            build_dir=$2
            shift 2
            ;;
        --debug)
            build_type=Debug
            shift
            ;;
        --clean)
            clean=1
            shift
            ;;
        --run)
            run=1
            shift
            ;;
        --base)
            [ $# -ge 2 ] || die "--base needs a folder"
            base=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option $1"
            ;;
    esac
done

[ "$(uname -s)" = Linux ] || die "the setup target builds on Linux only"

for tool in cmake git ninja; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool not found"
done

if [ ! -f ps3recomp/tools/pkg_extract.py ]; then
    echo "== ps3recomp submodule"
    git rev-parse --is-inside-work-tree >/dev/null 2>&1 ||
        die "ps3recomp is missing; clone with --recurse-submodules"
    git submodule update --init --depth 1
fi

if [ "$clean" = 1 ]; then
    [ "$build_dir" != "/" ] || die "refusing to remove /"
    rm -rf -- "$build_dir"
fi

jobs=${JOBS:-}
if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
fi

echo "== configure ($build_type)"
cmake -S "$root" -B "$build_dir" -G Ninja \
    -DDOD3_SETUP_ONLY=ON -DCMAKE_BUILD_TYPE="$build_type"

echo "== build"
cmake --build "$build_dir" --parallel "$jobs"

setup=$(cd "$build_dir" && pwd)/dod3-setup
[ -x "$setup" ] || die "$setup was not built"
echo "dod3-setup: $setup"

if [ "$run" = 1 ]; then
    if [ -n "$base" ]; then
        exec "$setup" --base "$base"
    fi
    exec "$setup"
fi
