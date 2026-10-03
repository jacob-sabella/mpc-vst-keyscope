/* Offline checks of the analysis core on synthesized audio: harmonic tones (a few partials, a soft decay),
 * chord progressions in every key, drums and noise on top, detuned bands. Built by vst/test.sh under
 * ASan/UBSan. Exit 1 on failure; `analyzer_test -v` prints every result. */
#include "analyzer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, verbose;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } \
                           else if (verbose) { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define SR KS_SR
static unsigned rng = 1;
static float noise(void) { rng = rng * 1664525u + 1013904223u; return (float)((rng >> 8) & 0xffff) / 32768.0f - 1.0f; }

typedef struct {
    float *x;
    int n, cap;
} buf_t;

static void grow(buf_t *b, int n) {
    if (b->n + n <= b->cap) return;
    b->cap = (b->n + n) * 2;
    b->x = realloc(b->x, sizeof(float) * b->cap);
}

/* a note: partials 1..6 at 1/h, attack 10 ms, exponential decay */
static void add_note(float *x, int len, float midi, float cents, float amp) {
    float f0 = 440.0f * powf(2.0f, (midi - 69.0f + cents / 100.0f) / 12.0f);
    for (int h = 1; h <= 6; h++) {
        float f = f0 * h;
        if (f > 5500) break;
        float a = amp / h, ph = 0, dph = 2.0f * (float)M_PI * f / SR;
        for (int i = 0; i < len; i++) {
            float env = fminf(1.0f, i / (0.01f * SR)) * expf(-(float)i / (1.2f * SR));
            x[i] += a * env * sinf(ph);
            ph += dph;
        }
    }
}

typedef struct { float cents, drums, noise; } style_t;

/* one bar: a chord (pitch classes from a root and intervals) in the middle register, its root in the bass */
static void add_chord(buf_t *b, int root, const int *iv, int n, float secs, style_t st) {
    int len = (int)(secs * SR);
    grow(b, len);
    float *x = b->x + b->n;
    memset(x, 0, sizeof(float) * len);
    for (int i = 0; i < n; i++) add_note(x, len, 60 + ((root + iv[i]) % 12), st.cents, 0.08f);
    add_note(x, len, 36 + root % 12, st.cents, 0.12f);
    for (int beat = 0; st.drums > 0 && beat < (int)(secs * 2); beat++) {   /* kick and a noisy snare at 120 bpm */
        float *k = x + beat * SR / 2;
        int kl = SR / 6;
        if ((beat + 1) * SR / 2 > len) break;
        float ph = 0;
        for (int i = 0; i < kl; i++) {
            float f = 50 + 100 * expf(-i / (0.02f * SR));
            ph += 2 * (float)M_PI * f / SR;
            k[i] += st.drums * expf(-i / (0.05f * SR)) * sinf(ph) * (beat % 2 == 0);
            k[i] += st.drums * 0.6f * expf(-i / (0.03f * SR)) * noise() * (beat % 2 == 1);
        }
    }
    for (int i = 0; st.noise > 0 && i < len; i++) x[i] += st.noise * noise();
    b->n += len;
}

static void silence(buf_t *b, float secs) {
    int len = (int)(secs * SR);
    grow(b, len);
    memset(b->x + b->n, 0, sizeof(float) * len);
    b->n += len;
}

/* run the audio through the decimator and the analyser as the plugin does */
static void run(ks_t *k, const buf_t *b) {
    static ks_decim_t d;
    static float ring[KS_N], frame[KS_N], dec[64];
    static int rpos, since;
    static int inited;
    if (!inited) { ks_decim_init(&d); inited = 1; }
    for (int off = 0; off < b->n; off += 128) {
        int n = b->n - off < 128 ? b->n - off : 128;
        int m = ks_decim(&d, b->x + off, n, dec);
        for (int i = 0; i < m; i++) {
            ring[rpos] = dec[i];
            rpos = (rpos + 1) % KS_N;
            if (++since >= KS_HOP) {
                since = 0;
                for (int j = 0; j < KS_N; j++) frame[j] = ring[(rpos + j) % KS_N];
                ks_frame(k, frame);
            }
        }
    }
}

/* MIDI: the notes held (MIDI numbers, velocity 100) for secs, one frame every KS_HOP; audio is the frame given */
static void hold_notes(ks_t *k, const int *notes, int n, float secs, const float *audio) {
    static float zero[KS_N];
    memset(k->midi, 0, sizeof k->midi);
    for (int i = 0; i < n; i++) k->midi[notes[i]] = 0.4f + 0.6f * 100 / 127.0f;
    for (int f = 0; f < (int)(secs * KS_FS / KS_HOP); f++) ks_frame(k, audio ? audio : zero);
}

static const int MAJ[3] = {0, 4, 7}, MIN[3] = {0, 3, 7}, DOM7[4] = {0, 4, 7, 10}, MAJ7[4] = {0, 4, 7, 11};

/* I V vi IV, four bars each, twice */
static void pop_progression(buf_t *b, int tonic, style_t st) {
    for (int rep = 0; rep < 2; rep++) {
        add_chord(b, tonic, MAJ, 3, 2.0f, st);
        add_chord(b, tonic + 7, MAJ, 3, 2.0f, st);
        add_chord(b, tonic + 9, MIN, 3, 2.0f, st);
        add_chord(b, tonic + 5, MAJ, 3, 2.0f, st);
    }
}

/* i iv V i in harmonic minor */
static void minor_progression(buf_t *b, int tonic, style_t st) {
    for (int rep = 0; rep < 2; rep++) {
        add_chord(b, tonic, MIN, 3, 2.0f, st);
        add_chord(b, tonic + 5, MIN, 3, 2.0f, st);
        add_chord(b, tonic + 7, MAJ, 3, 2.0f, st);
        add_chord(b, tonic, MIN, 3, 2.0f, st);
    }
}

static int key_test(int profile, int minor, int tonic, style_t st) {
    ks_t *k = malloc(sizeof *k);
    buf_t b = {0};
    ks_init(k);
    k->profile = profile;
    k->memory = MEM_SONG;
    if (minor) minor_progression(&b, tonic, st); else pop_progression(&b, tonic, st);
    run(k, &b);
    int want = tonic % 12 + (minor ? 12 : 0), got = k->key;
    if (verbose && got != want)
        printf("     profile %d: %s heard as %s (%.2f; want %.2f)\n", profile, ks_key_name(want), ks_key_name(got),
               got >= 0 ? k->key_score[got] : 0.0f, k->key_score[want]);
    free(k);
    free(b.x);
    return got == want;
}

static void chord_name(ks_chord_t c, char buf[32]) {
    if (c.root < 0) { snprintf(buf, 32, "N.C."); return; }
    int n = snprintf(buf, 32, "%s%s", ks_pc_name(c.root, 0), ks_chord_types[c.type].suffix);
    if (c.bass >= 0) snprintf(buf + n, 32 - n, "/%s", ks_pc_name(c.bass, 0));
}

int main(int argc, char **argv) {
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    style_t clean = {0, 0, 0}, busy = {0, 0.25f, 0.01f}, detuned = {-35, 0.2f, 0.005f};

    /* keys: every major and minor tonic, clean and with drums, per profile */
    for (int p = 0; p < PROF_COUNT && !(argc > 2 && !strcmp(argv[2], "-q")); p++) {
        int ok[3] = {0, 0, 0};
        for (int t = 0; t < 12; t++) {
            ok[0] += key_test(p, 0, t, clean) + key_test(p, 1, t, clean);
            ok[1] += key_test(p, 0, t, busy) + key_test(p, 1, t, busy);
            if (t % 3 == 0) ok[2] += key_test(p, 0, t, detuned) + key_test(p, 1, t, detuned);
        }
        printf("profile %d: clean %d/24, drums+noise %d/24, detuned -35c %d/8\n", p, ok[0], ok[1], ok[2]);
        CHECK(ok[0] >= 22 && ok[1] >= 20 && ok[2] >= 6, "profile %d finds the key", p);
    }

    /* chords, tuning, bass, the trail and the quiet behaviour on one run */
    {
        ks_t *k = malloc(sizeof *k);
        buf_t b = {0};
        char got[32];
        ks_init(k);
        k->chords = CHORDS_SEVENTHS;
        style_t st = {0, 0.2f, 0.005f};
        struct { int root; const int *iv; int n; const char *want; } prog[] = {
            {0, MAJ, 3, "C"}, {9, MIN, 3, "Am"}, {7, DOM7, 4, "G7"}, {5, MAJ7, 4, "Fmaj7"}, {2, MIN, 3, "Dm"}, {4, MAJ, 3, "E"},
        };
        for (int i = 0; i < 6; i++) {
            buf_t one = {0};
            add_chord(&one, prog[i].root, prog[i].iv, prog[i].n, 2.0f, st);
            run(k, &one);
            chord_name(k->chord, got);
            CHECK(!strcmp(got, prog[i].want), "chord %d: %s (want %s)", i, got, prog[i].want);
            free(one.x);
        }
        if (verbose) for (int i = k->ntrail - 1; i >= 0; i--) { chord_name(k->trail[i], got); printf("     trail %s\n", got); }
        CHECK(k->ntrail == 6, "trail holds the six chords (%d)", k->ntrail);
        chord_name(k->trail[5], got);
        CHECK(!strcmp(got, "C"), "oldest in the trail is C (%s)", got);
        CHECK(fabsf(k->tuning_cents) < 5, "tuning reads about 0 cents (%.1f)", k->tuning_cents);
        CHECK(k->loud_hz > 60 && k->loud_hz < 90, "the bass note is the loudest peak (%.1f Hz)", k->loud_hz);
        silence(&b, 1.0f);
        run(k, &b);
        CHECK(k->chord.root < 0, "a second of silence clears the chord");
        CHECK(k->key >= 0, "but the key stays (%s)", ks_key_name(k->key));
        free(k);
        free(b.x);
    }

    /* tuning: a band 30 cents sharp reads +30 and still names its chords */
    {
        ks_t *k = malloc(sizeof *k);
        buf_t b = {0};
        char got[32];
        ks_init(k);
        style_t sharp = {30, 0, 0};
        for (int i = 0; i < 4; i++) add_chord(&b, 2, MAJ, 3, 2.0f, sharp);
        run(k, &b);
        chord_name(k->chord, got);
        CHECK(fabsf(k->tuning_cents - 30) < 4, "tuning reads +30 cents (%.1f)", k->tuning_cents);
        CHECK(!strcmp(got, "D"), "sharp D major reads D (%s)", got);
        free(k);
        free(b.x);
    }

    /* a slash chord: C over E in the bass */
    {
        ks_t *k = malloc(sizeof *k);
        buf_t b = {0};
        char got[32];
        ks_init(k);
        int len = 3 * SR;
        grow(&b, len);
        memset(b.x, 0, sizeof(float) * len);
        for (int i = 0; i < 3; i++) add_note(b.x, len, 60 + MAJ[i], 0, 0.08f);
        add_note(b.x, len, 40, 0, 0.14f);   /* E2 */
        b.n = len;
        run(k, &b);
        chord_name(k->chord, got);
        CHECK(!strcmp(got, "C/E"), "C over E reads C/E (%s)", got);
        free(k);
        free(b.x);
    }

    /* a key change: C major, then A-flat major; with a 10 s memory the shown key follows and the log has both */
    {
        ks_t *k = malloc(sizeof *k);
        buf_t b = {0};
        ks_init(k);
        k->memory = MEM_10S;
        pop_progression(&b, 0, busy);
        pop_progression(&b, 8, busy);
        pop_progression(&b, 8, busy);
        run(k, &b);
        if (verbose) for (int i = k->nlog - 1; i >= 0; i--) printf("     log %5.1f s %s\n", ks_seconds(k->log_frame[i]), ks_key_name(k->log_key[i]));
        CHECK(k->key == 8, "the key moved to Ab Major (%s)", ks_key_name(k->key));
        CHECK(k->nlog >= 2 && k->log_key[k->nlog - 1] == 0, "the log starts at C Major (%d entries)", k->nlog);
        ks_reset(k);
        CHECK(k->key < 0 && k->nlog == 0 && k->ntrail == 0, "reset forgets it all");
        free(k);
        free(b.x);
    }

    /* the gate: audio under it shows nothing */
    {
        ks_t *k = malloc(sizeof *k);
        buf_t b = {0};
        ks_init(k);
        style_t quiet = {0, 0, 0};
        pop_progression(&b, 0, quiet);
        for (int i = 0; i < b.n; i++) b.x[i] *= 0.0005f;   /* about -70 dBFS */
        run(k, &b);
        CHECK(k->key < 0 && k->chord.root < 0, "under the gate: no key, no chord");
        free(k);
        free(b.x);
    }

    /* MIDI: a progression in D major played as notes, no audio; the chords, the slash chord, the key */
    {
        ks_t *k = malloc(sizeof *k);
        char got[32];
        ks_init(k);
        k->source = SRC_MIDI;
        static const int D[] = {50, 62, 66, 69}, G[] = {43, 62, 67, 71}, A[] = {45, 61, 64, 69}, Bm[] = {47, 62, 66, 71},
                         Em[] = {40, 64, 67, 71};
        for (int r = 0; r < 3; r++) {
            hold_notes(k, D, 4, 2, NULL);
            hold_notes(k, G, 4, 2, NULL);
            hold_notes(k, Em, 4, 2, NULL);
            hold_notes(k, A, 4, 2, NULL);
        }
        chord_name(k->chord, got);
        CHECK(!strcmp(got, "A"), "MIDI: the last chord is A (%s)", got);
        CHECK(k->key == 2, "MIDI: D Major (%s)", ks_key_name(k->key));
        CHECK(k->midi_now && !k->audio_now && k->midi_notes == 4, "MIDI: 4 notes, no audio (%d %d %d)", k->midi_now, k->audio_now, k->midi_notes);
        static const int D_over_Fs[] = {42, 62, 66, 69};
        hold_notes(k, D_over_Fs, 4, 1, NULL);
        chord_name(k->chord, got);
        CHECK(!strcmp(got, "D/F#"), "MIDI: the lowest note is the bass (%s)", got);
        hold_notes(k, Bm, 4, 1, NULL);
        hold_notes(k, NULL, 0, 2, NULL);
        CHECK(!k->voiced_now && k->chord.root < 0 && k->key == 2, "MIDI: notes off, the chord clears and the key stays");

        /* RANGE BASS reads only the notes under 300 Hz */
        k->range = RANGE_BASS;
        static const int high[] = {72, 76, 79};
        hold_notes(k, high, 3, 0.2f, NULL);
        CHECK(!k->midi_now && k->midi_notes == 0, "RANGE BASS leaves out notes above 300 Hz (%d)", k->midi_notes);
        k->range = RANGE_FULL;
        static const int extremes[] = {21, 108};
        hold_notes(k, extremes, 2, 0.2f, NULL);
        CHECK(k->midi_notes == 2, "RANGE FULL takes every MIDI note, A0 to C8 (%d)", k->midi_notes);
        free(k);
    }

    /* SOURCE: MIDI ignores the audio, AUDIO ignores MIDI, BOTH hears either */
    {
        ks_t *k = malloc(sizeof *k);
        static float tone[KS_N];
        for (int i = 0; i < KS_N; i++) tone[i] = 0.3f * sinf(2 * (float)M_PI * 440.0f * i / KS_FS);   /* A4 */
        static const int C_maj[] = {48, 60, 64, 67};
        ks_init(k);
        k->source = SRC_MIDI;
        hold_notes(k, C_maj, 4, 1, tone);
        CHECK(!k->audio_now && k->fast_chroma[9] < 0.01f && k->chord.root == 0, "SOURCE MIDI: the A4 tone counts for nothing");
        ks_init(k);
        k->source = SRC_AUDIO;
        hold_notes(k, C_maj, 4, 1, NULL);
        CHECK(!k->voiced_now && !k->midi_now && k->chord.root < 0, "SOURCE AUDIO: MIDI counts for nothing");
        ks_init(k);
        k->source = SRC_BOTH;
        hold_notes(k, C_maj, 4, 1, NULL);
        CHECK(k->midi_now && k->chord.root == 0, "SOURCE BOTH, MIDI only: C (%d)", k->chord.root);
        hold_notes(k, C_maj, 4, 1, tone);
        CHECK(k->midi_now && k->audio_now, "SOURCE BOTH: both heard");
        CHECK(k->fast_chroma[9] > 0.2f && k->fast_chroma[0] > 0.05f, "SOURCE BOTH: half each (A %.2f, C %.2f)", k->fast_chroma[9], k->fast_chroma[0]);
        CHECK(k->audio_frames > 0, "the tuning has audio to read");
        free(k);
    }

    /* spelling helpers */
    CHECK(ks_key_uses_flats(5) && ks_key_uses_flats(2 + 12) && !ks_key_uses_flats(7) && !ks_key_uses_flats(4 + 12),
          "F major and D minor use flats, G major and E minor sharps");
    CHECK(ks_key_uses_flats(3 + 12) && !ks_key_uses_flats(6) && !ks_key_uses_flats(1 + 12),
          "Eb minor is spelled with flats like its name; F# major and C# minor with sharps");
    CHECK(ks_relative(0) == 21 && ks_relative(21) == 0, "C major and A minor are relatives");
    CHECK(ks_in_key(21, 7) && ks_in_key(21, 8) && !ks_in_key(21, 1), "A minor: G and G# in key, C# not");

    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
