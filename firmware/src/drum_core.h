/* SPDX-License-Identifier: GPL-3.0-only */
/* B3+DRUM: the rhythm section's sound engine, the Cheesebox 931's drums (tools/cb931/drums.c) in integers for the FM-1.
 *
 * Seven kits of synthesised drums (CT-403, Mini Pops, TR-606, CR-78, MT-40, MT-18, VL-1), 18 slots each (DRM_BD ..),
 * no samples. A sound is up to four parts, as in CB-931: a tone (sine or square, second tone, pitch drop, attack,
 * click, low-pass), filtered noise, metal (TR-606 six squares or CR-78 three, band-passed), and a rasp (a pulse train
 * ringing a resonator: guiro, tambourine, quijada). Noise and metal share an envelope with hold, tail and choke.
 *
 * Every coefficient is worked out on the computer from CB-931's float values (tools/gen_drums.c -> drum_tab.h), so
 * this file has no float, no 64-bit division and no maths library: the FM-1's rules (dinghy.c).
 *
 * Made to be cheap (an exact port, commit 39da472, cost an estimated 145 to 345 us a voice a block on the FM-1):
 *  - sound is made in steps of 16 samples (DRM_SUB): envelopes, the attack and the pitch drop move once a step and
 *    are drawn straight between steps; drm_render() takes a multiple of 16 frames, so hits start on a step (0.36 ms);
 *  - each part runs over the step in its own loop, into the voice's buffer; then the voice is panned in;
 *  - the tone, the noise and metal filters and the pan run on 32-bit products (sound Q15, filters Q13);
 *  - all metal voices share one oscillator bank per machine, as on the real TR-606 and CR-78; it is read from a loop
 *    of half a second the computer made with CB-931's own oscillators (its frequencies rounded to even Hz, 1 Hz at
 *    most, so that every oscillator fits the loop: no seam), DRM_BANK606 / DRM_BANK78 in drum_tab.h.
 * tests/drum_test.c checks every sound against CB-931's: loudness, octave bands and the shape of the hit over time.
 *
 * Fixed point: envelopes kept in Q30, drawn in Q15 (tone) or Q17 (noise, metal), rounded; sound Q15 (1 << 15 = full scale); filters Q13; the rasp's
 * resonator Q29 with its state Q20 (64-bit products: few sounds use it). drm_render() adds into Q15 buffers, the
 * organ's output scale. */
#ifndef DRUM_CORE_H
#define DRUM_CORE_H

#include <stdint.h>

#ifndef DRM_VOICES
#define DRM_VOICES 12                           /* CB-931 has 24; the FM-1 shares its time with the organ */
#endif
#ifndef DRM_CODE
#define DRM_CODE                                 /* the firmware may put the render in RAM */
#endif
#ifndef DRM_CHK
#define DRM_CHK(x) ((void)0)                     /* tests: catch a 32-bit product that would overflow */
#endif

#define DRM_SUB 16                              /* samples a step */
#if defined(__GNUC__) || defined(__clang__)
#define DRM_INL static inline __attribute__((always_inline))   /* the firmware builds with -Os, which would call these */
#else
#define DRM_INL static inline
#endif
#define DRM_MAXN 256                            /* frames a call, at most */

enum {
    DRM_BD = 0, DRM_SD, DRM_RS, DRM_CH, DRM_OH, DRM_CY, DRM_LT, DRM_HT,
    DRM_CL, DRM_MA, DRM_HB, DRM_LB, DRM_CG, DRM_CB, DRM_TB, DRM_EX,
    DRM_S2, DRM_QJ, DRM_SLOTS
};

#define DRM_F_TONE   0x0001u
#define DRM_F_SQUARE 0x0002u
#define DRM_F_TLP    0x0004u
#define DRM_F_TONE2  0x0008u
#define DRM_F_NOISE  0x0010u
#define DRM_F_NHP    0x0020u                    /* the noise filter is a high-pass (else a band-pass) */
#define DRM_F_METAL  0x0040u
#define DRM_F_M78    0x0080u                    /* the CR-78's three metal oscillators (else the TR-606's six) */
#define DRM_F_RASP   0x0100u
#define DRM_F_RBP    0x0200u                    /* the rasp's extras: first tick at once, a true band-pass */
#define DRM_F_RRAMP  0x0400u                    /* the rasp rises 0.4 -> 1 over its length (no fade of its own) */
#define DRM_F_RACC   0x0800u
#define DRM_F_RJIT   0x1000u
#define DRM_F_TFAST  0x2000u                    /* the tone falls too fast for steps (under 3.7 ms to -20 dB): sample by sample */
#define DRM_F_THERE  0x8000u                    /* the kit has this sound */

