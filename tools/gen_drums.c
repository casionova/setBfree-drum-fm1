/* SPDX-License-Identifier: GPL-3.0-only */
/* B3+DRUM: turns the Cheesebox 931's drum kits (tools/cb931/drums.c, float) into the integer tables the FM-1's
 * engine plays from (firmware/src/drum_core.h). Runs on the computer: tools/gen_drums.sh.
 *   cc -O1 -Itools/cb931 -o build/gen_drums tools/gen_drums.c tools/cb931/drums.c -lm && build/gen_drums > firmware/src/drum_tab.h */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "drums.h"

#define FS 44100.0
#define LN20DB 2.302585

static long q(double x, int bits, double lim)   /* x in Q bits, checked against the range the engine assumes */
{
    if (fabs(x) >= lim) { fprintf(stderr, "gen_drums: %g out of range (limit %g)\n", x, lim); exit(1); }
    return lround(x * (double)(1L << bits));
}
static long q30(double x) { return q(x, 30, 2.0); }
static long q28(double x) { return q(x, 28, 8.0); }
static long q27(double x) { return q(x, 27, 15.9); }
static long q29(double x) { return q(x, 29, 3.99); }
static long inc32(double f)                      /* phase step of 2^32 per sample, as a signed 32-bit value */
{
    double v = f / FS * 4294967296.0;
    if (v < 0 || v >= 2147483648.0) { fprintf(stderr, "gen_drums: frequency %g too high\n", f); exit(1); }
    return lround(v);
}
static double decay_k(double ms) { return ms <= 0 ? 0.0 : exp(-LN20DB / (ms * 0.001 * FS)); }

static void svf(double fc, double qq, long *g, long *kg, long *d, long *k)
{
    double gg, kk;
    if (fc > 0.45 * FS) fc = 0.45 * FS;
    gg = tan(M_PI * fc / FS);
    kk = 1.0 / (qq > 0.1 ? qq : 0.1);
    *g = q27(gg); *kg = q27(kk + gg); *d = q27(1.0 / (1.0 + gg * (gg + kk))); *k = q27(kk);
}

static void cstr(const char *s)
{
    putchar('"');
    for (; s && *s; s++) putchar(*s == '"' ? '\'' : *s);
    putchar('"');
}

