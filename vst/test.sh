#!/usr/bin/env bash
# Offline tests (x86, sanitizers), before anything goes to a device:
#   1. tests/analyzer_test.c: the detectors on synthesized audio (keys, chords, tuning, bass, gate)
#   2. tests/engine_test.c: the plugin engine end to end (worker thread, readouts, lock, reset, state)
#   3. tests/settings_test.c: every setting (MEMORY, PROFILE, RANGE, CHORDS, TUNING, NOTATION, GATE), the key log,
#      the wheel for a minor key
#   4. tests/robust_test.c: bad keys and values, garbage state, tiny text buffers, odd audio, create/destroy churn
#   5. tests/seq_in_test.c: the ALSA MIDI input against the machine's sequencer (skipped where there is none) and its
#      event layout against <alsa/asoundlib.h> (ASan/UBSan and TSan)
#   6. tests/rt_test.c: the production build (no KS_TEST) at four times real time with a UI thread reading and
#      tapping, under ASan/UBSan and under TSan; reports process() time per block
#   7. mpc-vst-plugins tools/test_port.sh: the generic wrapper's host test (instances, params, chunk)
# tests/device.sh runs 1-4 and 6 on a device. Needs an mpc-vst-plugins checkout (MPC_VST, default: next to this repo).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
MPC_VST="${MPC_VST:-$here/../../mpc-vst-plugins}"
[ -x "$MPC_VST/tools/test_port.sh" ] || { echo "need an mpc-vst-plugins checkout (MPC_VST)" >&2; exit 1; }
mkdir -p "$here/build"
cp "$MPC_VST/wrapper/engine.h" "$here/build/"
python3 "$MPC_VST/tools/gen_vst.py" "$here/vst.json" --params-h
cd "$here/.."
BASE="-std=gnu11 -Wall -Wextra -Werror -fno-omit-frame-pointer -g -O1 -Isrc -Ivst/build"
CF="$BASE -fsanitize=address,undefined"
ENGINE="src/keyscope.c src/analyzer.c src/seq_in.c -lm -lpthread -ldl"
gcc $CF tests/analyzer_test.c src/analyzer.c -lm -o vst/build/analyzer_test
vst/build/analyzer_test
for t in engine settings robust; do
    gcc $CF -DKS_TEST tests/${t}_test.c $ENGINE -o vst/build/${t}_test
    vst/build/${t}_test
done
if [ -f /usr/include/alsa/asoundlib.h ]; then
    gcc $CF tests/seq_in_test.c -lasound -ldl -lpthread -o vst/build/seq_in_test
    vst/build/seq_in_test
    gcc $BASE -fsanitize=thread tests/seq_in_test.c -lasound -ldl -lpthread -o vst/build/seq_in_test_tsan
    vst/build/seq_in_test_tsan
else
    echo "no ALSA headers (libasound2-dev): tests/seq_in_test.c skipped"
fi
gcc $CF tests/rt_test.c $ENGINE -o vst/build/rt_test
vst/build/rt_test
gcc $BASE -fsanitize=thread tests/rt_test.c $ENGINE -o vst/build/rt_test_tsan
vst/build/rt_test_tsan
"$MPC_VST/tools/test_port.sh" "$here/vst.json"
