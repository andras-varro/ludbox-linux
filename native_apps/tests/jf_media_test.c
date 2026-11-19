/* Host regression for jf_media_event (jellyfin/jf_core.c): which Ludfin event a Bluetooth media key
 * (InputMedia, from gamepad_take_media) becomes, given the play state and queue.
 *
 *   cd native_apps && gcc -Wall -Wextra -I common -I jellyfin -I jf_shared -fsanitize=undefined \
 *       -o build/jf_media_test tests/jf_media_test.c jellyfin/jf_core.c jf_shared/jf_model.c jf_shared/jf_url.c \
 *       jellyfin/jf_url_music.c jf_shared/jf_login.c jf_shared/jf_tags.c common/config.c common/json.c -lm \
 *       && ./build/jf_media_test
 *
 * An Echo's voice "stop" arrives as PAUSE, "resume" as PLAY: PLAY must not pause a playing track and PAUSE
 * must not resume a paused one, or a repeated command flips the music.
 */
#include <stdio.h>
#include <string.h>
#include "jf_core.h"

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static JfApp app;

/* want_type < 0: the key must be ignored */
static void expect(int line, JfPlayState st, int qn, InputMedia m, int pos, int want_type, int want_i)
{
    JfEvent ev;
    memset(&ev, 0, sizeof ev);
    app.play_state = st;
    app.queue_n = qn;
    bool got = jf_media_event(&app, m, pos, &ev);
    if (want_type < 0) { CHECK(!got, "line %d: state %d media %d should be ignored, got type %d", line, (int)st, (int)m, (int)ev.type); return; }
    CHECK(got, "line %d: state %d media %d should act", line, (int)st, (int)m);
    if (!got) return;
    CHECK((int)ev.type == want_type, "line %d: state %d media %d gave type %d, want %d", line, (int)st, (int)m, (int)ev.type, want_type);
    if (want_i >= 0) CHECK(ev.i == want_i, "line %d: seek target %d, want %d", line, ev.i, want_i);
}

int main(void)
{
    jf_app_init(&app);
    const int NO = -1;
    const InputMedia PP = INPUT_MEDIA_PLAYPAUSE, PL = INPUT_MEDIA_PLAY, PA = INPUT_MEDIA_PAUSE, ST = INPUT_MEDIA_STOP,
                     NX = INPUT_MEDIA_NEXT, PV = INPUT_MEDIA_PREV, FF = INPUT_MEDIA_FFWD, RW = INPUT_MEDIA_REWIND;

    /* PLAYPAUSE toggles in every state with a queue (the core's PAUSE event is the toggle) */
    expect(__LINE__, JF_PLAY_PLAYING, 3, PP, 0, JF_EV_PAUSE, -1);
    expect(__LINE__, JF_PLAY_FETCHING, 3, PP, 0, JF_EV_PAUSE, -1);
    expect(__LINE__, JF_PLAY_PAUSED, 3, PP, 0, JF_EV_PAUSE, -1);
    /* PLAY resumes; never pauses */
    expect(__LINE__, JF_PLAY_PAUSED, 3, PL, 0, JF_EV_PAUSE, -1);
    expect(__LINE__, JF_PLAY_STOPPED, 3, PL, 0, JF_EV_PAUSE, -1);
    expect(__LINE__, JF_PLAY_END, 3, PL, 0, JF_EV_PAUSE, -1);
    expect(__LINE__, JF_PLAY_PLAYING, 3, PL, 0, NO, 0);
    expect(__LINE__, JF_PLAY_FETCHING, 3, PL, 0, NO, 0);
    /* PAUSE pauses; never resumes */
    expect(__LINE__, JF_PLAY_PLAYING, 3, PA, 0, JF_EV_PAUSE, -1);
    expect(__LINE__, JF_PLAY_FETCHING, 3, PA, 0, JF_EV_PAUSE, -1);
    expect(__LINE__, JF_PLAY_PAUSED, 3, PA, 0, NO, 0);
    expect(__LINE__, JF_PLAY_STOPPED, 3, PA, 0, NO, 0);
    expect(__LINE__, JF_PLAY_END, 3, PA, 0, NO, 0);
    /* STOP ends playback when there is any */
    expect(__LINE__, JF_PLAY_PLAYING, 3, ST, 0, JF_EV_STOP, -1);
    expect(__LINE__, JF_PLAY_FETCHING, 3, ST, 0, JF_EV_STOP, -1);
    expect(__LINE__, JF_PLAY_PAUSED, 3, ST, 0, JF_EV_STOP, -1);
    expect(__LINE__, JF_PLAY_STOPPED, 3, ST, 0, NO, 0);
    /* NEXT / PREV: the on-screen events (PREV's restart rule lives in the core) */
    expect(__LINE__, JF_PLAY_PLAYING, 3, NX, 0, JF_EV_NEXT, -1);
    expect(__LINE__, JF_PLAY_PAUSED, 3, NX, 0, JF_EV_NEXT, -1);
    expect(__LINE__, JF_PLAY_PLAYING, 3, PV, 0, JF_EV_PREV, -1);
    expect(__LINE__, JF_PLAY_PAUSED, 3, PV, 0, JF_EV_PREV, -1);
    /* seek by the Now Playing screen's step, from the position now; none while stopped */
    expect(__LINE__, JF_PLAY_PLAYING, 3, FF, 50000, JF_EV_SEEK, 50000 + JF_SEEK_STEP_MS);
    expect(__LINE__, JF_PLAY_PAUSED, 3, FF, 50000, JF_EV_SEEK, 50000 + JF_SEEK_STEP_MS);
    expect(__LINE__, JF_PLAY_PLAYING, 3, RW, 50000, JF_EV_SEEK, 50000 - JF_SEEK_STEP_MS);
    expect(__LINE__, JF_PLAY_PLAYING, 3, RW, 4000, JF_EV_SEEK, 0);
    expect(__LINE__, JF_PLAY_STOPPED, 3, FF, 0, NO, 0);
    expect(__LINE__, JF_PLAY_END, 3, RW, 0, NO, 0);
    /* nothing queued, or no key: nothing happens */
    const InputMedia all[] = { PP, PL, PA, ST, NX, PV, FF, RW, INPUT_MEDIA_NONE };
    for (unsigned k = 0; k < sizeof all / sizeof all[0]; k++) {
        expect(__LINE__, JF_PLAY_IDLE, 0, all[k], 0, NO, 0);
        expect(__LINE__, JF_PLAY_PLAYING, 0, all[k], 0, NO, 0);
    }
    expect(__LINE__, JF_PLAY_PLAYING, 3, INPUT_MEDIA_NONE, 0, NO, 0);

    printf("jf_media_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
