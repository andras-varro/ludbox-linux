/* Host regression for the Ludfin visualiser engine (jellyfin/jf_visu.c).
 *
 *   cd native_apps && gcc -Wall -Wextra -I. -I jellyfin -o build/jf_visu_test \
 *       tests/jf_visu_test.c jellyfin/jf_visu.c -lm && ./build/jf_visu_test
 *
 *   A  every map pixel of both maps stays inside the buffer (+1 and +w+1), weights sum 240..256
 *   B  decay: after a burst the image max never rises and reaches 0 in silence
 *   C  waveform: a full-scale square touches h/2 +- h/3; n == 0 draws nothing and the warp still runs;
 *      fewer than w samples read exactly n (ASAN with an exact-size malloc)
 *   D  chunking: at most ceil(h/8) rows per step, fields change only after the dwell
 *   E  uint32 now_ms wrap: no hang, no field change every frame
 *   F  palette: entry 0 black; each family has >= 3 hue buckets among saturated entries
 *   G  every field: offsets incl. +1 taps in bounds, weights <= 256
 *   H  every field: silence never raises the image maximum
 *   I  FFT peak lands on a bin-centred sine;  J  pal_mix endpoints
 *   K  200 scene changes: field, wave, palette each differ from the previous
 *   L  beat: pulsed tone >= 3, steady <= 2;  N  beat flash off (default) never lifts the palette, on does;  M  host ms per step/map chunk < 2 (skip: -DJF_VISU_NO_TIMING_ASSERT) */
#include "jf_visu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int map_bad(const JfVisu *v, int which, int *wsum_bad) {
    const uint32_t *off, *wgt;
    int n = jf_visu_w(v) * jf_visu_h(v), w = jf_visu_w(v), bad = 0;
    jf_visu_map_peek(v, which, &off, &wgt);
    *wsum_bad = 0;
    for (int i = 0; i < n; i++) {
        if (off[i] + (uint32_t)w + 1 >= (uint32_t)n) bad++;
        uint32_t k = wgt[i];
        int s = (int)(k & 255) + (int)((k >> 8) & 255) + (int)((k >> 16) & 255) + (int)(k >> 24);
        if (s < 240 || s > 256) (*wsum_bad)++;
    }
    return bad;
}

static void group_a(void) {
    static const int sz[][2] = {{16, 16}, {37, 23}, {101, 64}, {400, 240}, {512, 512}};
    for (unsigned k = 0; k < sizeof sz / sizeof sz[0]; k++) {
        JfVisu v;
        CHECK(jf_visu_init(&v, sz[k][0], sz[k][1]), "init %dx%d", sz[k][0], sz[k][1]);
        uint32_t t = 0;
        int bad = 0, wb = 0, b;
        for (int f = 0; f < 5; f++) {          /* init + several field swaps */
            for (int i = 0; i < 700; i++, t += 33) {
                jf_visu_step(&v, NULL, 0, t);
                if (!(i & 7)) {
                    for (int m = 0; m < 2; m++) { if (m && (v.building || !jf_visu_fields(&v))) continue; bad += map_bad(&v, m, &b); wb += b; }
                }
            }
        }
        CHECK(jf_visu_fields(&v) >= 3, "A %dx%d: only %d fields swapped", sz[k][0], sz[k][1], jf_visu_fields(&v));
        CHECK(bad == 0, "A %dx%d: %d out-of-range offsets", sz[k][0], sz[k][1], bad);
        CHECK(wb == 0, "A %dx%d: %d weight sums outside 240..256", sz[k][0], sz[k][1], wb);
        jf_visu_free(&v);
    }
    JfVisu v;
    CHECK(!jf_visu_init(&v, 15, 100) && !jf_visu_init(&v, 100, 513), "init refuses out-of-range sizes");
}

static int img_max(const JfVisu *v) {
    int m = 0, n = jf_visu_w(v) * jf_visu_h(v);
    const uint8_t *p = jf_visu_image(v);
    for (int i = 0; i < n; i++) if (p[i] > m) m = p[i];
    return m;
}

