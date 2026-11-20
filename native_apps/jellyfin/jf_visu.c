/* jf_visu.c - warp-feedback music visualiser engine (pure: libc + libm at init).
 *
 * Each frame the previous picture is resampled through a precomputed warp map
 * with bilinear weights summing to <= 252/256, so the picture decays by itself;
 * a wave (scope, ring, mirrored scope, spectrum bars, radial spectrum) is then
 * drawn at 255 and a 256-entry palette colours it. A scene is (field, wave,
 * palette) and changes every 15-19 s, never repeating on any of the three axes.
 *
 * Fields (all built into the spare map, 1/8 of the rows per frame, int32 maths
 * and table lookups only, no libm, sqrt, atan or divide per pixel):
 *   ZOOM zoom+rotate+swirl, TUNNEL a ring that attracts or repels, SPIRAL a
 *   rotation growing with radius, RIPPLE a radial scale wave, KALEIDO 6 mirrored
 *   sectors (angle folded inside the map).
 * Per pixel radius and angle are tabulated once at init (sqrtf and an atan
 * table; init-time only). Per radius a table gives the scale and rotation.
 *
 * Costs: the warp is the only per-pixel pass over the image; waves draw at most
 * a few hundred pixels, the 256-point FFT is ~1000 butterflies (tests/jf_visu_test.c
 * prints host timings; the device is ~30x slower than the host).
 *
 * Empty audio (n == 0) draws no wave at all: the warp keeps running and the
 * picture fades out. */
#include "jf_visu.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CHUNKS 8                /* map rows are built over this many frames */
#define DWELL_MIN_MS 15000u
#define DECAY_SUM 252           /* weights sum to ~this of 256 */
#define ANG 3072                /* angle units per turn */
#define FADE_MS_X128 131        /* 256/2000 ms in 1/1024 */
#define BEAT_FLOOR 300

static uint32_t rnd(JfVisu *v) {
    v->rng = v->rng * 1664525u + 1013904223u;
    return v->rng >> 16;
}

static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static int wrapa(int a) { a %= ANG; return a < 0 ? a + ANG : a; }   /* constant modulus: no divide */
static int sin_at(const JfVisu *v, int a) { return v->sin_tab[wrapa(a)]; }
static int cos_at(const JfVisu *v, int a) { return v->sin_tab[wrapa(a + ANG / 4)]; }

/* ---- scene ---- */

static JfVisuScene scene_pick(JfVisu *v) {
    JfVisuScene s;
    s.field = (v->scene.field + 1 + (int)(rnd(v) % (JF_VISU_NFIELDS - 1))) % JF_VISU_NFIELDS;
    s.wave = (v->scene.wave + 1 + (int)(rnd(v) % (JF_VISU_NWAVES - 1))) % JF_VISU_NWAVES;
    s.pal = (v->scene.pal + 1 + (int)(rnd(v) % (JF_VISU_NPALS - 1))) % JF_VISU_NPALS;
    return s;
}

/* ---- fields ---- */

