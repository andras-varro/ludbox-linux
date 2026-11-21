/* Host-side regression for fb_present_indexed2x(): an 8-bit palettised image
 * shown 2x (nearest) over the logical surface.
 *
 * Runs on the DEV MACHINE with native gcc (the framebuffer_bpp_test pattern: a
 * synthetic Framebuffer over malloc'd buffers, no /dev/fb0).  The front buffer
 * is a "panel" bigger than the logical surface with a sentinel in every byte, so
 * a write into the bezel bands, or past the panel, is visible.
 *
 *   cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I common \
 *       -o build/framebuffer_present_test tests/framebuffer_present_test.c \
 *       common/framebuffer.c common/font.c common/hardware.c common/config.c \
 *       common/touch_input.c -lm && ./build/framebuffer_present_test
 *
 * The reference image is computed here from the spec alone (pixel (x, y) is
 * pal[src[(y/2)*sw + x/2]] while x < 2*sw and y < 2*sh, else black), never by
 * calling the code under test.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "framebuffer.h"
#include "rotation.h"

static int fails = 0;
static int checks = 0;

static void check(bool ok, const char *what) {
    checks++;
    if (!ok) { printf("  FAIL %s\n", what); fails++; }
}

#define SENTINEL 0xA5
#define PANEL_W 64
#define PANEL_H 48
#define GUARD 256

static uint8_t *front_raw;   /* panel + guard */
static uint8_t *back_raw;    /* logical back buffer + guard */
static Framebuffer fb;
static size_t front_bytes, back_bytes;

static uint16_t to565(uint32_t c) {
    return (uint16_t)((((c >> 16) & 0xF8) << 8) | (((c >> 8) & 0xFC) << 3) | ((c & 0xF8) >> 3));
}

/* Logical lw x lh surface at (vx, vy) on a PANEL_W x PANEL_H panel. */
static void setup(int lw, int lh, int vx, int vy, int bpp, bool portrait) {
    memset(&fb, 0, sizeof fb);
    fb.width = (uint32_t)lw;
    fb.height = (uint32_t)lh;
    fb.phys_width = PANEL_W;
    fb.phys_height = PANEL_H;
    fb.bytes_per_pixel = (uint32_t)bpp;
    fb.line_length = PANEL_W * (uint32_t)bpp;
    fb.view_x = vx;
    fb.view_y = vy;
    fb.portrait_mode = portrait;
    fb.sw_rotate = portrait;   /* sw copy only; hw rotation is a straight copy */
    fb.rotation = portrait ? ROT_270 : ROT_0;
    fb.double_buffering = true;
    front_bytes = (size_t)fb.line_length * PANEL_H;
    back_bytes = (size_t)lw * lh * bpp;
    free(front_raw); free(back_raw);
    front_raw = (uint8_t *)malloc(front_bytes + GUARD);
    back_raw = (uint8_t *)malloc(back_bytes + GUARD);
    if (!front_raw || !back_raw) { printf("  FAIL out of memory\n"); exit(2); }
    memset(front_raw, SENTINEL, front_bytes + GUARD);
    memset(back_raw, SENTINEL, back_bytes + GUARD);
    fb.buffer = (uint32_t *)front_raw;
    fb.back_buffer = (uint32_t *)back_raw;
    fb.screen_size = front_bytes;
    fb.back_buffer_size = back_bytes;
}

static uint32_t pal[256];
static void make_pal(void) {
    for (int i = 0; i < 256; i++)
        pal[i] = (uint32_t)((i * 37) & 0xFF) << 16 | (uint32_t)((i * 91 + 5) & 0xFF) << 8 |
                 (uint32_t)((i * 13 + 7) & 0xFF);
}

static uint8_t *make_src(int sw, int sh) {
    uint8_t *s = (uint8_t *)malloc((size_t)sw * sh);
    for (int i = 0; i < sw * sh; i++) s[i] = (uint8_t)(i * 7 + 3);
    return s;
}

/* Expected colour (0xRRGGBB) of logical pixel (x, y), black outside 2*sw x 2*sh. */
static uint32_t expect_px(const uint8_t *src, int sw, int sh, int x, int y) {
    if (x >= 2 * sw || y >= 2 * sh) return 0;
    return pal[src[(y / 2) * sw + x / 2]] & 0xFFFFFF;
}