static int img_sum(const JfVisu *v) {
    int s = 0, n = jf_visu_w(v) * jf_visu_h(v);
    const uint8_t *p = jf_visu_image(v);
    for (int i = 0; i < n; i++) s += p[i];
    return s;
}

static void group_b(void) {
    JfVisu v;
    int16_t sq[400];
    jf_visu_init(&v, 400, 240);
    for (int i = 0; i < 400; i++) sq[i] = (i / 50) & 1 ? 32767 : -32768;
    uint32_t t = 0;
    for (int f = 0; f < 5; f++, t += 33) jf_visu_step(&v, sq, 400, t);
    CHECK(img_max(&v) == 255, "B burst did not reach 255");
    int prev = img_max(&v), rises = 0, zero_at = -1;
    for (int f = 0; f < 300; f++, t += 33) {
        jf_visu_step(&v, NULL, 0, t);
        int m = img_max(&v);
        if (m > prev) rises++;
        if (m == 0 && zero_at < 0) zero_at = f;
        prev = m;
    }
    CHECK(rises == 0, "B max rose %d times in silence", rises);
    CHECK(zero_at >= 0 && img_sum(&v) == 0, "B image not dark after 300 silent frames (max %d)", img_max(&v));
    printf("B: image reached 0 after %d silent frames\n", zero_at);
    jf_visu_free(&v);
}

static void group_c(void) {
    JfVisu v;
    int w = 400, h = 240;
    jf_visu_init(&v, w, h); jf_visu_set_scene(&v, JF_VISU_F_ZOOM, JF_VISU_W_SCOPE, JF_VISU_P_FIRE);
    int16_t sq[400];
    for (int i = 0; i < w; i++) sq[i] = (i / 25) & 1 ? 32767 : -32768;
    jf_visu_step(&v, sq, w, 0);
    const uint8_t *p = jf_visu_image(&v);
    int lo = h, hi = -1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if (p[y * w + x] == 255) { if (y < lo) lo = y; if (y > hi) hi = y; }
    int amp = h / 3;
    CHECK(lo <= h / 2 - amp + 3 && lo >= h / 2 - amp - 3, "C square top row %d, want ~%d", lo, h / 2 - amp);
    CHECK(hi >= h / 2 + amp - 3 && hi <= h / 2 + amp + 3, "C square bottom row %d, want ~%d", hi, h / 2 + amp);
    /* n == 0 after a wave: nothing new at 255 (old pixels decayed), warp still moves the picture */
    int before = img_sum(&v);
    jf_visu_step(&v, NULL, 0, 33);
    CHECK(img_max(&v) < 255, "C n==0 still drew 255");
    CHECK(img_sum(&v) != before, "C warp did not run with n==0");
    jf_visu_free(&v);
    /* fresh engine, n == 0: blank */
    jf_visu_init(&v, w, h); jf_visu_set_scene(&v, JF_VISU_F_ZOOM, JF_VISU_W_SCOPE, JF_VISU_P_FIRE);
    jf_visu_step(&v, NULL, 0, 0);
    CHECK(img_max(&v) == 0, "C n==0 drew on a blank image");
    jf_visu_free(&v);
    /* fewer samples than w, exact-size allocation (ASAN sees an over-read) */
    int ns[] = {1, 2, 7, 399, 400, 401, 1000};
    jf_visu_init(&v, w, h); jf_visu_set_scene(&v, JF_VISU_F_ZOOM, JF_VISU_W_SCOPE, JF_VISU_P_FIRE);
    for (unsigned k = 0; k < sizeof ns / sizeof ns[0]; k++) {
        int16_t *a = malloc((size_t)ns[k] * sizeof *a);
        for (int i = 0; i < ns[k]; i++) a[i] = (int16_t)(i * 97 - 20000);
        jf_visu_step(&v, a, ns[k], (uint32_t)(k * 33));
        free(a);
    }
    /* 7 samples: only the right 7 columns carry wave */
    jf_visu_free(&v);
    jf_visu_init(&v, w, h); jf_visu_set_scene(&v, JF_VISU_F_ZOOM, JF_VISU_W_SCOPE, JF_VISU_P_FIRE);
    int16_t seven[7] = {32767, 32767, 32767, 32767, 32767, 32767, 32767};
    jf_visu_step(&v, seven, 7, 0);
    p = jf_visu_image(&v);
    int left = 0;
    for (int y = 0; y < h; y++) for (int x = 0; x < w - 7; x++) if (p[y * w + x]) left++;
    CHECK(left == 0, "C 7 samples drew %d pixels left of the wave", left);
    CHECK(p[(h / 2 + (32767 * (h / 3) >> 15)) * w + w - 1] == 255, "C last sample not drawn at the right edge");
    jf_visu_free(&v);
}

