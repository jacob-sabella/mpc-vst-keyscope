/* Keyscope's analysis core: polyphonic pitch-class detection from the spectrum of any audio, and the key,
 * chord, bass note and tuning read from it. Single-threaded and allocation-free after ks_init(); the plugin
 * (keyscope.c) feeds it from the audio thread through a decimator and runs ks_frame() on a worker thread.
 *
 * Pipeline per frame (KS_N samples at KS_FS, every KS_HOP):
 *   Hann window -> FFT -> magnitude peaks (parabolic refinement) in the RANGE band -> each peak's pitch
 *   relative to the estimated tuning -> a 12-bin chroma (weight sqrt(amplitude), linear fall-off to zero at
 *   a quarter tone, a third/fifth harmonic of a stronger lower peak discounted) -> decaying averages:
 *     fast   (~0.25 s) the note tiles,
 *     chord  (~0.35 s) chord templates, held for a few frames before the shown chord changes,
 *     key    (MEMORY)  correlated with 24 rotated key profiles, the shown key changing only after the new
 *                      best has led for about 1.5 s,
 *     bass   (~0.35 s) peaks under 260 Hz, for slash chords.
 *   Frames quieter than the gate count for nothing. */
#pragma once
#include <stdint.h>

#define KS_SR 44100
#define KS_DECIM 4
#define KS_FS (KS_SR / KS_DECIM)          /* 11025 Hz: analysis rate */
#define KS_N 4096                         /* frame: 371 ms, 2.69 Hz bins */
#define KS_HOP 512                        /* 46 ms between frames */
#define KS_FIR 48                         /* decimator taps */
#define KS_MAX_PEAKS 48
#define KS_TRAIL 8                        /* chords remembered */
#define KS_LOG 6                          /* key changes remembered */

enum { RANGE_FULL, RANGE_BASS, RANGE_MIDS, RANGE_HIGHS, RANGE_COUNT };
enum { PROF_KRUMHANSL, PROF_TEMPERLEY, PROF_SIMPLE, PROF_COUNT };
enum { CHORDS_TRIADS, CHORDS_SEVENTHS };
enum { MEM_10S, MEM_30S, MEM_1MIN, MEM_SONG, MEM_COUNT };

/* key index: root + 12 * minor (0-11 major, 12-23 minor); -1 = none */
#define KEY_ROOT(k) ((k) % 12)
#define KEY_MINOR(k) ((k) >= 12)

typedef struct {   /* a detected chord: root pitch class, type (ks_chord_types), bass pitch class or -1 */
    int root, type, bass;
} ks_chord_t;

typedef struct {
    const char *suffix;
    int n, iv[4];
    float bias;     /* score multiplier: simpler chords win a near tie */
    int seventh;    /* only with CHORDS_SEVENTHS */
} ks_chord_type_t;

extern const ks_chord_type_t ks_chord_types[];
extern const int ks_num_chord_types;

typedef struct {
    float fir[KS_FIR], hist[2 * KS_FIR];
    int hpos, phase;
} ks_decim_t;

void ks_decim_init(ks_decim_t *d);
/* n input samples at KS_SR -> up to n / KS_DECIM + 1 output samples at KS_FS; returns how many */
int ks_decim(ks_decim_t *d, const float *in, int n, float *out);

typedef struct {
    /* settings (read once per frame) */
    int range, profile, chords, auto_tune, memory;
    float gate_db;

    /* state */
    float win[KS_N], re[KS_N], im[KS_N], cs[KS_N / 2], sn[KS_N / 2];
    int rev[KS_N];
    float pk_f[KS_MAX_PEAKS], pk_a[KS_MAX_PEAKS];
    int npk;
    float frame_chroma[12], fast_chroma[12], chord_chroma[12], key_chroma[12], bass_chroma[12];
    double tune_c, tune_s;
    long frames, voiced, quiet_run;

    /* results */
    float level_db, tuning_cents;
    float key_score[24];
    int top[3];                   /* best three keys by score */
    int key;                      /* the key shown: -1 until there is enough to go on */
    int key_lead, lead_frames;    /* a challenger and how long it has led */
    ks_chord_t chord;             /* root -1 = no chord */
    ks_chord_t chord_cand;
    int chord_cand_frames;
    ks_chord_t trail[KS_TRAIL];   /* trail[0] = newest */
    int ntrail;
    long log_frame[KS_LOG];       /* key changes, newest first: frame index and key */
    int log_key[KS_LOG], nlog;
    float loud_hz;                /* strongest peak this frame, 0 = none */
    int voiced_now;
} ks_t;

void ks_init(ks_t *k);
void ks_reset(ks_t *k);              /* forget everything heard; settings stay */
void ks_frame(ks_t *k, const float *x);   /* analyse KS_N samples (oldest first) */

/* key helpers */
const float *ks_profile(int profile, int minor);
int ks_key_uses_flats(int key);      /* the usual spelling of a key's notes */
const char *ks_pc_name(int pc, int flats);
const char *ks_key_name(int key);    /* "F# Minor", "Bb Major" */
int ks_in_key(int key, int pc);      /* pc in the key's scale (natural minor; harmonic minor's 7th counts too) */
int ks_relative(int key);
float ks_seconds(long frames);       /* frames -> seconds */