/* Fill the per-radius tables for a field. Divides here (<= 364 per pick) are fine. */
static void field_setup(JfVisu *v, int field) {
    const int rm = v->rmaxi;
    int zoom = 62800 + (int)(rnd(v) & 4095);            /* 0.958 .. 1.021 */
    int zr = (int)(rnd(v) & 4095) - 2048;
    int rot = (int)(rnd(v) & 31) - 16, swl = (int)(rnd(v) & 31) - 16;   /* units of 1/3072 turn */
    int sgn = (rnd(v) & 1) ? 1 : -1;
    int ox = (((int)(rnd(v) & 255) - 128) * v->w) >> 11, oy = (((int)(rnd(v) & 255) - 128) * v->h) >> 11;
    int r0 = 0, wd = 1, amp = 0, k = 0, ph = 0, sp = 0;
    v->spin = 0; v->wedge = 0;
    if (field == JF_VISU_F_TUNNEL) {
        zoom = 64600 + (int)(rnd(v) & 1023); amp = sgn * (1400 + (int)(rnd(v) & 1023));
        r0 = (rm * (64 + (int)(rnd(v) & 127))) >> 8; wd = 12 + (int)(rnd(v) & 15); rot = sgn * (int)(rnd(v) & 7);
    } else if (field == JF_VISU_F_SPIRAL) {
        zoom = 63300 + (int)(rnd(v) & 1023); sp = sgn * (16 + (int)(rnd(v) % 24));
    } else if (field == JF_VISU_F_RIPPLE) {
        int lam = 28 + (int)(rnd(v) & 31);
        zoom = 63900 + (int)(rnd(v) & 1023); amp = 900 + (int)(rnd(v) & 1023);
        k = ANG / lam; ph = (int)(rnd(v) % ANG);
    } else if (field == JF_VISU_F_KALEIDO) {
        zoom = 63500 + (int)(rnd(v) & 1023); v->wedge = (int)(rnd(v) % ANG); v->spin = (int)(rnd(v) % ANG);
        ox = oy = 0;
    }
    v->bxo = ox * (65536 - zoom); v->byo = oy * (65536 - zoom);
    for (int ri = 0; ri <= rm; ri++) {
        int t = (int)(((int64_t)ri * v->inv_rmax) >> 8);               /* 0..65536 */
        int t2 = (int)(((int64_t)t * t) >> 16);
        int S = zoom, th = 0;
        switch (field) {
        case JF_VISU_F_ZOOM:   S = zoom + ((zr * t2) >> 16); th = rot + ((swl * t2) >> 16); break;
        case JF_VISU_F_TUNNEL: { int d = ri - r0, ad = d < 0 ? -d : d; S = zoom + amp * d / (ad + wd); th = rot; break; }
        case JF_VISU_F_SPIRAL: th = (sp * t) >> 16; break;
        case JF_VISU_F_RIPPLE: S = zoom + ((amp * sin_at(v, ri * k + ph)) >> 15); th = (swl * t2) >> 17; break;
        default:               S = zoom + ((zr * t2) >> 17); th = (swl * t) >> 16; break;
        }
        if (field == JF_VISU_F_KALEIDO) { v->tab_a[ri] = S; v->tab_b[ri] = th; }
        else {
            v->tab_a[ri] = (int32_t)(((int64_t)cos_at(v, th) * S) >> 15);
            v->tab_b[ri] = (int32_t)(((int64_t)sin_at(v, th) * S) >> 15);
        }
    }
}

static inline void emit(uint32_t *off, uint32_t *wgt, uint32_t i, int sx, int sy, uint32_t w) {
    int ix = sx >> 16, iy = sy >> 16;       /* <= w-2, h-2 by the caller's clamp */
    int fx = (sx >> 8) & 255, fy = (sy >> 8) & 255;
    int w00 = (((256 - fx) * (256 - fy)) >> 8) * DECAY_SUM >> 8;
    int w01 = ((fx * (256 - fy)) >> 8) * DECAY_SUM >> 8;
    int w10 = (((256 - fx) * fy) >> 8) * DECAY_SUM >> 8;
    int w11 = ((fx * fy) >> 8) * DECAY_SUM >> 8;
    off[i] = (uint32_t)iy * w + (uint32_t)ix;
    wgt[i] = (uint32_t)w00 | (uint32_t)w01 << 8 | (uint32_t)w10 << 16 | (uint32_t)w11 << 24;
}

