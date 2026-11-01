/* usbfault.h — the USB-overload warning's pure decisions.
 *
 * An overloaded USB host port makes the kernel log "VBUS_ERROR in a_host
 * (91, <VBusValid)" about every 4 s, and musb's own retry re-enumerates the hub
 * within ~1 s every time, indefinitely (measured on .115, 2026-10-31: hub plus
 * four devices; with arcamond stopped the port never died, and the errors
 * ended when the load was unplugged).  arcamond (sysmon/arcamond.c) feeds this
 * state machine once a second and only warns: it never touches the port.  It
 * publishes the latch as MON_FLAG_USBFAULT in the ring header, which
 * framebuffer.c paints and which arcamond also flashes on red_led (usbf_led).
 * Header-only and pure: no file, clock or process access; the caller passes
 * a uint32_t millisecond clock (wraps are handled by signed differences).
 * tests/usbfault_test.c drives it.
 *
 * Rules: USBF_CUT_COUNT a_host errors within USBF_CUT_WIN_MS latch the warning;
 * it clears once USBF_QUIET_MS pass with no error.  Quiet is several error
 * periods (~4 s), so one missed record cannot flap it.
 */
#ifndef USBFAULT_H
#define USBFAULT_H

#include <stdint.h>

#define USBF_CUT_COUNT  3
#define USBF_CUT_WIN_MS 30000u
#define USBF_QUIET_MS   30000u   /* no error for this long clears the warning */

typedef struct {
    int latched;       /* warning on */
    int nerr;          /* recent error stamps held (0..3) */
    uint32_t errs[3];  /* the newest error times, oldest first */
    uint32_t last_ms;  /* time of the newest error */
} UsbFault;

static inline void usbf_init(UsbFault *f) {
    f->latched = 0;
    f->nerr = 0;
    f->errs[0] = f->errs[1] = f->errs[2] = 0;
    f->last_ms = 0;
}

/* The ring flag. */
static inline int usbf_flag(const UsbFault *f) { return f->latched; }

/* The fault's flash cadence, shared by the screen's sidebars (framebuffer.c)
 * and the red LED (arcamond.c): lit in the even 200 ms slots of the first
 * 1200 ms of every 10 s of the monotonic millisecond clock, so both flash
 * together. */
#define USBF_FLASH_PERIOD_MS 10000u
#define USBF_FLASH_PHASE_MS  200u

static inline int usbf_flash_on(uint32_t now_ms) {
    const uint32_t t = now_ms % USBF_FLASH_PERIOD_MS;
    return t < 6u * USBF_FLASH_PHASE_MS && ((t / USBF_FLASH_PHASE_MS) & 1u) == 0;
}

/* What arcamond does with red_led: give it back, light it, or hold it dark
 * (a flash's off phase; the saved value stays saved).  Over-temperature is
 * solid and wins over a latched fault. */
enum { USBF_LED_RELEASE = 0, USBF_LED_LIT = 1, USBF_LED_DARK = 2 };

static inline int usbf_led(int overtemp, int latched, uint32_t now_ms) {
    if (overtemp) return USBF_LED_LIT;
    if (!latched) return USBF_LED_RELEASE;
    return usbf_flash_on(now_ms) ? USBF_LED_LIT : USBF_LED_DARK;
}

/* One step.  error_seen: how many a_host VBUS_ERROR records arrived since the
 * last step.  The result is read with usbf_flag(). */
static inline void usbf_step(UsbFault *f, uint32_t now_ms, int error_seen) {
    for (int i = 0; i < error_seen && i < USBF_CUT_COUNT; i++) {
        if (f->nerr == USBF_CUT_COUNT) {
            f->errs[0] = f->errs[1]; f->errs[1] = f->errs[2]; f->nerr--;
        }
        f->errs[f->nerr++] = now_ms;
        f->last_ms = now_ms;
    }
    if (f->nerr == USBF_CUT_COUNT &&
        (uint32_t)(now_ms - f->errs[0]) <= USBF_CUT_WIN_MS)
        f->latched = 1;
    if (f->latched && !error_seen && (int32_t)(now_ms - f->last_ms) >= (int32_t)USBF_QUIET_MS) {
        f->latched = 0;
        f->nerr = 0;
    }
}
#endif
