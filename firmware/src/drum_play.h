/* SPDX-License-Identifier: GPL-3.0-only */
/* B3+DRUM: the rhythm section's player: CB-931's styles (drum_styles.h, made by tools/gen_styles.c) played into the drum
 * engine (drum_core.h), from the audio ISR. An integer port of CB-931's rhythm.c, the same behaviour:
 *
 * A style has six parts, as on an arranger keyboard: Intro, Main A, Fill into B, Main B, Fill into A, Outro, on a grid of
 * 12 ticks a beat (sixteenths and triplets exact). Commands: Start/Stop; Intro/Fill (one button: stopped, it starts with
 * the intro; playing, it plays the fill into the other variation at once, to the end of the bar, or in the last beat the
 * whole next bar); A and B (the variation changes at the next bar line, no fill); Outro (the ending at the next bar line,
 * then stop). After a fill, the next part opens with a cymbal. Swing moves the off-beat sixteenths later (50 % straight ..
 * 75 %). A new style while playing takes over at the next bar line.
 *
 * Hits start on the drum engine's 16-sample steps (0.36 ms): the player moves a step at a time, and the engine renders
 * the steps between hits in one go. The drums play after the Leslie, before the output equaliser and limiter
 * (organ_core.h: ORGAN_DRUMS). The main loop sets what to play (drp_style .. drp_level) and sends commands (drp_cmd()); the
 * ISR shows what it does (drp_running .. drp_beat). */
#ifndef DRUM_PLAY_H
#define DRUM_PLAY_H

#include "drum_core.h"

typedef struct { uint16_t tick; uint8_t slot, vel; } drs_hit_t;
typedef struct { uint8_t bars; int16_t n; const drs_hit_t *hits; } drs_part_t;
typedef struct { const char *name; uint8_t beats, swing; drs_part_t part[6]; } drs_style_t;
#include "drum_styles.h"

enum { DRP_INTRO = 0, DRP_MAIN_A, DRP_FILL_B, DRP_MAIN_B, DRP_FILL_A, DRP_OUTRO, DRP_PARTS };
enum { DRP_CMD_START_STOP = 0, DRP_CMD_INTRO_FILL, DRP_CMD_A, DRP_CMD_B, DRP_CMD_OUTRO };

/* the busy test bar (every kind of sound, hats choking): the last style, for measuring the worst case (TEST 1) */
static const drs_hit_t DRP_BUSY_HITS[] = {
    {0, DRM_BD, 127}, {0, DRM_CH, 100}, {3, DRM_CH, 80}, {6, DRM_OH, 110}, {9, DRM_CH, 90}, {12, DRM_SD, 127}, {12, DRM_CH, 100},
    {15, DRM_CB, 90}, {18, DRM_BD, 100}, {18, DRM_CH, 80}, {21, DRM_OH, 100}, {24, DRM_BD, 127}, {24, DRM_CY, 110}, {27, DRM_CL, 90},
    {30, DRM_LT, 110}, {30, DRM_MA, 90}, {33, DRM_HT, 110}, {36, DRM_SD, 127}, {36, DRM_TB, 100}, {39, DRM_EX, 100},
    {42, DRM_QJ, 100}, {42, DRM_CG, 90}, {45, DRM_S2, 110}, {45, DRM_RS, 100}, {45, DRM_HB, 90}, {45, DRM_LB, 90},
};
#define DRP_BUSY_N ((int16_t)(sizeof DRP_BUSY_HITS / sizeof DRP_BUSY_HITS[0]))
static const drs_style_t DRP_BUSY = { "TEST BUSY", 4, 50, {
    {1, DRP_BUSY_N, DRP_BUSY_HITS}, {1, DRP_BUSY_N, DRP_BUSY_HITS}, {1, DRP_BUSY_N, DRP_BUSY_HITS},
    {1, DRP_BUSY_N, DRP_BUSY_HITS}, {1, DRP_BUSY_N, DRP_BUSY_HITS}, {1, DRP_BUSY_N, DRP_BUSY_HITS} } };