/* Build rows [y0,y1) of map m for the field v->nsc.field: source = centre + rotate/scale by radius. */
static void map_rows(JfVisu *v, int m, int y0, int y1) {
    const int w = v->w, h = v->h, cx = w / 2, cy = h / 2;
    const int maxx = ((w - 1) << 16) - 1, maxy = ((h - 1) << 16) - 1;
    const int bx = (cx << 16) + v->bxo, by = (cy << 16) + v->byo;
    uint32_t *off = v->off[m], *wgt = v->wgt[m];
    const int32_t *ta = v->tab_a, *tb = v->tab_b;
    if (v->nsc.field != JF_VISU_F_KALEIDO) {
        for (int y = y0; y < y1; y++) {
            int dy = y - cy;
            uint32_t i = (uint32_t)y * (uint32_t)w;
            for (int x = 0; x < w; x++, i++) {
                int ri = v->rad[i] >> 4, dx = x - cx, c = ta[ri], s = tb[ri];
                int sx = clampi(bx + dx * c - dy * s, 0, maxx);
                int sy = clampi(by + dx * s + dy * c, 0, maxy);
                emit(off, wgt, i, sx, sy, (uint32_t)w);
            }
        }
    } else {
        for (int y = y0; y < y1; y++) {
            uint32_t i = (uint32_t)y * (uint32_t)w;
            for (int x = 0; x < w; x++, i++) {
                int ri = v->rad[i] >> 4;
                int a = v->ang[i] + v->spin;
                if (a >= ANG) a -= ANG;
                int t = a & 511;                          /* 6 sectors of 512, alternate ones mirrored */
                if ((a >> 9) & 1) t = 511 - t;
                int idx = t + (int)tb[ri] + v->wedge;
                if (idx >= ANG) idx -= ANG;
                if (idx < 0) idx += ANG;
                int ci = idx + ANG / 4;
                if (ci >= ANG) ci -= ANG;
                int R = (int)(((uint32_t)v->rad[i] * (uint32_t)ta[ri]) >> 4);   /* 16.16 px */
                int sx = clampi(bx + (int)(((int64_t)R * v->sin_tab[ci]) >> 15), 0, maxx);
                int sy = clampi(by + (int)(((int64_t)R * v->sin_tab[idx]) >> 15), 0, maxy);
                emit(off, wgt, i, sx, sy, (uint32_t)w);
            }
        }
    }
    (void)h;
}

static void warp(const JfVisu *v, const uint8_t *src, uint8_t *dst) {
    const uint32_t *off = v->off[v->act], *wgt = v->wgt[v->act];
    const int w = v->w, n = v->w * v->h;
    for (int i = 0; i < n; i++) {
        const uint8_t *s = src + off[i];
        uint32_t k = wgt[i];
        dst[i] = (uint8_t)((s[0] * (k & 255) + s[1] * ((k >> 8) & 255) +
                            s[w] * ((k >> 16) & 255) + s[w + 1] * (k >> 24)) >> 8);
    }
}

/* ---- FFT (256 points, radix 2, Q15 twiddles, >>1 per stage: no overflow, no divide) ---- */

void jf_visu_spectrum(const JfVisu *v, const int16_t *in, int n, uint16_t *mag) {
    int32_t re[256], im[256];
    int m = n < 256 ? (n < 0 ? 0 : n) : 256, mean = 0;
    const int16_t *s = in + (n - m);
    for (int k = 0; k < m; k++) mean += s[k];
    mean = m ? (mean >> 8) : 0;                         /* sum / 256: the zero padding counts as 0 */
    if (m < 256 && m) mean = 0;                         /* a partial block keeps its offset: zero pad has none to remove */
    for (int k = 0; k < 256; k++) {
        int x = k < m ? s[k] - mean : 0;
        re[v->bitrev[k]] = (x * v->win[k]) >> 15;
        im[k] = 0;
    }
    for (int st = 0; st < 8; st++) {
        int half = 1 << st, stride = 128 >> st;
        for (int i = 0; i < 256; i += half << 1) {
            for (int j = 0; j < half; j++) {
                int wr = v->tw_re[j * stride], wi = v->tw_im[j * stride], a = i + j, b = a + half;
                int tr = (re[b] * wr - im[b] * wi) >> 15, ti = (re[b] * wi + im[b] * wr) >> 15;
                re[b] = (re[a] - tr) >> 1; im[b] = (im[a] - ti) >> 1;
                re[a] = (re[a] + tr) >> 1; im[a] = (im[a] + ti) >> 1;
            }
        }
    }
    for (int k = 0; k < 128; k++) {
        int ar = re[k] < 0 ? -re[k] : re[k], ai = im[k] < 0 ? -im[k] : im[k];
        int mx = ar > ai ? ar : ai, mn = ar > ai ? ai : ar;
        mag[k] = (uint16_t)(mx + ((mn * 3) >> 3));      /* alpha-max beta-min */
    }
}

