/* The plugin engine end to end: synthesized chords through process(), the worker thread, the readouts,
 * the candidate tiles, lock, hold, reset and the saved state. Built by vst/test.sh with -DKS_TEST (process()
 * waits for the worker instead of skipping a frame, as the test runs faster than real time). */
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

/* one chord for secs seconds through process(): partials 1-5 of each note, a soft decay */
static void play(void *inst, const int *notes, int n, float secs, float amp, int *out_differs) {
    static int16_t in[256], out[256];
    int total = (int)(secs * KS_SR);
    float ph[8][5] = {{0}};
    for (int pos = 0; pos < total; pos += 128) {
        for (int i = 0; i < 128; i++) {
            float x = 0, env = expf(-(float)(pos + i) / (1.5f * KS_SR));
            for (int j = 0; j < n; j++) {
                float f0 = 440.0f * powf(2.0f, (notes[j] - 69) / 12.0f);
                for (int h = 1; h <= 5; h++) {
                    ph[j][h - 1] += 2 * (float)M_PI * f0 * h / KS_SR;
                    x += amp / h * env * sinf(ph[j][h - 1]);
                }
            }
            int16_t v = (int16_t)lrintf(fmaxf(-1, fminf(1, x)) * 32000.0f);
            in[2 * i] = v;
            in[2 * i + 1] = (int16_t)(v / 2);
        }
        E->process(inst, in, out, 128);
        if (memcmp(in, out, sizeof in)) *out_differs = 1;
    }
    usleep(20000);   /* the last frame's results reach the view */
}

static const char *get(void *inst, const char *key) {
    static char buf[256];
    buf[0] = 0;
    E->get_param(inst, key, buf, sizeof buf);
    return buf;
}

