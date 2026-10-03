/* MIDI in through an ALSA sequencer port. MPC OS doesn't send VST MIDI to an insert effect, but it lists every
 * writable sequencer port as a MIDI output a track can play into ("Keyscope MIDI In"). libasound is dlopen'd, so
 * the plugin builds without ALSA headers and still loads (MIDI from the VST host only) where it is missing.
 * Each instance opens its own port, numbered from the second one on ("MIDI In 2"), and reads it on its own thread:
 * the callback runs there, never on the audio thread. */
#pragma once
#include <stdint.h>

typedef void (*seq_in_cb_t)(void *user, const uint8_t *msg, int len);   /* a raw MIDI message, 1-3 bytes */
typedef struct seq_in seq_in_t;

seq_in_t *seq_in_open(seq_in_cb_t cb, void *user);   /* never NULL unless out of memory; see seq_in_status() */
void seq_in_close(seq_in_t *s);                       /* stops the reader; NULL is fine */
const char *seq_in_status(const seq_in_t *s);         /* "MIDI IN: Keyscope MIDI In 2", "MIDI IN: NO ALSA", ... */