static int map_hash(const JfVisu *v) {
    const uint32_t *off, *wgt;
    jf_visu_map_peek(v, 0, &off, &wgt);
    uint32_t h = 2166136261u;
    int n = jf_visu_w(v) * jf_visu_h(v);
    for (int i = 0; i < n; i++) h = (h ^ off[i] ^ wgt[i]) * 16777619u;
    return (int)h;
}

static void group_d(void) {
    JfVisu v;
    int h = 240, per = (h + 7) / 8;
    jf_visu_init(&v, 400, h);
    uint32_t t = 1000;
    int maxrows = 0, changes = 0, differs = 0, last_fields = 0, h0 = map_hash(&v), first_change_ms = -1;
    for (int f = 0; f < 60000 / 33; f++, t += 33) {
        jf_visu_step(&v, NULL, 0, t);
        int r = jf_visu_rows_built_last_step(&v);
        if (r > maxrows) maxrows = r;
        if (jf_visu_fields(&v) != last_fields) {
            changes++; last_fields = jf_visu_fields(&v);
            int hh = map_hash(&v);
            if (hh != h0) differs++;
            h0 = hh;
            if (first_change_ms < 0) first_change_ms = (int)(t - 1000);
        }
    }
    CHECK(maxrows == per, "D max rows per step %d, want %d", maxrows, per);
    CHECK(changes >= 2 && changes <= 5, "D %d field changes in 60 s (want 2..5)", changes);
    CHECK(differs == changes, "D only %d of %d new maps differ", differs, changes);
    CHECK(first_change_ms >= 15000, "D first change at %d ms, before the dwell", first_change_ms);
    printf("D: %d field changes in 60 s, first at %d ms, max %d rows/step\n", changes, first_change_ms, maxrows);
    jf_visu_free(&v);
}

static void group_e(void) {
    JfVisu v;
    jf_visu_init(&v, 64, 48);
    uint32_t t = 0xFFFFFFF0u;
    int changes = 0, last = 0;
    for (int f = 0; f < 3000; f++, t += 33) {   /* ~100 s across the wrap */
        jf_visu_step(&v, NULL, 0, t);
        if (jf_visu_fields(&v) != last) { changes++; last = jf_visu_fields(&v); }
    }
    CHECK(changes >= 4 && changes <= 8, "E %d field changes across the wrap in 99 s", changes);
    /* a clock jumping backwards or far ahead must not wedge it */
    jf_visu_step(&v, NULL, 0, 5);
    jf_visu_step(&v, NULL, 0, 0x80000000u);
    jf_visu_free(&v);
}

/* hue of an 0x00RRGGBB entry in degrees, or -1 when its HSV saturation is <= 0.25 */
static int hue_of(uint32_t c) {
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    if (mx == 0 || (mx - mn) * 4 <= mx) return -1;
    double d = mx - mn, hh;
    if (mx == r) hh = fmod((g - b) / d, 6.0);
    else if (mx == g) hh = (b - r) / d + 2;
    else hh = (r - g) / d + 4;
    hh *= 60;
    if (hh < 0) hh += 360;
    return (int)hh % 360;
}