static void sound(const dr_sound *s)
{
    unsigned fl = 0;
    int click_n = 0;
    long ng = 0, nkg = 0, nd = 0, nk = 0, mg = 0, mkg = 0, md = 0, mk = 0;
    double ring, rr, w, decay, p;
    long r_n0 = 0, r_pos = 0;
    if (!s || !s->name) { printf("        {0},\n"); return; }
    if (s->t_level > 0) fl |= 0x0001u;
    if (s->t_level > 0 && s->t_square > 0) fl |= 0x0002u;
    if (s->t_level > 0 && s->t_lp > 0) fl |= 0x0004u;
    if (s->t_level > 0 && s->t_level2 > 0) fl |= 0x0008u;
    if (s->n_level > 0) fl |= 0x0010u;
    if (s->n_level > 0 && s->n_mode) fl |= 0x0020u;
    if (s->m_level > 0) fl |= 0x0040u;
    if (s->m_level > 0 && s->m_set) fl |= 0x0080u;
    if (s->r_level > 0) {
        int extras = s->r_ring > 0 || s->r_decay > 0 || s->r_accel > 0 || s->r_jitter > 0 || s->r_attack > 0;
        fl |= 0x0100u;
        if (extras) fl |= 0x0200u;
        if (!(s->r_decay > 0)) fl |= 0x0400u;
        if (s->r_accel > 0) fl |= 0x0800u;
        if (s->r_jitter > 0) fl |= 0x1000u;
    }
    if (!fl) fl = 0x8000u;                      /* named but silent: still "there" */
    if (s->t_level > 0) {                       /* CB-931 adds the click while it is above 1e-4, falling 0.92 per sample */
        float c = s->t_click;
        while (c > 1e-4f) { click_n++; c *= 0.92f; }
        if (click_n > 255) { fprintf(stderr, "gen_drums: click too long\n"); exit(1); }
    }
    if (s->n_level > 0) svf(s->n_centre, s->n_q, &ng, &nkg, &nd, &nk);
    if (s->m_level > 0) svf(s->m_centre, s->m_q, &mg, &mkg, &md, &mk);
    decay = s->n_level > 0 ? s->n_decay : s->m_decay;
    ring = s->r_ring > 0 ? s->r_ring : 1.5;
    rr = exp(-1.0 / (ring * 0.001 * FS));
    w = 2.0 * M_PI * s->r_freq / FS;
    if (s->r_level > 0) {
        if (2.0 / (1.0 - rr) * 1048576.0 > 1073741824.0) {   /* the resonator's peak (ticks through a band-pass) must fit Q20 */
            fprintf(stderr, "gen_drums: %s rings too long for the rasp's Q20\n", s->name); exit(1);
        }
        r_n0 = (long)(s->r_len * 0.001 * FS);
        r_pos = r_n0 > 0 ? lround(1073741824.0 / (double)r_n0) : 0;
    }
    p = (s->pan + 1.0) * 0.25 * M_PI;
    printf("        { /* %s */ 0x%04X, %d, %d,\n", s->name, fl, s->choke, click_n);
    printf("          %ld, %ld, %ld,\n", q30(pow(10.0, s->level_db / 20.0) * 0.35), q30(cos(p) * M_SQRT2), q30(sin(p) * M_SQRT2));
    printf("          %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld,\n",
           q28(s->t_level), q28(s->t_level2), inc32(s->t_freq), inc32(s->t_freq2),
           q28(s->t_drop > 1.0f ? s->t_drop - 1.0 : 0.0), q30(s->t_drop_ms > 0 ? exp(-1.0 / (s->t_drop_ms * 0.001 * FS)) : 0.0),
           q30(decay_k(s->t_decay)), q30(s->t_attack > 0 ? 1.0 - exp(-3.0 / (s->t_attack * 0.001 * FS)) : 1.0),
           q30(s->t_click), lround(s->t_square * 32768.0), q30(s->t_lp > 0 ? 1.0 - exp(-2.0 * M_PI * s->t_lp / FS) : 1.0));
    printf("          %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld,\n",
           q28(s->n_level), ng, nkg, nd, nk, q28(s->m_level * 1.6), mg, mkg, md, mk);
    printf("          %ld, %ld, %ld, %ld, %ld,\n", q30(1.0 - s->tail_level), q30(s->tail_level), q30(decay_k(decay)),
           q30(decay_k(s->tail_decay > 0 ? s->tail_decay : decay)), s->hold_ms > 0 ? (long)(s->hold_ms * 0.001 * FS) : 0L);
    printf("          %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld },\n",
           q28(s->r_level * 0.67), q29(2.0 * rr * cos(w)), q29(-rr * rr), r_n0, r_pos,
           s->r_level > 0 ? lround(s->r_rate / FS * 1073741824.0) : 0L,
           q30(s->r_decay > 0 ? decay_k(s->r_decay) : 1.0),
           q30(s->r_attack > 0 ? 1.0 - exp(-3.0 / (s->r_attack * 0.001 * FS)) : 1.0),
           s->r_attack > 0 ? 0L : q30(1.0), q28(s->r_accel > 0 ? s->r_accel - 1.0 : 0.0), q30(s->r_jitter),
           s->r_level > 0 ? (long)ceil(12.0 * ring * 0.001 * FS) : 0L);
}

