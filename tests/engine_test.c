/* The plugin engine end to end: synthesized chords through process(), the worker thread, the readouts,
 * the candidate tiles, lock, hold, reset and the saved state. Built by vst/test.sh with -DKS_TEST (process()
 * waits for the worker instead of skipping a frame, as the test runs faster than real time). */
#include "play.h"

int main(void) {
    E = mpc_engine();
    CHECK(E->process != NULL, "an effect provides process()");
    void *a = E->create(NULL), *b = E->create(NULL);
    int differs = 0;
    CHECK(!strcmp(get(a, "key_name"), "NO KEY YET"), "fresh: %s", get(a, "key_name"));

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
    CHECK(starts(get(a, "cand_1"), "G Major") || strstr(get(a, "cand_1"), "G Major"), "cand 1: %s", get(a, "cand_1"));
    CHECK(!strcmp(get(a, "cand_1_on"), "1") && !strcmp(get(a, "cand_2_on"), "0"), "the shown key's candidate is lit");
    CHECK(!strcmp(get(a, "note_7"), "F#") && !strcmp(get(a, "note_11"), "A#"), "note names: %s %s", get(a, "note_7"), get(a, "note_11"));
    CHECK(!strcmp(get(a, "note_1_on"), "1") && !strcmp(get(a, "note_2_on"), "0"), "C is sounding, C# not");
    CHECK(!strcmp(get(a, "outside"), "ALL IN KEY"), "outside: %s", get(a, "outside"));
    CHECK(!strcmp(get(a, "log_1"), "0:01   G Major"), "log 1: %s", get(a, "log_1"));
    CHECK(!strcmp(get(a, "log_2"), " "), "log 2 empty: '%s'", get(a, "log_2"));
    CHECK(strstr(get(a, "tuning_read"), "(+0 C)") || strstr(get(a, "tuning_read"), "(-1 C)") || strstr(get(a, "tuning_read"), "(+1 C)"),
          "tuning within a cent of 440: %s", get(a, "tuning_read"));
    CHECK(starts(get(a, "loudest"), "LOUDEST NOTE: "), "loudest: %s", get(a, "loudest"));
    CHECK(starts(get(a, "status"), "LISTENING"), "status: %s", get(a, "status"));
    CHECK(!strcmp(get(b, "key_name"), "NO KEY YET"), "instance b heard nothing");

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
    CHECK(strcmp(get(a, "lock"), "0") != 0 && starts(get(a, "key_info"), "LOCKED"), "lock shows: %s", get(a, "key_info"));
    CHECK(!strcmp(get(a, "cand_2_on"), "1"), "the locked candidate is lit");
    E->set_param(a, "cand_2", "0");   /* MPC writes a lit tile's opposite: 0 */
    CHECK(!strcmp(get(a, "lock"), "0") && !strcmp(get(a, "key_name"), "G Major"), "tapped again: back to %s", get(a, "key_name"));

    /* the circle of fifths: G major lights C G D and Am Em Bm; a tap on the wheel locks, again unlocks */
    static const char *ring_text[][2] = {{"ring_1_1", "C"}, {"ring_2_1", "G"}, {"ring_7_1", "F#"}, {"ring_8_1", "Db"},
                                         {"ring_12_1", "F"}, {"ring_13_1", "Am"}, {"ring_19_1", "Ebm"}, {"ring_24_1", "Dm"}};
    for (size_t r = 0; r < sizeof ring_text / sizeof ring_text[0]; r++)
        CHECK(!strcmp(get(a, ring_text[r][0]) + strspn(get(a, ring_text[r][0]), " "), ring_text[r][1]), "%s: %s", ring_text[r][0], get(a, ring_text[r][0]));
    char lit[32] = "";
    for (int r = 1; r <= 24; r++) {
        char k[24];
        snprintf(k, sizeof k, "ring_%d_1_on", r);
        if (!strcmp(get(a, k), "1")) snprintf(lit + strlen(lit), sizeof lit - strlen(lit), "%d ", r);
    }
    CHECK(!strcmp(lit, "1 2 3 13 14 15 "), "G major lights: %s", lit);
    E->set_param(a, "ring_22_1", "1");
    CHECK(!strcmp(get(a, "key_name"), "C Minor") && !strcmp(get(a, "ring_10_1_on"), "1") && !strcmp(get(a, "ring_2_1_on"), "0"),
          "wheel tap locks: %s", get(a, "key_name"));
    E->set_param(a, "ring_22_1", "0");
    CHECK(!strcmp(get(a, "lock"), "0") && !strcmp(get(a, "key_name"), "G Major"), "wheel tap again: back to %s", get(a, "key_name"));
    E->set_param(a, "ring_14_1", "0");   /* Em, lit as a chord of G major: a tap still locks to it */
    CHECK(!strcmp(get(a, "key_name"), "E Minor"), "a lit chord tile locks: %s", get(a, "key_name"));
    E->set_param(a, "ring_14_1", "0");
    CHECK(!strcmp(get(a, "lock"), "0"), "and unlocks: %s", get(a, "lock"));

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
    CHECK(starts(get(a, "status"), "HOLD"), "status: %s", get(a, "status"));
    E->set_param(a, "hold", "0");
    E->set_param(a, "reset", "1");
    static const int quiet[] = {60};
    play(a, quiet, 1, 0.5f, 0.00001f, &differs);
    CHECK(!strcmp(get(a, "key_name"), "NO KEY YET") && !strcmp(get(a, "trail"), "CHORDS: -"), "reset: %s / %s", get(a, "key_name"), get(a, "trail"));
    CHECK(starts(get(a, "status"), "TOO QUIET"), "quiet: %s", get(a, "status"));

    E->destroy(a);
    E->destroy(b);
    printf(fails ? "engine FAILED (%d)\n" : "engine PASSED\n", fails);
    return fails != 0;
}
