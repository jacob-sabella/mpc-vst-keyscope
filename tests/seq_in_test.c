/* The ALSA MIDI input against a real sequencer: the hand-written event layout against <alsa/asoundlib.h>, notes and
 * controllers sent from another client arriving as MIDI bytes, the port numbering across instances, and a close
 * while events are arriving. Skipped (passes) where the machine has no sequencer. Built by vst/test.sh under
 * ASan/UBSan and TSan. */
#include "../src/seq_in.c"
#include <alsa/asoundlib.h>
#include <stddef.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint8_t got[64][3];
static atomic_int ngot;
static void cb(void *user, const uint8_t *m, int len) {
    (void)user;
    int i = atomic_load(&ngot);
    if (len == 3 && i < 64) { memcpy(got[i], m, 3); atomic_store(&ngot, i + 1); }
}

static void send(snd_seq_t *tx, int port, void (*set)(snd_seq_event_t *)) {
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    set(&ev);
    snd_seq_event_output_direct(tx, &ev);
}
static void ev_on(snd_seq_event_t *e) { snd_seq_ev_set_noteon(e, 3, 61, 99); }
static void ev_off(snd_seq_event_t *e) { snd_seq_ev_set_noteoff(e, 3, 61, 0); }
static void ev_pedal(snd_seq_event_t *e) { snd_seq_ev_set_controller(e, 0, 64, 127); }
static void ev_pgm(snd_seq_event_t *e) { snd_seq_ev_set_pgmchange(e, 0, 5); }

static int wait_for(int n) {
    for (int i = 0; i < 200 && atomic_load(&ngot) < n; i++) usleep(5000);
    return atomic_load(&ngot);
}

int main(void) {
    CHECK(sizeof(seq_event_t) == sizeof(snd_seq_event_t), "event size %zu, ALSA's %zu", sizeof(seq_event_t), sizeof(snd_seq_event_t));
    CHECK(offsetof(seq_event_t, data) == offsetof(snd_seq_event_t, data), "data offset");
    CHECK(offsetof(seq_event_t, data.control.param) == offsetof(snd_seq_event_t, data.control.param) &&
          offsetof(seq_event_t, data.control.value) == offsetof(snd_seq_event_t, data.control.value), "control layout");
    CHECK((int)SND_SEQ_EVENT_NOTEON == EV_NOTEON && (int)SND_SEQ_EVENT_NOTEOFF == EV_NOTEOFF && (int)SND_SEQ_EVENT_CONTROLLER == EV_CONTROLLER &&
          SND_SEQ_OPEN_INPUT == SEQ_OPEN_INPUT && SND_SEQ_NONBLOCK == SEQ_NONBLOCK && SND_SEQ_PORT_CAP_WRITE == CAP_WRITE &&
          SND_SEQ_PORT_CAP_SUBS_WRITE == CAP_SUBS_WRITE && SND_SEQ_PORT_TYPE_MIDI_GENERIC == TYPE_MIDI_GENERIC &&
          SND_SEQ_PORT_TYPE_APPLICATION == TYPE_APPLICATION, "constants match ALSA's");

    snd_seq_t *tx;
    if (access("/dev/snd/seq", R_OK | W_OK) || snd_seq_open(&tx, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
        printf("no ALSA sequencer here: the port test is skipped\n");
        printf(fails ? "seq_in FAILED (%d)\n" : "seq_in PASSED\n", fails);
        return fails != 0;
    }
    int txp = snd_seq_create_simple_port(tx, "out", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_APPLICATION);

    seq_in_t *a = seq_in_open(cb, NULL), *b = seq_in_open(cb, NULL);
    CHECK(!strcmp(seq_in_status(a), "MIDI IN: Keyscope MIDI In"), "a: %s", seq_in_status(a));
    CHECK(!strcmp(seq_in_status(b), "MIDI IN: Keyscope MIDI In 2"), "b: %s", seq_in_status(b));
    CHECK(snd_seq_connect_to(tx, txp, snd_seq_client_id(a->seq), a->port) == 0, "connect to a");

    send(tx, txp, ev_on);
    send(tx, txp, ev_pgm);   /* not passed on */
    send(tx, txp, ev_pedal);
    send(tx, txp, ev_off);
    int n = wait_for(3);
    CHECK(n == 3, "3 messages arrive, got %d", n);
    CHECK(n >= 1 && got[0][0] == 0x93 && got[0][1] == 61 && got[0][2] == 99, "note on: %02x %d %d", got[0][0], got[0][1], got[0][2]);
    CHECK(n >= 2 && got[1][0] == 0xB0 && got[1][1] == 64 && got[1][2] == 127, "pedal: %02x %d %d", got[1][0], got[1][1], got[1][2]);
    CHECK(n >= 3 && got[2][0] == 0x83 && got[2][1] == 61, "note off: %02x %d", got[2][0], got[2][1]);

    /* a closed port's number is free again; close while events are arriving */
    seq_in_close(a);
    seq_in_t *c = seq_in_open(cb, NULL);
    CHECK(!strcmp(seq_in_status(c), "MIDI IN: Keyscope MIDI In"), "c reuses the first name: %s", seq_in_status(c));
    snd_seq_connect_to(tx, txp, snd_seq_client_id(c->seq), c->port);
    for (int i = 0; i < 200; i++) send(tx, txp, i % 2 ? ev_off : ev_on);
    seq_in_close(c);
    seq_in_close(b);
    seq_in_close(NULL);
    CHECK(!strcmp(seq_in_status(NULL), "MIDI IN: OFF"), "NULL: %s", seq_in_status(NULL));
    snd_seq_close(tx);
    printf(fails ? "seq_in FAILED (%d)\n" : "seq_in PASSED\n", fails);
    return fails != 0;
}
