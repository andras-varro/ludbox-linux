/* Host regression for control_panel/status_hold.h, the expiry rule of the page
 * status line (header-only).
 *
 *   cd native_apps && gcc -Wall -Wextra -I. -o build/status_hold_test \
 *       tests/status_hold_test.c && ./build/status_hold_test
 *
 * A  a timed message expires after its hold, and not before;
 * B  hold 0 takes the default;
 * C  a kept message never expires, however late;
 * D  no message, nothing to expire; the clock wrapping does not expire early.
 * It cannot see the panel: the title tap that clears a kept message, and the
 * repaint.
 */
#include "control_panel/status_hold.h"

#include <stdio.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
    /* A */
    CHECK(!status_hold_expired(true, 1000, 6000, 3000, 7000));
    CHECK(status_hold_expired(true, 1000, 6000, 3000, 7001));
    /* B */
    CHECK(!status_hold_expired(true, 1000, 0, 3000, 4000));
    CHECK(status_hold_expired(true, 1000, 0, 3000, 4001));
    /* C */
    CHECK(status_hold_is_kept(STATUS_HOLD_KEEP));
    CHECK(!status_hold_is_kept(6000) && !status_hold_is_kept(0));
    CHECK(!status_hold_expired(true, 1000, STATUS_HOLD_KEEP, 3000, 1000 + 3001));
    CHECK(!status_hold_expired(true, 1000, STATUS_HOLD_KEEP, 3000, 1000 + 86400000u));
    /* D */
    CHECK(!status_hold_expired(false, 1000, 6000, 3000, 999999));
    CHECK(!status_hold_expired(true, 0xFFFFFF00u, 6000, 3000, 0x100u));
    CHECK(status_hold_expired(true, 0xFFFFFF00u, 6000, 3000, 0x2000u));

    printf("status_hold_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
