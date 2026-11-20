/* audio_tap_test.c - the visualiser ring in common/audio_tap.h.
 * Build: see the audio_tap_test row in tests/run-all.sh. */
#include <stdio.h>
#include <string.h>
#include "audio_tap.h"

static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; printf("FAIL: %s\n", msg); } } while (0)

int main(void)
{
    AudioTap t;
    int16_t out[600];
    memset(&t, 0, sizeof(t));
    audio_tap_clear(&t);

    CHECK(audio_tap_read(&t, out, 100) == 0, "empty ring reads 0 samples");

    int16_t m[10];
    for (int i = 0; i < 10; i++) m[i] = (int16_t)(i + 1);
    audio_tap_push_interleaved(&t, m, 10, 1);
    CHECK(audio_tap_read(&t, out, 512) == 10, "fewer than 512 available: count is what was pushed");
    CHECK(out[0] == 1 && out[9] == 10, "mono is stored as is, oldest first");
    CHECK(audio_tap_read(&t, out, 4) == 4 && out[0] == 7 && out[3] == 10,
          "a short read returns the MOST RECENT n, oldest first");

    int16_t st[8] = { 100, 300, -100, -300, 32767, 32767, -32768, -32768 };
    audio_tap_clear(&t);
    audio_tap_push_interleaved(&t, st, 4, 2);
    CHECK(audio_tap_read(&t, out, 8) == 4, "4 stereo frames = 4 mono samples");
    CHECK(out[0] == 200 && out[1] == -200 && out[2] == 32767 && out[3] == -32768,
          "stereo mixdown is (L+R)>>1 without overflow");

    /* wrap: push 3 x 300 ramp samples, keep the last 512 in order */
    audio_tap_clear(&t);
    static int16_t big[900];
    for (int i = 0; i < 900; i++) big[i] = (int16_t)i;
    audio_tap_push_interleaved(&t, big, 300, 1);
    audio_tap_push_interleaved(&t, big + 300, 300, 1);
    audio_tap_push_interleaved(&t, big + 600, 300, 1);
    int n = audio_tap_read(&t, out, 600);
    CHECK(n == 512, "read is capped at 512");
    int ok = 1;
    for (int i = 0; i < 512; i++) if (out[i] != (int16_t)(388 + i)) ok = 0;
    CHECK(ok, "after wrapping the ring holds samples 388..899 in order");

    /* one push larger than the ring keeps the last 512 frames */
    audio_tap_clear(&t);
    audio_tap_push_interleaved(&t, big, 900, 1);
    n = audio_tap_read(&t, out, 512);
    CHECK(n == 512 && out[0] == 388 && out[511] == 899, "frames > 512 in one push keeps the last 512");

    /* a push after a clear starts flat */
    audio_tap_clear(&t);
    CHECK(audio_tap_read(&t, out, 512) == 0, "clear empties the ring");

    /* bad arguments */
    CHECK(audio_tap_read(&t, out, 0) == 0 && audio_tap_read(NULL, out, 5) == 0, "bad args read 0");

    printf("audio_tap_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
