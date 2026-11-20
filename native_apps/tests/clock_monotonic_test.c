/*
 * clock_monotonic_test: get_time_ms() must follow CLOCK_MONOTONIC, never the wall clock.
 *
 * Units have no RTC battery and boot at 1969-12-31; the network time sync steps the wall clock
 * to 2026 about 20 s after boot.  With a gettimeofday() source that step froze every
 * "next due" test (top_bar_info_due: an int32 difference against a stored time) for ~0.95 days.
 *
 * What it CAN see: the value tracks CLOCK_MONOTONIC within a few ms (the wall-clock version is
 * decades off, so it fails this on any host) and advances in real time.
 * What it CANNOT see: an actual clock step
 * (needs root and would disturb the host), so it proves the source, not the survival.
 *
 * Build (see tests/run-all.sh): gcc -I common -o build/clock_monotonic_test tests/clock_monotonic_test.c common/common.c ...
 */
#include "common.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>


static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static uint32_t mono_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
}

int main(void) {
    uint32_t a = mono_ms(), g = get_time_ms(), b = mono_ms();
    CHECK((int32_t)(g - a) >= -2 && (int32_t)(b - g) >= -2, "get_time_ms tracks CLOCK_MONOTONIC");
    uint32_t t0 = get_time_ms();
    usleep(30000);
    int32_t el = (int32_t)(get_time_ms() - t0);
    CHECK(el >= 25 && el < 500, "get_time_ms advances ~30 ms over a 30 ms sleep");
    if (fails) { printf("clock_monotonic_test: %d FAILED\n", fails); return 1; }
    printf("clock_monotonic_test: all passed\n");
    return 0;
}
