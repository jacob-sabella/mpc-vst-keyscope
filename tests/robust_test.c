/* Bad input through the plugin's entry points, under ASan/UBSan: unknown and malformed keys, out-of-range and
 * non-numeric values, garbage saved state, tiny text buffers on every key the skin reads, odd audio (silence, full
 * scale, DC, Nyquist, a click), and create/destroy churn including a destroy while the worker is busy. Built by
 * vst/test.sh with -DKS_TEST. */
#include "play.h"
#include "params.h"

/* every key the wrapper may read: the parameters, their "_on" lights and the readouts */
static const char *READOUTS[] = {"key_name", "key_info", "scale_notes", "chord", "trail", "tuning_read", "loudest",
                                 "outside", "status", "state", "reset", "cand_1", "cand_2", "cand_3", "cand_1_on",
                                 "cand_3_on", "note_1", "note_12", "note_1_on", "note_12_on", "ring_1_1", "ring_24_1",
                                 "ring_1_1_on", "ring_24_1_on", "log_1", "log_6"};

/* every key at buffer sizes 1..4 and 300: always NUL-terminated inside the buffer, nothing written past it */
static void small_buffers(void *a, const char *what) {
    char buf[300];
    for (int k = 0; k < NPARAMS + (int)(sizeof READOUTS / sizeof *READOUTS); k++) {
        const char *key = k < NPARAMS ? PARAMS[k].key : READOUTS[k - NPARAMS];
        for (int len = 1; len <= 5; len++) {
            int sz = len == 5 ? (int)sizeof buf : len;
            char *b = malloc(sz);   /* exactly sz bytes, so ASan sees any overrun */
            memset(b, 'x', sz);
            if (E->get_param(a, key, b, sz) > 0)   /* 0: not the engine's (the wrapper's own popup state) */
                CHECK(memchr(b, 0, sz) != NULL, "%s: %s with %d bytes is not terminated", what, key, sz);
            free(b);
        }
    }
}

static void feed(void *a, int16_t (*gen)(int i), int frames, int *differs) {
    int16_t in[256], out[256];
    for (int pos = 0; pos < frames; pos += 128) {
        for (int i = 0; i < 128; i++) in[2 * i] = in[2 * i + 1] = gen(pos + i);
        E->process(a, in, out, 128);
        if (memcmp(in, out, sizeof in)) *differs = 1;
    }
    usleep(20000);
}
static int16_t g_silence(int i) { (void)i; return 0; }
static int16_t g_full(int i) { return (int16_t)((i / 50) % 2 ? 32767 : -32768); }   /* a full-scale square, 441 Hz */
static int16_t g_dc(int i) { (void)i; return 20000; }
static int16_t g_nyquist(int i) { return (int16_t)(i % 2 ? 30000 : -30000); }
static int16_t g_click(int i) { return (int16_t)(i % 44100 == 0 ? 32767 : 0); }

