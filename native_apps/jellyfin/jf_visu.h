/* jf_visu.h - music-visualiser engine for Ludfin: a warp-feedback picture in
 * an 8-bit indexed image, no framebuffer and no device I/O (see jf_visu.c).
 * The struct is public so a caller can embed it; init allocates, free releases. */
#ifndef JF_VISU_H
#define JF_VISU_H

#include <stdbool.h>
#include <stdint.h>

/* A scene is one (field, wave, palette) triple; indices below. */
enum { JF_VISU_F_ZOOM, JF_VISU_F_TUNNEL, JF_VISU_F_SPIRAL, JF_VISU_F_RIPPLE, JF_VISU_F_KALEIDO, JF_VISU_NFIELDS };
enum { JF_VISU_W_SCOPE, JF_VISU_W_RING, JF_VISU_W_MIRROR, JF_VISU_W_BARS, JF_VISU_W_RADIAL, JF_VISU_NWAVES };
enum { JF_VISU_P_FIRE, JF_VISU_P_OCEAN, JF_VISU_P_NEON, JF_VISU_P_SUNSET, JF_VISU_P_RAINBOW, JF_VISU_NPALS };

typedef struct { int field, wave, pal; } JfVisuScene;

#define JF_VISU_TAB 384        /* radial table length: covers a 512x512 image (r <= 363) */

typedef struct {
    int w, h;                  /* image size, 16..512 each */
    uint8_t *img[2];           /* double buffer; img[cur] is the finished frame */
    int cur;
    uint32_t *off[2];          /* two warp maps: source index of the top-left pixel */
    uint32_t *wgt[2];          /* four packed 8-bit bilinear weights per pixel */
    uint16_t *rad, *ang;       /* per pixel, once at init: radius in 1/16 px, angle in 1/3072 turn */
    int act;                   /* map in use; 1-act is the one being built */
    bool building;             /* a field is under construction in 1-act */
    int row;                   /* next row to build */
    int rows_last;             /* rows built by the last step (test hook) */
    int fields;                /* fields swapped in after init (test hook) */
    bool have_time;
    uint32_t next_ms;          /* when the next scene build may start */
    uint32_t rng;
    JfVisuScene scene;         /* the scene on screen (its wave and palette) */
    JfVisuScene nsc;           /* the scene whose field map is being built (== scene between builds) */
    int rmaxi, inv_rmax;       /* largest radius in px, 2^24 / rmaxi */
    int bxo, byo, wedge, spin; /* the field being built: pan (16.16), kaleidoscope wedge angle and spin */
    int32_t tab_a[JF_VISU_TAB], tab_b[JF_VISU_TAB];   /* per-radius: C,S (rotate+scale) or scale,dtheta (kaleidoscope) */
    int16_t sin_tab[3072];     /* full period, amplitude 32767 */
    int16_t atan_tab[513];     /* atan(i/512) in 1/3072 turn */
    uint8_t sqrt_tab[256];     /* 255*sqrt(i/255) */
    uint32_t recip[513];       /* 65536 / n */
    int16_t tw_re[128], tw_im[128], win[256];   /* 256-point FFT twiddles and Hann window, Q15 */
    uint8_t bitrev[256];
    uint16_t spec[128];        /* last spectrum magnitudes (FFT of the latest 256 samples) */
    int bass_avg, beat_cd, flash, beats;
    uint32_t pal[256];         /* shown: 0x00RRGGBB */
    uint32_t pal_cur[256];     /* base palette (fade applied, flash not) */
    uint32_t pal_tgt[256];     /* the scene's family at the current hue */
    uint32_t pal_from[256];    /* palette when the crossfade began */
    bool pal_ok, fading;
    int hk, hfam;              /* hue step and family pal_tgt was built for */
    uint32_t fade_t0;
} JfVisu;

bool jf_visu_init(JfVisu *v, int w, int h);
void jf_visu_free(JfVisu *v);
/* ONE frame; mono may be NULL when n == 0. */
void jf_visu_step(JfVisu *v, const int16_t *mono, int n, uint32_t now_ms);
const uint8_t *jf_visu_image(const JfVisu *v);
const uint32_t *jf_visu_palette(const JfVisu *v);
int jf_visu_w(const JfVisu *v);
int jf_visu_h(const JfVisu *v);

/* pure pieces (host-tested) */
/* 256-point FFT magnitudes (bins 0..127) of the last <= 256 of n samples (zero padded, DC removed, Hann). */
void jf_visu_spectrum(const JfVisu *v, const int16_t *in, int n, uint16_t *mag128);
/* palette family fam at hue 0..3071 (a hue rotation of the stops); entry 0 is black. */
void jf_visu_pal_family(const JfVisu *v, int fam, int hue, uint32_t *out256);
/* per-channel (a*(256-t) + b*t) >> 8, t 0..256: t == 0 gives a, t == 256 gives b. */
void jf_visu_pal_mix(const uint32_t *a, const uint32_t *b, int t, uint32_t *out256);

/* test hooks */
int jf_visu_rows_built_last_step(const JfVisu *v);
int jf_visu_fields(const JfVisu *v);
int jf_visu_beats(const JfVisu *v);
void jf_visu_scene(const JfVisu *v, int *field, int *wave, int *pal);
/* which: 0 = active map, 1 = the one under construction */
void jf_visu_map_peek(const JfVisu *v, int which, const uint32_t **off, const uint32_t **wgt);
/* force a scene now: field map rebuilt whole into the active map, palette instant */
void jf_visu_set_scene(JfVisu *v, int field, int wave, int pal);
/* build rows [y0,y1) of the given field into the spare map (times one chunk) */
void jf_visu_test_build_rows(JfVisu *v, int field, int y0, int y1);

#endif
