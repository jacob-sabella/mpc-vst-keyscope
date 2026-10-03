#!/usr/bin/env bash
# The engine tests on an MPC OS device: tests 1-5 of vst/test.sh, built for 32-bit ARM (static, -O2, no
# sanitizers) and run over ssh at the lowest priority from a scratch folder in /tmp, which is removed afterwards.
# MPC itself is not stopped or touched; its audio runs at real-time priority, so a song playing keeps playing.
#   tests/device.sh <[user@]host>     (user defaults to root; key-based ssh)
# rt_test's process() timing is the figure to watch: on the device it runs beside MPC's own load.
# Needs Docker (QEMU for arm32v7) and an mpc-vst-plugins checkout (MPC_VST, default: next to this repo).
set -euo pipefail
[ $# = 1 ] || { echo "usage: tests/device.sh <[user@]host>" >&2; exit 2; }
case "$1" in *@*) target="$1" ;; *) target="root@$1" ;; esac
here="$(cd "$(dirname "$0")/.." && pwd)"
MPC_VST="${MPC_VST:-$here/../mpc-vst-plugins}"
[ -f "$MPC_VST/wrapper/engine.h" ] || { echo "need an mpc-vst-plugins checkout (MPC_VST)" >&2; exit 1; }
out="$here/vst/build/device"
mkdir -p "$out"
cp "$MPC_VST/wrapper/engine.h" "$here/vst/build/"
python3 "$MPC_VST/tools/gen_vst.py" "$here/vst/vst.json" --params-h

echo "building for armv7 (QEMU, a few minutes)"
docker run --rm --platform linux/arm/v7 -u "$(id -u):$(id -g)" -v "$here":/b -w /b arm32v7/gcc:11-bullseye bash -euc '
    CF="-std=gnu11 -Wall -Wextra -Werror -O2 -g -static -Isrc -Ivst/build"
    E="src/keyscope.c src/analyzer.c src/seq_in.c -lm -lpthread -ldl"
    gcc $CF tests/analyzer_test.c src/analyzer.c -lm -o vst/build/device/analyzer_test
    for t in engine settings robust; do gcc $CF -DKS_TEST tests/${t}_test.c $E -o vst/build/device/${t}_test; done
    gcc $CF tests/rt_test.c $E -o vst/build/device/rt_test'

ssh_() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$target" "$@"; }
dir=/tmp/keyscope-test
ssh_ "uname -m" | grep -q '^armv7' || { echo "$target is not a 32-bit ARM device" >&2; exit 1; }
ssh_ "rm -rf $dir && mkdir -p $dir"
trap 'ssh_ "rm -rf $dir" || true' EXIT
scp -q -o BatchMode=yes "$out"/*_test "$target:$dir/"
# one shell on the device, lowered to nice 19 with renice (MPC OS has no nice) so every test inherits it; every
# test runs even if one fails, and the exit status says whether all passed
ssh_ "cd $dir && renice -n 19 -p \$\$ >/dev/null && rc=0 &&
      for t in 'analyzer_test -q' engine_test settings_test robust_test rt_test; do
          echo \"== \$t\"; ./\$t || rc=1
      done; exit \$rc"
