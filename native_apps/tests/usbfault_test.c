/* Host regression for the USB-overload warning's decisions (common/usbfault.h).
 *
 *   cd native_apps && gcc -Wall -Wextra -I. -o build/usbfault_test \
 *       tests/usbfault_test.c && ./build/usbfault_test
 *
 *   A  errors ~4 s apart (the measured overload cadence) latch at the third
 *      and the warning stays on for as long as they continue;
 *   B  the warning clears USBF_QUIET_MS after the last error, not before, and
 *      one missed 4 s record does not clear it;
 *   C  negative controls: two errors, or three spread past 30 s, never latch;
 *   D  the 30 s window edge (0, 15, 30 s) latches;
 *   E  a cleared warning latches again on a new burst; errors during the quiet
 *      period restart it;
 *   F  no error, no flag; the clock wrapping changes nothing;
 *   H  the red LED: solid for over-temperature, three 200 ms flashes at the
 *      start of every 10 s for a latched fault (the sidebars' cadence), released
 *      when neither, and over-temperature wins.
 */
#include "common/usbfault.h"
#include <stdio.h>

static int fails;
#define CHECK(g, cond, ...) do { if (!(cond)) { fails++; printf("FAIL " g ": "); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Errors at the given second offsets from base (-1 ends the list); steps once a
 * second to secs.  Returns the first second the flag was on and the first second
 * it was off again after that. */
static void run(uint32_t base, const int *errs, int secs, int *on_s, int *off_s) {
    UsbFault f; usbf_init(&f);
    *on_s = *off_s = -1;
    for (int s = 0; s <= secs; s++) {
        int e = 0;
        for (const int *p = errs; *p >= 0; p++) e += *p == s;
        usbf_step(&f, base + (uint32_t)s * 1000u, e);
        if (usbf_flag(&f) && *on_s < 0) *on_s = s;
        if (!usbf_flag(&f) && *on_s >= 0 && *off_s < 0) *off_s = s;
    }
}

int main(void) {
    int on, off;

    { static const int e[] = { 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, -1 };
      run(1000, e, 90, &on, &off);
      CHECK("A", on == 8, "latched at %d s, want 8 (third error)", on);
      CHECK("A", off == 48 + (int)(USBF_QUIET_MS / 1000u), "off at %d, want %d", off,
            48 + (int)(USBF_QUIET_MS / 1000u)); }

    { static const int e[] = { 0, 4, 8, -1 };
      run(1000, e, 100, &on, &off);
      CHECK("B", on == 8 && off == 8 + (int)(USBF_QUIET_MS / 1000u),
            "on %d off %d, want 8 and %d", on, off, 8 + (int)(USBF_QUIET_MS / 1000u));
      /* one 4 s record missing: the 12 s gap is far under the quiet period */
      static const int g[] = { 0, 4, 8, 20, 24, -1 };
      run(1000, g, 40, &on, &off);
      CHECK("B", on == 8 && off == -1, "gap flapped the flag: on %d off %d", on, off); }

    { static const int pat[][4] = { {0, 10, -1, -1}, {0, 20, 40, -1}, {0, 31, 62, -1}, {0, 14, 31, -1} };
      for (unsigned p = 0; p < 4; p++) {
          run(5000, pat[p], 120, &on, &off);
          CHECK("C", on == -1, "pattern %u latched at %d", p, on);
      } }

    { static const int e[] = { 0, 15, 30, -1 };
      run(0, e, 90, &on, &off);
      CHECK("D", on == 30, "0,15,30 s latched at %d, want 30", on); }

    { UsbFault f; usbf_init(&f); int s;
      for (s = 0; s <= 8; s += 4) usbf_step(&f, (uint32_t)s * 1000u, 1);
      CHECK("E", usbf_flag(&f), "not latched");
      usbf_step(&f, 25000u, 1);                       /* error inside the quiet period */
      usbf_step(&f, 8000u + USBF_QUIET_MS, 0);
      CHECK("E", usbf_flag(&f), "cleared although an error restarted the quiet period");
      usbf_step(&f, 25000u + USBF_QUIET_MS, 0);
      CHECK("E", !usbf_flag(&f), "not cleared a quiet period after the last error");
      for (s = 100; s <= 108; s += 4) usbf_step(&f, (uint32_t)s * 1000u, 1);
      CHECK("E", usbf_flag(&f), "a new burst did not latch again"); }

    { UsbFault f; usbf_init(&f);
      for (int s = 0; s <= 100; s++) usbf_step(&f, (uint32_t)s * 1000u, 0);
      CHECK("F", !usbf_flag(&f), "flag on with no error");
      static const int e[] = { 0, 4, 8, -1 };
      run(0xFFFFFFFFu - 5000u, e, 100, &on, &off);
      CHECK("F", on == 8 && off == 8 + (int)(USBF_QUIET_MS / 1000u), "wrap: on %d off %d", on, off); }

    for (uint32_t t = 0; t < 30000u; t += 50u) {   /* H */
        uint32_t m = t % 10000u;
        int want = (m < 1200u && (m / 200u) % 2u == 0u) ? USBF_LED_LIT : USBF_LED_DARK;
        CHECK("H", usbf_led(0, 1, t) == want, "flash at %u: %d", t, usbf_led(0, 1, t));
        CHECK("H", usbf_led(1, 1, t) == USBF_LED_LIT && usbf_led(1, 0, t) == USBF_LED_LIT,
              "over-temperature not solid at %u", t);
        CHECK("H", usbf_led(0, 0, t) == USBF_LED_RELEASE, "idle not released at %u", t);
    }
    { int on = 0; for (uint32_t t = 0; t < 10000u; t += 10u) on += usbf_led(0, 1, t) == USBF_LED_LIT;
      CHECK("H", on == 3 * 20, "lit %d ticks of 10 ms per period, want 60", on); }

    if (fails) { printf("usbfault_test: %d FAILED\n", fails); return 1; }
    printf("usbfault_test: ok\n");
    return 0;
}
