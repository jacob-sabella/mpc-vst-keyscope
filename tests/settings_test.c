/* Every setting through the plugin's own entry points, on synthesized audio: MEMORY, PROFILE, RANGE, CHORDS,
 * TUNING, NOTATION, GATE, the key-change log, the wheel for a minor key and ANIMATION. Built by vst/test.sh with -DKS_TEST. */
#include "play.h"

static void *fresh(void) { return E->create(NULL); }

int main(int argc, char **argv) {
    int verbose = argc > 1 && !strcmp(argv[1], "-v");
    E = mpc_engine();
    int n[8];

    /* MEMORY: 30 s of C major, then 12 s of an A-flat major cadence. A 10 s memory follows the change and logs both keys;
     * SONG still weighs the whole run and stays in C. */
    for (int mem = 0; mem < MEM_COUNT; mem++) {
        void *a = fresh();
        char v[16];
        snprintf(v, sizeof v, "%d", mem);
        set(a, "memory", v);
        progression(a, 0, 4, 1.9f);
        for (int rep = 0; rep < 2; rep++)   /* I IV V I in A-flat: no vi, so not heard as F minor */
            for (int c = 0; c < 4; c++) {
                static const int cad[4] = {8, 1, 3, 8};
                int m = chord_notes(cad[c], MAJ, 3, n);
                play(a, n, m, 1.5f, 0.06f, NULL);
            }
        if (verbose) printf("memory %d: %s | log %s / %s\n", mem, get(a, "key_name"), get(a, "log_1"), get(a, "log_2"));
        if (mem == MEM_10S) {
            CHECK(!strcmp(get(a, "key_name"), "Ab Major"), "10 s memory follows the change: %s", get(a, "key_name"));
            /* newest first; the window passes through keys between the two (C minor shares A-flat and E-flat) */
            int c_at = 0;
            for (int i = 2; i <= 6 && !c_at; i++) {
                char key[16];
                snprintf(key, sizeof key, "log_%d", i);
                if (strstr(get(a, key), "C Major")) c_at = i;
            }
            CHECK(strstr(get(a, "log_1"), "Ab Major") && c_at, "log: %s / %s / %s", get(a, "log_1"), get(a, "log_2"), get(a, "log_3"));
            CHECK(starts(get(a, "log_1"), "0:3") || starts(get(a, "log_1"), "0:4"), "log time: %s", get(a, "log_1"));
        }
        if (mem == MEM_SONG) CHECK(!strcmp(get(a, "key_name"), "C Major"), "song memory keeps C: %s", get(a, "key_name"));
        E->destroy(a);
    }

    /* PROFILE: each one finds E major and A minor */
    for (int p = 0; p < PROF_COUNT; p++) {
        char v[16];
        snprintf(v, sizeof v, "%d", p);
        void *a = fresh();
        set(a, "profile", v);
        progression(a, 4, 2, 1.5f);
        CHECK(!strcmp(get(a, "key_name"), "E Major"), "profile %d: E major heard as %s", p, get(a, "key_name"));
        E->destroy(a);
        a = fresh();
        set(a, "profile", v);
        static const int am[4][4] = {{45, 69, 72, 76}, {38, 62, 65, 69}, {40, 64, 68, 71}, {45, 69, 72, 76}};   /* Am Dm E Am */
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 4; c++) play(a, am[c], 4, 1.5f, 0.06f, NULL);
        CHECK(!strcmp(get(a, "key_name"), "A Minor"), "profile %d: A minor heard as %s", p, get(a, "key_name"));
        E->destroy(a);
    }

    /* CHORDS: G7 is "G" with TRIADS and "G7" with + 7THS; a plain triad stays a triad either way */
    {
        static const int g7[] = {43, 67, 71, 74, 77};
        void *a = fresh();
        play(a, g7, 5, 2.0f, 0.06f, NULL);
        CHECK(!strcmp(get(a, "chord"), "G"), "triads: G7 reads %s", get(a, "chord"));
        set(a, "chords", "1");
        play(a, g7, 5, 2.0f, 0.06f, NULL);
        CHECK(!strcmp(get(a, "chord"), "G7"), "sevenths: G7 reads %s", get(a, "chord"));
        int m = chord_notes(9, MIN, 3, n);
        play(a, n, m, 2.0f, 0.06f, NULL);
        CHECK(!strcmp(get(a, "chord"), "Am"), "sevenths: Am reads %s", get(a, "chord"));
        E->destroy(a);
    }

    /* TUNING: a band 40 cents flat. AUTO measures it and still names the chords; A = 440 says the measure is
     * not used. */
    {
        void *a = fresh();
        int m = chord_notes(2, MAJ, 3, n);   /* D */
        play_tone(a, n, m, 10.0f, 0.06f, (tone_t){-40, 5, 4.0f}, NULL);
        if (verbose) printf("tuning auto: %s | %s\n", get(a, "tuning_read"), get(a, "chord"));
        CHECK(strstr(get(a, "tuning_read"), "(-40 C)") || strstr(get(a, "tuning_read"), "(-39 C)") || strstr(get(a, "tuning_read"), "(-41 C)"),
              "auto tuning: %s", get(a, "tuning_read"));
        CHECK(!strcmp(get(a, "chord"), "D"), "a flat band's chord: %s", get(a, "chord"));
        set(a, "tuning", "1");
        CHECK(strstr(get(a, "tuning_read"), "NOT USED") != NULL, "A = 440: %s", get(a, "tuning_read"));
        E->destroy(a);
    }

    /* NOTATION: AUTO spells as the key does, SHARPS and FLATS always; the note tiles and the wheel follow */
    {
        void *a = fresh();
        progression(a, 5, 2, 1.5f);   /* F major: B-flat */
        CHECK(!strcmp(get(a, "key_name"), "F Major"), "F major: %s", get(a, "key_name"));
        CHECK(!strcmp(get(a, "note_11"), "Bb") && strstr(get(a, "scale_notes"), "Bb"), "auto in F: %s / %s", get(a, "note_11"), get(a, "scale_notes"));
        CHECK(!strcmp(trimmed(a, "ring_7_1"), "F#") && !strcmp(trimmed(a, "ring_8_1"), "Db"), "auto wheel: %s %s", trimmed(a, "ring_7_1"), trimmed(a, "ring_8_1"));
        set(a, "notation", "1");
        CHECK(!strcmp(get(a, "note_11"), "A#") && strstr(get(a, "scale_notes"), "A#"), "sharps: %s / %s", get(a, "note_11"), get(a, "scale_notes"));
        CHECK(!strcmp(trimmed(a, "ring_8_1"), "C#") && !strcmp(trimmed(a, "ring_19_1"), "D#m"), "sharps wheel: %s %s", trimmed(a, "ring_8_1"), trimmed(a, "ring_19_1"));
        set(a, "notation", "2");
        CHECK(!strcmp(get(a, "note_2"), "Db") && !strcmp(trimmed(a, "ring_7_1"), "Gb"), "flats: %s %s", get(a, "note_2"), trimmed(a, "ring_7_1"));
        E->destroy(a);
    }

    /* GATE: the same chord at about -36 dBFS counts under a -55 gate and not under -20 */
    {
        void *a = fresh();
        int m = chord_notes(0, MAJ, 3, n);
        set(a, "gate", "-20");
        play(a, n, m, 2.0f, 0.004f, NULL);
        CHECK(starts(get(a, "status"), "TOO QUIET") && !strcmp(get(a, "chord"), "-"), "gate -20: %s / %s", get(a, "status"), get(a, "chord"));
        set(a, "gate", "-55");
        play(a, n, m, 2.0f, 0.004f, NULL);
        CHECK(starts(get(a, "status"), "LISTENING") && !strcmp(get(a, "chord"), "C"), "gate -55: %s / %s", get(a, "status"), get(a, "chord"));
        E->destroy(a);
    }

    /* RANGE: a G major progression in the middle register under a loud high line on the notes of D-flat major
     * (2.3-3.7 kHz). MIDS stops at 2 kHz and hears G; HIGHS hears the line. BASS hears only the bass notes. */
    for (int r = 0; r < RANGE_COUNT; r++) {
        void *a = fresh();
        char v[16];
        snprintf(v, sizeof v, "%d", r);
        set(a, "range", v);
        static const int roots[4] = {7, 2, 4, 0}, minor[4] = {0, 0, 1, 0}, high[4][3] = {{97, 101, 104}, {99, 102, 106}, {97, 102, 105}, {99, 104, 106}};
        for (int rep = 0; rep < 2; rep++)
            for (int c = 0; c < 4; c++) {
                int notes[8], m = chord_notes(roots[c], minor[c] ? MIN : MAJ, 3, notes);
                for (int h = 0; h < 3; h++) notes[m + h] = high[c][h];
                play(a, notes, m + 3, 1.5f, 0.05f, NULL);
            }
        if (verbose) printf("range %d: %s | %s\n", r, get(a, "key_name"), get(a, "chord"));
        if (r == RANGE_MIDS) CHECK(!strcmp(get(a, "key_name"), "G Major"), "mids ignores the high line: %s", get(a, "key_name"));
        if (r == RANGE_HIGHS) CHECK(strcmp(get(a, "key_name"), "G Major") != 0, "highs hears the high line: %s", get(a, "key_name"));
        E->destroy(a);
    }

    /* the wheel for a minor key: A minor lights the same six as C major (F C G, Dm Am Em) */
    {
        void *a = fresh();
        set(a, "lock", "22");   /* 1 + A minor (21) */
        CHECK(!strcmp(get(a, "key_name"), "A Minor"), "locked: %s", get(a, "key_name"));
        CHECK(!strcmp(lit_ring(a), "1 2 12 13 14 24 "), "A minor lights: %s", lit_ring(a));
        set(a, "lock", "0");
        CHECK(!strcmp(lit_ring(a), ""), "no key, nothing lit: %s", lit_ring(a));
        E->destroy(a);
    }

    /* ANIMATION: on by default, one frame at a time, 3 a second through all sixteen; off shows none */
    {
        void *a = fresh();
        static int16_t z[2 * 147];
        char key[24];
        int seen = 0, ok = 1;
        CHECK(!strcmp(get(a, "anim"), "1"), "animation on by default: %s", get(a, "anim"));
        for (int t = 0; t < 6 * 44100; t += 147) {   /* 6 s of silence, read every 147 frames */
            int lit = 0, which = -1;
            for (int k = 1; k <= 16; k++) {
                snprintf(key, sizeof key, "bg_%d_on", k);
                if (!strcmp(get(a, key), "1")) { lit++; which = k; }
            }
            ok &= lit == 1;
            if (which > 0) seen |= 1 << which;
            E->process(a, z, z, 147);
        }
        CHECK(ok, "exactly one frame shown at a time");
        CHECK(seen == 0x1fffe, "all sixteen frames shown in 6 s: mask %#x", seen);
        set(a, "anim", "0");
        for (int k = 1; k <= 16; k++) {
            snprintf(key, sizeof key, "bg_%d_on", k);
            CHECK(!strcmp(get(a, key), "0"), "animation off: %s is %s", key, get(a, key));
        }
        CHECK(strstr(get(a, "state"), "anim=0") != NULL, "anim saved: %s", get(a, "state"));
        E->destroy(a);
    }

    printf(fails ? "settings FAILED (%d)\n" : "settings PASSED\n", fails);
    return fails != 0;
}
