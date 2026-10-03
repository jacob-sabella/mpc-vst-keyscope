# Testing Keyscope

## Offline (no device)

```sh
vst/test.sh              # x86 with sanitizers: analyzer, engine, settings, robustness, real time (ASan and TSan),
                         # and the wrapper's host test. CI runs the same on every push.
tests/device.sh <host>   # tests 1-5 built for armv7 and run on a device over ssh (root, key-based), at nice 19
                         # from /tmp/keyscope-test, removed afterwards. MPC is not stopped or touched.
```

On a device, the line to read is rt_test's `process(): ... p99.9 ...`: the audio thread's cost per 2.9 ms block
next to MPC's own load.

## On the device, by hand

What the tests can't reach: the screen, touch, Q-Links and a real mix. Put Keyscope as an insert on a track that
plays something in a key you know (a loop, a song, a keyboard part), and go through these. Note the device, the
MPC version and the date with the result.

| # | Check | How | Expect |
|---|---|---|---|
| 1 | Loads | Add Keyscope from the effect browser | The KEY page: the wheel in the sun, "NO KEY YET" |
| 2 | Pass-through | Play the track with Keyscope on and bypassed | No difference in sound, no clicks |
| 3 | Key | Play for 10-20 s | The key in the middle of the wheel; its six chords lit (I IV V outside, ii iii vi inside) |
| 4 | Chord | Play chords | The chord under the key name follows them; it clears a moment after stopping |
| 5 | Wheel lock | Tap a key on the wheel, then tap it again | Locks to it (KEY shows "LOCKED"); the second tap follows the audio again |
| 6 | Candidates | Tap a candidate tile | Locks to that key; tapping it again unlocks |
| 7 | KEY LOCK | Open the popup, pick a key, then Auto | As for the wheel |
| 8 | Sounding now | Hold single notes | Their tiles light; a note outside the key shows in the OUTSIDE line |
| 9 | HOLD | Turn on HOLD, play something else | Nothing changes; status "HOLD: NOT LISTENING" |
| 10 | RESET | Tap RESET | Back to "LISTENING..." with the history empty |
| 11 | CHORDS page | Play a 7th chord with TRIADS, then + 7THS | The triad name, then the 7th name |
| 12 | HISTORY page | Change key in the music | A new line at the top with the time; the old key below |
| 13 | SETUP page | Each of MEMORY, PROFILE, RANGE, NOTATION, TUNING, GATE | The readouts change as described in the README; a GATE above the track's level reads TOO QUIET |
| 14 | Q-Links | On each tab, turn each Q-Link | The four controls in the README table, one option per few ticks |
| 15 | Save and reload | Change settings, save the project, load it again | The settings come back (what was heard does not) |
| 16 | Two instances | Keyscope on two tracks | Each shows its own track's key |
| 17 | Load | Run it for a few minutes | CPU in MPC's meter as before; no audio dropouts |
| 18 | ANIMATION | Watch the mountains, then turn ANIMATION off on SETUP and on again | They slide left a step at a time; off, the page is still; on, they move again |
