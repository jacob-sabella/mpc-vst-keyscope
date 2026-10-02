/* Keyscope: an MPC insert effect that listens and names the key, the chord, the bass note and the tuning of
 * whatever passes through it. The audio itself passes through unchanged.
 *
 * Threads: the audio thread (process) downmixes, decimates (analyzer.h) and hands a frame every KS_HOP to a
 * worker thread, which runs the FFT and the detectors and publishes a view under a mutex held only for a copy.
 * get_param (the host's UI thread and the wrapper's polling on the audio thread) reads that view. A frame
 * that arrives while the worker is still busy is skipped. */
#include "engine.h"
#include "analyzer.h"
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    P_LOCK, P_MEMORY, P_PROFILE, P_RANGE, P_CHORDS, P_TUNING, P_NOTATION, P_GATE, P_HOLD,
    NUM_P
};
static const struct { const char *key; int min, max, def; } PDEF[NUM_P] = {
    {"lock", 0, 24, 0},       /* 0 = AUTO, 1-24 = a key (index + 1) */
    {"memory", 0, MEM_COUNT - 1, MEM_30S},
    {"profile", 0, PROF_COUNT - 1, PROF_KRUMHANSL},
    {"range", 0, RANGE_COUNT - 1, RANGE_FULL},
    {"chords", 0, 1, CHORDS_TRIADS},
    {"tuning", 0, 1, 0},      /* 0 = auto, 1 = A = 440 */
    {"notation", 0, 2, 0},    /* auto, sharps, flats */
    {"gate", -80, -20, -55},
    {"hold", 0, 1, 0},
};
#define NUM_CAND 3
#define NUM_RING 24   /* the circle of fifths: ring_1..12 the major keys clockwise from C, ring_13..24 their relative minors */
#define TEXT_MAX 47   /* the wrapper shows a readout's first 47 characters */

typedef struct {   /* what the screen shows: a copy of the analyser's results */
    int key, top[3], ntrail, nlog, voiced_now, has_audio;
    float score[24], tuning_cents, loud_hz, level_db, fast[12];
    ks_chord_t chord, trail[KS_TRAIL];
    long log_frame[KS_LOG], frames, quiet_run;
    int log_key[KS_LOG];
} view_t;

typedef struct {
    _Atomic int p[NUM_P];
    atomic_int reset_req, ring_clear, busy, quit;   /* reset: the worker forgets, the audio thread empties the frame */

    /* audio thread */
    ks_decim_t dec;
    float ring[KS_N];
    int rpos, since;

    /* worker */
    float frame[KS_N];
    ks_t ks;
    pthread_t th;
    sem_t sem;
    int th_ok;

    pthread_mutex_t mu;
    view_t view;
} inst_t;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int find_param(const char *key) {
    for (int i = 0; i < NUM_P; i++) if (!strcmp(key, PDEF[i].key)) return i;
    return -1;
}

static void publish(inst_t *s) {
    const ks_t *k = &s->ks;
    view_t v;
    v.key = k->key;
    memcpy(v.top, k->top, sizeof v.top);
    memcpy(v.score, k->key_score, sizeof v.score);
    v.ntrail = k->ntrail;
    memcpy(v.trail, k->trail, sizeof v.trail);
    v.nlog = k->nlog;
    memcpy(v.log_frame, k->log_frame, sizeof v.log_frame);
    memcpy(v.log_key, k->log_key, sizeof v.log_key);
    v.chord = k->chord;
    v.tuning_cents = k->tuning_cents;
    v.loud_hz = k->loud_hz;
    v.level_db = k->level_db;
    v.voiced_now = k->voiced_now;
    v.has_audio = k->voiced > 0;
    v.frames = k->frames;
    v.quiet_run = k->quiet_run;
    memcpy(v.fast, k->fast_chroma, sizeof v.fast);
    pthread_mutex_lock(&s->mu);
    s->view = v;
    pthread_mutex_unlock(&s->mu);
}

static void *worker(void *arg) {
    inst_t *s = arg;
    for (;;) {
        sem_wait(&s->sem);
        if (atomic_load(&s->quit)) break;
        ks_t *k = &s->ks;
        k->memory = atomic_load(&s->p[P_MEMORY]);
        k->profile = atomic_load(&s->p[P_PROFILE]);
        k->range = atomic_load(&s->p[P_RANGE]);
        k->chords = atomic_load(&s->p[P_CHORDS]);
        k->auto_tune = atomic_load(&s->p[P_TUNING]) == 0;
        k->gate_db = (float)atomic_load(&s->p[P_GATE]);
        if (atomic_exchange(&s->reset_req, 0)) ks_reset(k);
        if (!atomic_load(&s->p[P_HOLD])) ks_frame(k, s->frame);
        publish(s);
        atomic_store(&s->busy, 0);
    }
    return NULL;
}

