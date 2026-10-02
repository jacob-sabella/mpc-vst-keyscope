# Keyscope

An insert effect for MPC OS standalone devices (MPC Live, One, X, Key, Force) that listens to whatever passes
through it and names its **key**, **chord**, **bass note** and **tuning**, from the spectrum of the whole mix:
a sample, a full loop, a keyboard part, a band. The audio passes through untouched.

Built on [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (native VST2 for MPC's plugin host, with an
MPC screen skin and Q-Links).

## What it shows

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
- **Tuning**: where the music sits against A = 440 (a band tuned to 432 Hz reads -32 cents), followed when TUNING
  is AUTO so a detuned recording still lands on the right notes.

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

Audio under GATE counts for nothing, so silence between songs doesn't pull the key around. HOLD freezes
everything; RESET forgets everything heard.

## Pages and Q-Links

| Page | Q-Links 1-4 |
|---|---|
| KEY | KEY LOCK, MEMORY, GATE, HOLD |
| CHORDS | CHORDS, RANGE, GATE, HOLD |
| HISTORY | MEMORY, GATE, HOLD, KEY LOCK |
| SETUP | MEMORY, PROFILE, RANGE, GATE |

RANGE picks the band analysed: FULL (50 Hz to 5 kHz), BASS (40-300 Hz, for a bassline), MIDS (120 Hz to 2 kHz,
keeps hi-hats and kick out), HIGHS (400 Hz to 5 kHz). NOTATION spells notes as the key does (AUTO), or always with
sharps or flats.

## Look

A synthwave sunset: the wheel is drawn into a striped sun over a neon grid. `vst/art/gen.py` draws the page
backgrounds as SVG (`vst/art/wheel.svg`, `vst/art/scene.svg`); the wheel's geometry there matches the ring tiles in
`vst/layout.conf`. `vst/skin.css` styles the panels and controls.

## Building

Needs Docker (QEMU for arm32v7) and an mpc-vst-plugins checkout next to this repo (or `MPC_VST=<path>`):

```sh
vst/test.sh     # offline, x86, sanitizers: detectors, engine, settings, bad input, real time, the wrapper host test
vst/build.sh    # vst/build/keyscope.so, the skin, the plugin-list entry
```

See TESTING.md for every test and the on-device checklist. `tests/analyzer_test.c` synthesizes chord progressions in all 24 keys, clean, with drums and noise, and a band
35 cents flat, and checks the key under every profile, plus chords, sevenths, a slash chord, tuning, a key change,
the gate and RESET. `tests/engine_test.c` drives the plugin's own entry points (process, the worker thread, every
readout, the candidate tiles, KEY LOCK, HOLD, RESET and the saved state).

On an MPC Key 37 (`tools/bench.sh` in mpc-vst-plugins): worst block p99 2.3% of the 2.9 ms budget, the worker
thread under 1% of a core.

## License

MIT