typedef struct {
    uint16_t flags;
    int8_t choke;                               /* > 0 closes the voices whose choke is minus this */
    uint8_t click_n;                            /* samples the click lasts (where CB-931's falls below 1e-4) */
    int32_t lvl;                                /* 10^(level/20) x 0.35, Q30 */
    int16_t pl, pr;                             /* the pan's gains (sqrt 2 x cos / sin), Q13 */
    /* tone */
    int32_t t_lev, t_lev2;                      /* Q28; Q30 */
    int32_t t_inc, t_inc2;                      /* phase steps, of 2^32 per sample */
    int32_t drop0, drop_k16;                    /* the pitch drop: start (Q28, f x (1 + drop)); its fall per step (Q30) */
    int32_t tone_k16, att_k16;                  /* Q30 per step: the decay; what is left of the attack ((1 - a)^16; 0: none) */
    int32_t tone_k1, att_a1;                    /* Q30 per sample: the decay; the attack's step (while it rises, and TFAST) */
    int32_t click0;                             /* Q28 */
    int16_t t_sq, tlp_a;                        /* sine .. square, Q15; the tone's low-pass, Q14 (0: off) */
    /* noise and metal: their filters (TPT state variable: g, k + g, 1 / (1 + g (g + k)), k; Q13) and the envelope */
    int32_t n_lev, m_lev;                       /* Q28; m_lev includes CB-931's x 1.6 */
    int32_t n_g, n_kg, n_d, n_k;
    int32_t m_g, m_kg, m_d, m_k;
    int32_t env1, env2, nm_k1_16, nm_k2_16;     /* Q30; falls per step */
    int32_t hold_n;                             /* samples until an open hat closes (0: none) */
    /* rasp */
    int32_t r_lev;                              /* r_level x 0.67, Q28 */
    int32_t rr_a1, rr_a2;                       /* Q29 */
    int32_t r_n0, r_pos;                        /* length in samples; 2^30 / length */
    int32_t r_inc;                              /* ticks: phase step per sample, of 2^30 */
    int32_t r_k16, r_att_a1, r_att0;            /* Q30: fade per step; the attack's step per sample (0: none); the start */
    int32_t r_acc, r_jit;                       /* accelerando - 1 (Q28); jitter (Q30) */
    int32_t r_tail;                             /* samples the resonator may ring after the last tick (12 ring times) */
} drm_sound_t;

#include "drum_tab.h"                           /* generated by tools/gen_drums.c: DRM_KITS, DRM_SOUND[kit][slot], DRM_VEL, DRM_SINE ... */

typedef struct {
    const drm_sound_t *s;
    int32_t active;
    int32_t tg, ng, mg, cg;                     /* the parts' gains with the sound's level and the velocity, Q30 */
    int32_t rg;                                 /* the rasp's, Q28 */
    uint32_t ph, ph2;
    int32_t drop, tone_env, att, click, tlp_z;
    uint32_t click_n;
    int32_t ns1, ns2, ms1, ms2, mhp_z;          /* noise and metal filters, the metal's high-pass (Q13) */
    int32_t env1, env2, close, close_k16, hold_n;
    uint32_t r_ph, r_next;
    int32_t r_n, ry1, ry2, rx1, rx2, r_env, r_att;
} drm_voice_t;

static drm_voice_t drm_v[DRM_VOICES];
static uint32_t drm_age[DRM_VOICES], drm_agec;
static uint32_t drm_kit;
static uint32_t drm_rng = 0x2545F491u;          /* CB-931's seed and xorshift */
static uint32_t drm_bpos606 = 7919u, drm_bpos78 = 104729u % DRM_BANK_LEN;   /* where the shared metal banks are in their loops */
static int32_t drm_buf[DRM_MAXN], drm_m606[DRM_MAXN], drm_m78[DRM_MAXN];

