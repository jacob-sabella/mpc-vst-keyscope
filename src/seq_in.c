#include "seq_in.h"
#include <dlfcn.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CLIENT_NAME "Keyscope"
#define MAX_PORTS 32

/* The parts of alsa/asoundlib.h used here. This ABI (snd_seq_event_t is 28 bytes) has not changed since
 * ALSA 1.0; tests/seq_in_test.c checks it against the real header. */
typedef struct _snd_seq snd_seq_t;   /* ALSA's own tag, so the test can include the real header too */
typedef struct {
    unsigned char type, flags, tag, queue;
    struct { unsigned int sec, nsec; } time;
    struct { unsigned char client, port; } source, dest;
    union {
        struct { unsigned char channel, note, velocity, off_velocity; unsigned int duration; } note;
        struct { unsigned char channel, unused[3]; unsigned int param; int value; } control;
        unsigned char raw8[12];
    } data;
} seq_event_t;
_Static_assert(sizeof(seq_event_t) == 28, "snd_seq_event_t layout");

enum {
    SEQ_OPEN_INPUT = 2, SEQ_NONBLOCK = 1,
    EV_NOTEON = 6, EV_NOTEOFF = 7, EV_KEYPRESS = 8, EV_CONTROLLER = 10,
    CAP_WRITE = 1 << 1, CAP_SUBS_WRITE = 1 << 6,
    TYPE_MIDI_GENERIC = 1 << 1, TYPE_APPLICATION = 1 << 20,
};

static struct {
    int (*open)(snd_seq_t **, const char *, int, int);
    int (*set_client_name)(snd_seq_t *, const char *);
    int (*create_simple_port)(snd_seq_t *, const char *, unsigned int, unsigned int);
    int (*event_input)(snd_seq_t *, seq_event_t **);
    int (*poll_count)(snd_seq_t *, short);
    int (*poll_descriptors)(snd_seq_t *, struct pollfd *, unsigned int, short);
    int (*close)(snd_seq_t *);
} A;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void *lib;
static unsigned used;   /* port numbers taken, bit n = "MIDI In n+1" */

struct seq_in {
    seq_in_cb_t cb;
    void *user;
    snd_seq_t *seq;
    pthread_t th;
    int th_ok, slot, port;
    atomic_int quit;
    char status[48];
};

static int load(void) {   /* under lock */
    if (lib) return 1;
    lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return 0;
    *(void **)&A.open = dlsym(lib, "snd_seq_open");
    *(void **)&A.set_client_name = dlsym(lib, "snd_seq_set_client_name");
    *(void **)&A.create_simple_port = dlsym(lib, "snd_seq_create_simple_port");
    *(void **)&A.event_input = dlsym(lib, "snd_seq_event_input");
    *(void **)&A.poll_count = dlsym(lib, "snd_seq_poll_descriptors_count");
    *(void **)&A.poll_descriptors = dlsym(lib, "snd_seq_poll_descriptors");
    *(void **)&A.close = dlsym(lib, "snd_seq_close");
    if (A.open && A.set_client_name && A.create_simple_port && A.event_input && A.poll_count && A.poll_descriptors && A.close)
        return 1;
    dlclose(lib);
    lib = NULL;
    return 0;
}

static void deliver(seq_in_t *s, const seq_event_t *ev) {
    uint8_t m[3];
    int ch = ev->data.note.channel & 15;
    switch (ev->type) {
    case EV_NOTEON: m[0] = 0x90 | ch; m[1] = ev->data.note.note & 127; m[2] = ev->data.note.velocity & 127; break;
    case EV_NOTEOFF: m[0] = 0x80 | ch; m[1] = ev->data.note.note & 127; m[2] = 0; break;
    case EV_CONTROLLER:
        m[0] = 0xB0 | (ev->data.control.channel & 15);
        m[1] = ev->data.control.param & 127;
        m[2] = ev->data.control.value < 0 ? 0 : ev->data.control.value > 127 ? 127 : (uint8_t)ev->data.control.value;
        break;
    default: return;
    }
    s->cb(s->user, m, 3);
}

static void *reader(void *arg) {
    seq_in_t *s = arg;
    struct pollfd fd[4];
    int n = A.poll_count(s->seq, POLLIN);
    n = A.poll_descriptors(s->seq, fd, (unsigned)(n > 4 ? 4 : n), POLLIN);
    while (!atomic_load(&s->quit)) {
        if (n <= 0) nanosleep(&(struct timespec){0, 5000000}, NULL);   /* no descriptor to wait on: look every 5 ms */
        else if (poll(fd, (nfds_t)n, 100) <= 0) continue;            /* wakes every 100 ms to see if it should stop */
        seq_event_t *ev;
        while (!atomic_load(&s->quit) && A.event_input(s->seq, &ev) >= 0 && ev) deliver(s, ev);
    }
    return NULL;
}

seq_in_t *seq_in_open(seq_in_cb_t cb, void *user) {
    seq_in_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->cb = cb;
    s->user = user;
    s->slot = -1;
    pthread_mutex_lock(&lock);
    if (!load()) snprintf(s->status, sizeof s->status, "MIDI IN: NO ALSA");
    else {
        for (int i = 0; i < MAX_PORTS && s->slot < 0; i++) if (!(used & 1u << i)) s->slot = i;
        if (s->slot < 0) snprintf(s->status, sizeof s->status, "MIDI IN: TOO MANY INSTANCES");
        else if (A.open(&s->seq, "default", SEQ_OPEN_INPUT, SEQ_NONBLOCK) < 0) {
            s->seq = NULL;
            snprintf(s->status, sizeof s->status, "MIDI IN: NO SEQUENCER");
        } else {
            char port[24];
            if (s->slot) snprintf(port, sizeof port, "MIDI In %d", s->slot + 1);
            else snprintf(port, sizeof port, "MIDI In");
            A.set_client_name(s->seq, CLIENT_NAME);
            if ((s->port = A.create_simple_port(s->seq, port, CAP_WRITE | CAP_SUBS_WRITE, TYPE_MIDI_GENERIC | TYPE_APPLICATION)) < 0) {
                snprintf(s->status, sizeof s->status, "MIDI IN: NO PORT");
            } else if (pthread_create(&s->th, NULL, reader, s) == 0) {
                s->th_ok = 1;
                used |= 1u << s->slot;
                snprintf(s->status, sizeof s->status, "MIDI IN: %s %s", CLIENT_NAME, port);
            } else snprintf(s->status, sizeof s->status, "MIDI IN: NO THREAD");
            if (!s->th_ok) { A.close(s->seq); s->seq = NULL; }
        }
        if (!s->th_ok) s->slot = -1;
    }
    pthread_mutex_unlock(&lock);
    return s;
}

void seq_in_close(seq_in_t *s) {
    if (!s) return;
    if (s->th_ok) {
        atomic_store(&s->quit, 1);
        pthread_join(s->th, NULL);
        pthread_mutex_lock(&lock);
        A.close(s->seq);
        used &= ~(1u << s->slot);
        pthread_mutex_unlock(&lock);
    }
    free(s);
}

const char *seq_in_status(const seq_in_t *s) { return s ? s->status : "MIDI IN: OFF"; }