#define DRP_NSTYLES (DRS_STYLES + 1u)
static const drs_style_t *drp_style_get(uint32_t i) { return i < DRS_STYLES ? &DRS_STYLE[i] : &DRP_BUSY; }

/* the drum level: index 0 off, 1..24 = -22.5 .. +12 dB in 1.5 dB steps (16 = CB-931's own). The drums are rendered at
 * their own level into drp_l / drp_r and scaled as they go into the organ's output, so the engine's arithmetic stays as
 * tested and a new level reaches the ringing voices too. Q11: the drums' Q15 sound (well under 2^17) x at most 8156 fits */
#define DRP_LEVELS 25u
#define DRP_LEVEL_0DB 16u
static const int16_t DRP_GAIN[DRP_LEVELS] = {
    0, 154, 183, 217, 258, 306, 364, 433, 514, 611, 727, 864, 1026, 1220, 1450, 1723, 2048, 2434, 2893, 3438, 4086,
    4857, 5772, 6860, 8153
};
static int32_t drp_l[DRM_MAXN], drp_r[DRM_MAXN];

/* set by the main loop */
static volatile uint32_t drp_style = 14u, drp_kit = 3u, drp_bpm = 120u, drp_swing = 50u, drp_level = 18u;   /* Rock 1 on the CR-78, +3 dB */
static volatile uint8_t drp_q[8];
static volatile uint32_t drp_qw, drp_qr;
static void drp_cmd(uint32_t c) { uint32_t w = drp_qw; if (w - drp_qr < 8u) { drp_q[w & 7u] = (uint8_t)c; drp_qw = w + 1u; } }
/* shown by the ISR */
static volatile uint32_t drp_running, drp_part = DRP_MAIN_A, drp_pending = 0xFFu, drp_beat, drp_beats = 4u, drp_shown_style = 14u;
static volatile uint32_t load_dr, drp_vmax;     /* peaks since the screen last looked: the drums' time (us), voices sounding */

/* ---- the player (ISR only) */
#define DRP_TQ 65536u                           /* a tick, Q16 */
#define DRP_TPB 12u
static struct {
    const drs_style_t *s;
    int32_t next;                               /* a style to take over at the next bar line, -1 none */
    uint32_t running, part, variation, stop_after, intro_armed;
    int32_t pending;                            /* a part waiting for the next bar line, -1 none */
    uint32_t pos, rem;                          /* ticks into the part (Q16); the tempo's remainder */
    uint32_t swing, want;                       /* want: the style the main loop last asked for */
    uint8_t qs[48], qv[48];                     /* hits found in the step being worked out */
    uint32_t qn;
} P_;