#define DRM_ONE 1073741824                      /* 1.0, Q30 */
#define DRM_END 107374                          /* 1e-4, Q30: where CB-931 calls a voice finished */
#define DRM_OUT_Q 15
#define DRM_RTICK 1048576                       /* a rasp tick, Q20 */
/* the rasp's resonator has rung out: CB-931 stops when it is below 1e-5 of a tick (10.5 in Q20). Its feedback is
 * rounded toward zero; and in case a resonator settles into a small wobble anyway, 12 ring times after the last tick
 * (where CB-931's is below -100 dB) ends it too. r_n counts on below 0 through the tail. */
#define DRM_RQUIET(v) (((v)->ry1 >= -10 && (v)->ry1 <= 10) || (v)->r_n <= -(v)->s->r_tail)

DRM_INL int32_t drm_m(int32_t a, int32_t b, int sh) { return (int32_t)(((int64_t)a * b) >> sh); }
DRM_INL int32_t drm_e(int32_t a, int32_t k) { return (int32_t)(((int64_t)a * k + (1 << 29)) >> 30); }   /* Q30, rounded */
DRM_INL int32_t drm_p(int32_t a, int32_t b) { DRM_CHK((int64_t)a * b); return a * b; }   /* a 32-bit product */

DRM_INL uint32_t drm_rnd(void)
{
    uint32_t s = drm_rng;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    drm_rng = s;
    return s;
}

DRM_INL int32_t drm_sin(uint32_t ph)      /* Q15, from a 1024-step table, linear */
{
    uint32_t i = ph >> 22;
    int32_t a = DRM_SINE[i], b = DRM_SINE[i + 1u];
    return a + (drm_p(b - a, (int32_t)((ph >> 12) & 0x3FFu)) >> 10);
}

/* CB-931's polyBLEP squares. u / dt for the edge's correction without a division in the loop: dt is brought to
 * 16 bits by a shift (sh) once, and multiplied by its reciprocal (rcp = 2^31 / (dt >> sh)) */
typedef struct { uint32_t dt, sh, rcp; } drm_dt_t;
static void drm_dt(drm_dt_t *d, uint32_t dt)
{
    uint32_t sh = 0;
    while ((dt >> sh) >= 65536u) sh++;
    d->dt = dt; d->sh = sh; d->rcp = (dt >> sh) ? 2147483648u / (dt >> sh) : 0u;
}
DRM_INL int32_t drm_blep(uint32_t t, const drm_dt_t *d)   /* Q15 */
{
    int32_t x;
    if (t < d->dt) { x = (int32_t)(((t >> d->sh) * d->rcp) >> 16); return x + x - ((x * x) >> 15) - 32768; }
    if (t > 0u - d->dt) { x = -(int32_t)((((0u - t) >> d->sh) * d->rcp) >> 16); return ((x * x) >> 15) + x + x + 32768; }
    return 0;
}
DRM_INL int32_t drm_square(uint32_t ph, const drm_dt_t *d)   /* Q15 */
{
    return (ph < 0x80000000u ? 32768 : -32768) + drm_blep(ph, d) - drm_blep(ph + 0x80000000u, d);
}

/* TPT state-variable filter: Q13 coefficients, sound and states; hp_out 1: high-pass, else band-pass x k.
 * The products are rounded: truncated, a low filter (g small) gathers a bias step by step and goes off level. */
#define DRM_R13(x) (((x) + 4096) >> 13)
DRM_INL int32_t drm_svf(int32_t x, int32_t g, int32_t kg, int32_t d, int32_t k, int32_t *s1, int32_t *s2, int hp_out)
{
    int32_t hp = DRM_R13(drm_p(x - DRM_R13(drm_p(kg, *s1)) - *s2, d));
    int32_t gh = DRM_R13(drm_p(g, hp)), bp = gh + *s1, gb;
    *s1 = gh + bp;
    gb = DRM_R13(drm_p(g, bp));
    *s2 += gb + gb;
    return hp_out ? hp : DRM_R13(drm_p(bp, k));
}

static void drm_set_kit(uint32_t kit) { drm_kit = kit < DRM_KITS ? kit : DRM_KITS - 1u; }
static void drm_all_off(void) { uint32_t i; for (i = 0; i < DRM_VOICES; i++) drm_v[i].active = 0; }

