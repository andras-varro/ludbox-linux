/* When a page's status line (cp_status / cp_status_keep) leaves the title bar.
 * Pure, so tests/status_hold_test.c can drive it.  A hold of 0 means the
 * panel's default; STATUS_HOLD_KEEP means until something clears it — a
 * backup result is read at leisure, not inside a few seconds. */
#ifndef STATUS_HOLD_H
#define STATUS_HOLD_H

#include <stdbool.h>
#include <stdint.h>

#define STATUS_HOLD_KEEP 0xFFFFFFFFu

static inline bool status_hold_is_kept(uint32_t hold_ms) {
    return hold_ms == STATUS_HOLD_KEEP;
}

/* True when a shown message has outlived its hold (unsigned wrap-safe). */
static inline bool status_hold_expired(bool has_msg, uint32_t set_ms, uint32_t hold_ms,
                                       uint32_t default_ms, uint32_t now) {
    if (!has_msg || status_hold_is_kept(hold_ms)) return false;
    uint32_t hold = hold_ms ? hold_ms : default_ms;
    return now - set_ms > hold;
}

#endif