/* ---- waves ---- */

static inline void put(const JfVisu *v, uint8_t *dst, int x, int y) {
    if ((unsigned)x < (unsigned)v->w && (unsigned)y < (unsigned)v->h) dst[y * v->w + x] = 255;
}
static void put_t(const JfVisu *v, uint8_t *dst, int x, int y, int t) {
    for (int j = 0; j < t; j++) for (int i = 0; i < t; i++) put(v, dst, x + i, y + j);   /* right/down only */
}
static void line(const JfVisu *v, uint8_t *dst, int x0, int y0, int x1, int y1, int t) {
    int dx = x1 - x0, dy = y1 - y0, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, n = ax > ay ? ax : ay;
    if (n == 0) { put_t(v, dst, x0, y0, t); return; }
    if (n > 512) n = 512;
    int r = (int)v->recip[n], sx = (x0 << 16) + 32768, sy = (y0 << 16) + 32768, ex = dx * r, ey = dy * r;
    for (int k = 0; k <= n; k++, sx += ex, sy += ey) put_t(v, dst, sx >> 16, sy >> 16, t);
}

static void draw_scope(const JfVisu *v, uint8_t *dst, const int16_t *s, int m, int cy, int amp, bool mirror, int t) {
    const int w = v->w, h = v->h;
    int px = 0, py = 0, qy = 0;
    for (int k = 0; k < m; k++) {
        int x = w - m + k, y = clampi(cy + ((s[k] * amp) >> 15), 0, h - 1);
        int y2 = clampi(2 * (h / 2) - y, 0, h - 1);
        if (k) { line(v, dst, px, py, x, y, t); if (mirror) line(v, dst, px, qy, x, y2, t); }
        else { put_t(v, dst, x, y, t); if (mirror) put_t(v, dst, x, y2, t); }
        px = x; py = y; qy = y2;
    }
}

static void polar(const JfVisu *v, int idx, int r, int *x, int *y) {
    *x = v->w / 2 + ((r * cos_at(v, idx)) >> 15);
    *y = v->h / 2 + ((r * sin_at(v, idx)) >> 15);
}

static int level(const JfVisu *v, int i) {            /* spectrum bin 1+i as 0..255, compressed */
    int m = ((int)v->spec[1 + i] * (8 + (i >> 2))) >> 3;
    m >>= 5;
    return v->sqrt_tab[m > 255 ? 255 : m];
}