static bool guard_ok(const uint8_t *raw, size_t bytes) {
    for (size_t i = 0; i < GUARD; i++) if (raw[bytes + i] != SENTINEL) return false;
    return true;
}

/* Compare the whole panel: inside the surface against the reference, outside
 * against the sentinel.  Landscape only. */
static void check_panel(const char *what, const uint8_t *src, int sw, int sh) {
    int bpp = (int)fb.bytes_per_pixel, bad_in = 0, bad_out = 0;
    for (int py = 0; py < PANEL_H; py++)
        for (int px = 0; px < PANEL_W; px++) {
            const uint8_t *p = front_raw + (size_t)py * fb.line_length + (size_t)px * bpp;
            int lx = px - fb.view_x, ly = py - fb.view_y;
            bool in = lx >= 0 && ly >= 0 && lx < (int)fb.width && ly < (int)fb.height;
            if (in) {
                uint32_t want = expect_px(src, sw, sh, lx, ly), got;
                if (bpp == 2) { uint16_t v; memcpy(&v, p, 2); got = v; want = to565(want); }
                else { uint32_t v; memcpy(&v, p, 4); got = v & 0xFFFFFF; }
                if (got != want) bad_in++;
            } else {
                for (int b = 0; b < bpp; b++) if (p[b] != SENTINEL) { bad_out++; break; }
            }
        }
    char msg[160];
    snprintf(msg, sizeof msg, "%s: surface matches reference (%d bad)", what, bad_in);
    check(bad_in == 0, msg);
    snprintf(msg, sizeof msg, "%s: outside the surface untouched (%d bad)", what, bad_out);
    check(bad_out == 0, msg);
    snprintf(msg, sizeof msg, "%s: no write past the panel", what);
    check(guard_ok(front_raw, front_bytes), msg);
    snprintf(msg, sizeof msg, "%s: no write past the back buffer", what);
    check(guard_ok(back_raw, back_bytes), msg);
}

