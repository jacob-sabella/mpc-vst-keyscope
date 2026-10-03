/* The plugin engine end to end: synthesized chords through process(), the worker thread, the readouts,
 * the candidate tiles, lock, hold, reset and the saved state. Built by vst/test.sh with -DKS_TEST (process()
 * waits for the worker instead of skipping a frame, as the test runs faster than real time). */
#include "play.h"

static void msg(void *inst, int status, int d1, int d2) {
    uint8_t m[3] = {(uint8_t)status, (uint8_t)d1, (uint8_t)d2};
    E->midi(inst, m, 3);
}

static void chord_on(void *inst, const int *notes, int n, int on) {
    for (int i = 0; i < n; i++) msg(inst, on ? 0x90 : 0x80, notes[i], on ? 100 : 0);
}

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

    /* MIDI: notes into midi() with silence through process(); SOURCE is BOTH by default */
    void *c = E->create(NULL);
    CHECK(!strcmp(get(c, "source"), "0") && !strcmp(get(c, "midi_in"), "MIDI IN: OFF"), "source BOTH, no port in tests: %s", get(c, "midi_in"));
    static const int mD[] = {50, 62, 66, 69}, mG[] = {43, 62, 67, 71}, mA[] = {45, 61, 64, 69}, mEm[] = {40, 64, 67, 71};
    const int *prog[] = {mD, mG, mEm, mA};
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < 4; i++) {
            chord_on(c, prog[i], 4, 1);
            play(c, NULL, 0, 2, 0, &differs);
            chord_on(c, prog[i], 4, 0);
        }
    chord_on(c, mA, 4, 1);
    play(c, NULL, 0, 0.5f, 0, &differs);
    CHECK(!strcmp(get(c, "key_name"), "D Major"), "MIDI key: %s", get(c, "key_name"));
    CHECK(!strcmp(get(c, "chord"), "A"), "MIDI chord: %s", get(c, "chord"));
    CHECK(!strcmp(get(c, "status"), "MIDI: 4 NOTES"), "MIDI status: %s", get(c, "status"));
    CHECK(!strcmp(get(c, "note_2_on"), "1") && !strcmp(get(c, "note_5_on"), "1") && !strcmp(get(c, "note_10_on"), "1") &&
          !strcmp(get(c, "note_1_on"), "0"), "A C# E lit (A doubled), C not");
    chord_on(c, mA, 4, 0);
    static const int mD2[] = {38, 50, 62, 66, 69};   /* D in three octaves: F# and A still light */
    chord_on(c, mD2, 5, 1);
    play(c, NULL, 0, 1.0f, 0, &differs);
    CHECK(!strcmp(get(c, "note_3_on"), "1") && !strcmp(get(c, "note_7_on"), "1") && !strcmp(get(c, "note_10_on"), "1"),
          "D F# A lit with D tripled: %s %s %s", get(c, "note_3_on"), get(c, "note_7_on"), get(c, "note_10_on"));
    CHECK(!strcmp(get(c, "chord"), "D"), "chord D: %s", get(c, "chord"));
    CHECK(!strcmp(get(c, "tuning_read"), "TUNING: -"), "no tuning from MIDI: %s", get(c, "tuning_read"));
    msg(c, 0xB0, 123, 0);   /* all notes off */
    CHECK(!strcmp(get(c, "status"), "MIDI: 5 NOTES"), "before all notes off: %s", get(c, "status"));
    play(c, NULL, 0, 1.5f, 0, &differs);
    CHECK(!strcmp(get(c, "chord"), "-") && starts(get(c, "status"), "TOO QUIET") && strstr(get(c, "status"), "NO MIDI"),
          "all notes off: %s / %s", get(c, "chord"), get(c, "status"));

    /* a note struck and released between two frames still counts */
    msg(c, 0x90, 64, 90);
    msg(c, 0x80, 64, 0);
    play(c, NULL, 0, 0.05f, 0, &differs);
    CHECK(!strcmp(get(c, "note_5_on"), "1"), "a short E is seen");

    /* the sustain pedal holds released notes; lifting it lets them go, but not one struck again */
    play(c, NULL, 0, 1.5f, 0, &differs);
    msg(c, 0xB1, 64, 127);
    msg(c, 0x91, 60, 100);
    msg(c, 0x91, 67, 100);
    msg(c, 0x81, 60, 0);
    msg(c, 0x81, 67, 0);
    play(c, NULL, 0, 1.0f, 0, &differs);
    CHECK(!strcmp(get(c, "status"), "MIDI: 2 NOTES"), "pedal down: %s", get(c, "status"));
    msg(c, 0x91, 67, 100);   /* G again, held */
    msg(c, 0xB0, 64, 0);     /* the pedal on another channel: nothing changes */
    play(c, NULL, 0, 0.2f, 0, &differs);
    CHECK(!strcmp(get(c, "status"), "MIDI: 2 NOTES"), "another channel's pedal: %s", get(c, "status"));
    msg(c, 0xB1, 64, 0);
    play(c, NULL, 0, 0.2f, 0, &differs);
    CHECK(!strcmp(get(c, "status"), "MIDI: 1 NOTE"), "pedal up, G still held: %s", get(c, "status"));
    msg(c, 0x81, 67, 0);
    msg(c, 0x90, 60, 0);   /* note-on with velocity 0 is a note-off */
    play(c, NULL, 0, 0.2f, 0, &differs);

    /* SOURCE AUDIO ignores MIDI; MIDI ignores audio; a note-on with velocity 0 is a note-off */
    set(c, "source", "1");
    msg(c, 0x90, 60, 100);
    play(c, NULL, 0, 0.5f, 0, &differs);
    CHECK(starts(get(c, "status"), "TOO QUIET") && !strstr(get(c, "status"), "MIDI"), "SOURCE AUDIO: %s", get(c, "status"));
    CHECK(!strcmp(get(c, "midi_in"), "MIDI IN: NOT USED (SOURCE: AUDIO)"), "midi_in: %s", get(c, "midi_in"));
    set(c, "source", "2");
    static const int A4[] = {69};
    play(c, A4, 1, 0.5f, 0.1f, &differs);
    CHECK(!strcmp(get(c, "status"), "MIDI: 1 NOTE") && !strcmp(get(c, "note_10_on"), "0"), "SOURCE MIDI: %s", get(c, "status"));
    msg(c, 0x90, 60, 0);
    play(c, NULL, 0, 0.5f, 0, &differs);
    CHECK(!strcmp(get(c, "status"), "WAITING FOR MIDI NOTES"), "SOURCE MIDI, nothing held: %s", get(c, "status"));
    CHECK(strstr(get(c, "state"), "source=2") != NULL, "source is saved: %s", get(c, "state"));
    set(c, "source", "0");
    msg(c, 0x90, 64, 100);
    play(c, A4, 1, 0.5f, 0.1f, &differs);
    CHECK(starts(get(c, "status"), "LISTENING") && strstr(get(c, "status"), "+  MIDI: 1 NOTE"), "BOTH: %s", get(c, "status"));
    E->destroy(c);

    E->destroy(a);
    E->destroy(b);
    printf(fails ? "engine FAILED (%d)\n" : "engine PASSED\n", fails);
    return fails != 0;
}