static void group_f(void) {
    JfVisu v;
    jf_visu_init(&v, 32, 32);
    uint32_t t = 0;
    for (int f = 0; f < 400; f++, t += 700) {   /* sweep the palette phases: entry 0 stays black */
        jf_visu_step(&v, NULL, 0, t);
        CHECK(jf_visu_palette(&v)[0] == 0, "F entry 0 is 0x%06x", (unsigned)jf_visu_palette(&v)[0]);
    }
    static const char *nm[] = {"fire", "ocean", "neon", "sunset", "rainbow"};
    for (int fam = 0; fam < JF_VISU_NPALS; fam++) {
        for (int hue = 0; hue < 3072; hue += 768) {
            uint32_t p[256];
            jf_visu_pal_family(&v, fam, hue, p);
            int seen[6] = {0}, nb = 0;
            for (int i = 0; i < 256; i++) { int hd = hue_of(p[i]); if (hd >= 0) seen[hd / 60]++; }
            for (int k = 0; k < 6; k++) nb += seen[k] > 0;
            CHECK(p[0] == 0, "F %s hue %d entry 0 not black", nm[fam], hue);
            CHECK(nb >= (hue == 0 ? 3 : 2), "F %s hue %d: only %d hue buckets", nm[fam], hue, nb);   /* rotated: at least two */
        }
    }
    jf_visu_free(&v);
}

static const int all_fields[] = {JF_VISU_F_ZOOM, JF_VISU_F_TUNNEL, JF_VISU_F_SPIRAL, JF_VISU_F_RIPPLE, JF_VISU_F_KALEIDO};

static void group_g(void) {   /* every field: offsets incl. +1 taps in bounds, weights <= 256 */
    static const int sz[][2] = {{16, 16}, {37, 23}, {400, 240}, {512, 512}};
    for (unsigned k = 0; k < sizeof sz / sizeof sz[0]; k++) {
        JfVisu v;
        jf_visu_init(&v, sz[k][0], sz[k][1]);
        int w = sz[k][0], h = sz[k][1], n = w * h;
        for (int f = 0; f < JF_VISU_NFIELDS; f++) {
            for (int rep = 0; rep < 3; rep++) {      /* each set_scene draws a fresh pan/spin */
                jf_visu_set_scene(&v, all_fields[f], 0, 0);
                const uint32_t *off, *wgt;
                jf_visu_map_peek(&v, 0, &off, &wgt);
                int bad = 0, wb = 0;
                for (int i = 0; i < n; i++) {
                    uint32_t o = off[i];
                    if (o + (uint32_t)w + 1 >= (uint32_t)n) bad++;
                    if (o / (uint32_t)w + 1 >= (uint32_t)h || o % (uint32_t)w + 1 >= (uint32_t)w) bad++;
                    uint32_t q = wgt[i];
                    int s = (int)(q & 255) + (int)((q >> 8) & 255) + (int)((q >> 16) & 255) + (int)(q >> 24);
                    if (s > 256) wb++;
                }
                CHECK(bad == 0, "G %dx%d field %d: %d bad offsets", w, h, f, bad);
                CHECK(wb == 0, "G %dx%d field %d: %d weight sums > 256", w, h, f, wb);
            }
        }
        jf_visu_free(&v);
    }
}

static void group_h(void) {   /* every field: with no input the image maximum never increases */
    for (int f = 0; f < JF_VISU_NFIELDS; f++) {
        JfVisu v;
        int16_t sq[400];
        jf_visu_init(&v, 400, 240);
        jf_visu_set_scene(&v, all_fields[f], JF_VISU_W_SCOPE, JF_VISU_P_FIRE);
        for (int i = 0; i < 400; i++) sq[i] = (i / 50) & 1 ? 32767 : -32768;
        uint32_t t = 0;
        for (int k = 0; k < 5; k++, t += 33) jf_visu_step(&v, sq, 400, t);
        int prev = img_max(&v), rises = 0;
        CHECK(prev > 0, "H field %d: burst drew nothing", f);
        for (int k = 0; k < 60; k++, t += 33) {
            jf_visu_step(&v, NULL, 0, t);
            int m = img_max(&v);
            if (m > prev) rises++;
            prev = m;
        }
        CHECK(rises == 0, "H field %d: max rose %d times in silence", f, rises);
        jf_visu_free(&v);
    }
}

static void tone(int16_t *s, int n, int k, double amp) {
    for (int i = 0; i < n; i++) s[i] = (int16_t)lrint(amp * sin(2 * M_PI * k * i / 256.0));
}

