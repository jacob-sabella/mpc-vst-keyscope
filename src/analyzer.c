/* Keyscope's analysis core (see analyzer.h). */
#include "analyzer.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FAST_TAU 0.25f
#define CHORD_TAU 0.15f
#define BASS_TAU 0.15f
#define TUNE_TAU 8.0f
#define KEY_SWITCH_S 1.5f      /* a new best key must lead this long to be shown, */
#define KEY_MIN_S 1.0f         /* voiced audio needed before any key is shown */
#define KEY_MARGIN 0.03f       /* ... and lead by this much, */
#define KEY_MARGIN_REL 0.10f   /* or by this much over its relative: Ab major and F minor share every note and
                                * trade places with whichever chord came last */
#define CHORD_HOLD 5           /* frames (230 ms) a new chord must win before it is shown: a crossfade
                                * between two chords (C ringing into Am reads Am7) is shorter */
#define CHORD_MIN 0.80f        /* best template score under this: no new chord */
#define CHORD_QUIET_S 0.4f     /* this long under the gate clears the chord */
#define PEAK_FLOOR_DB -45.0f   /* peaks this far under the frame's strongest are ignored */
#define BASS_MAX_HZ 260.0f

static const float RANGE_LO[RANGE_COUNT] = {50.0f, 40.0f, 120.0f, 400.0f};
static const float RANGE_HI[RANGE_COUNT] = {5000.0f, 300.0f, 2000.0f, 5000.0f};
static const float MEMORY_S[MEM_COUNT] = {10.0f, 30.0f, 60.0f, 0.0f};   /* 0 = no decay */

/* Krumhansl-Kessler probe-tone ratings */
static const float KK_MAJ[12] = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
static const float KK_MIN[12] = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};
/* Temperley's revision of the same (Music and Probability) */
static const float TP_MAJ[12] = {5.0f, 2.0f, 3.5f, 2.0f, 4.5f, 4.0f, 2.0f, 4.5f, 2.0f, 3.5f, 1.5f, 4.0f};
static const float TP_MIN[12] = {5.0f, 2.0f, 3.5f, 4.5f, 2.0f, 4.0f, 2.0f, 4.5f, 3.5f, 2.0f, 1.5f, 4.0f};
/* the scale, tonic triad weighted up */
static const float SM_MAJ[12] = {2.0f, 0.0f, 1.0f, 0.0f, 1.5f, 1.0f, 0.0f, 1.5f, 0.0f, 1.0f, 0.0f, 1.0f};
static const float SM_MIN[12] = {2.0f, 0.0f, 1.0f, 1.5f, 0.0f, 1.0f, 0.0f, 1.5f, 1.0f, 0.0f, 0.5f, 0.5f};

const ks_chord_type_t ks_chord_types[] = {
    {"", 3, {0, 4, 7, 0}, 1.00f, 0},
    {"m", 3, {0, 3, 7, 0}, 1.00f, 0},
    {"dim", 3, {0, 3, 6, 0}, 0.95f, 0},
    {"aug", 3, {0, 4, 8, 0}, 0.93f, 0},
    {"sus2", 3, {0, 2, 7, 0}, 0.95f, 0},
    {"sus4", 3, {0, 5, 7, 0}, 0.95f, 0},
    {"5", 2, {0, 7, 0, 0}, 0.90f, 0},
    {"7", 4, {0, 4, 7, 10}, 0.98f, 1},
    {"maj7", 4, {0, 4, 7, 11}, 0.98f, 1},
    {"m7", 4, {0, 3, 7, 10}, 0.98f, 1},
    {"m7b5", 4, {0, 3, 6, 10}, 0.96f, 1},
};
const int ks_num_chord_types = sizeof ks_chord_types / sizeof ks_chord_types[0];

