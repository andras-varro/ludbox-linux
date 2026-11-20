/**
 * @file audio_tap.h
 * @brief A tiny mono ring of the last 512 samples the output stage produced,
 *        for a visualiser.  Pure and header-only: it also compiles as C++ because
 *        ScummVM links audio_out.c.
 *
 * Off by default; a disabled tap costs one branch per service.  Samples are the
 * post-EQ, post-volume PCM, i.e. up to the device queue's lead AHEAD of what is
 * audible.
 */
#ifndef AUDIO_TAP_H
#define AUDIO_TAP_H

#include <stdint.h>
#include <stdbool.h>

#define AUDIO_TAP_N 512

typedef struct {
    int16_t  buf[AUDIO_TAP_N];
    uint32_t head;      /**< total samples ever pushed (wraps; index = head % N) */
    bool     on;
} AudioTap;

static inline void audio_tap_clear(AudioTap *t)
{
    for (int i = 0; i < AUDIO_TAP_N; i++) t->buf[i] = 0;
    t->head = 0;
}

/** Pushes `frames` interleaved frames, mixed to mono ((L+R)>>1 for two
 *  channels, the first two only for more).  Only the last 512 are kept. */
static inline void audio_tap_push_interleaved(AudioTap *t, const int16_t *buf,
                                              long frames, int channels)
{
    if (!t || !buf || frames <= 0 || channels < 1) return;
    long first = frames > AUDIO_TAP_N ? frames - AUDIO_TAP_N : 0;
    for (long i = first; i < frames; i++) {
        const int16_t *f = buf + i * channels;
        int16_t m = (channels >= 2) ? (int16_t)(((int32_t)f[0] + f[1]) >> 1) : f[0];
        t->buf[t->head % AUDIO_TAP_N] = m;
        t->head++;
    }
}

/** Copies the most recent min(n, 512, available) samples, oldest first.
 *  Returns the count. */
static inline int audio_tap_read(const AudioTap *t, int16_t *dst, int n)
{
    if (!t || !dst || n <= 0) return 0;
    uint32_t avail = t->head < AUDIO_TAP_N ? t->head : AUDIO_TAP_N;
    uint32_t cnt = (uint32_t)n < avail ? (uint32_t)n : avail;
    uint32_t start = t->head - cnt;
    for (uint32_t i = 0; i < cnt; i++)
        dst[i] = t->buf[(start + i) % AUDIO_TAP_N];
    return (int)cnt;
}

#endif