static void drm_trigger_sound(const drm_sound_t *s, uint32_t vel)
{
    drm_voice_t *v;
    uint32_t i, vi = DRM_VOICES;
    int32_t g;
    if (!(s->flags & ~DRM_F_THERE)) return;     /* a slot the kit doesn't have, or a silent one */
    if (s->choke > 0)                           /* a closed hat stops an open one */
        for (i = 0; i < DRM_VOICES; i++)
            if (drm_v[i].active && drm_v[i].s->choke == -s->choke) {
                if (drm_v[i].close <= 0) drm_v[i].close = DRM_ONE;
                drm_v[i].close_k16 = DRM_CLOSE4_16;
                drm_v[i].hold_n = 0;
            }
    if (s->flags & DRM_F_METAL) {               /* the bank idle: new random phases (as a free-running analogue bank would be
                                                 * at any moment); with hits at a steady tempo it would otherwise meet them on
                                                 * the same phases every time, and its coinciding partials (5 x 2373 = 3 x 3955 Hz)
                                                 * add up the same way every time. Nobody hears the jump. */
        uint32_t busy = 0, m78 = s->flags & DRM_F_M78;
        for (i = 0; i < DRM_VOICES; i++)
            if (drm_v[i].active && (drm_v[i].s->flags & DRM_F_METAL) && (drm_v[i].s->flags & DRM_F_M78) == m78) busy = 1;
        if (!busy) {
            if (m78) drm_bpos78 = drm_rnd() % DRM_BANK_LEN;
            else drm_bpos606 = drm_rnd() % DRM_BANK_LEN;
        }
    }
    for (i = 0; i < DRM_VOICES; i++) if (!drm_v[i].active) { vi = i; break; }
    if (vi == DRM_VOICES) {                     /* none free: the oldest */
        vi = 0;
        for (i = 1; i < DRM_VOICES; i++) if (drm_age[i] < drm_age[vi]) vi = i;
    }
    v = &drm_v[vi];
    {
        uint8_t *p = (uint8_t *)v;
        for (i = 0; i < sizeof *v; i++) p[i] = 0;
    }
    v->s = s;
    v->active = 1;
    drm_age[vi] = ++drm_agec;
    if (vel < 1u) vel = 1u;
    if (vel > 127u) vel = 127u;
    g = drm_m(s->lvl, DRM_VEL[vel], 30);        /* accent: full at 127, about 4 dB down at 88 */
    v->tg = drm_m(s->t_lev, g, 28);
    v->ng = drm_m(s->n_lev, g, 28);
    v->mg = drm_m(s->m_lev, g, 28);
    v->cg = drm_m(s->click0, g, 28);
    v->rg = drm_m(s->r_lev, g, 30);
    v->tone_env = DRM_ONE;
    v->att = s->att_k16 ? 0 : DRM_ONE;
    v->drop = s->drop0;
    v->click = DRM_ONE;
    v->click_n = s->click_n;
    v->env1 = s->env1;
    v->env2 = s->env2;
    v->hold_n = s->hold_n;
    if (s->flags & DRM_F_RASP) {
        v->r_n = s->r_n0;
        v->r_env = DRM_ONE;
        v->r_att = s->r_att0;
        v->r_ph = (s->flags & DRM_F_RBP) ? (uint32_t)DRM_ONE : 0u;   /* with the extras, the first tick at once */
        v->r_next = (uint32_t)DRM_ONE;
    }
}

static void drm_trigger(uint32_t slot, uint32_t vel)   /* vel 1..127 */
{
    if (slot < DRM_SLOTS) drm_trigger_sound(&DRM_SOUND[drm_kit][slot], vel);
}

static void drm_bank(int32_t *out, const int16_t *loop, uint32_t *pos, uint32_t n)
{                                               /* the shared metal: n samples of its loop, Q13 */
    uint32_t i, p = *pos;
    for (i = 0; i < n; i++) { out[i] = loop[p]; if (++p == DRM_BANK_LEN) p = 0; }
    *pos = p;
}

