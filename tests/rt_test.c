/* The production build (no KS_TEST: process() never waits for the worker, it skips a frame instead), fed at four
 * times real time while a second thread does what the wrapper's UI does: reads every readout and light at 100 Hz
 * and taps the wheel. Checks the key is still found, and how long process() takes against the 2.9 ms a 128-frame
 * block lasts. A third thread plays the same chords as MIDI, as the sequencer port's reader does (the production
 * build also opens a real port where the machine has a sequencer). vst/test.sh builds it twice, with ASan/UBSan and with TSan (races between audio, worker and UI). */
#include "play.h"
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#define SPEED 4   /* times real time */

static atomic_int stop;
static void *ui(void *inst) {
    static const char *keys[] = {"key_name", "key_info", "scale_notes", "chord", "trail", "tuning_read", "loudest",
                                 "outside", "status", "cand_1", "cand_1_on", "note_5", "note_5_on", "ring_3_1",
                                 "ring_3_1_on", "log_1", "state", "midi_in"};
    char buf[256];
    for (int n = 0; !atomic_load(&stop); n++) {
        for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++) E->get_param(inst, keys[i], buf, sizeof buf);
        if (n % 50 == 25) set(inst, "ring_9_1", "1");   /* lock to Ab major... */
        if (n % 50 == 26) set(inst, "ring_9_1", "0");   /* ...and back to AUTO */
        if (n % 100 == 60) { set(inst, "notation", n % 200 ? "1" : "0"); set(inst, "chords", n % 200 ? "1" : "0"); }
        usleep(10000);
    }
    return NULL;
}

static void *port(void *inst) {   /* G C D G as MIDI, a chord every 250 ms, with the pedal now and then */
    static const int ch[4][3] = {{55, 59, 62}, {48, 52, 55}, {50, 54, 57}, {55, 59, 62}};
    for (int n = 0; !atomic_load(&stop); n++) {
        const int *c = ch[n % 4];
        uint8_t m[3];
        for (int i = 0; i < 3; i++) { m[0] = 0x90; m[1] = (uint8_t)c[i]; m[2] = 90; E->midi(inst, m, 3); }
        if (n % 8 == 3) { m[0] = 0xB0; m[1] = 64; m[2] = 127; E->midi(inst, m, 3); }
        usleep(250000 / SPEED);
        for (int i = 0; i < 3; i++) { m[0] = 0x80; m[1] = (uint8_t)c[i]; m[2] = 0; E->midi(inst, m, 3); }
        if (n % 8 == 4) { m[0] = 0xB0; m[1] = 64; m[2] = 0; E->midi(inst, m, 3); }
    }
    return NULL;
}

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(void) {
    E = mpc_engine();
    void *a = E->create(NULL);
    pthread_t th, th2;
    pthread_create(&th, NULL, ui, a);
    pthread_create(&th2, NULL, port, a);

    /* G major, I IV V I (held chords at one level: I V vi IV would be as much E minor), 6 rounds of 1 s chords */
    const int blocks = 24 * KS_SR / 128;
    double *dt = malloc(sizeof(double) * blocks), start = now_us();
    int16_t in[256], out[256], differs = 0;
    float ph[8][5] = {{0}};
    static const int prog[4] = {7, 0, 2, 7};
    for (int b = 0; b < blocks; b++) {
        int pos = b * 128, c = (pos / KS_SR) % 4, notes[8];
        int m = chord_notes(prog[c], MAJ, 3, notes);
        for (int i = 0; i < 128; i++) {
            float x = 0;
            for (int j = 0; j < m; j++) {
                float f0 = 440.0f * powf(2.0f, (notes[j] - 69) / 12.0f);
                for (int h = 1; h <= 5; h++) {
                    ph[j][h - 1] += 2 * (float)M_PI * f0 * h / KS_SR;
                    x += 0.06f / h * sinf(ph[j][h - 1]);
                }
            }
            in[2 * i] = in[2 * i + 1] = (int16_t)lrintf(fmaxf(-1, fminf(1, x)) * 32000.0f);
        }
        double t0 = now_us();
        E->process(a, in, out, 128);
        dt[b] = now_us() - t0;
        if (memcmp(in, out, sizeof in)) differs = 1;
        double due = start + (b + 1) * 128.0 / KS_SR * 1e6 / SPEED, left = due - now_us();   /* pace the next block */
        if (left > 0) usleep((useconds_t)left);
    }
    usleep(100000);
    atomic_store(&stop, 1);
    pthread_join(th, NULL);
    pthread_join(th2, NULL);
    printf("MIDI port: %s\n", get(a, "midi_in"));

    set(a, "lock", "0");
    CHECK(!differs, "the output differs from the input");
    CHECK(!strcmp(get(a, "key_name"), "G Major"), "real time: G major heard as %s", get(a, "key_name"));

    qsort(dt, blocks, sizeof *dt, cmp);
    double budget = 128.0 / KS_SR * 1e6, p99 = dt[blocks * 99 / 100], p999 = dt[blocks * 999 / 1000], mx = dt[blocks - 1];
    printf("process(): median %.1f us, p99 %.1f us, p99.9 %.1f us, max %.1f us (a block lasts %.0f us)\n",
           dt[blocks / 2], p99, p999, mx, budget);
    /* sanitizers slow it several times over; the device's own figure comes from tools/bench.sh. This catches a
     * process() that waits for the worker or does the analysis itself. */
    CHECK(p999 < budget / 4, "process() p99.9 %.0f us is over a quarter of a block", p999);
    if (mx > budget) printf("note: one block took %.0f us (a scheduling hiccup on a busy machine, or a wait)\n", mx);
    free(dt);
    E->destroy(a);
    printf(fails ? "rt FAILED (%d)\n" : "rt PASSED\n", fails);
    return fails != 0;
}
