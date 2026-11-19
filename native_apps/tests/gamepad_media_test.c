/* Host regression for the media-key input source: a Bluetooth AVRCP link
 * (Amazon Echo, ...) is an evdev node with digits, Enter, F-keys and media
 * keys but no letter keys.  It used to classify as nothing and was never
 * opened.  Asserts: input_classify() calls that capability set
 * INPUT_KIND_MEDIA; a letter-key keyboard that also has media keys stays a
 * keyboard; pad, mouse, touchscreen and a power-button-only node are unchanged;
 * input_media_action() maps the measured codes (PLAYCD -> PLAY, PAUSECD ->
 * PAUSE, NEXTSONG -> NEXT ...) and nothing else; and through gamepad_poll()
 * only a press queues an action (release and autorepeat do not), the queue
 * drains oldest first and is bounded, and a media node leaves every
 * ButtonId, the mouse and the keyboard-connected flag untouched.
 *
 *   cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I common \
 *       -o build/gamepad_media_test tests/gamepad_media_test.c \
 *       common/gamepad.c common/input_slots.c common/input_scan.c common/framebuffer.c \
 *       common/font.c common/hardware.c common/config.c common/touch_input.c -lm \
 *       && ./build/gamepad_media_test
 *
 * Fails to compile against the pre-change source (no INPUT_KIND_MEDIA).
 * Cannot see: a real evdev node or BlueZ. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include "gamepad.h"
#include "framebuffer.h"
#include "input_scan.h"

static int fails, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static InputCaps caps_new(void) {
    InputCaps c; memset(&c, 0, sizeof(c));
    input_caps_set(c.ev, EV_KEY);
    return c;
}
static void add_keys(InputCaps *c, const int *k, int n) {
    for (int i = 0; i < n; i++) input_caps_set(c->key, k[i]);
}
static const int AVRCP[] = { KEY_1, KEY_2, KEY_ENTER, KEY_F1, KEY_F2, KEY_PLAYCD,
    KEY_PAUSECD, KEY_NEXTSONG, KEY_PREVIOUSSONG, KEY_STOPCD, KEY_PLAYPAUSE };
static const int LETTERS[] = {
    KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
    KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
    KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M };
#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int fake_fd(void) {
    char path[] = "/tmp/arca_gamepad_media_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); exit(2); }
    unlink(path);
    return fd;
}
static void ev(int fd, int code, int value) {
    struct input_event e; memset(&e, 0, sizeof(e));
    e.type = EV_KEY; e.code = code; e.value = value;
    if (write(fd, &e, sizeof(e)) != (ssize_t)sizeof(e)) { perror("write"); exit(2); }
}
static off_t poll_from(GamepadManager *gm, InputState *st, int fd, off_t from) {
    off_t end = lseek(fd, 0, SEEK_END);
    if (lseek(fd, from, SEEK_SET) < 0) { perror("lseek"); exit(2); }
    gamepad_poll(gm, st, 0, 0, false);
    return end;
}
static void fresh(GamepadManager *gm, int *fd) {
    gamepad_init(gm);
    gamepad_close(gm);
    memset(gm->held_latched, 0, sizeof(gm->held_latched));
    memset(gm->prev_held, 0, sizeof(gm->prev_held));
    *fd = fake_fd();
    gm->media_fds[0] = *fd;
    gm->media_count = 1;
}

int main(void) {
    /* classification */
    InputCaps c = caps_new(); add_keys(&c, AVRCP, N(AVRCP));
    CHECK(input_classify(&c, "Echo Dot-2MF (AVRCP)") == INPUT_KIND_MEDIA);
    CHECK(!input_caps_is_keyboard(&c));
    InputCaps kb = caps_new(); add_keys(&kb, LETTERS, N(LETTERS)); add_keys(&kb, AVRCP, N(AVRCP));
    CHECK(input_classify(&kb, "kbd with media keys") == INPUT_KIND_KEYBOARD);
    InputCaps pw = caps_new(); input_caps_set(pw.key, KEY_POWER);
    CHECK(input_classify(&pw, "twl4030_pwrbutton") == INPUT_KIND_NONE);
    InputCaps pad = caps_new(); input_caps_set(pad.key, BTN_SOUTH); input_caps_set(pad.key, KEY_PLAYCD);
    input_caps_set(pad.ev, EV_ABS); input_caps_set(pad.abs, ABS_X); input_caps_set(pad.abs, ABS_Y);
    CHECK(input_classify(&pad, "pad with media key") == INPUT_KIND_PAD);
    InputCaps ms = caps_new(); input_caps_set(ms.key, BTN_LEFT); input_caps_set(ms.key, KEY_PLAYCD);
    input_caps_set(ms.ev, EV_REL); input_caps_set(ms.rel, REL_X); input_caps_set(ms.rel, REL_Y);
    CHECK(input_classify(&ms, "mouse with media key") == INPUT_KIND_MOUSE);
    CHECK(input_classify(&c, "panjit touchscreen") == INPUT_KIND_NONE);
    CHECK(input_classify(NULL, "x") == INPUT_KIND_NONE);
    InputCaps plain = caps_new(); input_caps_set(plain.key, KEY_1);
    CHECK(input_classify(&plain, "digits only") == INPUT_KIND_NONE);
    int held[INPUT_KIND_COUNT] = {0}, cap[INPUT_KIND_COUNT] = {0};
    CHECK(input_select(INPUT_KIND_MEDIA, held, cap) == INPUT_KIND_NONE);   /* not wanted by default */
    CHECK(gamepad_scan_caps()[INPUT_KIND_MEDIA] > 0);

    /* mapping */
    CHECK(input_media_action(KEY_PLAYCD) == INPUT_MEDIA_PLAY);
    CHECK(input_media_action(KEY_PAUSECD) == INPUT_MEDIA_PAUSE);
    CHECK(input_media_action(KEY_PLAYPAUSE) == INPUT_MEDIA_PLAYPAUSE);
    CHECK(input_media_action(KEY_STOPCD) == INPUT_MEDIA_STOP);
    CHECK(input_media_action(KEY_NEXTSONG) == INPUT_MEDIA_NEXT);
    CHECK(input_media_action(KEY_PREVIOUSSONG) == INPUT_MEDIA_PREV);
    CHECK(input_media_action(KEY_FASTFORWARD) == INPUT_MEDIA_FFWD);
    CHECK(input_media_action(KEY_REWIND) == INPUT_MEDIA_REWIND);
    CHECK(input_media_action(KEY_A) == INPUT_MEDIA_NONE);
    CHECK(input_media_action(KEY_ENTER) == INPUT_MEDIA_NONE);

    /* delivery */
    GamepadManager gm; InputState st; int fd; off_t pos;
    memset(&st, 0, sizeof(st)); fresh(&gm, &fd);
    CHECK(gamepad_take_media(&gm) == INPUT_MEDIA_NONE);
    ev(fd, KEY_PLAYCD, 1); ev(fd, KEY_PLAYCD, 0);
    ev(fd, KEY_PAUSECD, 1); ev(fd, KEY_PAUSECD, 2); ev(fd, KEY_PAUSECD, 0);
    ev(fd, KEY_NEXTSONG, 1); ev(fd, KEY_NEXTSONG, 0);
    ev(fd, KEY_ENTER, 1); ev(fd, KEY_ENTER, 0);   /* not a media key */
    pos = poll_from(&gm, &st, fd, 0);
    CHECK(gamepad_take_media(&gm) == INPUT_MEDIA_PLAY);
    CHECK(gamepad_take_media(&gm) == INPUT_MEDIA_PAUSE);
    CHECK(gamepad_take_media(&gm) == INPUT_MEDIA_NEXT);
    CHECK(gamepad_take_media(&gm) == INPUT_MEDIA_NONE);   /* release/repeat/Enter queued nothing */
    for (int i = 0; i < BTN_ID_COUNT; i++)
        CHECK(!st.buttons[i].held && !st.buttons[i].pressed);
    CHECK(!st.keyboard_connected);
    CHECK(!st.mouse_connected);
    (void)pos;
    gamepad_close(&gm);

    /* bounded queue */
    memset(&st, 0, sizeof(st)); fresh(&gm, &fd);
    for (int i = 0; i < GAMEPAD_MEDIAQ_MAX + 5; i++) { ev(fd, KEY_NEXTSONG, 1); ev(fd, KEY_NEXTSONG, 0); }
    poll_from(&gm, &st, fd, 0);
    int n = 0;
    while (gamepad_take_media(&gm) != INPUT_MEDIA_NONE) n++;
    CHECK(n == GAMEPAD_MEDIAQ_MAX);
    gamepad_close(&gm);

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