static uint32_t drp_bar(void) { return P_.s->beats * DRP_TPB * DRP_TQ; }
static uint32_t drp_part_len(uint32_t part) { return (P_.s->part[part].bars ? P_.s->part[part].bars : 1u) * drp_bar(); }
static void drp_hit(uint32_t slot, uint32_t vel) { if (P_.qn < 48u) { P_.qs[P_.qn] = (uint8_t)slot; P_.qv[P_.qn] = (uint8_t)vel; P_.qn++; } }
static uint32_t drp_swung(uint32_t tick)        /* the off-beat sixteenths (tick 3 of each eighth) later */
{
    uint32_t t = tick * DRP_TQ, sw = P_.swing > 75u ? 75u : P_.swing;
    if (tick % (DRP_TPB / 2u) == DRP_TPB / 4u && sw > 50u) t += (sw - 50u) * 393216u / 100u;   /* (sw - 50) % of 6 ticks */
    return t;
}
static void drp_fire(uint32_t from, uint32_t to)
{
    const drs_part_t *pt = &P_.s->part[P_.part];
    int32_t i;
    for (i = 0; i < pt->n; i++) {
        uint32_t t = drp_swung(pt->hits[i].tick);
        if (t >= from && t < to) drp_hit(pt->hits[i].slot, pt->hits[i].vel);
    }
}
static void drp_stop(void) { P_.running = 0; P_.pending = -1; P_.stop_after = 0; P_.pos = 0; }
static void drp_start(void)
{
    P_.running = 1;
    P_.part = P_.intro_armed ? DRP_INTRO : P_.variation;
    P_.intro_armed = 0;
    P_.pending = -1;
    P_.stop_after = P_.part == DRP_OUTRO;
    P_.pos = 0;
}
static void drp_fill_now(void)                  /* the fill at once, to the end of the bar; in its last beat, the whole next bar */
{
    uint32_t fill, bt, in_bar;
    if (P_.part != DRP_MAIN_A && P_.part != DRP_MAIN_B) return;
    if (P_.pending == DRP_OUTRO) return;
    fill = P_.part == DRP_MAIN_A ? DRP_FILL_B : DRP_FILL_A;
    bt = drp_bar();
    in_bar = P_.pos % bt;
    if (bt - in_bar < DRP_TPB * DRP_TQ) { P_.pending = (int32_t)fill; return; }
    P_.part = fill;
    P_.pos = in_bar;
    P_.pending = -1;
    P_.stop_after = 0;
}
static void drp_command(uint32_t c)
{
    switch (c) {
    case DRP_CMD_START_STOP: if (P_.running) drp_stop(); else drp_start(); break;
    case DRP_CMD_INTRO_FILL: if (!P_.running) { P_.intro_armed = 1; drp_start(); } else drp_fill_now(); break;
    case DRP_CMD_A: case DRP_CMD_B: {
        uint32_t v = c == DRP_CMD_A ? DRP_MAIN_A : DRP_MAIN_B;
        if (!P_.running) { P_.variation = v; break; }
        if (P_.pending == DRP_OUTRO) break;
        P_.pending = P_.part == v ? -1 : (int32_t)v;
        break; }
    case DRP_CMD_OUTRO: if (P_.running) P_.pending = DRP_OUTRO; break;
    }
}
static int32_t drp_default_next(void)
{
    switch (P_.part) {
    case DRP_INTRO: return (int32_t)P_.variation;
    case DRP_FILL_A: case DRP_MAIN_A: return DRP_MAIN_A;
    case DRP_FILL_B: case DRP_MAIN_B: return DRP_MAIN_B;
    default: return -1;                         /* the outro: stop */
    }
}
static void drp_enter(uint32_t next)
{
    uint32_t from_fill = P_.part == DRP_FILL_A || P_.part == DRP_FILL_B;
    if (P_.next >= 0) { P_.s = drp_style_get((uint32_t)P_.next); P_.next = -1; }
    P_.part = next;
    P_.pos = 0;
    P_.pending = -1;
    if (next == DRP_MAIN_A || next == DRP_MAIN_B) P_.variation = next;
    P_.stop_after = next == DRP_OUTRO;
    if (from_fill && next != DRP_OUTRO) drp_hit(DRM_CY, 120);   /* land the fill */
}
static void drp_advance(uint32_t ticks)          /* Q16 */
{
    int guard = 64;
    while (P_.running && ticks > 0u && guard-- > 0) {
        uint32_t bt = drp_bar(), pt = drp_part_len(P_.part), next_bar = (P_.pos / bt + 1u) * bt, step, is_main;
        if (next_bar > pt) next_bar = pt;
        step = next_bar - P_.pos;
        if (step > ticks) step = ticks;
        drp_fire(P_.pos, P_.pos + step);
        P_.pos += step;
        ticks -= step;
        if (P_.pos < next_bar) break;           /* mid-bar */
        is_main = P_.part == DRP_MAIN_A || P_.part == DRP_MAIN_B;
        if (P_.pos >= pt) {
            int32_t nx;
            if (P_.stop_after) { drp_stop(); break; }
            nx = P_.pending >= 0 ? P_.pending : drp_default_next();
            if (nx < 0) { drp_stop(); break; }
            drp_enter((uint32_t)nx);
        } else if (is_main && P_.pending >= 0) drp_enter((uint32_t)P_.pending);
        else if (P_.next >= 0 && is_main) { P_.s = drp_style_get((uint32_t)P_.next); P_.next = -1; P_.pos = 0; }
    }
}
static void drp_set_style(uint32_t i)            /* at the next bar line if running */
{
    const drs_style_t *s = drp_style_get(i);
    if (s == P_.s) { P_.next = -1; return; }
    if (P_.running) P_.next = (int32_t)i;
    else { P_.s = s; P_.next = -1; P_.pos = 0; }
}