static void draw_wave(JfVisu *v, uint8_t *dst, const int16_t *mono, int n) {
    const int w = v->w, h = v->h, mn = w < h ? w : h, t = v->flash > 128 ? 3 : 2;
    int m = n < w ? n : w;
    const int16_t *s = mono + (n - m);
    int px[128], py[128];
    switch (v->scene.wave) {
    case JF_VISU_W_SCOPE:  draw_scope(v, dst, s, m, h / 2, h / 3, false, t); break;
    case JF_VISU_W_MIRROR: draw_scope(v, dst, s, m, h / 3, h / 6, true, t); break;
    case JF_VISU_W_RING: {
        int m2 = n < 512 ? n : 512;
        const int16_t *s2 = mono + (n - m2);
        for (int k = 0; k < 128; k++) polar(v, k * 24, (mn >> 2) + ((s2[(k * m2) >> 7] * (mn / 6)) >> 15), &px[k], &py[k]);
        for (int k = 0; k < 128; k++) line(v, dst, px[(k + 127) & 127], py[(k + 127) & 127], px[k], py[k], t);
        break;
    }
    case JF_VISU_W_BARS: {
        int bw = w >> 6, cy = h / 2, hh = h / 3;
        if (bw < 1) bw = 1;
        int x0 = (w - 64 * bw) >> 1, bwd = bw > 1 ? bw - 1 : 1;
        for (int i = 0; i < 64; i++) {
            int len = (level(v, i) * hh) >> 8;
            if (!len) continue;
            for (int xx = 0; xx < bwd; xx++) { put_t(v, dst, x0 + i * bw + xx, cy - len, t); put_t(v, dst, x0 + i * bw + xx, cy + len, t); }
        }
        break;
    }
    default: {
        int rin = mn / 6, lmax = mn / 4;
        for (int k = 0; k < 96; k++) polar(v, k * 32, rin + ((level(v, k < 48 ? k : 95 - k) * lmax) >> 8), &px[k], &py[k]);
        for (int k = 0; k < 96; k++) line(v, dst, px[(k + 95) % 96], py[(k + 95) % 96], px[k], py[k], t);
        break;
    }
    }
}

/* ---- colour ---- */

/* five stops at 0,64,128,192,255, each a different hue; entry 0 black. RAINBOW is a banded wheel (3 bands). */
static const uint8_t stops[4][5][3] = {
    {{0,0,0},{20,0,150},{200,0,120},{255,140,0},{255,255,190}},     /* fire: black, blue, magenta, orange, white-yellow */
    {{0,0,0},{0,40,170},{0,170,170},{90,220,50},{240,255,200}},     /* ocean: blue, teal, green, pale */
    {{0,0,0},{120,0,210},{255,0,140},{0,230,255},{255,255,255}},    /* neon: violet, pink, cyan, white */
    {{0,0,0},{60,0,120},{230,40,60},{255,170,0},{255,245,210}},     /* sunset: purple, red, amber, cream */
};

void jf_visu_pal_family(const JfVisu *v, int fam, int hue, uint32_t *out) {
    if (fam == JF_VISU_P_RAINBOW) {                  /* hue = cycle shift: colours flow along the index */
        int sh = wrapa(hue) >> 2;                    /* 0..767 of 768 */
        out[0] = 0;
        for (int i = 1; i < 256; i++) {
            int hp = (i * 3 + sh) % 768, seg = hp >> 7, f = (hp & 127) << 1;   /* seg 0..5, f 0..254 */
            int bri = 70 + ((i * 185) >> 8), q = 255 - f, r, g, b;
            switch (seg) {
            case 0: r = 255; g = f;   b = 0;   break;
            case 1: r = q;   g = 255; b = 0;   break;
            case 2: r = 0;   g = 255; b = f;   break;
            case 3: r = 0;   g = q;   b = 255; break;
            case 4: r = f;   g = 0;   b = 255; break;
            default: r = 255; g = 0;  b = q;   break;
            }
            out[i] = (uint32_t)((r * bri) >> 8) << 16 | (uint32_t)((g * bri) >> 8) << 8 | (uint32_t)((b * bri) >> 8);
        }
        return;
    }
    int c = (v->sin_tab[wrapa(hue + ANG / 4)] + 16) >> 5, s = (v->sin_tab[wrapa(hue)] + 16) >> 5;   /* +-1024 */
    int m[9] = {
        218 + ((806 * c) >> 10) - ((218 * s) >> 10), 732 - ((732 * c) >> 10) - ((732 * s) >> 10), 74 - ((74 * c) >> 10) + ((950 * s) >> 10),
        218 - ((218 * c) >> 10) + ((146 * s) >> 10), 732 + ((292 * c) >> 10) + ((143 * s) >> 10), 74 - ((74 * c) >> 10) - ((290 * s) >> 10),
        218 - ((218 * c) >> 10) - ((806 * s) >> 10), 732 - ((732 * c) >> 10) + ((732 * s) >> 10), 74 + ((950 * c) >> 10) + ((74 * s) >> 10) };
    int st[5][3];
    for (int k = 0; k < 5; k++) {
        const uint8_t *p = stops[fam & 3][k];
        for (int j = 0; j < 3; j++) st[k][j] = clampi((p[0] * m[j * 3] + p[1] * m[j * 3 + 1] + p[2] * m[j * 3 + 2]) >> 10, 0, 255);
    }
    for (int i = 0; i < 256; i++) {
        int seg = i >> 6, t = (i & 63) << 2, ch[3];
        if (i == 255) { seg = 3; t = 256; }
        for (int j = 0; j < 3; j++) ch[j] = st[seg][j] + (((st[seg + 1][j] - st[seg][j]) * t) >> 8);
        out[i] = (uint32_t)ch[0] << 16 | (uint32_t)ch[1] << 8 | (uint32_t)ch[2];
    }
}

