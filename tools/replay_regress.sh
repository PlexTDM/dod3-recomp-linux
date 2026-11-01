#!/bin/bash
# replay_regress.sh [out-dir] -- the rendering regression: replay the RSX
# captures in out/cap and compare their frames with a reference set.
#
#   bash tools/replay_regress.sh                 # frames into out/replay
#   REF=out/ref_vk bash tools/replay_regress.sh
#
# The captures (out/cap/<name>.rsxcap) and the references are made from the
# game and are not in the repository: record captures with RSX_CAPTURE, and
# keep a known-good replay of them as the reference. The reference is the
# renderer's own -- REF defaults to out/ref_vk (Vulkan); frames from another
# API never match byte for byte (tools/compare_frames.py has the allowances).
# Build the replay tool first, it is not part of a plain build:
# cmake --build build --target rsx_replay. A stale one tests nothing.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1
out=${1:-out/replay}
ref=${REF:-out/ref_vk}
replay=${REPLAY:-$(ls build*/ps3recomp_sdk/rsx_replay 2>/dev/null | head -1)}
[ -n "$replay" ] || { echo "no rsx_replay under build*/ps3recomp_sdk: build the rsx_replay target"; exit 1; }
[ -d "$ref" ] || { echo "no reference frames in $ref (REF=<dir>)"; exit 1; }
python=${PYTHON:-python3}
export RSX_FP_SAT_ALPHA=1a9b74dc1afc2a84 RSX_ASYNC_SHADERS=0
mkdir -p "$out"
n=0
for cap in out/cap/*.rsxcap; do
  [ -f "$cap" ] || continue
  c=$(basename "$cap" .rsxcap)
  "$replay" "$cap" --out "$out/$c" --every 10 > "$out/$c.log" 2>&1 || { echo "$c: the replay failed (see $out/$c.log)"; exit 1; }
  n=$((n + 1))
done
[ "$n" -gt 0 ] || { echo "no captures in out/cap"; exit 1; }
"$python" -I tools/compare_frames.py "$out" "$ref"