/* the tone over one step into b (Q15) */
DRM_INL void drm_tone(drm_voice_t *v, const drm_sound_t *s, int32_t *b)
{
    uint32_t f = s->flags, i, inc, inc1, inc2, inc21;
    int32_t a0, a1, da, a, dinc, dinc2, drop1;
    drm_dt_t sqd = {0u, 0u, 0u};
    drop1 = drm_e(v->drop, s->drop_k16);
    inc = (uint32_t)(s->t_inc + drm_m(s->t_inc, v->drop, 28));
    inc1 = (uint32_t)(s->t_inc + drm_m(s->t_inc, drop1, 28));
    inc2 = (uint32_t)(s->t_inc2 + drm_m(s->t_inc2, v->drop, 28));
    inc21 = (uint32_t)(s->t_inc2 + drm_m(s->t_inc2, drop1, 28));
    dinc = ((int32_t)inc1 - (int32_t)inc) >> 4;
    dinc2 = ((int32_t)inc21 - (int32_t)inc2) >> 4;
    v->drop = drop1;
    if (f & DRM_F_SQUARE) drm_dt(&sqd, inc + ((uint32_t)dinc << 3));   /* the step's middle: the drop moves little within a step */
    if (v->att < DRM_ONE - (DRM_ONE >> 10) || (f & DRM_F_TFAST)) {   /* the attack rises, or a very short tone: sample by sample */
        for (i = 0; i < DRM_SUB; i++) {
            int32_t t = drm_sin(v->ph);
            a = (drm_m(drm_m(v->tone_env, v->att, 30), v->tg, 30) + 16384) >> 15;
            if (f & DRM_F_SQUARE) t += drm_p(s->t_sq, drm_square(v->ph, &sqd) - t) >> 15;
            if (f & DRM_F_TONE2) t += drm_m(s->t_lev2, drm_sin(v->ph2), 30);
            v->ph += inc; v->ph2 += inc2;
            inc += (uint32_t)dinc; inc2 += (uint32_t)dinc2;
            if (f & DRM_F_TLP) { v->tlp_z += drm_p(t - v->tlp_z, s->tlp_a) >> 14; t = v->tlp_z; }
            b[i] = drm_p(t, a) >> 15;
            v->tone_env = drm_e(v->tone_env, s->tone_k1);
            if (s->att_a1) v->att += drm_m(DRM_ONE - v->att, s->att_a1, 30);
        }
        goto click;
    }
    a0 = (drm_m(drm_m(v->tone_env, v->att, 30), v->tg, 30) + 16384) >> 15;   /* Q15, rounded: truncated, quiet tails lose level */
    v->tone_env = drm_e(v->tone_env, s->tone_k16);
    if (s->att_k16) v->att = DRM_ONE - drm_e(DRM_ONE - v->att, s->att_k16);
    a1 = (drm_m(drm_m(v->tone_env, v->att, 30), v->tg, 30) + 16384) >> 15;
    da = (a1 - a0 + 8) >> 4;
    a = a0;
    if (!(f & (DRM_F_SQUARE | DRM_F_TONE2 | DRM_F_TLP))) {   /* a plain sine: most of them */
        uint32_t ph = v->ph;
        for (i = 0; i < DRM_SUB; i++) {
            b[i] = drm_p(drm_sin(ph), a) >> 15;
            ph += inc; inc += (uint32_t)dinc; a += da;
        }
        v->ph = ph;
    } else {
        for (i = 0; i < DRM_SUB; i++) {
            int32_t t = drm_sin(v->ph);
            if (f & DRM_F_SQUARE) t += drm_p(s->t_sq, drm_square(v->ph, &sqd) - t) >> 15;
            if (f & DRM_F_TONE2) t += drm_m(s->t_lev2, drm_sin(v->ph2), 30);
            v->ph += inc; v->ph2 += inc2;
            inc += (uint32_t)dinc; inc2 += (uint32_t)dinc2;
            if (f & DRM_F_TLP) { v->tlp_z += drm_p(t - v->tlp_z, s->tlp_a) >> 14; t = v->tlp_z; }
            b[i] = drm_p(t, a) >> 15;
            a += da;
        }
    }
click:
    if (f & DRM_F_NOISE) return;                /* with noise, the click is drawn in the noise loop (drm_render) */
    for (i = 0; i < DRM_SUB && v->click_n; i++, v->click_n--) {   /* the click: noise falling 0.92 a sample */
        b[i] += drm_m(drm_m(v->click, v->cg, 30), (int32_t)drm_rnd() >> 16, 30);
        v->click = drm_m(v->click, DRM_CLICK_K, 30);
    }
}