int main(void) {
    make_pal();
    printf("fb_present_indexed2x\n");

    /* exact fit, nonzero view offset, 32bpp fast path */
    {
        int sw = 10, sh = 7;
        uint8_t *src = make_src(sw, sh);
        setup(2 * sw, 2 * sh, 5, 3, 4, false);
        fb_present_indexed2x(&fb, src, sw, sh, pal);
        check_panel("32bpp exact fit at (5,3)", src, sw, sh);
        /* the fast path presents straight to the front buffer: back buffer untouched */
        check(back_raw[0] == SENTINEL && back_raw[back_bytes - 1] == SENTINEL,
              "32bpp: back buffer not used");
        free(src);
    }
    /* palette lookup: every index reaches the panel (256 entries, 16x16 source) */
    {
        int sw = 16, sh = 16;
        uint8_t *src = (uint8_t *)malloc(256);
        for (int i = 0; i < 256; i++) src[i] = (uint8_t)i;
        setup(32, 32, 7, 2, 4, false);
        fb_present_indexed2x(&fb, src, sw, sh, pal);
        check_panel("32bpp all 256 palette entries", src, sw, sh);
        free(src);
    }
    /* odd logical size: last column and row black */
    {
        int sw = 9, sh = 5;
        uint8_t *src = make_src(sw, sh);
        setup(2 * sw + 1, 2 * sh + 1, 4, 5, 4, false);
        fb_present_indexed2x(&fb, src, sw, sh, pal);
        check_panel("32bpp odd logical size (2sw+1 x 2sh+1)", src, sw, sh);
        const uint8_t *p = front_raw + (size_t)(5 + 2 * sh) * fb.line_length + 4 * 4;
        uint32_t v; memcpy(&v, p, 4);
        check((v & 0xFFFFFF) == 0, "odd size: extra row is black");
        free(src);
    }
    /* source larger than the surface: clipped, no overflow */
    {
        int sw = 30, sh = 20;
        uint8_t *src = make_src(sw, sh);
        setup(20, 14, 6, 4, 4, false);
        fb_present_indexed2x(&fb, src, sw, sh, pal);
        check_panel("32bpp source larger than surface", src, sw, sh);
        free(src);
    }
    /* source smaller than the surface: remainder black */
    {
        int sw = 4, sh = 3;
        uint8_t *src = make_src(sw, sh);
        setup(20, 14, 2, 2, 4, false);
        fb_present_indexed2x(&fb, src, sw, sh, pal);
        check_panel("32bpp source smaller than surface", src, sw, sh);
        free(src);
    }
    /* 16bpp fallback: back buffer + swap, rgb565 */
    {
        int sw = 10, sh = 7;
        uint8_t *src = make_src(sw, sh);
        setup(2 * sw, 2 * sh, 5, 3, 2, false);
        fb_present_indexed2x(&fb, src, sw, sh, pal);
        check_panel("16bpp fallback exact fit", src, sw, sh);
        free(src);
    }
    /* 16bpp odd size and oversize source */
    {
        int sw = 12, sh = 9;
        uint8_t *src = make_src(sw, sh);
        setup(2 * 8 + 1, 2 * 5 + 1, 3, 2, 2, false);
        fb_present_indexed2x(&fb, src, sw, sh, pal);
        check_panel("16bpp odd size, source larger", src, sw, sh);
        free(src);
    }
    /* portrait: must equal fb_swap() of the same reference image */
    {
        int sw = 8, sh = 5;
        uint8_t *src = make_src(sw, sh);
        for (int bpp = 2; bpp <= 4; bpp += 2) {
            setup(2 * sw, 2 * sh, 3, 2, bpp, true);
            fb_present_indexed2x(&fb, src, sw, sh, pal);
            uint8_t *got = (uint8_t *)malloc(front_bytes);
            memcpy(got, front_raw, front_bytes);
            setup(2 * sw, 2 * sh, 3, 2, bpp, true);
            for (int y = 0; y < 2 * sh; y++)
                for (int x = 0; x < 2 * sw; x++) {
                    uint32_t c = expect_px(src, sw, sh, x, y);
                    size_t i = (size_t)y * (2 * sw) + x;
                    if (bpp == 2) ((uint16_t *)back_raw)[i] = to565(c);
                    else ((uint32_t *)back_raw)[i] = c;
                }
            fb_swap(&fb);
            check(memcmp(got, front_raw, front_bytes) == 0,
                  bpp == 2 ? "16bpp portrait equals fb_swap of reference"
                           : "32bpp portrait equals fb_swap of reference");
            check(guard_ok(front_raw, front_bytes), "portrait: no write past the panel");
            free(got);
        }
        free(src);
    }
    /* VRFB rotation: the surface is tall but the fb behind it is too, so both
     * present paths are the straight copy, not the 90 CCW one */
    {
        int sw = 8, sh = 5;
        uint8_t *src = make_src(sw, sh);
        setup(2 * sw, 2 * sh, 3, 2, 4, false);
        fb.portrait_mode = true; fb.rotation = ROT_90; fb.sw_rotate = false;
        fb_present_indexed2x(&fb, src, sw, sh, pal);
        check_panel("32bpp tall VRFB surface takes the straight fast path", src, sw, sh);
        uint8_t *got = (uint8_t *)malloc(front_bytes);
        memcpy(got, front_raw, front_bytes);
        setup(2 * sw, 2 * sh, 3, 2, 4, false);
        for (int y = 0; y < 2 * sh; y++)
            for (int x = 0; x < 2 * sw; x++)
                ((uint32_t *)back_raw)[(size_t)y * (2 * sw) + x] = expect_px(src, sw, sh, x, y);
        fb.portrait_mode = true; fb.rotation = ROT_90; fb.sw_rotate = false;
        fb_swap(&fb);
        check(memcmp(got, front_raw, front_bytes) == 0, "tall VRFB surface: fb_swap is the straight copy");
        free(got);
        free(src);
    }
    /* no double buffering: draws into fb->buffer directly, no crash */
    {
        int sw = 4, sh = 3;
        uint8_t *src = make_src(sw, sh);
        setup(2 * sw, 2 * sh, 0, 0, 4, false);
        fb.double_buffering = false;
        fb_present_indexed2x(&fb, src, sw, sh, pal);
        check(guard_ok(front_raw, front_bytes), "no double buffering: no overflow");
        free(src);
    }

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
