/* Helpers shared by the engine-level tests: synthesized notes through the plugin's own process(), and its
 * text values. Each test file includes this once. */
#ifndef KS_PLAY_H
#define KS_PLAY_H
#pragma GCC diagnostic ignored "-Wunused-function"   /* each test uses some of these */
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "engine.h"
#include "analyzer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const mpc_engine_t *E;

typedef struct {
    float cents;     /* detune of every note */
    int partials;    /* harmonics per note (default 5) */
    float decay_s;   /* envelope time constant (default 1.5 s) */
} tone_t;

/* MIDI notes for secs seconds through process(): partials 1..n at 1/h, a soft decay; the right channel at half
 * level. *out_differs is set if the output ever differs from the input (the plugin must pass audio through). */
static void play_tone(void *inst, const int *notes, int n, float secs, float amp, tone_t t, int *out_differs) {
    static int16_t in[256], out[256];
    int total = (int)(secs * KS_SR), parts = t.partials ? t.partials : 5;
    float decay = t.decay_s > 0 ? t.decay_s : 1.5f, ph[16][12] = {{0}};
    for (int pos = 0; pos < total; pos += 128) {
        for (int i = 0; i < 128; i++) {
            float x = 0, env = expf(-(float)(pos + i) / (decay * KS_SR));
            for (int j = 0; j < n && j < 16; j++) {
                float f0 = 440.0f * powf(2.0f, (notes[j] - 69 + t.cents / 100.0f) / 12.0f);
                for (int h = 1; h <= parts && h <= 12; h++) {
                    if (f0 * h > 0.45f * KS_SR) break;
                    ph[j][h - 1] += 2 * (float)M_PI * f0 * h / KS_SR;
                    x += amp / h * env * sinf(ph[j][h - 1]);
                }
            }
            int16_t v = (int16_t)lrintf(fmaxf(-1, fminf(1, x)) * 32000.0f);
            in[2 * i] = v;
            in[2 * i + 1] = (int16_t)(v / 2);
        }
        E->process(inst, in, out, 128);
        if (out_differs && memcmp(in, out, sizeof in)) *out_differs = 1;
    }
    usleep(20000);   /* the last frame's results reach the view */
}

static void play(void *inst, const int *notes, int n, float secs, float amp, int *out_differs) {
    play_tone(inst, notes, n, secs, amp, (tone_t){0, 0, 0}, out_differs);
}

/* get_param into one of a few rotating buffers, so two values can be compared in one expression */
static const char *get(void *inst, const char *key) {
    static char buf[8][256];
    static int i;
    i = (i + 1) % 8;
    buf[i][0] = 0;
    E->get_param(inst, key, buf[i], sizeof buf[i]);
    return buf[i];
}

/* a tile's text without the leading spaces that centre it on the wheel */
static const char *trimmed(void *inst, const char *key) {
    const char *s = get(inst, key);
    return s + strspn(s, " ");
}

static int starts(const char *s, const char *prefix) { return !strncmp(s, prefix, strlen(prefix)); }

static void set(void *inst, const char *key, const char *val) { E->set_param(inst, key, val); }

/* which of ring_1..24 are lit, as "1 2 3 13 14 15 " */
static const char *lit_ring(void *inst) {
    static char lit[96];
    lit[0] = 0;
    for (int r = 1; r <= 24; r++) {
        char k[24];
        snprintf(k, sizeof k, "ring_%d_1_on", r);
        if (!strcmp(get(inst, k), "1")) snprintf(lit + strlen(lit), sizeof lit - strlen(lit), "%d ", r);
    }
    return lit;
}

static const int MAJ[] = {0, 4, 7}, MIN[] = {0, 3, 7};

/* a chord: the root in the bass (octave 2) and the triad from middle C up */
static int chord_notes(int root, const int *iv, int niv, int *out) {
    out[0] = 36 + (root % 12);
    for (int i = 0; i < niv; i++) out[1 + i] = 60 + (root % 12) + iv[i];
    return niv + 1;
}

/* I V vi IV in a major key, bars of secs each */
static void progression(void *inst, int tonic, int reps, float secs) {
    static const int roots[4] = {0, 7, 9, 5}, minor[4] = {0, 0, 1, 0};
    int n[8];
    for (int r = 0; r < reps; r++)
        for (int c = 0; c < 4; c++) {
            int m = chord_notes(tonic + roots[c], minor[c] ? MIN : MAJ, 3, n);
            play(inst, n, m, secs, 0.06f, NULL);
        }
}
#endif