static void group_i(void) {   /* FFT: a bin-centred sine lands on its bin */
    JfVisu v;
    jf_visu_init(&v, 64, 48);
    static const int ks[] = {8, 40};
    for (unsigned q = 0; q < 2; q++) {
        int16_t s[256];
        uint16_t mag[128];
        tone(s, 256, ks[q], 20000);
        jf_visu_spectrum(&v, s, 256, mag);
        int pk = 0;
        for (int i = 1; i < 128; i++) if (mag[i] > mag[pk]) pk = i;
        CHECK(pk >= ks[q] - 1 && pk <= ks[q] + 1, "I sine k=%d peaks at bin %d", ks[q], pk);
        CHECK(mag[pk] > 0, "I sine k=%d: empty spectrum", ks[q]);
    }
    jf_visu_free(&v);
}

static void group_j(void) {   /* pal_mix endpoints */
    JfVisu v;
    jf_visu_init(&v, 32, 32);
    uint32_t a[256], b[256], o[256];
    jf_visu_pal_family(&v, JF_VISU_P_FIRE, 0, a);
    jf_visu_pal_family(&v, JF_VISU_P_OCEAN, 1000, b);
    CHECK(memcmp(a, b, sizeof a) != 0, "J the two palettes are equal");
    jf_visu_pal_mix(a, b, 0, o);
    CHECK(memcmp(o, a, sizeof a) == 0, "J mix t=0 is not a");
    jf_visu_pal_mix(a, b, 256, o);
    CHECK(memcmp(o, b, sizeof b) == 0, "J mix t=256 is not b");
    jf_visu_free(&v);
}

static void group_k(void) {   /* 200 scene changes: field, wave, palette each differ from the previous */
    JfVisu v;
    jf_visu_init(&v, 32, 32);
    int pf, pw, pp, nf, nw, np, same = 0, changes = 0;
    jf_visu_scene(&v, &pf, &pw, &pp);
    uint32_t t = 1000;
    for (long guard = 0; changes < 200 && guard < 400000; guard++, t += 33) {
        int before = jf_visu_fields(&v);
        jf_visu_step(&v, NULL, 0, t);
        if (jf_visu_fields(&v) == before) continue;
        changes++;
        jf_visu_scene(&v, &nf, &nw, &np);
        if (nf == pf || nw == pw || np == pp) same++;
        pf = nf; pw = nw; pp = np;
    }
    CHECK(changes == 200, "K only %d scene changes reached", changes);
    CHECK(same == 0, "K %d of 200 changes kept an axis unchanged", same);
    jf_visu_free(&v);
}

static void group_l(void) {   /* beat detector */
    int16_t s[256], z[256];
    tone(s, 256, 2, 16000);
    memset(z, 0, sizeof z);
    JfVisu v;
    jf_visu_init(&v, 64, 48);
    uint32_t t = 0;
    for (int f = 0; f < 120; f++, t += 33) {      /* 15 frames off, 15 on, ... */
        if ((f / 15) & 1) jf_visu_step(&v, s, 256, t); else jf_visu_step(&v, z, 256, t);
    }
    int pulsed = jf_visu_beats(&v);
    jf_visu_free(&v);
    jf_visu_init(&v, 64, 48);
    for (int f = 0; f < 120; f++, t += 33) jf_visu_step(&v, s, 256, t);
    int steady = jf_visu_beats(&v);
    jf_visu_free(&v);
    printf("L: pulsed tone %d beats, steady tone %d beats in 120 frames\n", pulsed, steady);
    CHECK(pulsed >= 3, "L pulsed tone gave only %d beats", pulsed);
    CHECK(steady <= 2, "L steady tone gave %d beats (want <= 2)", steady);
}

/* N  beat flash is a setting: pulsed tone with it OFF (the default) never lifts the palette off the scene's own family (and
 *    still counts beats); with it ON the palette is lifted on a beat.  Frames are 3 ms apart so the hue stays at bucket 0. */
