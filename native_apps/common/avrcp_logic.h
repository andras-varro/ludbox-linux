/* The part of the Bluetooth player registration that needs no bus: the state names BlueZ
 * expects, the method-to-action table, the dedupe and the retry clock.  Host-tested
 * (tests/avrcp_player_test.c); avrcp_player.c is the D-Bus half. */
#ifndef AVRCP_LOGIC_H
#define AVRCP_LOGIC_H

#include <stdbool.h>
#include <stdint.h>
#include "input_scan.h"   /* InputMedia */

typedef enum { AVRCP_STOPPED = 0, AVRCP_PLAYING, AVRCP_PAUSED } AvrcpState;

/* The MPRIS PlaybackStatus string for a state ("Stopped" for anything out of range). */
const char *avrcp_state_name(int state);

/* The media action an org.mpris.MediaPlayer2.Player method call stands for (the same
 * action its AVRCP key would queue); INPUT_MEDIA_NONE for any other method name. */
InputMedia avrcp_method_action(const char *method);

/* Dedupe: store `next` in *cur and return true when it differs (a *cur of -1 differs from
 * everything: nothing reported yet), false when nothing changed. */
bool avrcp_state_changed(int *cur, int next);

/* True when `now` has reached `due` (both millisecond counters, wrap-safe). */
bool avrcp_retry_due(uint32_t now, uint32_t due);

/* The registration clock: is a (re)try due at `now`?  The first call arms *due to `now`, so
 * the first try is due at once whatever the caller's clock reads — get_time_ms() is
 * wall-clock time cut to 32 bits, and a *due of 0 can lie 2^31 ms in its "future". */
bool avrcp_retry_tick(bool *armed, uint32_t *due, uint32_t now);

#endif