/* adds n frames (a multiple of 16, at most 256) into l and r (Q15) */
DRM_CODE static void drm_render(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t vi, i, o, need606 = 0, need78 = 0;
    if (n > DRM_MAXN) n = DRM_MAXN;
    n &= ~(uint32_t)(DRM_SUB - 1);
    for (vi = 0; vi < DRM_VOICES; vi++)
        if (drm_v[vi].active && (drm_v[vi].s->flags & DRM_F_METAL)) { if (drm_v[vi].s->flags & DRM_F_M78) need78 = 1; else need606 = 1; }
    if (need606) drm_bank(drm_m606, DRM_BANK606, &drm_bpos606, n);
    if (need78) drm_bank(drm_m78, DRM_BANK78, &drm_bpos78, n);
    for (vi = 0; vi < DRM_VOICES; vi++) {
        drm_voice_t *v = &drm_v[vi];
        const drm_sound_t *s;
        uint32_t f;
        if (!v->active) continue;
        s = v->s;
        f = s->flags;
        for (o = 0; o < n; o += DRM_SUB) {
            int32_t *b = drm_buf + o;
            if (f & DRM_F_TONE) drm_tone(v, s, b);
            else for (i = 0; i < DRM_SUB; i++) b[i] = 0;
            if (f & (DRM_F_NOISE | DRM_F_METAL)) {   /* noise and metal share an envelope */
                int32_t e0, e1, a, da;
                e0 = v->close > 0 ? drm_m(v->env1 + v->env2, v->close, 30) : v->env1 + v->env2;
                v->env1 = drm_e(v->env1, s->nm_k1_16);
                v->env2 = drm_e(v->env2, s->nm_k2_16);
                if (v->hold_n > 0 && (v->hold_n -= (int32_t)DRM_SUB) <= 0) { v->hold_n = 0; v->close = DRM_ONE; v->close_k16 = DRM_CLOSE12_16; }
                else if (v->close > 0) v->close = drm_e(v->close, v->close_k16);
                e1 = v->close > 0 ? drm_m(v->env1 + v->env2, v->close, 30) : v->env1 + v->env2;
                if (f & DRM_F_NOISE) {
                    int32_t g = s->n_g, kg = s->n_kg, d = s->n_d, k = s->n_k, s1 = v->ns1, s2 = v->ns2, hpo = (f & DRM_F_NHP) != 0;
                    a = (drm_m(e0, v->ng, 30) + 4096) >> 13;   /* Q17, rounded: a quiet part keeps its level */
                    da = (((drm_m(e1, v->ng, 30) + 4096) >> 13) - a + 8) >> 4;
                    for (i = 0; i < DRM_SUB; i++) {
                        int32_t x;
                        if (v->click_n) {       /* CB-931 draws the click's random number, then the noise's, each sample: the */
                            v->click_n--;       /* two come out a little alike, and the kits were fitted with that, so the same */
                            b[i] += drm_m(drm_m(v->click, v->cg, 30), (int32_t)drm_rnd() >> 16, 30);
                            v->click = drm_m(v->click, DRM_CLICK_K, 30);
                        }
                        x = drm_svf((int32_t)drm_rnd() >> 18, g, kg, d, k, &s1, &s2, hpo);   /* noise -1..1, Q13 */
                        b[i] += drm_p(x, a) >> 15;
                        a += da;
                    }
                    v->ns1 = s1; v->ns2 = s2;
                }
                if (f & DRM_F_METAL) {
                    const int32_t *mb = ((f & DRM_F_M78) ? drm_m78 : drm_m606) + o;
                    int32_t g = s->m_g, kg = s->m_kg, d = s->m_d, k = s->m_k, s1 = v->ms1, s2 = v->ms2, z = v->mhp_z;
                    a = (drm_m(e0, v->mg, 30) + 4096) >> 13;
                    da = (((drm_m(e1, v->mg, 30) + 4096) >> 13) - a + 8) >> 4;
                    for (i = 0; i < DRM_SUB; i++) {
                        int32_t hp = drm_svf(mb[i], g, kg, d, k, &s1, &s2, 0) - z;   /* a gentle high-pass after: the body is above 2 kHz */
                        z += hp >> 2;
                        b[i] += drm_p(hp, a) >> 15;
                        a += da;
                    }
                    v->ms1 = s1; v->ms2 = s2; v->mhp_z = z;
                }
            }
            if ((f & DRM_F_RASP) && (v->r_n > 0 || !DRM_RQUIET(v))) {   /* rasp: a pulse train ringing a resonator */
                int32_t sh0, sh1, dsh, sh, env1, rising = v->r_att < DRM_ONE;
                env1 = drm_e(v->r_env, s->r_k16);
                if (f & DRM_F_RRAMP) {          /* 0.4 + 0.6 x how far through */
                    int32_t p0 = s->r_n0 > 0 && v->r_n > 0 ? (int32_t)((uint32_t)(s->r_n0 - v->r_n) * (uint32_t)s->r_pos) : DRM_ONE;
                    int32_t p1 = s->r_n0 > 0 && v->r_n > (int32_t)DRM_SUB ? (int32_t)((uint32_t)(s->r_n0 - v->r_n + (int32_t)DRM_SUB) * (uint32_t)s->r_pos) : DRM_ONE;
                    sh0 = 429496730 + drm_m(644245094, p0, 30);
                    sh1 = 429496730 + drm_m(644245094, p1, 30);
                } else {
                    sh0 = v->r_env;
                    sh1 = env1;
                }
                dsh = (sh1 - sh0) >> 4;
                sh = sh0;
                for (i = 0; i < DRM_SUB; i++) {
                    int32_t x = 0, in, yy;
                    int64_t w;
                    if (v->r_n > 0) {
                        uint32_t inc = (uint32_t)s->r_inc;
                        if (f & DRM_F_RACC) {   /* faster towards the end */
                            int32_t pos = (int32_t)((uint32_t)(s->r_n0 - v->r_n) * (uint32_t)s->r_pos);
                            inc += (uint32_t)drm_m(s->r_inc, drm_m(s->r_acc, drm_m(pos, pos, 30), 30), 28);
                        }
                        v->r_ph += inc;
                        if (v->r_ph >= v->r_next) {
                            v->r_ph -= v->r_next;
                            v->r_next = (uint32_t)DRM_ONE + ((f & DRM_F_RJIT) ? (uint32_t)drm_m(s->r_jit, (int32_t)drm_rnd() >> 1, 30) : 0u);
                            x = DRM_RTICK;
                        }
                    }
                    v->r_n--;
                    in = x;
                    if (f & DRM_F_RBP) { in = x - v->rx2; v->rx2 = v->rx1; v->rx1 = x; }
                    w = (int64_t)s->rr_a1 * v->ry1 + (int64_t)s->rr_a2 * v->ry2;
                    yy = in + (int32_t)(w >= 0 ? w >> 29 : -((-w) >> 29));   /* toward zero: rings out instead of wobbling */
                    v->ry2 = v->ry1;
                    v->ry1 = yy;
                    if (rising) {               /* the attack, sample by sample while it rises */
                        v->r_att += drm_m(DRM_ONE - v->r_att, s->r_att_a1, 30);
                        if (v->r_att >= DRM_ONE - 1024) { v->r_att = DRM_ONE; rising = 0; }
                        w = ((int64_t)yy * drm_m(sh, v->r_att, 30)) >> 30;
                    } else
                        w = ((int64_t)yy * sh) >> 30;   /* Q20 */
                    b[i] += (int32_t)((w * v->rg) >> 33);   /* x Q28 -> Q15 */
                    sh += dsh;
                }
                v->r_env = env1;
            }
        }
        {                                       /* pan into the output */
            int32_t pl = s->pl, pr = s->pr;
            for (i = 0; i < n; i++) {
                int32_t y = drm_buf[i];
                l[i] += drm_p(y, pl) >> 13;
                r[i] += drm_p(y, pr) >> 13;
            }
        }
        {                                       /* finished? (as CB-931: once a block) */
            int tone_done = !(f & DRM_F_TONE) || v->tone_env < DRM_END;
            int32_t e = v->env1 + v->env2;
            int nm_done = !(f & (DRM_F_NOISE | DRM_F_METAL)) ||
                          ((v->close > 0 ? drm_m(e, v->close, 30) : e) < DRM_END && v->hold_n <= 0);
            int r_done = !(f & DRM_F_RASP) || (v->r_n <= 0 && DRM_RQUIET(v));
            if (tone_done && nm_done && r_done) v->active = 0;
        }
    }
}

static uint32_t drm_voices_on(void) { uint32_t i, c = 0; for (i = 0; i < DRM_VOICES; i++) c += drm_v[i].active != 0; return c; }

#endif
