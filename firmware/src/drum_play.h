/* SPDX-License-Identifier: GPL-3.0-only */
/* B3+DRUM: the drums in the organ, for now a test rhythm to measure them on the FM-1 (screen 4, TEST):
 * PLAY starts and stops it, SELECT picks the kit, ALGORITHM the pattern (BEAT: an ordinary bar; BUSY: 26 hits a bar,
 * hats choking each other, every kind of sound, to find the worst case). 120 bpm.
 * The drums play after the Leslie and before the output equaliser and limiter (organ_core.h: ORGAN_DRUMS), so they
 * don't swirl and the limiter keeps the sum below full scale. Hits start on 16-sample steps (drum_core.h). */
#ifndef DRUM_PLAY_H
#define DRUM_PLAY_H

#include "drum_core.h"

static volatile uint32_t drp_on, drp_kit = 3u, drp_pat;   /* set by the main loop; kit 3: the CR-78 */
static volatile uint32_t load_dr, drp_vmax;     /* peaks since the screen last looked: the drums' time (us), voices sounding */
static uint32_t drp_run, drp_step, drp_left, drp_acc;

#define DRP_STEP_Q8 1411200u                    /* a sixteenth at 120 bpm: 44100 x 15 / 120 samples, Q8 */
#define DRP_NPAT 2u
static const char *const DRP_PAT_NAME[DRP_NPAT] = {"BEAT", "BUSY"};
typedef struct { uint8_t step, slot, vel; } drp_hit_t;
static const drp_hit_t DRP_BEAT[] = {
    {0, DRM_BD, 127}, {0, DRM_CH, 110}, {2, DRM_CH, 80}, {4, DRM_SD, 120}, {4, DRM_CH, 110}, {6, DRM_CH, 80},
    {8, DRM_BD, 127}, {8, DRM_CH, 110}, {10, DRM_BD, 100}, {10, DRM_CH, 80}, {12, DRM_SD, 120}, {12, DRM_CH, 110},
    {14, DRM_OH, 100}, {255, 0, 0}
};
static const drp_hit_t DRP_BUSY[] = {           /* tests/drum_test.c's busy bar */
    {0, DRM_BD, 127}, {0, DRM_CH, 100}, {1, DRM_CH, 80}, {2, DRM_OH, 110}, {3, DRM_CH, 90}, {4, DRM_SD, 127}, {4, DRM_CH, 100},
    {5, DRM_CB, 90}, {6, DRM_BD, 100}, {6, DRM_CH, 80}, {7, DRM_OH, 100}, {8, DRM_BD, 127}, {8, DRM_CY, 110}, {9, DRM_CL, 90},
    {10, DRM_LT, 110}, {10, DRM_MA, 90}, {11, DRM_HT, 110}, {12, DRM_SD, 127}, {12, DRM_TB, 100}, {13, DRM_EX, 100},
    {14, DRM_QJ, 100}, {14, DRM_CG, 90}, {15, DRM_S2, 110}, {15, DRM_RS, 100}, {15, DRM_HB, 90}, {15, DRM_LB, 90},
    {255, 0, 0}
};

static void drp_hits(uint32_t step)
{
    const drp_hit_t *h = drp_pat ? DRP_BUSY : DRP_BEAT;
    for (; h->step != 255u; h++) if (h->step == step) drm_trigger(h->slot, h->vel);
}

/* the audio ISR, after the Leslie: adds the drums into l and r (Q15, n a multiple of 16); returns its time in ticks */
static uint32_t drum_block(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t t0 = fm1_ticks(), o = 0, v;
    if (drp_on != drp_run) {
        drp_run = drp_on;
        drp_step = 0; drp_left = 0; drp_acc = 0;
    }
    if (!drp_run && !drm_voices_on()) return 0u;   /* stopped and rung out: nothing to do */
    while (o < n) {
        uint32_t m = n - o;
        if (drp_run && drp_left == 0u) {        /* the next sixteenth: its hits, and how long until the one after */
            drm_set_kit(drp_kit);
            drp_hits(drp_step);
            drp_step = (drp_step + 1u) & 15u;
            drp_acc += DRP_STEP_Q8;
            drp_left = (drp_acc >> 8) & ~(uint32_t)(DRM_SUB - 1);
            drp_acc -= drp_left << 8;
        }
        if (drp_run && drp_left < m) m = drp_left;
        drm_render(l + o, r + o, m);
        o += m;
        if (drp_run) drp_left -= m;
    }
    v = drm_voices_on();
    if (v > drp_vmax) drp_vmax = v;
    return (uint32_t)(fm1_ticks() - t0);
}

#define ORGAN_DRUMS(l, r, n) drum_block(l, r, n)

#endif
