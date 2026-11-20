/* A Bluetooth media player, for the speaker's AVRCP buttons.
 *
 * BlueZ answers an AVRCP speaker's PlayStatus / RegisterNotification from the player a
 * program registered with org.bluez.Media1.RegisterPlayer, and with none it answers
 * STOPPED: the speaker then sends PAUSE for a paused app and the press does nothing.
 * Registering a player with CanControl/CanPlay/CanPause set also makes bluetoothd hand
 * the speaker's PLAY/PAUSE/STOP/NEXT/PREVIOUS to this player's MPRIS methods INSTEAD of
 * the "<name> (AVRCP)" uinput node (profiles/audio/avrcp.c handle_passthrough, media.c
 * media_player_play...), so a method call comes back from avrcp_player_poll() as the same
 * InputMedia action the key would have been.
 *
 * Every call is non-blocking and every failure (no system bus, no bluetoothd) is silent:
 * the player simply stays unregistered and is retried every 10 s.  A NULL handle is
 * accepted everywhere. */
#ifndef AVRCP_PLAYER_H
#define AVRCP_PLAYER_H

#include <stdint.h>
#include "avrcp_logic.h"

typedef struct AvrcpPlayer AvrcpPlayer;

/* Connect to the system bus; NULL when it cannot (the caller carries on without). */
AvrcpPlayer *avrcp_player_open(void);

/* The playback state now.  Idempotent: only a change is signalled. */
void avrcp_player_set_status(AvrcpPlayer *p, AvrcpState s);

/* Once per frame: flush and read the bus without blocking, (re)register when due, and
 * return the oldest queued media action, INPUT_MEDIA_NONE when there is none.  A method
 * call wakes a dark panel (hw_blank_note_activity + hw_blank_keepalive) before it returns. */
InputMedia avrcp_player_poll(AvrcpPlayer *p, uint32_t now_ms);

/* Unregister and disconnect. */
void avrcp_player_close(AvrcpPlayer *p);

#endif