static const char *SHARP[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *FLAT[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
static const char *KEY_NAMES[24] = {
    "C Major", "Db Major", "D Major", "Eb Major", "E Major", "F Major", "F# Major", "G Major", "Ab Major", "A Major",
    "Bb Major", "B Major", "C Minor", "C# Minor", "D Minor", "Eb Minor", "E Minor", "F Minor", "F# Minor", "G Minor",
    "G# Minor", "A Minor", "Bb Minor", "B Minor"};

const char *ks_pc_name(int pc, int flats) { return (flats ? FLAT : SHARP)[((pc % 12) + 12) % 12]; }
const char *ks_key_name(int key) { return key >= 0 && key < 24 ? KEY_NAMES[key] : "-"; }
int ks_relative(int key) { return KEY_MINOR(key) ? (KEY_ROOT(key) + 3) % 12 : (KEY_ROOT(key) + 9) % 12 + 12; }
float ks_seconds(long frames) { return (float)frames * KS_HOP / KS_FS; }

int ks_key_uses_flats(int key) {
    if (key < 0) return 0;
    int major = KEY_MINOR(key) ? (KEY_ROOT(key) + 3) % 12 : KEY_ROOT(key);   /* the relative major's signature */
    /* F Bb Eb Ab Db, and Eb minor (named so, not D# minor; its relative is F# major, spelled with sharps) */
    return major == 5 || major == 10 || major == 3 || major == 8 || major == 1 || (KEY_MINOR(key) && major == 6);
}

int ks_in_key(int key, int pc) {
    static const int MAJ = 0xAB5, MIN = 0x5AD | 0x800;   /* bit i = semitone i above the root; minor's raised 7th too */
    if (key < 0) return 1;
    return ((KEY_MINOR(key) ? MIN : MAJ) >> (((pc - KEY_ROOT(key)) % 12 + 12) % 12)) & 1;
}

const float *ks_profile(int profile, int minor) {
    switch (profile) {
    case PROF_TEMPERLEY: return minor ? TP_MIN : TP_MAJ;
    case PROF_SIMPLE: return minor ? SM_MIN : SM_MAJ;
    default: return minor ? KK_MIN : KK_MAJ;
    }
}

/* ---- decimator: windowed-sinc lowpass at 4.5 kHz, keep every 4th sample ---- */

void ks_decim_init(ks_decim_t *d) {
    memset(d, 0, sizeof *d);
    double fc = 4500.0 / KS_SR, sum = 0;
    for (int i = 0; i < KS_FIR; i++) {
        double t = i - (KS_FIR - 1) / 2.0;
        double s = t == 0 ? 2 * fc : sin(2 * M_PI * fc * t) / (M_PI * t);
        double w = 0.54 - 0.46 * cos(2 * M_PI * i / (KS_FIR - 1));
        d->fir[i] = (float)(s * w);
        sum += s * w;
    }
    for (int i = 0; i < KS_FIR; i++) d->fir[i] = (float)(d->fir[i] / sum);
}

int ks_decim(ks_decim_t *d, const float *in, int n, float *out) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        d->hist[d->hpos] = d->hist[d->hpos + KS_FIR] = in[i];   /* doubled ring: a contiguous window */
        d->hpos = (d->hpos + 1) % KS_FIR;
        if (++d->phase < KS_DECIM) continue;
        d->phase = 0;
        const float *h = d->hist + d->hpos;   /* oldest first */
        float acc = 0;
        for (int j = 0; j < KS_FIR; j++) acc += h[j] * d->fir[KS_FIR - 1 - j];
        out[m++] = acc;
    }
    return m;
}

/* ---- analysis ---- */

void ks_init(ks_t *k) {
    memset(k, 0, sizeof *k);
    k->auto_tune = 1;
    k->memory = MEM_30S;
    k->gate_db = -55.0f;
    int bits = 0;
    while ((1 << bits) < KS_N) bits++;
    for (int i = 0; i < KS_N; i++) {
        int r = 0;
        for (int b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b);
        k->rev[i] = r;
        k->win[i] = (float)(0.5 - 0.5 * cos(2 * M_PI * i / KS_N));
    }
    for (int i = 0; i < KS_N / 2; i++) {
        k->cs[i] = (float)cos(2 * M_PI * i / KS_N);
        k->sn[i] = (float)-sin(2 * M_PI * i / KS_N);
    }
    ks_reset(k);
}

void ks_reset(ks_t *k) {
    memset(k->frame_chroma, 0, sizeof k->frame_chroma);
    memset(k->fast_chroma, 0, sizeof k->fast_chroma);
    memset(k->chord_chroma, 0, sizeof k->chord_chroma);
    memset(k->key_chroma, 0, sizeof k->key_chroma);
    memset(k->bass_chroma, 0, sizeof k->bass_chroma);
    memset(k->key_score, 0, sizeof k->key_score);
    k->tune_c = k->tune_s = 0;
    k->tuning_cents = 0;
    k->frames = k->voiced = k->quiet_run = 0;
    k->level_db = -120.0f;
    k->top[0] = k->top[1] = k->top[2] = -1;
    k->key = k->key_lead = -1;
    k->lead_frames = 0;
    k->chord.root = k->chord_cand.root = -1;
    k->chord_cand_frames = 0;
    k->ntrail = k->nlog = 0;
    k->loud_hz = 0;
    k->voiced_now = 0;
    k->npk = 0;
}