void jf_visu_pal_mix(const uint32_t *a, const uint32_t *b, int t, uint32_t *out) {
    for (int i = 0; i < 256; i++) {
        uint32_t r = 0;
        for (int sh = 0; sh < 24; sh += 8) r |= ((((a[i] >> sh) & 255) * (uint32_t)(256 - t) + ((b[i] >> sh) & 255) * (uint32_t)t) >> 8) << sh;
        out[i] = r;
    }
}

/* Hue drifts slowly (a full turn in ~3 min; the banded family cycles a full turn in ~49 s). */
static void palette_update(JfVisu *v, uint32_t now_ms, bool flash_was) {
    int fam = v->scene.pal;
    int phase = (int)((now_ms >> (fam == JF_VISU_P_RAINBOW ? 4 : 6)) % ANG), hk = phase >> 3;
    bool dirty = false;
    if (!v->pal_ok || hk != v->hk || fam != v->hfam) {
        jf_visu_pal_family(v, fam, hk << 3, v->pal_tgt);
        v->hk = hk; v->hfam = fam; dirty = true;
    }
    if (v->fading) {
        uint32_t dt = now_ms - v->fade_t0;
        int t = dt > 4000u ? 256 : (int)((dt * FADE_MS_X128) >> 10);
        if (t >= 256) { v->fading = false; memcpy(v->pal_cur, v->pal_tgt, sizeof v->pal_cur); }
        else jf_visu_pal_mix(v->pal_from, v->pal_tgt, t, v->pal_cur);
        dirty = true;
    } else if (dirty || !v->pal_ok) {
        memcpy(v->pal_cur, v->pal_tgt, sizeof v->pal_cur);
    }
    if (dirty || !v->pal_ok || v->flash > 0 || flash_was) {
        int f = v->flash;                              /* a beat lifts every colour toward white */
        for (int i = 0; i < 256; i++) {
            uint32_t c = v->pal_cur[i], o = 0;
            for (int sh = 0; sh < 24; sh += 8) { int x = (int)((c >> sh) & 255); o |= (uint32_t)(x + (((255 - x) * f) >> 9)) << sh; }
            v->pal[i] = o;
        }
    }
    v->pal_ok = true;
}

/* ---- engine ---- */

static int angle_of(const JfVisu *v, int dx, int dy) {
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, q;
    if (!ax && !ay) return 0;
    q = ax >= ay ? v->atan_tab[(ay << 9) / ax] : ANG / 4 - v->atan_tab[(ax << 9) / ay];   /* 0..768 */
    if (dx >= 0) return dy >= 0 ? q : (q ? ANG - q : 0);
    return dy >= 0 ? ANG / 2 - q : ANG / 2 + q;
}

