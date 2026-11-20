/* See avrcp_logic.h. */
#include "avrcp_logic.h"
#include <string.h>

const char *avrcp_state_name(int state)
{
    switch (state) {
    case AVRCP_PLAYING: return "Playing";
    case AVRCP_PAUSED:  return "Paused";
    default:            return "Stopped";
    }
}

InputMedia avrcp_method_action(const char *method)
{
    static const struct { const char *name; InputMedia act; } map[] = {
        { "Play",      INPUT_MEDIA_PLAY },
        { "Pause",     INPUT_MEDIA_PAUSE },
        { "PlayPause", INPUT_MEDIA_PLAYPAUSE },
        { "Stop",      INPUT_MEDIA_STOP },
        { "Next",      INPUT_MEDIA_NEXT },
        { "Previous",  INPUT_MEDIA_PREV },
    };
    if (!method) return INPUT_MEDIA_NONE;
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
        if (strcmp(method, map[i].name) == 0) return map[i].act;
    return INPUT_MEDIA_NONE;
}

bool avrcp_state_changed(int *cur, int next)
{
    if (*cur == next) return false;
    *cur = next;
    return true;
}

bool avrcp_retry_due(uint32_t now, uint32_t due)
{
    return (int32_t)(now - due) >= 0;
}

bool avrcp_retry_tick(bool *armed, uint32_t *due, uint32_t now)
{
    if (!*armed) { *armed = true; *due = now; }
    return avrcp_retry_due(now, *due);
}