static void fft(ks_t *k) {
    float *re = k->re, *im = k->im;
    for (int i = 0; i < KS_N; i++) {
        int j = k->rev[i];
        if (j > i) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= KS_N; len <<= 1) {
        int half = len >> 1, step = KS_N / len;
        for (int i = 0; i < KS_N; i += len) {
            for (int j = 0; j < half; j++) {
                float wr = k->cs[j * step], wi = k->sn[j * step];
                int a = i + j, b = a + half;
                float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
            }
        }
    }
}

static float decay(float tau) { return expf(-(float)KS_HOP / KS_FS / tau); }

static void find_peaks(ks_t *k) {
    /* magnitudes into re[] (bins 0..N/2), sine amplitude scale: a full-scale sine reads 1 */
    const float norm = 4.0f / KS_N;   /* 2 / sum(hann) */
    int nb = KS_N / 2;
    float mx = 0;
    for (int i = 0; i <= nb; i++) {
        float m = sqrtf(k->re[i] * k->re[i] + k->im[i] * k->im[i]) * norm;
        k->re[i] = m;
        if (m > mx) mx = m;
    }
    k->npk = 0;
    if (mx <= 0) return;
    float floor_ = mx * powf(10.0f, PEAK_FLOOR_DB / 20.0f), binhz = (float)KS_FS / KS_N;
    int lo = (int)(30.0f / binhz), hi = (int)(5200.0f / binhz);
    if (hi > nb - 3) hi = nb - 3;
    if (lo < 3) lo = 3;
    for (int i = lo; i <= hi; i++) {
        float m = k->re[i];
        if (m < floor_ || m <= k->re[i - 1] || m < k->re[i + 1] || m <= k->re[i - 2] || m < k->re[i + 2]) continue;
        /* parabolic refinement on log magnitude */
        float a = logf(k->re[i - 1] + 1e-12f), b = logf(m + 1e-12f), c = logf(k->re[i + 1] + 1e-12f);
        float den = a - 2 * b + c, off = den != 0 ? 0.5f * (a - c) / den : 0;
        if (off > 0.5f) off = 0.5f;
        if (off < -0.5f) off = -0.5f;
        float amp = expf(b - 0.25f * (a - c) * off);
        float f = (i + off) * binhz;
        if (k->npk < KS_MAX_PEAKS) {
            k->pk_f[k->npk] = f; k->pk_a[k->npk] = amp; k->npk++;
        } else {   /* full: replace the weakest */
            int w = 0;
            for (int j = 1; j < KS_MAX_PEAKS; j++) if (k->pk_a[j] < k->pk_a[w]) w = j;
            if (amp > k->pk_a[w]) { k->pk_f[w] = f; k->pk_a[w] = amp; }
        }
    }
}

static float semis(float f, float ref) { return 12.0f * log2f(f / ref) + 69.0f; }

static void update_tuning(ks_t *k) {
    float d = decay(TUNE_TAU);
    k->tune_c *= d;
    k->tune_s *= d;
    for (int i = 0; i < k->npk; i++) {
        if (k->pk_f[i] < 80.0f || k->pk_f[i] > 2000.0f) continue;
        float m = semis(k->pk_f[i], 440.0f), dev = m - roundf(m);
        float w = sqrtf(k->pk_a[i]);
        k->tune_c += w * cos(2 * M_PI * dev);
        k->tune_s += w * sin(2 * M_PI * dev);
    }
    if (k->tune_c * k->tune_c + k->tune_s * k->tune_s > 1e-6)
        k->tuning_cents = (float)(atan2(k->tune_s, k->tune_c) / (2 * M_PI) * 100.0);
}

