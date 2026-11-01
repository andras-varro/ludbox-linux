/* Host regression for the Log page's Follow decisions
 * (control_panel/log_filter.c, log_follow_step).
 *
 *   cd native_apps && gcc -Wall -Wextra -I. -o build/log_follow_test \
 *       tests/log_follow_test.c control_panel/log_filter.c && ./build/log_follow_test
 *
 * What it asserts:
 *   A  the toggle flips the state and jumps to the end only when turning ON;
 *   B  scrolling toward older lines turns Follow off and never moves the view,
 *      and does so from either state;
 *   C  a changed source reloads to the end only while Follow is ON;
 *   D  a sequence: ON, change (reload), scroll up (off), change (ignored),
 *      ON again (jump).
 * "Failing before" = compile against a stub whose log_follow_step returns
 * LF_ACT_NONE and leaves *follow alone. */
#include "control_panel/log_filter.h"

#include <stdio.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    bool f = false;
    LogFollowAction a;

    a = log_follow_step(&f, LF_EV_TOGGLE);
    CHECK(f && a == LF_ACT_JUMP_END, "A: toggle ON: follow=%d act=%d", f, a);
    a = log_follow_step(&f, LF_EV_TOGGLE);
    CHECK(!f && a == LF_ACT_NONE, "A: toggle OFF: follow=%d act=%d", f, a);

    f = true;
    a = log_follow_step(&f, LF_EV_SCROLLED_UP);
    CHECK(!f && a == LF_ACT_NONE, "B: scroll up from ON: follow=%d act=%d", f, a);
    a = log_follow_step(&f, LF_EV_SCROLLED_UP);
    CHECK(!f && a == LF_ACT_NONE, "B: scroll up from OFF: follow=%d act=%d", f, a);

    f = true;
    a = log_follow_step(&f, LF_EV_CHANGED);
    CHECK(f && a == LF_ACT_RELOAD_END, "C: change while ON: follow=%d act=%d", f, a);
    f = false;
    a = log_follow_step(&f, LF_EV_CHANGED);
    CHECK(!f && a == LF_ACT_NONE, "C: change while OFF: follow=%d act=%d", f, a);

    f = false;
    log_follow_step(&f, LF_EV_TOGGLE);
    a = log_follow_step(&f, LF_EV_CHANGED);
    CHECK(f && a == LF_ACT_RELOAD_END, "D1: act=%d", a);
    log_follow_step(&f, LF_EV_SCROLLED_UP);
    a = log_follow_step(&f, LF_EV_CHANGED);
    CHECK(!f && a == LF_ACT_NONE, "D2: follow=%d act=%d", f, a);
    a = log_follow_step(&f, LF_EV_TOGGLE);
    CHECK(f && a == LF_ACT_JUMP_END, "D3: follow=%d act=%d", f, a);

    if (fails) { printf("%d FAILED\n", fails); return 1; }
    printf("log_follow_test: all passed\n");
    return 0;
}