/* the audio ISR, after the Leslie: adds the drums into l and r (Q15, n a multiple of 16); returns its time in ticks */
static uint32_t drum_block(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t t0 = fm1_ticks(), o, start = 0, num, inc, v, k;
    if (!P_.s) { P_.want = drp_style; P_.s = drp_style_get(P_.want); P_.next = -1; P_.part = P_.variation = DRP_MAIN_A; P_.pending = -1; }
    if (drp_style != P_.want) { P_.want = drp_style; drp_set_style(P_.want); }
    while (drp_qr != drp_qw) { drp_command(drp_q[drp_qr & 7u]); drp_qr++; }
    drm_set_kit(drp_kit);
    P_.swing = drp_swing;
    if (!P_.running && !drm_voices_on()) {      /* stopped and rung out: nothing to do */
        drp_running = 0; drp_pending = 0xFFu; drp_part = P_.variation;
        return 0u;
    }
    num = (drp_bpm < 30u ? 30u : drp_bpm > 300u ? 300u : drp_bpm) * 12582912u;   /* ticks a step: bpm x 12 x 16 / (60 x 44100), Q16 */
    if (n > DRM_MAXN) n = DRM_MAXN;
    for (o = 0; o < n; o++) drp_l[o] = drp_r[o] = 0;
    for (o = 0; o < n; o += DRM_SUB) {
        P_.qn = 0;
        if (P_.running) {
            inc = num / 2646000u;
            P_.rem += num % 2646000u;
            if (P_.rem >= 2646000u) { P_.rem -= 2646000u; inc++; }
            drp_advance(inc);
        }
        if (P_.qn) {                            /* hits in this step: render the steps before it, then start them */
            if (o > start) drm_render(drp_l + start, drp_r + start, o - start);
            for (k = 0; k < P_.qn; k++) drm_trigger(P_.qs[k], P_.qv[k]);
            start = o;
        }
    }
    if (n > start) drm_render(drp_l + start, drp_r + start, n - start);
    {                                           /* into the output at the drum level */
        int32_t g = DRP_GAIN[drp_level < DRP_LEVELS ? drp_level : DRP_LEVEL_0DB];
        for (o = 0; o < n; o++) { l[o] += (drp_l[o] * g) >> 11; r[o] += (drp_r[o] * g) >> 11; }
    }
    drp_running = P_.running;
    drp_part = P_.part;
    drp_pending = P_.pending >= 0 ? (uint32_t)P_.pending : 0xFFu;
    drp_beats = P_.s->beats;
    drp_beat = P_.pos / (DRP_TPB * DRP_TQ) % P_.s->beats;
    drp_shown_style = P_.s == &DRP_BUSY ? DRS_STYLES : (uint32_t)(P_.s - DRS_STYLE);
    v = drm_voices_on();
    if (v > drp_vmax) drp_vmax = v;
    return (uint32_t)(fm1_ticks() - t0);
}

#define ORGAN_DRUMS(l, r, n) drum_block(l, r, n)

#endif