/* pitch class weights of this frame's peaks: frame_chroma (in RANGE) and bass (all peaks under BASS_MAX_HZ) */
static void chroma(ks_t *k, float *bass) {
    float ref = 440.0f * (k->auto_tune ? powf(2.0f, k->tuning_cents / 1200.0f) : 1.0f);
    float lo = RANGE_LO[k->range], hi = RANGE_HI[k->range];
    float w[KS_MAX_PEAKS];
    for (int i = 0; i < k->npk; i++) w[i] = sqrtf(k->pk_a[i]);
    /* a peak that is the 3rd or 5th harmonic of a stronger lower one is mostly that note's colour, not a note */
    for (int i = 0; i < k->npk; i++) {
        float cut = 0;
        for (int j = 0; j < k->npk; j++) {
            if (j == i || k->pk_a[j] < k->pk_a[i] * 0.5f) continue;
            float r = k->pk_f[i] / k->pk_f[j];
            if (fabsf(12.0f * log2f(r / 3.0f)) < 0.3f) cut += 0.5f * w[j];
            else if (fabsf(12.0f * log2f(r / 5.0f)) < 0.3f) cut += 0.3f * w[j];
        }
        w[i] = fmaxf(w[i] - cut, 0.2f * w[i]);
    }
    memset(k->frame_chroma, 0, sizeof k->frame_chroma);
    for (int i = 0; i < 12; i++) bass[i] = 0;
    for (int i = 0; i < k->npk; i++) {
        float f = k->pk_f[i], m = semis(f, ref), n = roundf(m), d = fabsf(m - n);
        float g = 1.0f - 2.0f * d;   /* 1 on the note, 0 at a quarter tone */
        if (g <= 0) continue;
        int pc = ((int)n % 12 + 12) % 12;
        if (f >= lo && f <= hi) k->frame_chroma[pc] += w[i] * g;
        if (f <= BASS_MAX_HZ && f >= 30.0f) bass[pc] += w[i] * g;
    }
}

static float pearson(const float *x, const float *y) {
    float sx = 0, sy = 0, sxy = 0, sx2 = 0, sy2 = 0;
    for (int i = 0; i < 12; i++) { sx += x[i]; sy += y[i]; sxy += x[i] * y[i]; sx2 += x[i] * x[i]; sy2 += y[i] * y[i]; }
    float num = 12 * sxy - sx * sy, den = sqrtf((12 * sx2 - sx * sx) * (12 * sy2 - sy * sy));
    return den > 1e-9f ? num / den : 0;
}

static void detect_key(ks_t *k) {
    float rot[12];
    for (int key = 0; key < 24; key++) {
        for (int i = 0; i < 12; i++) rot[i] = k->key_chroma[(i + KEY_ROOT(key)) % 12];
        k->key_score[key] = pearson(rot, ks_profile(k->profile, KEY_MINOR(key)));
    }
    k->top[0] = k->top[1] = k->top[2] = -1;
    for (int key = 0; key < 24; key++) {
        for (int t = 0; t < 3; t++) {
            if (k->top[t] < 0 || k->key_score[key] > k->key_score[k->top[t]]) {
                for (int s = 2; s > t; s--) k->top[s] = k->top[s - 1];
                k->top[t] = key;
                break;
            }
        }
    }
    if (ks_seconds(k->voiced) < KEY_MIN_S) return;
    int best = k->top[0];
    if (k->key < 0) {
        k->key = best;
    } else if (best != k->key && k->key_score[best] - k->key_score[k->key] >
                                     (best == ks_relative(k->key) ? KEY_MARGIN_REL : KEY_MARGIN)) {
        if (best != k->key_lead) { k->key_lead = best; k->lead_frames = 0; }
        if (ks_seconds(++k->lead_frames) >= KEY_SWITCH_S) k->key = best;
        else return;
    } else {
        k->key_lead = -1;
        k->lead_frames = 0;
        return;
    }
    k->key_lead = -1;
    k->lead_frames = 0;
    memmove(k->log_frame + 1, k->log_frame, sizeof k->log_frame[0] * (KS_LOG - 1));
    memmove(k->log_key + 1, k->log_key, sizeof k->log_key[0] * (KS_LOG - 1));
    k->log_frame[0] = k->frames;
    k->log_key[0] = k->key;
    if (k->nlog < KS_LOG) k->nlog++;
}

static int same_chord(ks_chord_t a, ks_chord_t b) { return a.root == b.root && (a.root < 0 || (a.type == b.type && a.bass == b.bass)); }