static void *create(const char *dir) {
    (void)dir;
    inst_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    for (int i = 0; i < NUM_P; i++) atomic_init(&s->p[i], PDEF[i].def);
    ks_decim_init(&s->dec);
    ks_init(&s->ks);
    pthread_mutex_init(&s->mu, NULL);
    publish(s);
    if (sem_init(&s->sem, 0, 0) == 0 && pthread_create(&s->th, NULL, worker, s) == 0) s->th_ok = 1;
    return s;
}

static void destroy(void *inst) {
    inst_t *s = inst;
    if (s->th_ok) {
        atomic_store(&s->quit, 1);
        sem_post(&s->sem);
        pthread_join(s->th, NULL);
        sem_destroy(&s->sem);
    }
    pthread_mutex_destroy(&s->mu);
    free(s);
}

static void midi(void *inst, const uint8_t *msg, int len) { (void)inst; (void)msg; (void)len; }

static void process(void *inst, const int16_t *in, int16_t *out, int frames) {
    inst_t *s = inst;
    float mono[256], dec[80];
    memcpy(out, in, sizeof(int16_t) * 2 * frames);   /* the audio passes through untouched */
    if (atomic_exchange(&s->ring_clear, 0)) {   /* RESET: nothing heard before it reaches a frame */
        memset(s->ring, 0, sizeof s->ring);
        s->since = 0;
    }
    for (int off = 0; off < frames; off += 256) {
        int n = frames - off < 256 ? frames - off : 256;
        for (int i = 0; i < n; i++) mono[i] = (in[2 * (off + i)] + in[2 * (off + i) + 1]) * (0.5f / 32768.0f);
        int m = ks_decim(&s->dec, mono, n, dec);
        for (int i = 0; i < m; i++) {
            s->ring[s->rpos] = dec[i];
            s->rpos = (s->rpos + 1) % KS_N;
            if (++s->since < KS_HOP) continue;
            s->since = 0;
#ifdef KS_TEST
            while (s->th_ok && atomic_load(&s->busy)) sched_yield();   /* offline tests run faster than real time */
#endif
            if (!s->th_ok || atomic_load(&s->busy)) continue;   /* the worker is behind: skip this frame */
            memcpy(s->frame, s->ring + s->rpos, sizeof(float) * (KS_N - s->rpos));
            memcpy(s->frame + (KS_N - s->rpos), s->ring, sizeof(float) * s->rpos);
            atomic_store(&s->busy, 1);
            sem_post(&s->sem);
        }
    }
}

static void render(void *inst, int16_t *out, int frames) { (void)inst; memset(out, 0, sizeof(int16_t) * 2 * frames); }

/* ---- text ---- */

static int shown_key(inst_t *s, const view_t *v) {
    int lock = atomic_load(&s->p[P_LOCK]);
    return lock > 0 ? lock - 1 : v->key;
}

static int flats_for(inst_t *s, int key) {
    int n = atomic_load(&s->p[P_NOTATION]);
    return n == 1 ? 0 : n == 2 ? 1 : ks_key_uses_flats(key);
}

static void chord_text(ks_chord_t c, int flats, char *buf, int len) {
    if (c.root < 0) { snprintf(buf, len, "-"); return; }
    int n = snprintf(buf, len, "%s%s", ks_pc_name(c.root, flats), ks_chord_types[c.type].suffix);
    if (c.bass >= 0 && n > 0 && n < len) snprintf(buf + n, len - n, "/%s", ks_pc_name(c.bass, flats));
}

static int pct(float r) { return clampi((int)lroundf(r * 100.0f), 0, 100); }

/* ring tile t (0-23) -> its key: outer tile i is the major key i fifths above C, inner tile i its relative minor */
static int ring_key(int t) {
    int root = (t % 12) * 7 % 12;
    return t < 12 ? root : 12 + (root + 9) % 12;
}

/* a key's place on the circle: its relative major's number of fifths above C */
static int ring_pos(int key) {
    int major = KEY_MINOR(key) ? (KEY_ROOT(key) + 3) % 12 : KEY_ROOT(key);
    return major * 7 % 12;
}

/* MPC draws a tile's text left-aligned (24 px Titillium Web, 12 px in from each side); on the wheel it should sit
 * in the middle, so lead it with spaces. Widths are rough advances at that size. */
static int centred(char *buf, int len, const char *text, int area) {
    float w = 0;
    for (const char *c = text; *c; c++) w += *c == 'm' ? 21.0f : *c == '#' || *c == 'b' ? 13.0f : 14.0f;
    int pad = clampi((int)lroundf((area - w) / 2.0f / 5.5f), 0, 12);
    return snprintf(buf, len, "%*s%s", pad, "", text);
}