static int flash_run(bool set, bool on, int *beats) {
    int16_t s[256], z[256];
    tone(s, 256, 2, 16000);
    memset(z, 0, sizeof z);
    JfVisu v;
    uint32_t exp[256];
    int lifted = 0;
    jf_visu_init(&v, 64, 48);
    if (set) jf_visu_set_flash(&v, on);
    int f0, w0, p0;
    jf_visu_scene(&v, &f0, &w0, &p0);
    jf_visu_pal_family(&v, p0, 0, exp);
    uint32_t t = 0;
    for (int f = 0; f < 120; f++, t += 3) {
        if ((f / 15) & 1) jf_visu_step(&v, s, 256, t); else jf_visu_step(&v, z, 256, t);
        if (memcmp(jf_visu_palette(&v), exp, sizeof exp) != 0) lifted++;
    }
    *beats = jf_visu_beats(&v);
    jf_visu_free(&v);
    return lifted;
}

static void group_n(void) {
    int b0, b1;
    int off = flash_run(false, false, &b0);
    printf("N: default (flash off): %d lifted frames, %d beats\n", off, b0);
    CHECK(b0 >= 3, "N the pulsed tone gave only %d beats, the test premise failed", b0);
    CHECK(off == 0, "N a beat lifted the palette in %d frames with the flash off (the default)", off);
    int on = flash_run(true, true, &b1);
    CHECK(on > 0 && b1 >= 3, "N flash on: %d lifted frames, %d beats (want a lift)", on, b1);
    int off2 = flash_run(true, false, &b1);
    CHECK(off2 == 0, "N explicit off lifted the palette in %d frames", off2);
}

/* O  next scene on demand: well inside the dwell no field changes by itself; after jf_visu_next_scene one does within the
 *    chunked build, and the dwell restarts (no second change soon after). */
static void group_o(void) {
    JfVisu v;
    jf_visu_init(&v, 64, 48);
    uint32_t t = 0;
    for (int f = 0; f < 5; f++, t += 33) jf_visu_step(&v, NULL, 0, t);
    CHECK(jf_visu_fields(&v) == 0, "O a field changed inside the dwell");
    jf_visu_next_scene(&v, t);
    for (int f = 0; f < 40; f++, t += 33) jf_visu_step(&v, NULL, 0, t);
    CHECK(jf_visu_fields(&v) == 1, "O next_scene gave %d new fields (want 1)", jf_visu_fields(&v));
    for (int f = 0; f < 60; f++, t += 33) jf_visu_step(&v, NULL, 0, t);
    CHECK(jf_visu_fields(&v) == 1, "O the dwell timer was not restarted (%d fields)", jf_visu_fields(&v));
    jf_visu_free(&v);
}

static double host_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void group_m(void) {   /* host timing; asserts skipped in a sanitized build */
    JfVisu v;
    int16_t s[256];
    tone(s, 256, 20, 12000);
    jf_visu_init(&v, 400, 240);
    jf_visu_set_scene(&v, JF_VISU_F_ZOOM, JF_VISU_W_SCOPE, JF_VISU_P_FIRE);
    uint32_t t = 0;
    for (int f = 0; f < 20; f++, t += 33) jf_visu_step(&v, s, 256, t);
    double t0 = host_ms();
    for (int f = 0; f < 200; f++, t += 33) jf_visu_step(&v, s, 256, t);
    double step_ms = (host_ms() - t0) / 200, worst = 0;
    for (int f = 0; f < JF_VISU_NFIELDS; f++) {
        t0 = host_ms();
        for (int r = 0; r < 20; r++) jf_visu_test_build_rows(&v, all_fields[f], 0, 30);
        double c = (host_ms() - t0) / 20;
        if (c > worst) worst = c;
    }
    printf("M: step %.3f ms (400x240), worst map chunk (30 rows) %.3f ms\n", step_ms, worst);
#ifndef JF_VISU_NO_TIMING_ASSERT
    CHECK(step_ms < 2.0, "M step %.3f ms >= 2 ms", step_ms);
    CHECK(worst < 2.0, "M map chunk %.3f ms >= 2 ms", worst);
#endif
    jf_visu_free(&v);
}

int main(void) {
    group_a(); group_b(); group_c(); group_d(); group_e(); group_f();
    group_g(); group_h(); group_i(); group_j(); group_k(); group_l(); group_n(); group_o(); group_m();
    printf("jf_visu_test: %s (%d failure(s))\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
