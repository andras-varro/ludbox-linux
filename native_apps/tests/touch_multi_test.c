/* Host regression for touch_input.c multi-touch: finger count, the two-finger
 * bounding box, and the latched >= 1 s two-finger hold.
 *
 * touch_poll() reads struct input_event from touch->fd, so a non-blocking pipe
 * stands in for the evdev node and the test writes the exact frames
 * kernel/drivers/cy8ctmg120_ts emits (ABS_MT_SLOT, TRACKING_ID, POSITION_X/Y,
 * then ABS_X/ABS_Y/BTN_TOUCH, SYN_REPORT). The clock is injected through
 * TouchInput.mt_clock so 0.9 s and 1.0 s are exact. touch_init() is never
 * called (it opens a device).
 *
 *   cd native_apps && mkdir -p /mnt/c/work/rw-scratch/f147mt && gcc -Wall -Wextra \
 *     -Wno-unused-parameter -I common -o /mnt/c/work/rw-scratch/f147mt/touch_multi_test \
 *     tests/touch_multi_test.c common/touch_input.c common/framebuffer.c common/font.c \
 *     common/hardware.c common/config.c -lm && /mnt/c/work/rw-scratch/f147mt/touch_multi_test
 */
#include "touch_input.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static uint32_t now_ms;
static uint32_t fake_clock(void) { return now_ms; }
static int wfd;

static void ev(int type, int code, int value) {
    struct input_event e;
    memset(&e, 0, sizeof e);
    e.type = type; e.code = code; e.value = value;
    if (write(wfd, &e, sizeof e) != (ssize_t)sizeof e) { puts("pipe write"); fails++; }
}
static void slot(int s, int on, int x, int y) {
    ev(EV_ABS, ABS_MT_SLOT, s);
    if (on) {
        ev(EV_ABS, ABS_MT_TRACKING_ID, 100 + s);
        ev(EV_ABS, ABS_MT_POSITION_X, x);
        ev(EV_ABS, ABS_MT_POSITION_Y, y);
    } else {
        ev(EV_ABS, ABS_MT_TRACKING_ID, -1);
    }
}
static void sync_(void) { ev(EV_SYN, SYN_REPORT, 0); }

int main(void) {
    TouchInput t;
    int p[2];
    memset(&t, 0, sizeof t);
    if (pipe(p)) return 2;
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    t.fd = p[0]; wfd = p[1];
    t.panel_width = 800; t.panel_height = 480;
    t.screen_width = 800; t.screen_height = 480;
    t.raw_min_x = 0; t.raw_max_x = 4095;     /* knots 0,0: linear */
    t.raw_min_y = 0; t.raw_max_y = 4095;
    t.mt_clock = fake_clock;

    int x0, y0, x1, y1;

    /* A: count 0 */
    now_ms = 1000;
    touch_poll(&t);
    CHECK(touch_finger_count(&t) == 0);
    CHECK(!touch_finger_box(&t, &x0, &y0, &x1, &y1));

    /* B: finger 1 -> slot 1 only (the driver's measured behaviour) */
    slot(0, 0, 0, 0); slot(1, 1, 2048, 2048); sync_();
    touch_poll(&t);
    CHECK(touch_finger_count(&t) == 1);
    CHECK(!touch_finger_box(&t, &x0, &y0, &x1, &y1));   /* box only for 2 */

    /* C: second finger -> both slots; box is the mapped min/max */
    now_ms = 2000;
    slot(0, 1, 1024, 3072); slot(1, 1, 3072, 1024); sync_();
    touch_poll(&t);
    CHECK(touch_finger_count(&t) == 2);
    int ax = 1024, ay = 3072, bx = 3072, by = 1024;
    touch_map_raw(&t, &ax, &ay);
    touch_map_raw(&t, &bx, &by);
    CHECK(touch_finger_box(&t, &x0, &y0, &x1, &y1));
    CHECK(x0 == (ax < bx ? ax : bx) && x1 == (ax < bx ? bx : ax));
    CHECK(y0 == (ay < by ? ay : by) && y1 == (ay < by ? by : ay));
    CHECK(x0 < x1 && y0 < y1);
    CHECK(x0 > 100 && x1 > 500);   /* really mapped, not raw: raw would be 1024/3072 */
    CHECK(x1 < 800 && y1 < 480);

    /* D: hold: not at 0.9 s, fires at 1.0 s, once */
    now_ms = 2000 + 900;
    touch_poll(&t);
    CHECK(!touch_take_two_finger_hold(&t));
    now_ms = 2000 + 1000;
    touch_poll(&t);
    CHECK(touch_take_two_finger_hold(&t));
    CHECK(!touch_take_two_finger_hold(&t));      /* cleared by the take */
    now_ms = 2000 + 5000;
    touch_poll(&t);
    CHECK(!touch_take_two_finger_hold(&t));      /* one latch per hold */

    /* E: lift finger 1 (slot 0): finger 2 (slot 1) remains; count 1 */
    slot(0, 0, 0, 0); sync_();
    touch_poll(&t);
    CHECK(touch_finger_count(&t) == 1);
    CHECK(!touch_finger_box(&t, &x0, &y0, &x1, &y1));

    /* F: re-arms: second finger again, new hold fires again, not early */
    now_ms = 10000;
    slot(0, 1, 500, 500); sync_();
    touch_poll(&t);
    CHECK(touch_finger_count(&t) == 2);
    now_ms = 10000 + 999;
    touch_poll(&t);
    CHECK(!touch_take_two_finger_hold(&t));
    now_ms = 10000 + 1001;
    touch_poll(&t);
    CHECK(touch_take_two_finger_hold(&t));
    CHECK(!touch_take_two_finger_hold(&t));

    /* G: a dip below 2 restarts the timer (continuity) */
    slot(1, 0, 0, 0); sync_();
    touch_poll(&t);
    now_ms += 500;
    slot(1, 1, 3000, 3000); sync_();
    touch_poll(&t);
    now_ms += 600;                  /* 600 ms since the dip ended, 1.1 s overall */
    touch_poll(&t);
    CHECK(!touch_take_two_finger_hold(&t));
    now_ms += 400;
    touch_poll(&t);
    CHECK(touch_take_two_finger_hold(&t));

    /* H: all lifted -> 0, and an untaken latch is not left stale */
    now_ms += 5000;
    slot(0, 0, 0, 0); slot(1, 0, 0, 0); sync_();
    touch_poll(&t);
    CHECK(touch_finger_count(&t) == 0);
    CHECK(!touch_take_two_finger_hold(&t));

    /* I: single-touch path unchanged: BTN_TOUCH press still works */
    ev(EV_ABS, ABS_X, 2048); ev(EV_ABS, ABS_Y, 2048); ev(EV_KEY, BTN_TOUCH, 1); sync_();
    touch_poll(&t);
    CHECK(t.state.pressed && t.state.held);
    CHECK(t.state.x > 300 && t.state.x < 500);

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