static int index_of(const char *key, const char *prefix, const char *suffix, int max) {
    size_t pl = strlen(prefix);
    int n = 0, used = 0;
    if (strncmp(key, prefix, pl) || sscanf(key + pl, "%d%n", &n, &used) != 1 || strcmp(key + pl + used, suffix)) return -1;
    return n >= 1 && n <= max ? n - 1 : -1;
}

static void state_text(inst_t *s, char *buf, int len) {
    int n = 0;
    for (int i = 0; i < NUM_P && n < len; i++)
        n += snprintf(buf + n, len - n, "%s%s=%d", i ? ";" : "", PDEF[i].key, atomic_load(&s->p[i]));
}

static void set_param(void *inst, const char *key, const char *val);

static void load_state(inst_t *s, const char *st) {
    char buf[512], *save = NULL;
    snprintf(buf, sizeof buf, "%s", st);
    for (char *kv = strtok_r(buf, ";", &save); kv; kv = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(kv, '=');
        if (!eq) continue;
        *eq = 0;
        if (find_param(kv) >= 0) set_param(s, kv, eq + 1);
    }
}

static void set_param(void *inst, const char *key, const char *val) {
    inst_t *s = inst;
    int i = find_param(key), c;
    if (i >= 0) atomic_store(&s->p[i], clampi((int)lroundf((float)atof(val)), PDEF[i].min, PDEF[i].max));
    else if (!strcmp(key, "reset")) {
        if (atof(val) > 0.5) { atomic_store(&s->ring_clear, 1); atomic_store(&s->reset_req, 1); }
    }
    else if ((c = index_of(key, "cand_", "", NUM_CAND)) >= 0) {   /* a tap on a candidate locks to it; again unlocks */
        if (atof(val) <= 0.5) return;
        view_t v;
        pthread_mutex_lock(&s->mu);
        v = s->view;
        pthread_mutex_unlock(&s->mu);
        int key2 = v.top[c];
        if (key2 < 0) return;
        atomic_store(&s->p[P_LOCK], atomic_load(&s->p[P_LOCK]) == key2 + 1 ? 0 : key2 + 1);
    }
    else if ((c = index_of(key, "ring_", "_1", NUM_RING)) >= 0) {   /* a tap on the wheel locks to that key; again unlocks */
        if (atof(val) <= 0.5) return;
        int key2 = ring_key(c);
        atomic_store(&s->p[P_LOCK], atomic_load(&s->p[P_LOCK]) == key2 + 1 ? 0 : key2 + 1);
    }
    else if (!strcmp(key, "state")) load_state(s, val);
}