int main(void) {
    E = mpc_engine();
    char buf[600];

    /* unknown, malformed and out-of-range keys are ignored, and read back as nothing */
    {
        void *a = E->create(NULL);
        static const char *bad[] = {"", "x", "lock_", "LOCK", "ring_0_1", "ring_25_1", "ring_1_1x", "ring_-1_1", "ring__1",
                                    "ring_99999_1", "cand_0", "cand_4", "cand_99", "note_0", "note_13", "log_0", "log_7",
                                    "ring_1_1_on_on", "cand_1x", "state;lock=3"};
        for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
            set(a, bad[i], "1");
            buf[0] = 0;
            CHECK(E->get_param(a, bad[i], buf, sizeof buf) <= 0, "%s reads back %s", bad[i], buf);
        }
        CHECK(!strcmp(get(a, "lock"), "0"), "a bad key changed the lock: %s", get(a, "lock"));
        set(a, "cand_1", "1");   /* a candidate with nothing heard yet: nothing to lock to */
        CHECK(!strcmp(get(a, "lock"), "0"), "an empty candidate locked: %s", get(a, "lock"));

        /* values clamp to the range; text that is no number reads as 0, NaN as the default */
        static const struct { const char *key, *val, *want; } vals[] = {
            {"lock", "25", "24"}, {"lock", "-3", "0"}, {"lock", "7.6", "8"}, {"lock", "1e9", "24"}, {"lock", "-1e9", "0"},
            {"gate", "0", "-20"}, {"gate", "-200", "-80"}, {"gate", "-40.4", "-40"}, {"memory", "9", "3"},
            {"profile", "abc", "0"}, {"range", "", "0"}, {"hold", "0.4", "0"}, {"hold", "0.6", "1"}, {"notation", " 2", "2"},
            {"chords", "1junk", "1"}, {"tuning", "nan", "0"}, {"gate", "inf", "-20"}, {"gate", "-inf", "-80"},
        };
        for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
            set(a, vals[i].key, vals[i].val);
            const char *v = get(a, vals[i].key);
            CHECK(!strcmp(v, vals[i].want), "%s = \"%s\" reads %s, want %s", vals[i].key, vals[i].val, v, vals[i].want);
        }
        small_buffers(a, "fresh");
        E->destroy(a);
    }

    /* garbage saved state: ignored where it makes no sense, the rest applied, nothing overruns */
    {
        void *a = E->create(NULL);
        static const char *states[] = {"", ";", "=", ";;;=;=", "lock", "lock=", "=5", "nokey=3", "lock=5;lock=6",
                                       "lock=5;;memory=2;", "lock=99;gate=-999;hold=7", "state=lock=3", "reset=1;lock=2"};
        for (unsigned i = 0; i < sizeof states / sizeof *states; i++) set(a, "state", states[i]);
        CHECK(!strcmp(get(a, "lock"), "2") && !strcmp(get(a, "memory"), "2") && !strcmp(get(a, "gate"), "-80") &&
              !strcmp(get(a, "hold"), "1"), "state: lock %s memory %s gate %s hold %s", get(a, "lock"), get(a, "memory"),
              get(a, "gate"), get(a, "hold"));
        char big[4096];   /* longer than the parser's buffer: cut, not overrun */
        int o = 0;
        while (o < (int)sizeof big - 16) o += snprintf(big + o, sizeof big - o, "lock=%d;", o % 25);
        set(a, "state", big);
        int lock = atoi(get(a, "lock"));
        CHECK(lock >= 0 && lock <= 24, "long state: lock %d", lock);
        E->get_param(a, "state", buf, sizeof buf);
        void *b = E->create(NULL);   /* the saved state round-trips */
        set(b, "state", buf);
        char buf2[600];
        E->get_param(b, "state", buf2, sizeof buf2);
        CHECK(!strcmp(buf, buf2), "state round trip: %s / %s", buf, buf2);
        E->destroy(b);
        E->destroy(a);
    }

    /* odd audio: passed through bit for bit, no crash, nothing claimed from it */
    {
        static const struct { const char *name; int16_t (*gen)(int); } sig[] = {
            {"silence", g_silence}, {"full scale", g_full}, {"DC", g_dc}, {"Nyquist", g_nyquist}, {"click", g_click}};
        for (unsigned i = 0; i < sizeof sig / sizeof *sig; i++) {
            void *a = E->create(NULL);
            int differs = 0;
            feed(a, sig[i].gen, 6 * KS_SR, &differs);
            CHECK(!differs, "%s: the output differs from the input", sig[i].name);
            if (sig[i].gen != g_full)
                CHECK(!strstr(get(a, "key_name"), "Major") && !strstr(get(a, "key_name"), "Minor"), "%s: a key from nothing: %s",
                      sig[i].name, get(a, "key_name"));
            small_buffers(a, sig[i].name);
            set(a, "reset", "1");
            feed(a, g_silence, KS_SR / 2, &differs);
            CHECK(!strcmp(get(a, "key_name"), "NO KEY YET") || !strcmp(get(a, "key_name"), "LISTENING..."), "%s: after reset %s",
                  sig[i].name, get(a, "key_name"));
            E->destroy(a);
        }
        void *a = E->create(NULL);   /* process() with fewer frames than a block, then odd sizes */
        int16_t in[2 * 128] = {0}, out[2 * 128];
        for (int f = 1; f <= 128; f += 9) E->process(a, in, out, f);
        E->render(a, out, 128);
        for (int i = 0; i < 256; i++) CHECK(out[i] == 0, "render is not silent");
        static const uint8_t note[3] = {0x90, 60, 100};
        E->midi(a, note, 3);   /* an effect ignores MIDI */
        E->destroy(a);
    }

    /* with a key and a full view: every text at every small size */
    {
        void *a = E->create(NULL);
        progression(a, 1, 2, 1.0f);
        set(a, "notation", "2");
        set(a, "chords", "1");
        small_buffers(a, "heard");
        E->destroy(a);
    }

    /* churn: 50 instances made and dropped, some mid-analysis; four at once */
    {
        int notes[4];
        int m = chord_notes(0, MAJ, 3, notes);
        for (int i = 0; i < 50; i++) {
            void *a = E->create(NULL);
            if (i % 2) {   /* destroy right after a burst, while the worker still has frames queued */
                int16_t in[256], out[256];
                for (int pos = 0; pos < 128 * 40; pos += 128) {
                    for (int j = 0; j < 128; j++) in[2 * j] = in[2 * j + 1] = (int16_t)(8000 * sinf(0.06f * (pos + j)));
                    E->process(a, in, out, 128);
                }
            }
            E->destroy(a);
        }
        void *many[4];
        for (int i = 0; i < 4; i++) many[i] = E->create(NULL);
        for (int i = 0; i < 4; i++) play(many[i], notes, m, 0.5f, 0.05f, NULL);
        for (int i = 3; i >= 0; i--) E->destroy(many[i]);
    }

    printf(fails ? "robust FAILED (%d)\n" : "robust PASSED\n", fails);
    return fails != 0;
}
