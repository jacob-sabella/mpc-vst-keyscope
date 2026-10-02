#!/usr/bin/env bash
# Offline tests (x86, ASan/UBSan), before anything goes to a device:
#   1. tests/analyzer_test.c: the detectors on synthesized audio (keys, chords, tuning, bass, gate)
#   2. tests/engine_test.c: the plugin engine end to end (worker thread, readouts, lock, reset, state)
#   3. mpc-vst-plugins tools/test_port.sh: the generic wrapper's host test (instances, params, chunk)
# Needs an mpc-vst-plugins checkout (MPC_VST, default: next to this repo).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
MPC_VST="${MPC_VST:-$here/../../mpc-vst-plugins}"
[ -x "$MPC_VST/tools/test_port.sh" ] || { echo "need an mpc-vst-plugins checkout (MPC_VST)" >&2; exit 1; }
mkdir -p "$here/build"
cp "$MPC_VST/wrapper/engine.h" "$here/build/"
cd "$here/.."
CF="-std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1 -Isrc -Ivst/build"
gcc $CF tests/analyzer_test.c src/analyzer.c -lm -o vst/build/analyzer_test
vst/build/analyzer_test
gcc $CF -DKS_TEST tests/engine_test.c src/keyscope.c src/analyzer.c -lm -lpthread -o vst/build/engine_test
vst/build/engine_test
"$MPC_VST/tools/test_port.sh" "$here/vst.json"
