/* Host regression for jv_media_event() (jfvideo/jv_core.c): which core event a Bluetooth media key stands for, given the
 * play state and the screen.  Pure: no device, no player.
 *
 *   cd native_apps && gcc -Wall -Wextra -I common -I jf_shared -I jfvideo -fsanitize=undefined,address \
 *       -o build/jv_media_test tests/jv_media_test.c jfvideo/jv_core.c jfvideo/jv_model.c jfvideo/jv_tracks.c \
 *       jfvideo/jv_url.c jfvideo/jv_settings.c jfvideo/jv_last.c jf_shared/jf_model.c jf_shared/jf_url.c jf_shared/jf_login.c \
 *       common/config.c common/json.c -lm && setarch $(uname -m) -R ./build/jv_media_test
 */
#include <stdio.h>
#include "jv_core.h"

static int fails, checks;
#define EXPECT(name, act, st, scr, want_ev, want_i) do { \
    int i_ = 0; JvEventType e_ = jv_media_event(act, st, scr, &i_); checks++; \
    if (e_ != (want_ev) || (e_ != JV_EV_NONE && (want_ev) == JV_EV_SEEK_STEP && i_ != (want_i))) { \
        fails++; printf("FAIL %s: got %d i=%d want %d i=%d\n", name, (int)e_, i_, (int)(want_ev), (int)(want_i)); } } while (0)

int main(void)
{
    const JvScreen P = JV_SCR_PLAYER;
    /* playing */
    EXPECT("playing PLAYPAUSE", INPUT_MEDIA_PLAYPAUSE, JV_PLAY_PLAYING, P, JV_EV_PAUSE, 0);
    EXPECT("playing PLAY",      INPUT_MEDIA_PLAY,      JV_PLAY_PLAYING, P, JV_EV_NONE, 0);
    EXPECT("playing PAUSE",     INPUT_MEDIA_PAUSE,     JV_PLAY_PLAYING, P, JV_EV_PAUSE, 0);
    EXPECT("playing STOP",      INPUT_MEDIA_STOP,      JV_PLAY_PLAYING, P, JV_EV_STOP, 0);
    EXPECT("playing FFWD",      INPUT_MEDIA_FFWD,      JV_PLAY_PLAYING, P, JV_EV_SEEK_STEP, 1);
    EXPECT("playing REWIND",    INPUT_MEDIA_REWIND,    JV_PLAY_PLAYING, P, JV_EV_SEEK_STEP, -1);
    EXPECT("playing NEXT",      INPUT_MEDIA_NEXT,      JV_PLAY_PLAYING, P, JV_EV_NONE, 0);
    EXPECT("playing PREV",      INPUT_MEDIA_PREV,      JV_PLAY_PLAYING, P, JV_EV_NONE, 0);
    /* paused */
    EXPECT("paused PLAYPAUSE",  INPUT_MEDIA_PLAYPAUSE, JV_PLAY_PAUSED, P, JV_EV_PAUSE, 0);
    EXPECT("paused PLAY",       INPUT_MEDIA_PLAY,      JV_PLAY_PAUSED, P, JV_EV_PAUSE, 0);
    EXPECT("paused PAUSE",      INPUT_MEDIA_PAUSE,     JV_PLAY_PAUSED, P, JV_EV_NONE, 0);
    EXPECT("paused STOP",       INPUT_MEDIA_STOP,      JV_PLAY_PAUSED, P, JV_EV_STOP, 0);
    EXPECT("paused FFWD",       INPUT_MEDIA_FFWD,      JV_PLAY_PAUSED, P, JV_EV_SEEK_STEP, 1);
    /* fetching / idle in the player: nothing is playing, nothing to act on */
    EXPECT("fetching PLAYPAUSE", INPUT_MEDIA_PLAYPAUSE, JV_PLAY_FETCHING, P, JV_EV_NONE, 0);
    EXPECT("fetching STOP",      INPUT_MEDIA_STOP,      JV_PLAY_FETCHING, P, JV_EV_NONE, 0);
    EXPECT("idle PLAY",          INPUT_MEDIA_PLAY,      JV_PLAY_IDLE,     P, JV_EV_NONE, 0);
    /* browse screens: every action is harmless, whatever the state says */
    for (int a = INPUT_MEDIA_NONE; a <= INPUT_MEDIA_REWIND; a++) {
        EXPECT("home", a, JV_PLAY_IDLE, JV_SCR_HOME, JV_EV_NONE, 0);
        EXPECT("details", a, JV_PLAY_IDLE, JV_SCR_DETAILS, JV_EV_NONE, 0);
        EXPECT("details-stale-playing", a, JV_PLAY_PLAYING, JV_SCR_DETAILS, JV_EV_NONE, 0);
    }
    EXPECT("none in player", INPUT_MEDIA_NONE, JV_PLAY_PLAYING, P, JV_EV_NONE, 0);
    /* a null arg pointer is fine */
    checks++;
    if (jv_media_event(INPUT_MEDIA_FFWD, JV_PLAY_PLAYING, P, NULL) != JV_EV_SEEK_STEP) { fails++; printf("FAIL null arg\n"); }
    printf("jv_media_test: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