static int get_param(void *inst, const char *key, char *buf, int len) {
    inst_t *s = inst;
    int i = find_param(key), n;
    if (i >= 0) return snprintf(buf, len, "%d", atomic_load(&s->p[i]));
    if (!strcmp(key, "state")) { state_text(s, buf, len); return (int)strlen(buf) + 1; }
    if (!strcmp(key, "reset")) return snprintf(buf, len, "0");

    view_t v;
    pthread_mutex_lock(&s->mu);
    v = s->view;
    pthread_mutex_unlock(&s->mu);
    int key0 = shown_key(s, &v), flats = flats_for(s, key0), locked = atomic_load(&s->p[P_LOCK]) > 0;

    if (!strcmp(key, "key_name")) {
        if (key0 >= 0) return snprintf(buf, len, "%s", ks_key_name(key0));
        return snprintf(buf, len, v.has_audio ? "LISTENING..." : "NO KEY YET");
    }
    if (!strcmp(key, "key_info")) {
        if (key0 < 0) return snprintf(buf, len, "-");
        return snprintf(buf, len, "%s %d%%   RELATIVE: %s", locked ? "LOCKED, MATCH" : "MATCH", pct(v.score[key0]),
                        ks_key_name(ks_relative(key0)));
    }
    if (!strcmp(key, "scale_notes")) {
        if (key0 < 0) return snprintf(buf, len, "-");
        static const int MAJ[7] = {0, 2, 4, 5, 7, 9, 11}, MIN[7] = {0, 2, 3, 5, 7, 8, 10};
        const int *iv = KEY_MINOR(key0) ? MIN : MAJ;
        int o = 0;
        for (int d = 0; d < 7 && o < len; d++) o += snprintf(buf + o, len - o, d ? "  %s" : "%s", ks_pc_name(KEY_ROOT(key0) + iv[d], flats));
        return o;
    }
    if (!strcmp(key, "chord")) { chord_text(v.chord, flats, buf, len); return (int)strlen(buf) + 1; }
    if (!strcmp(key, "trail")) {   /* oldest first, as heard: as many of the newest as fit a readout */
        char c[KS_TRAIL][16];
        int from = 0, total = (int)strlen("CHORDS: "), o;
        if (!v.ntrail) return snprintf(buf, len, "CHORDS: -");
        for (int t = 0; t < v.ntrail; t++) {
            chord_text(v.trail[t], flats, c[t], sizeof c[t]);
            int add = (int)strlen(c[t]) + (t ? 3 : 0);
            if (total + add > TEXT_MAX) break;
            total += add;
            from = t;
        }
        o = snprintf(buf, len, "CHORDS: ");
        for (int t = from; t >= 0 && o < len; t--) o += snprintf(buf + o, len - o, t == from ? "%s" : " > %s", c[t]);
        return o;
    }
    if (!strcmp(key, "tuning_read")) {
        if (!v.has_audio) return snprintf(buf, len, "TUNING: -");
        int c = (int)lroundf(v.tuning_cents);
        return snprintf(buf, len, "TUNING: A4 = %.1f HZ (%+d C)%s", 440.0 * pow(2.0, v.tuning_cents / 1200.0), c,
                        atomic_load(&s->p[P_TUNING]) ? ", NOT USED" : "");
    }
    if (!strcmp(key, "loudest")) {
        if (v.loud_hz <= 0) return snprintf(buf, len, "LOUDEST NOTE: -");
        float m = 12.0f * log2f(v.loud_hz / 440.0f) + 69.0f;
        int note = (int)lroundf(m), cents = (int)lroundf((m - note) * 100.0f);
        return snprintf(buf, len, "LOUDEST NOTE: %s%d  %.1f HZ  %+d C", ks_pc_name(note, flats), note / 12 - 1, v.loud_hz, cents);
    }
    if (!strcmp(key, "outside")) {
        if (key0 < 0 || !v.voiced_now) return snprintf(buf, len, "-");
        float mx = 0;
        for (int p = 0; p < 12; p++) mx = fmaxf(mx, v.fast[p]);
        int o = 0;
        for (int p = 0; p < 12 && o < len; p++)
            if (mx > 0 && v.fast[p] >= 0.5f * mx && !ks_in_key(key0, p)) o += snprintf(buf + o, len - o, "%s%s", o ? " " : "OUTSIDE THE KEY: ", ks_pc_name(p, flats));
        return o ? o : snprintf(buf, len, "ALL IN KEY");
    }
    if (!strcmp(key, "status")) {
        if (atomic_load(&s->p[P_HOLD])) return snprintf(buf, len, "HOLD: NOT LISTENING");
        if (!v.voiced_now) return snprintf(buf, len, "TOO QUIET (%d DB, GATE %d)", (int)lroundf(fmaxf(v.level_db, -99)), atomic_load(&s->p[P_GATE]));
        return snprintf(buf, len, "LISTENING  %d DB", (int)lroundf(v.level_db));
    }
    if ((n = index_of(key, "cand_", "", NUM_CAND)) >= 0) {
        if (v.top[n] < 0 || !v.has_audio) return snprintf(buf, len, " ");
        return snprintf(buf, len, "%s  %d%%", ks_key_name(v.top[n]), pct(v.score[v.top[n]]));
    }
    if ((n = index_of(key, "cand_", "_on", NUM_CAND)) >= 0) return snprintf(buf, len, "%d", v.top[n] >= 0 && v.has_audio && v.top[n] == key0);
    if ((n = index_of(key, "note_", "", 12)) >= 0) return snprintf(buf, len, "%s", ks_pc_name(n, flats));
    if ((n = index_of(key, "note_", "_on", 12)) >= 0) {
        float mx = 0;
        for (int p = 0; p < 12; p++) mx = fmaxf(mx, v.fast[p]);
        return snprintf(buf, len, "%d", v.voiced_now && mx > 0 && v.fast[n] >= 0.5f * mx);
    }
    if ((n = index_of(key, "ring_", "_1", NUM_RING)) >= 0) {
        int k = ring_key(n);
        char name[8];
        snprintf(name, sizeof name, "%s%s", ks_pc_name(KEY_ROOT(k), flats_for(s, k)), KEY_MINOR(k) ? "m" : "");
        return centred(buf, len, name, n < 12 ? 96 - 24 : 76 - 24);   /* the tile widths in vst/layout.conf */
    }
    if ((n = index_of(key, "ring_", "_1_on", NUM_RING)) >= 0) {   /* lit: the key's six chords, I IV V and ii iii vi */
        if (key0 < 0) return snprintf(buf, len, "0");
        int d = ((n % 12) - ring_pos(key0) + 12) % 12;
        return snprintf(buf, len, "%d", d <= 1 || d == 11);
    }
    if ((n = index_of(key, "log_", "", KS_LOG)) >= 0) {   /* newest first */
        if (n >= v.nlog) return snprintf(buf, len, " ");
        int t = (int)ks_seconds(v.log_frame[n]);
        return snprintf(buf, len, "%d:%02d   %s", t / 60, t % 60, ks_key_name(v.log_key[n]));
    }
    return 0;
}

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, process};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