bool jf_visu_init(JfVisu *v, int w, int h) {
    memset(v, 0, sizeof *v);
    if (w < 16 || w > 512 || h < 16 || h > 512) return false;
    size_t px = (size_t)w * (size_t)h;
    v->w = w; v->h = h;
    v->rad = calloc(px, sizeof(uint16_t));
    v->ang = calloc(px, sizeof(uint16_t));
    if (!v->rad || !v->ang) { jf_visu_free(v); return false; }
    for (int i = 0; i < 2; i++) {
        v->img[i] = calloc(px, 1);
        v->off[i] = calloc(px, sizeof(uint32_t));
        v->wgt[i] = calloc(px, sizeof(uint32_t));
        if (!v->img[i] || !v->off[i] || !v->wgt[i]) { jf_visu_free(v); return false; }
    }
    v->rng = 0x2545F491u;
    for (int i = 0; i < ANG; i++) v->sin_tab[i] = (int16_t)(sinf((float)i * (6.2831853f / (float)ANG)) * 32767.0f);
    for (int i = 0; i < 256; i++) v->sqrt_tab[i] = (uint8_t)(255.0f * sqrtf((float)i / 255.0f) + 0.5f);
    for (int i = 0; i <= 512; i++) {
        v->atan_tab[i] = (int16_t)(atanf((float)i / 512.0f) * ((float)ANG / 6.2831853f) + 0.5f);
        v->recip[i] = i ? 65536u / (uint32_t)i : 0;
    }
    for (int k = 0; k < 128; k++) {                    /* e^(-2 pi i k/256): step 12 units of 3072 */
        v->tw_re[k] = v->sin_tab[wrapa(k * 12 + ANG / 4)];
        v->tw_im[k] = (int16_t)-v->sin_tab[k * 12];
    }
    for (int k = 0; k < 256; k++) {
        v->win[k] = (int16_t)(16384 - (v->sin_tab[wrapa(k * 12 + ANG / 4)] >> 1));
        int r = 0;
        for (int b = 0; b < 8; b++) if (k & (1 << b)) r |= 1 << (7 - b);
        v->bitrev[k] = (uint8_t)r;
    }
    int cx = w / 2, cy = h / 2, dxm = cx > w - 1 - cx ? cx : w - 1 - cx, dym = cy > h - 1 - cy ? cy : h - 1 - cy;
    v->rmaxi = (int)sqrtf((float)(dxm * dxm + dym * dym)) + 1;
    v->inv_rmax = (1 << 24) / v->rmaxi;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int dx = x - cx, dy = y - cy;
        v->rad[y * w + x] = (uint16_t)(sqrtf((float)(dx * dx + dy * dy)) * 16.0f + 0.5f);
        v->ang[y * w + x] = (uint16_t)angle_of(v, dx, dy);
    }
    v->scene.field = (int)(rnd(v) % JF_VISU_NFIELDS); v->scene.wave = (int)(rnd(v) % JF_VISU_NWAVES); v->scene.pal = (int)(rnd(v) % JF_VISU_NPALS);
    jf_visu_set_scene(v, v->scene.field, v->scene.wave, v->scene.pal);
    return true;
}

void jf_visu_set_scene(JfVisu *v, int field, int wave, int pal) {
    v->scene.field = v->nsc.field = (int)((unsigned)field % JF_VISU_NFIELDS);
    v->scene.wave = v->nsc.wave = (int)((unsigned)wave % JF_VISU_NWAVES);
    v->scene.pal = v->nsc.pal = (int)((unsigned)pal % JF_VISU_NPALS);
    field_setup(v, v->nsc.field);
    map_rows(v, v->act, 0, v->h);                      /* frame 1 needs a map */
    v->building = false; v->fading = false; v->pal_ok = false; v->flash = 0;
    palette_update(v, 0, false);
}

