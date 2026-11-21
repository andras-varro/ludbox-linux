/* Screen rotation: the one policy, the one var.rotate table, the per-angle
 * bezel and point maps.  Pure functions, no I/O, so tests/rotation_test.c can
 * run every branch on the host.
 *
 * Header-only (static inline) on purpose: framebuffer.c and touch_input.c are
 * linked by all three components, and a new .c here would need adding to
 * native_apps' build, vnc_client/Makefile SRCS and ScummVM's configure.patch
 * OBJS.  A header in that chain still counts for redeploy scope: changing this
 * file means redeploying all three components.
 *
 * Conventions.  An angle is in QUARTER TURNS CLOCKWISE (0..3): the picture the
 * user sees is turned that far clockwise from the panel's native landscape.
 * The setting is stored in degrees (0/90/180/270) and converted with
 * rotation_from_degrees().  The software portrait copy in framebuffer.c
 * (logical (vx,vy) -> physical x = vy, y = H-1-vx) turns the picture 90
 * counter-clockwise, which is quarter 3 here; it is the only software
 * rotation that exists.
 */
#ifndef ROTATION_H
#define ROTATION_H

#include <stdbool.h>

enum { ROT_0 = 0, ROT_90 = 1, ROT_180 = 2, ROT_270 = 3 };

/* What fb_init should do for a given setting on a given unit. */
typedef struct {
    int  quarter;   /* effective rotation, quarter turns clockwise */
    bool hw;        /* applied by FBIOPUT var.rotate (VRFB): logical surface is drawn upright */
    bool sw;        /* applied by framebuffer.c's software portrait copy (quarter 3 only) */
} RotationPlan;

/* Degrees (any multiple of 90, negative or above 360 folded) -> quarter; any
 * other value is not a rotation and gives 0. */
static inline int rotation_from_degrees(int deg) {
    if (deg % 90 != 0) return ROT_0;
    int q = (deg / 90) % 4;
    return q < 0 ? q + 4 : q;
}

/* The one policy.
 *   vrfb            /sys/class/graphics/fbN/rotate_type == 2.  Without it a
 *                   DMA-rotated fb underflows DISPC and blacks the panel, so no
 *                   rotation is ever PUT there.
 *   landscape_only  the app cannot lay out 480x800 (fb_declare_landscape_only):
 *                   it gets 0 for settings 0/90 and 180 for 180/270 -- the
 *                   nearest landscape picture.  Easy to change: this line only.
 * Without VRFB: 270 is today's software portrait, 90 and 180 fall back to 0. */
static inline RotationPlan rotation_plan(int setting_quarter, bool vrfb, bool landscape_only) {
    RotationPlan p = { ROT_0, false, false };
    int q = setting_quarter & 3;
    if (landscape_only) q = (q >= ROT_180) ? ROT_180 : ROT_0;
    if (vrfb) {
        p.quarter = q;
        p.hw = true;            /* also for 0: every init PUTs rotate, so 0 restores */
    } else if (q == ROT_270) {
        p.quarter = ROT_270;
        p.sw = true;
    }
    return p;
}

/* True when the logical surface is tall (480x800): quarters 1 and 3. */
static inline bool rotation_is_tall(int quarter) { return (quarter & 1) != 0; }

/* The ONE table: quarter turns clockwise -> fb_var_screeninfo.rotate.
 * Basis (inferred, panel check pending): omapfb passes var.rotate through as
 * the fbdev FB_ROTATE_* value (omapfb-main.c omapfb_get_vrfb_offset switches on
 * FB_ROTATE_UR/CW/UD/CCW), and linux/fb.h names 1 FB_ROTATE_CW, 3 FB_ROTATE_CCW.
 * Whether DISPC turns the PANEL picture clockwise for 1 has not been seen. */
static inline int rotation_var_rotate(int quarter) {
    static const int table[4] = { 0 /* UR */, 1 /* CW */, 2 /* UD */, 3 /* CCW */ };
    return table[quarter & 3];
}

/* Bezel margins measured on the physical (landscape) panel -> margins of the
 * logical surface rotated `quarter` clockwise.  Quarter 3 is the rotation the
 * software portrait always applied: T=L, B=R, L=B, R=T. */
static inline void rotation_bezel(int quarter, int *t, int *b, int *l, int *r) {
    int pt = *t, pb = *b, pl = *l, pr = *r;
    switch (quarter & 3) {
    case ROT_90:  *t = pr; *b = pl; *l = pt; *r = pb; break;
    case ROT_180: *t = pb; *b = pt; *l = pr; *r = pl; break;
    case ROT_270: *t = pl; *b = pr; *l = pb; *r = pt; break;
    default: break;
    }
}

/* A point on the physical panel (pw x ph, landscape) -> the same point on the
 * logical panel rotated `quarter` clockwise (ph x pw when tall). */
static inline void rotation_point(int quarter, int pw, int ph, int px, int py,
                                  int *lx, int *ly) {
    switch (quarter & 3) {
    case ROT_90:  *lx = py;          *ly = pw - 1 - px; break;
    case ROT_180: *lx = pw - 1 - px; *ly = ph - 1 - py; break;
    case ROT_270: *lx = ph - 1 - py; *ly = px;          break;
    default:      *lx = px;          *ly = py;          break;
    }
}

#endif /* ROTATION_H */
