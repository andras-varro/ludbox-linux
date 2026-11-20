/* Host regression: a press that wakes a dark panel is swallowed, EXCEPT a media
 * key, which must still reach gamepad_take_media ("Alexa, stop" has to stop the
 * music, not only light the screen).  Covers both readers: the media node
 * (poll_media) and a keyboard node (poll_keyboard_fd).  A touch/nav/ordinary key
 * that wakes the panel stays swallowed, and the same key on a lit panel is
 * delivered, so the swallow check cannot pass on a dead keyboard path.
 *
 * The blank state lives in static variables of hardware.c with a real-time
 * clock, so a dark panel cannot be reached from a test.  This file therefore
 * DEFINES hw_blank_note_activity()/hw_blank_keepalive() itself (a fake panel
 * that is dark until the first press) and is linked with
 * -Wl,--allow-multiple-definition, test object first, so the linker keeps these
 * and ignores hardware.c's.  What it tests is gamepad.c's reaction to
 * "that press woke the panel", not the blank timer (tests/blank_decide_test.c).
 *
 *   cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I common \
 *       -Wl,--allow-multiple-definition -o build/gamepad_wake_test \
 *       tests/gamepad_wake_test.c common/gamepad.c common/input_slots.c \
 *       common/input_scan.c common/framebuffer.c common/font.c common/hardware.c \
 *       common/config.c common/touch_input.c -lm && ./build/gamepad_wake_test
 *
 * Cannot see: the real backlight, a real evdev node or BlueZ. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <linux/input.h>
#include "gamepad.h"
#include "framebuffer.h"
#include "input_scan.h"

static bool fake_dark;
bool hw_blank_note_activity(void) {
    if (fake_dark) { fake_dark = false; return true; }   /* woke it: swallow */
    return false;
}
void hw_blank_keepalive(void) {}

static int fails, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int fake_fd(void) {
    char path[] = "/tmp/arca_gamepad_wake_XXXXXX";
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
static void poll_once(GamepadManager *gm, InputState *st, int fd) {
    if (lseek(fd, 0, SEEK_SET) < 0) { perror("lseek"); exit(2); }
    gamepad_poll(gm, st, 0, 0, false);
}
static int any_button(const InputState *st) {
    for (int i = 0; i < BTN_ID_COUNT; i++)
        if (st->buttons[i].held || st->buttons[i].pressed) return 1;
    return 0;
}
/* kind 0: a media node, 1: a keyboard node */
static void fresh(GamepadManager *gm, int *fd, int kind) {
    gamepad_init(gm);
    gamepad_close(gm);
    memset(gm->held_latched, 0, sizeof(gm->held_latched));
    memset(gm->prev_held, 0, sizeof(gm->prev_held));
    *fd = fake_fd();
    if (kind == 0) { gm->media_fds[0] = *fd; gm->media_count = 1; }
    else { gm->keyboard_fds[0] = *fd; gm->keyboard_count = 1; }
}

int main(void) {
    GamepadManager gm; InputState st; int fd;

    /* media node, dark panel: the waking media key still acts */
    memset(&st, 0, sizeof(st)); fresh(&gm, &fd, 0); fake_dark = true;
    ev(fd, KEY_PAUSECD, 1); ev(fd, KEY_PAUSECD, 0);
    poll_once(&gm, &st, fd);
    CHECK(gamepad_take_media(&gm) == INPUT_MEDIA_PAUSE);
    CHECK(gamepad_take_media(&gm) == INPUT_MEDIA_NONE);
    CHECK(!fake_dark);
    gamepad_close(&gm);

    /* keyboard node, dark panel: the waking media key still acts */
    memset(&st, 0, sizeof(st)); fresh(&gm, &fd, 1); fake_dark = true;
    ev(fd, KEY_PAUSECD, 1); ev(fd, KEY_PAUSECD, 0);
    poll_once(&gm, &st, fd);
    CHECK(gamepad_take_media(&gm) == INPUT_MEDIA_PAUSE);
    CHECK(gamepad_take_media(&gm) == INPUT_MEDIA_NONE);
    CHECK(!fake_dark);
    CHECK(!any_button(&st));
    gamepad_close(&gm);

    /* keyboard node, dark panel: an ordinary key is swallowed ... */
    memset(&st, 0, sizeof(st)); fresh(&gm, &fd, 1); fake_dark = true;
    ev(fd, KEY_SPACE, 1);
    poll_once(&gm, &st, fd);
    CHECK(!any_button(&st));
    CHECK(gamepad_take_media(&gm) == INPUT_MEDIA_NONE);
    CHECK(!fake_dark);
    gamepad_close(&gm);

    /* ... and the same key on a lit panel is delivered (control: the keyboard
     * path works, so the swallow above is the wake and not a dead reader) */
    memset(&st, 0, sizeof(st)); fresh(&gm, &fd, 1); fake_dark = false;
    ev(fd, KEY_SPACE, 1);
    poll_once(&gm, &st, fd);
    CHECK(any_button(&st));
    gamepad_close(&gm);

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