static void detect_chord(ks_t *k) {
    ks_chord_t c = {-1, 0, -1};
    float n2 = 0, mx = 0;
    for (int i = 0; i < 12; i++) { n2 += k->chord_chroma[i] * k->chord_chroma[i]; mx = fmaxf(mx, k->chord_chroma[i]); }
    if (n2 > 1e-12f && ks_seconds(k->quiet_run) < CHORD_QUIET_S) {
        float best = 0, nrm = sqrtf(n2);
        for (int t = 0; t < ks_num_chord_types; t++) {
            const ks_chord_type_t *ct = &ks_chord_types[t];
            if (ct->seventh && k->chords != CHORDS_SEVENTHS) continue;
            for (int r = 0; r < 12; r++) {
                float dot = 0;
                for (int i = 0; i < ct->n; i++) dot += k->chord_chroma[(r + ct->iv[i]) % 12];
                float s = dot / (nrm * sqrtf((float)ct->n)) * ct->bias;
                if (s > best) { best = s; c.root = r; c.type = t; }
            }
        }
        if (best < CHORD_MIN) return;   /* nothing clear (a chord fading under drums): keep what is shown */
    }
    if (c.root >= 0) {   /* a bass note in the chord that isn't its root: a slash chord */
        int b = -1;
        float bm = 0, bsum = 0;
        for (int i = 0; i < 12; i++) { bsum += k->bass_chroma[i]; if (k->bass_chroma[i] > bm) { bm = k->bass_chroma[i]; b = i; } }
        if (b >= 0 && b != c.root && bm > 0.5f * bsum) {
            const ks_chord_type_t *ct = &ks_chord_types[c.type];
            for (int i = 1; i < ct->n; i++) if ((c.root + ct->iv[i]) % 12 == b) c.bass = b;
        }
    }
    if (!same_chord(c, k->chord_cand)) { k->chord_cand = c; k->chord_cand_frames = 0; }
    if (++k->chord_cand_frames < CHORD_HOLD || same_chord(c, k->chord)) return;
    k->chord = c;
    if (c.root < 0 || (k->ntrail && same_chord(k->trail[0], c))) return;
    memmove(k->trail + 1, k->trail, sizeof k->trail[0] * (KS_TRAIL - 1));
    k->trail[0] = c;
    if (k->ntrail < KS_TRAIL) k->ntrail++;
}

void ks_frame(ks_t *k, const float *x) {
    double e = 0;
    for (int i = 0; i < KS_N; i++) {
        k->re[i] = x[i] * k->win[i];
        k->im[i] = 0;
        e += (double)x[i] * x[i];
    }
    k->frames++;
    k->level_db = e > 0 ? (float)(10.0 * log10(e / KS_N * 2.0)) : -120.0f;   /* a full-scale sine reads 0 dB */
    k->voiced_now = k->level_db >= k->gate_db;
    float fd = decay(FAST_TAU), cd = decay(CHORD_TAU), bd = decay(BASS_TAU);
    float kd = MEMORY_S[k->memory] > 0 ? decay(MEMORY_S[k->memory]) : 1.0f;
    float bass[12];
    k->loud_hz = 0;
    if (k->voiced_now) {
        fft(k);
        find_peaks(k);
        update_tuning(k);
        chroma(k, bass);
        float sum = 0, bsum = 0, la = 0;
        for (int i = 0; i < 12; i++) { sum += k->frame_chroma[i]; bsum += bass[i]; }
        for (int i = 0; i < k->npk; i++) if (k->pk_a[i] > la) { la = k->pk_a[i]; k->loud_hz = k->pk_f[i]; }
        if (sum <= 0) k->voiced_now = 0;
        for (int i = 0; i < 12 && sum > 0; i++) {
            float c = k->frame_chroma[i] / sum;   /* every voiced frame counts the same, loud or soft */
            k->fast_chroma[i] = k->fast_chroma[i] * fd + c * (1 - fd);
            k->chord_chroma[i] = k->chord_chroma[i] * cd + c * (1 - cd);
            k->key_chroma[i] = k->key_chroma[i] * kd + c;
            k->bass_chroma[i] = k->bass_chroma[i] * bd + (bsum > 0 ? bass[i] / bsum : 0) * (1 - bd);
        }
    }
    if (k->voiced_now) {
        k->voiced++;
        k->quiet_run = 0;
    } else {
        k->quiet_run++;
        for (int i = 0; i < 12; i++) {   /* quiet: the tiles and the chord fade, the key stays */
            k->fast_chroma[i] *= fd;
            k->chord_chroma[i] *= cd;
            k->bass_chroma[i] *= bd;
        }
    }
    if (k->voiced) detect_key(k);
    detect_chord(k);
}
