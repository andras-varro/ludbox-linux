/* Host regression: the bus-free half of the Bluetooth player registration
 * (common/avrcp_logic.c).  A speaker's PLAY / PAUSE / STOP / NEXT / PREVIOUS reaches
 * Ludfin as an MPRIS method call, and it must become the very action its AVRCP key
 * would have been; the state must go to BlueZ as the MPRIS word it expects, and only
 * on a change.
 *
 * Negative control: swap two rows of the method table (Play <-> Pause) or two names
 * in avrcp_state_name and the first groups fail; make avrcp_state_changed always
 * return true and group 3 fails.
 *
 * Build: cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I common \
 *        tests/avrcp_player_test.c common/avrcp_logic.c -o /tmp/avrcp_player_test */
#include <stdio.h>
#include <string.h>
#include "avrcp_logic.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    /* 1: method name -> action */
    CHECK(avrcp_method_action("Play") == INPUT_MEDIA_PLAY);
    CHECK(avrcp_method_action("Pause") == INPUT_MEDIA_PAUSE);
    CHECK(avrcp_method_action("PlayPause") == INPUT_MEDIA_PLAYPAUSE);
    CHECK(avrcp_method_action("Stop") == INPUT_MEDIA_STOP);
    CHECK(avrcp_method_action("Next") == INPUT_MEDIA_NEXT);
    CHECK(avrcp_method_action("Previous") == INPUT_MEDIA_PREV);
    CHECK(avrcp_method_action("Seek") == INPUT_MEDIA_NONE);
    CHECK(avrcp_method_action("play") == INPUT_MEDIA_NONE);   /* case matters on D-Bus */
    CHECK(avrcp_method_action("") == INPUT_MEDIA_NONE);
    CHECK(avrcp_method_action(NULL) == INPUT_MEDIA_NONE);

    /* 2: state -> MPRIS PlaybackStatus */
    CHECK(strcmp(avrcp_state_name(AVRCP_PLAYING), "Playing") == 0);
    CHECK(strcmp(avrcp_state_name(AVRCP_PAUSED), "Paused") == 0);
    CHECK(strcmp(avrcp_state_name(AVRCP_STOPPED), "Stopped") == 0);
    CHECK(strcmp(avrcp_state_name(-1), "Stopped") == 0);
    CHECK(strcmp(avrcp_state_name(99), "Stopped") == 0);

    /* 3: dedupe */
    int cur = -1;
    CHECK(avrcp_state_changed(&cur, AVRCP_STOPPED));    /* first report always goes */
    CHECK(cur == AVRCP_STOPPED);
    CHECK(!avrcp_state_changed(&cur, AVRCP_STOPPED));
    CHECK(avrcp_state_changed(&cur, AVRCP_PLAYING));
    CHECK(!avrcp_state_changed(&cur, AVRCP_PLAYING));
    CHECK(avrcp_state_changed(&cur, AVRCP_PAUSED));
    CHECK(cur == AVRCP_PAUSED);

    /* 4: retry clock, across the 32-bit wrap */
    CHECK(avrcp_retry_due(1000, 1000));
    CHECK(!avrcp_retry_due(999, 1000));
    CHECK(avrcp_retry_due(11000, 1000));
    CHECK(!avrcp_retry_due(0xFFFFFFF0u, 5u));          /* due just past the wrap */
    CHECK(avrcp_retry_due(10u, 0xFFFFFFF0u));

    /* 5: the first registration try is due on the first poll even when the caller's clock
     * has bit 31 set (get_time_ms() is wall-clock ms cut to 32 bits; 0xF9978440 is what a
     * unit read on 2026-11-20, and with a due of 0 nothing was ever registered) */
    {
        bool armed = false;
        uint32_t due = 0;
        CHECK(avrcp_retry_tick(&armed, &due, 0xF9978440u));
        CHECK(armed && due == 0xF9978440u);
        due = 0xF9978440u + 10000u;                       /* a failed try moved it 10 s on */
        CHECK(!avrcp_retry_tick(&armed, &due, 0xF9978440u + 5000u));
        CHECK(avrcp_retry_tick(&armed, &due, 0xF9978440u + 10000u));
    }

    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("avrcp_player_test: all passed\n");
    return 0;
}