int main(void)
{
    static const double M606[6] = {2373, 2579, 2783, 3056, 3614, 3955}, M78[3] = {6170, 5620, 4080};
    int kit, slot, i;
    printf("/* Generated by tools/gen_drums.c from tools/cb931/drums.c (CB-931 0.16.1): do not edit. 44.1 kHz. */\n");
    printf("#ifndef DRUM_TAB_H\n#define DRUM_TAB_H\n\n#define DRM_KITS %du\n\n", DR_KITS);
    printf("static const char *const DRM_KIT_NAME[DRM_KITS] = {");
    for (kit = 0; kit < DR_KITS; kit++) { cstr(dr_kit_get(kit)->name); printf(kit + 1 < DR_KITS ? ", " : ""); }
    printf("};\n\nstatic const char *const DRM_SLOT_NAME[DRM_SLOTS] = {");
    for (slot = 0; slot < DR_SLOTS; slot++) { cstr(dr_slot_name(slot)); printf(slot + 1 < DR_SLOTS ? ", " : ""); }
    printf("};\n\n/* each kit's own name for each slot (\"\": the kit hasn't one) */\n");
    printf("static const char *const DRM_SOUND_NAME[DRM_KITS][DRM_SLOTS] = {\n");
    for (kit = 0; kit < DR_KITS; kit++) {
        printf("    {");
        for (slot = 0; slot < DR_SLOTS; slot++) {
            const dr_sound *s = &dr_kit_get(kit)->sound[slot];
            if (!s->name && slot == DR_S2) {
                const dr_sound *ex = &dr_kit_get(kit)->sound[DR_EX];
                s = ex->name && !strcmp(ex->name, "Snare 2") ? ex : &dr_kit_get(kit)->sound[DR_SD];
            }
            cstr(s->name ? s->name : "");
            printf(slot + 1 < DR_SLOTS ? ", " : "");
        }
        printf("},\n");
    }
    printf("};\n\n/* velocity: 10^(-12 (1 - v / 127) / 20), Q30 (CB-931's accent) */\nstatic const int32_t DRM_VEL[128] = {");
    for (i = 0; i < 128; i++) printf("%s%ld%s", i % 8 ? "" : "\n    ", q30(pow(10.0, -12.0 * (1.0 - i / 127.0) / 20.0)), i < 127 ? ", " : "");
    printf("\n};\n\n/* sine, 1024 steps and one more, Q30 */\nstatic const int32_t DRM_SINE[1025] = {");
    for (i = 0; i <= 1024; i++) printf("%s%ld%s", i % 8 ? "" : "\n    ", lround(sin(2.0 * M_PI * i / 1024.0) * 1073741823.0), i < 1024 ? ", " : "");
    printf("\n};\n\nstatic const uint32_t DRM_METAL606[6] = {");
    for (i = 0; i < 6; i++) printf("%ldu%s", inc32(M606[i]), i < 5 ? ", " : "");
    printf("};\nstatic const uint32_t DRM_METAL78[3] = {");
    for (i = 0; i < 3; i++) printf("%ldu%s", inc32(M78[i]), i < 2 ? ", " : "");
    printf("};\n\n#define DRM_CLICK_K %ld   /* the click falls 0.92 per sample */\n", q30(0.92f));
    printf("#define DRM_CLOSE4 %ld   /* a choked voice closes in 4 ms */\n", q30(exp(-1.0 / (0.004 * FS))));
    printf("#define DRM_CLOSE12 %ld   /* a held open hat closes in 12 ms */\n\n", q30(exp(-1.0 / (0.012 * FS))));
    printf("static const drm_sound_t DRM_SOUND[DRM_KITS][DRM_SLOTS] = {\n");
    for (kit = 0; kit < DR_KITS; kit++) {
        printf("    { /* %s */\n", dr_kit_get(kit)->name);
        for (slot = 0; slot < DR_SLOTS; slot++) {
            const dr_sound *s = &dr_kit_get(kit)->sound[slot];
            if (!s->name && slot == DR_S2) {    /* no snare 2: the kit's "Snare 2" in Extra, else its snare (CB-931 0.10.7) */
                const dr_sound *ex = &dr_kit_get(kit)->sound[DR_EX];
                s = ex->name && !strcmp(ex->name, "Snare 2") ? ex : &dr_kit_get(kit)->sound[DR_SD];
            }
            sound(s);
        }
        printf("    },\n");
    }
    printf("};\n\n#endif\n");
    return 0;
}