int main(void) {
    E = mpc_engine();
    CHECK(E->process != NULL, "an effect provides process()");
    void *a = E->create(NULL), *b = E->create(NULL);
    int differs = 0;
    CHECK(!strcmp(get(a, "key_name"), "PLAY SOMETHING"), "fresh: %s", get(a, "key_name"));

    /* G major: G D Em C, twice, with roots in the bass */
    static const int G[] = {43, 67, 71, 74}, D[] = {38, 66, 69, 74}, Em[] = {40, 67, 71, 76}, C[] = {36, 67, 72, 76};
    for (int r = 0; r < 2; r++) {
        play(a, G, 4, 2, 0.06f, &differs);
        play(a, D, 4, 2, 0.06f, &differs);
        play(a, Em, 4, 2, 0.06f, &differs);
        play(a, C, 4, 2, 0.06f, &differs);
    }
    CHECK(!differs, "the audio passes through unchanged");
    CHECK(!strcmp(get(a, "key_name"), "G Major"), "key: %s", get(a, "key_name"));
    CHECK(strstr(get(a, "key_info"), "RELATIVE: E Minor") != NULL, "info: %s", get(a, "key_info"));
    CHECK(!strcmp(get(a, "scale_notes"), "G  A  B  C  D  E  F#"), "scale: %s", get(a, "scale_notes"));
    CHECK(!strcmp(get(a, "chord"), "C"), "chord: %s", get(a, "chord"));
    CHECK(!strcmp(get(a, "trail"), "CHORDS: G > D > Em > C > G > D > Em > C"), "trail: %s", get(a, "trail"));
    CHECK(strlen(get(a, "trail")) <= 47, "the trail fits a readout");
    CHECK(strstr(get(a, "cand_1"), "G Major") == get(a, "cand_1") || strstr(get(a, "cand_1"), "G Major"), "cand 1: %s", get(a, "cand_1"));
    CHECK(!strcmp(get(a, "cand_1_on"), "1") && !strcmp(get(a, "cand_2_on"), "0"), "the shown key's candidate is lit");
    CHECK(!strcmp(get(a, "note_7"), "F#") && !strcmp(get(a, "note_11"), "A#"), "note names: %s %s", get(a, "note_7"), get(a, "note_11"));
    CHECK(!strcmp(get(a, "note_1_on"), "1") && !strcmp(get(a, "note_2_on"), "0"), "C is sounding, C# not");
    CHECK(!strcmp(get(a, "outside"), "ALL IN KEY"), "outside: %s", get(a, "outside"));
    CHECK(!strcmp(get(a, "log_1"), "0:01   G Major"), "log 1: %s", get(a, "log_1"));
    CHECK(!strcmp(get(a, "log_2"), " "), "log 2 empty: '%s'", get(a, "log_2"));
    CHECK(strstr(get(a, "tuning_read"), "(+0 C)") || strstr(get(a, "tuning_read"), "(-1 C)") || strstr(get(a, "tuning_read"), "(+1 C)"),
          "tuning within a cent of 440: %s", get(a, "tuning_read"));
    CHECK(strstr(get(a, "loudest"), "LOUDEST NOTE: ") == get(a, "loudest"), "loudest: %s", get(a, "loudest"));
    CHECK(strstr(get(a, "status"), "LISTENING") == get(a, "status"), "status: %s", get(a, "status"));
    CHECK(!strcmp(get(b, "key_name"), "PLAY SOMETHING"), "instance b heard nothing");

    /* a chord outside the key */
    static const int Bb[] = {46, 70, 74, 77};
    play(a, Bb, 4, 1.0f, 0.06f, &differs);
    CHECK(!strcmp(get(a, "chord"), "A#"), "Bb major in G spells with sharps: %s", get(a, "chord"));
    CHECK(strstr(get(a, "outside"), "A#") != NULL, "outside: %s", get(a, "outside"));
    E->set_param(a, "notation", "2");
    CHECK(!strcmp(get(a, "chord"), "Bb"), "notation flats: %s", get(a, "chord"));
    E->set_param(a, "notation", "0");

    /* lock: tap the second candidate, then again to follow the audio */
    char c2[64];
    snprintf(c2, sizeof c2, "%s", get(a, "cand_2"));
    E->set_param(a, "cand_2", "1");
    CHECK(strncmp(get(a, "key_name"), c2, strlen(get(a, "key_name"))) == 0, "locked to the second candidate: %s (%s)", get(a, "key_name"), c2);
    CHECK(strcmp(get(a, "lock"), "0") != 0 && strstr(get(a, "key_info"), "LOCKED") == get(a, "key_info"), "lock shows: %s", get(a, "key_info"));
    CHECK(!strcmp(get(a, "cand_2_on"), "1"), "the locked candidate is lit");
    E->set_param(a, "cand_2", "1");
    CHECK(!strcmp(get(a, "lock"), "0") && !strcmp(get(a, "key_name"), "G Major"), "tapped again: back to %s", get(a, "key_name"));

    /* state round trip */
    E->set_param(a, "memory", "3");
    E->set_param(a, "gate", "-40");
    E->set_param(a, "lock", "10");
    char st[512];
    snprintf(st, sizeof st, "%s", get(a, "state"));
    E->set_param(b, "state", st);
    CHECK(!strcmp(get(b, "memory"), "3") && !strcmp(get(b, "gate"), "-40") && !strcmp(get(b, "lock"), "10"), "state restores: %s", st);
    CHECK(!strcmp(get(b, "key_name"), "A Major"), "a restored lock shows its key: %s", get(b, "key_name"));
    E->set_param(b, "state", "");
    E->set_param(a, "lock", "0");

    /* hold freezes, reset forgets */
    E->set_param(a, "hold", "1");
    play(a, Bb, 4, 1.0f, 0.06f, &differs);
    CHECK(strstr(get(a, "status"), "HOLD") == get(a, "status"), "status: %s", get(a, "status"));
    E->set_param(a, "hold", "0");
    E->set_param(a, "reset", "1");
    static const int quiet[] = {60};
    play(a, quiet, 1, 0.5f, 0.00001f, &differs);
    CHECK(!strcmp(get(a, "key_name"), "PLAY SOMETHING") && !strcmp(get(a, "trail"), "CHORDS: -"), "reset: %s / %s", get(a, "key_name"), get(a, "trail"));
    CHECK(strstr(get(a, "status"), "TOO QUIET") == get(a, "status"), "quiet: %s", get(a, "status"));

    E->destroy(a);
    E->destroy(b);
    printf(fails ? "engine FAILED (%d)\n" : "engine PASSED\n", fails);
    return fails != 0;
}