void jf_visu_free(JfVisu *v) {
    for (int i = 0; i < 2; i++) {
        free(v->img[i]); free(v->off[i]); free(v->wgt[i]);
        v->img[i] = NULL; v->off[i] = v->wgt[i] = NULL;
    }
    free(v->rad); free(v->ang);
    v->rad = v->ang = NULL;
}

void jf_visu_step(JfVisu *v, const int16_t *mono, int n, uint32_t now_ms) {
    /* Deadlines compare as a signed difference, so a wrapped uint32 clock works. */
    if (!v->have_time) {
        v->have_time = true;
        v->next_ms = now_ms + DWELL_MIN_MS + (rnd(v) & 4095);
    }
    v->rows_last = 0;
    if (!v->building && (int32_t)(now_ms - v->next_ms) >= 0) {
        v->nsc = scene_pick(v);
        field_setup(v, v->nsc.field);
        v->building = true; v->row = 0;
    }
    if (v->building) {
        int per = (v->h + CHUNKS - 1) / CHUNKS, y1 = v->row + per;
        if (y1 > v->h) y1 = v->h;
        map_rows(v, 1 - v->act, v->row, y1);
        v->rows_last = y1 - v->row;
        v->row = y1;
        if (y1 >= v->h) {
            v->act = 1 - v->act; v->building = false; v->fields++;
            v->scene = v->nsc;
            memcpy(v->pal_from, v->pal_cur, sizeof v->pal_from);   /* the palette crossfades over ~2 s */
            v->fading = true; v->fade_t0 = now_ms;
            v->next_ms = now_ms + DWELL_MIN_MS + (rnd(v) & 4095);
        }
    }
    uint8_t *dst = v->img[1 - v->cur];
    warp(v, v->img[v->cur], dst);
    bool flash_was = v->flash > 0;
    if (v->flash > 0) v->flash = v->flash > 24 ? v->flash - 24 : 0;
    if (v->beat_cd > 0) v->beat_cd--;
    int bass = 0;
    if (n > 0 && mono) {
        jf_visu_spectrum(v, mono, n, v->spec);
        bass = v->spec[1] + v->spec[2] + v->spec[3] + v->spec[4];
        if (v->bass_avg == 0 && bass > 0) v->bass_avg = bass;   /* seed from the first non-silent frame: a steady tone is not a beat */
        if (v->beat_cd == 0 && bass > BEAT_FLOOR && bass * 2 > v->bass_avg * 3) { v->flash = 255; v->beat_cd = 8; v->beats++; flash_was = true; }
        draw_wave(v, dst, mono, n);
    }
    v->bass_avg += (bass - v->bass_avg) >> 4;
    palette_update(v, now_ms, flash_was);
    v->cur = 1 - v->cur;
}

const uint8_t *jf_visu_image(const JfVisu *v) { return v->img[v->cur]; }
const uint32_t *jf_visu_palette(const JfVisu *v) { return v->pal; }
int jf_visu_w(const JfVisu *v) { return v->w; }
int jf_visu_h(const JfVisu *v) { return v->h; }
int jf_visu_rows_built_last_step(const JfVisu *v) { return v->rows_last; }
int jf_visu_fields(const JfVisu *v) { return v->fields; }
int jf_visu_beats(const JfVisu *v) { return v->beats; }
void jf_visu_scene(const JfVisu *v, int *field, int *wave, int *pal) { *field = v->scene.field; *wave = v->scene.wave; *pal = v->scene.pal; }
void jf_visu_map_peek(const JfVisu *v, int which, const uint32_t **off, const uint32_t **wgt) {
    int m = which ? 1 - v->act : v->act;
    *off = v->off[m]; *wgt = v->wgt[m];
}
void jf_visu_test_build_rows(JfVisu *v, int field, int y0, int y1) {
    v->nsc.field = field;
    field_setup(v, field);
    map_rows(v, 1 - v->act, y0, y1);
}
