# Keyscope (MPC VST Plugin)

> **MPC OS.** From 1.0.2, this works on **MPC OS 2.x and 3.x**: its skin is written in the MPC OS 2.x format, which 3.x reads
> too. The skin is checked against MPC OS 2.15.1's own skins; it has not been tried on a 2.x unit yet. The
> [catalog](https://sd88me.github.io/mpc-vst-plugins/) shows which MPC OS each release works on. Earlier releases are 3.x only.
> See [MPC OS 2.x vs 3.x](https://github.com/sd88me/mpc-vst-plugins#mpc-os-2x-vs-3x) in the main repo.

Key and chord detection for Akai MPC OS standalone devices (MPC Live/One/X/Key, Force), built as a native VST2
insert effect for MPC's own plugin host, with its own screen skin and Q-Links. Put it on any track and it listens to
whatever passes through it, a sample, a loop, a keyboard part or a whole mix, and to the notes played into its MIDI
port, and names the **key**, the **chord**, the **bass note** and the **tuning**. The audio passes through untouched.
It all runs on the device.

**Status:** passes the offline tests (x86, ASan/UBSan and TSan), builds for armhf, its engine tests pass on the
device, and every function has been run on an MPC Key 37 (firmware 3.9.1.2) from the screen with chords playing
into it (`TESTING.md`).

![Keyscope on the device: the key on the circle of fifths, the notes and chord sounding now, locking a key, the chord trail, the key history and the settings](docs/keyscope.gif)

The same tour as a video, recorded from the device screen while chords played into it and the touchscreen was
driven from a script: [docs/keyscope.mp4](docs/keyscope.mp4) (with captions).

## What it does

- **KEY**: a circle of fifths. The twelve major keys run round the outside a fifth apart, their relative minors
  inside. The key heard lights its six chords (I, IV and V outside, ii, iii and vi inside), and its name and the
  chord sounding now sit in the middle. Tap any key on the wheel to lock to it, tap it again to follow the audio.
  Beside it: how well the key matches (the correlation with the key profile, as a percentage), its relative, its
  scale notes and the three best candidates (tap one to lock to it too). KEY LOCK picks any key outright.
- **SOUNDING NOW**: twelve note tiles; the ones sounding right now light up. A line says which of them are outside
  the key.
- **CHORDS**: the chord heard now (major, minor, dim, aug, sus2, sus4, power chords, and with + 7THS the 7, maj7,
  m7 and m7b5 chords), with the bass note as a slash chord (C/E), and the chords before it.
- **HISTORY**: every key change with the time since listening started, the chord trail, the tuning and the
  loudest note.
- **SETUP**: MEMORY, PROFILE, RANGE, NOTATION, TUNING, GATE and SOURCE (below), and the MIDI port's name.
- **Tuning**: where the music sits against A = 440 (a band tuned to 432 Hz reads -32 cents), followed when TUNING
  is AUTO so a detuned recording still lands on the right notes.

## Using it

Add Keyscope as an insert on the track you want to read (Channel Mixer, an insert slot, the VST list). It hears the
audio through that insert: on a plugin or keygroup track, the instrument's output. On a drum or full-mix track,
RANGE = MIDS keeps the kick and hi-hats out of the key.

**MIDI.** MPC sends no MIDI to an insert effect, so each Keyscope opens a MIDI port of its own, named on SETUP:
"Keyscope MIDI In" (a second instance gets "Keyscope MIDI In 2", and so on). To read notes, play them on a MIDI track
whose I/O → MIDI OUT is that port. The notes count for the key and chords like audio does: held
notes, the sustain pedal, and a note shorter than a frame still counts. MIDI gives exact notes even when the sound is
a drum, a pad or a heavily processed synth. SOURCE picks what is read: BOTH (the default; each source that has
something counts half), AUDIO or MIDI.

## Pages and Q-Links

| Page | Q-Links 1-4 |
|---|---|
| KEY | KEY LOCK, MEMORY, GATE, HOLD |
| CHORDS | CHORDS, RANGE, GATE, HOLD |
| HISTORY | MEMORY, GATE, HOLD, KEY LOCK |
| SETUP | MEMORY, PROFILE, RANGE, GATE |

RANGE picks the band analysed: FULL (50 Hz to 5 kHz; every MIDI note), BASS (40-300 Hz, for a bassline), MIDS
(120 Hz to 2 kHz, keeps hi-hats and kick out), HIGHS (400 Hz to 5 kHz). MIDI notes are banded by their pitch. NOTATION spells notes as the key does (AUTO), or always with
sharps or flats.

## How it works

The input is mixed to mono, low-passed and decimated to 11 kHz. Every 46 ms a worker thread (never the audio
thread) takes the last 371 ms, runs a 4096-point FFT, picks the spectral peaks (tones only: a click or a drum hit has none) and refines each one, and maps them
to the twelve pitch classes relative to the estimated tuning (a peak a quarter tone off a note counts for
nothing; a third or fifth harmonic of a stronger lower note is discounted as that note's colour). From that
chromagram:

- the **key** is the best of 24 rotated key profiles (Krumhansl-Kessler, Temperley, or a plain weighted scale)
  correlated with the chroma accumulated over MEMORY (10 s, 30 s, 1 min, or the whole song). The shown key moves
  only when another one has led by a margin for 1.5 s, and a relative (Ab major / F minor share every note) needs
  a larger lead;
- the **chord** is the best chord template against a 150 ms average, shown once it has won for 230 ms (a chord
  ringing into the next one is not a third chord), kept while nothing clear replaces it, cleared after silence;
- the **bass note** comes from the peaks under 260 Hz;
- the **tuning** is the circular mean of every peak's distance from the nearest note, over about 8 s.

MIDI notes make a chromagram of their own each frame (each pitch class weighs its loudest note, the lowest note under
260 Hz is the bass) and go through the same detectors. With SOURCE = BOTH, each source that has something is
normalised and counts half. The port is read on its own thread (libasound is loaded at run time, so the plugin still
loads without it); notes from it and from the VST host land in a table of atomics that the worker reads once a frame.

Audio under GATE counts for nothing, so silence between songs doesn't pull the key around. HOLD freezes
everything; RESET forgets everything heard.

## Look

A synthwave sunset: the wheel is drawn into a striped sun over a neon grid. `vst/art/gen.py` draws the page
backgrounds as SVG (`vst/art/wheel.svg`, `vst/art/scene.svg`); the wheel's geometry there matches the ring tiles in
`vst/layout.conf`. `vst/skin.css` styles the panels and controls.

## Performance

On an MPC Key 37 (mpc-vst-plugins' `tools/bench.sh`, `vst/bench.txt`): worst p99 4.3% of the 2.9 ms block, worst
block 8.3%, the analysis thread about 1% of a core: PASS.

## Building

Needs Docker (with QEMU for arm32v7) and a checkout of
[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) next to this repo (or `MPC_VST=/path`).

```sh
vst/test.sh              # offline: detectors, engine, settings, bad input, the MIDI port, real time (ASan, TSan), host test
vst/build.sh             # vst/build/keyscope.so, the skin and pluginlist-entry.xml (armhf, glibc <= 2.32)
tests/device.sh <host>   # the engine tests built for armv7 and run on a device over ssh (MPC is not touched)
```

`TESTING.md` lists every test and the on-device checklist. `vst/art/gen.py` redraws the page backgrounds.

### Releasing

`.github/workflows/release.yml` builds a draft release with mpc-vst-plugins' reusable workflow
(`workflow_dispatch`, pass a version). To release by hand instead, as mpc-vst-plugins' `docs/RELEASING.md`
describes:

```sh
B=vst/build; python3 $MPC_VST/tools/release.py --so $B/keyscope.so --skin "$B/skin/jacob-sabella - VST - Keyscope" \
  --entry $B/pluginlist-entry.xml --version 1.0.0 --repo jacob-sabella/mpc-vst-keyscope --license MIT \
  --bench vst/bench.txt -o dist
python3 $MPC_VST/tools/catalog_check.py dist/Keyscope-1.0.0-mpc-armv7.zip --catalog --expect-id keyscope \
  --expect-repo jacob-sabella/mpc-vst-keyscope
```

`tested.json` lists the devices and firmwares a version has been tested on (the catalog shows it as "Tested on").

The engine is plain C on mpc-vst-plugins' generic wrapper (`wrapper/engine.h`, `vst2_wrap.c`):

- `src/analyzer.c`: the decimator, the FFT and peak picking, tuning, the chromagram, the key profiles, chord
  templates, the bass note and the key-change log. No allocation and no locks; tested on its own
  (`tests/analyzer_test.c`).
- `src/keyscope.c`: the plugin: parameters and saved state, the audio thread (pass-through, decimation, a ring of
  the last 371 ms), the MIDI note table, the worker thread that runs the analysis, and every readout and lit tile.
- `src/seq_in.c`: the MIDI port: an ALSA sequencer client per instance, read on its own thread (`tests/seq_in_test.c`
  sends it real events where the machine has a sequencer).

Parameters are append-only (projects and Q-Links store them by index): add new ones at the end of
`vst/params.json`.

## License

MIT, see [LICENSE](LICENSE).
