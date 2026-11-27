/* SPDX-License-Identifier: GPL-3.0-only */
/* B3+DRUM: the FM-1's drums (firmware/src/drum_core.h) against the Cheesebox 931's float drums (tools/cb931/drums.c):
 * every sound of every kit at velocity 127 and 88, and a busy bar with chokes on each kit.
 * The FM-1's engine is made to be cheap (steps of 16 samples, 32-bit filters, a shared metal bank), so it is not the
 * same sample for sample; what is compared, each averaged over 8 noise seeds (64 for a rasp with random timing):
 *   level   the difference in loudness (dB)
 *   band    the largest difference in any octave band within 40 dB of the loudest (dB)
 *   shape   the largest difference in loudness over time, 5 ms windows within 30 dB of the loudest (dB)
 * and every 32-bit product is checked for overflow. It writes build/drums/<kit>.wav: each sound twice, CB-931's then
 * the FM-1's, for listening.
 *   cc -O1 -Ifirmware/src -Itools/cb931 -o build/drum_test tests/drum_test.c tools/cb931/drums.c -lm && build/drum_test */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static long overflows;
#define DRM_CHK(x) do { int64_t v_ = (x); if (v_ > 2147483647LL || v_ < -2147483648LL) overflows++; } while (0)
#include "drums.h"
#include "drum_core.h"

#define FS 44100
#define LEN 66048                               /* 1.5 s a sound, whole 256-frame blocks */
#define BLK 256
#ifndef SEEDS
#define SEEDS 8
#endif
#define WIN 220                                 /* 5 ms */
#define NWIN (LEN * 2 / WIN + 64)

static float rl[LEN * 3], rr[LEN * 3], fl[LEN * 3], fr[LEN * 3];
static void clear(float *a, int n) { while (n-- > 0) *a++ = 0.0f; }

static void fixed_reset(uint32_t kit, uint32_t seed)
{
    memset(drm_v, 0, sizeof drm_v);
    memset(drm_age, 0, sizeof drm_age);
    /* the shared metal banks run on from hit to hit, as on the FM-1 (and the real TR-606 / CR-78) */
    drm_agec = 0;
    drm_rng = seed;
    drm_set_kit(kit);
}

static void render_both(dr_engine *e, int n, int off)   /* n: a multiple of 16 */
{
    static int32_t il[BLK], ir[BLK];
    int i, b, k;
    for (i = off; i < off + n; i += BLK) {
        b = off + n - i < BLK ? off + n - i : BLK;
        memset(il, 0, sizeof il);
        memset(ir, 0, sizeof ir);
        dr_render(e, rl + i, rr + i, b);
        drm_render(il, ir, (uint32_t)b);
        for (k = 0; k < b; k++) { fl[i + k] = il[k] / 32768.0f; fr[i + k] = ir[k] / 32768.0f; }
    }
}

/* ---- octave bands by FFT */
static void fft(double *re, double *im, int n)
{
    int i, j, k, m;
    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (m = 2; m <= n; m <<= 1) {
        double a = -2.0 * M_PI / m;
        for (i = 0; i < n; i += m)
            for (k = 0; k < m / 2; k++) {
                double wr = cos(a * k), wi = sin(a * k);
                double xr = re[i + k + m / 2] * wr - im[i + k + m / 2] * wi, xi = re[i + k + m / 2] * wi + im[i + k + m / 2] * wr;
                re[i + k + m / 2] = re[i + k] - xr; im[i + k + m / 2] = im[i + k] - xi;
                re[i + k] += xr; im[i + k] += xi;
            }
    }
}
#define NB 10                                   /* octaves from 31 Hz to 16 kHz */
static void bands_add(const float *l, const float *r, int n, double *out)
{
    static double re[262144], im[262144];
    int N = 65536, i;
    while (N < n) N <<= 1;
    for (i = 0; i < N; i++) { re[i] = i < n ? 0.5 * (l[i] + r[i]) : 0.0; im[i] = 0.0; }
    fft(re, im, N);
    for (i = 1; i < N / 2; i++) {
        double f = (double)i * FS / N;
        int b = (int)floor(log2(f / 22.1));
        if (b >= 0 && b < NB) out[b] += re[i] * re[i] + im[i] * im[i];
    }
}

typedef struct { double pr, pf, br[NB], bf[NB], wr[NWIN], wf[NWIN]; int nw; } acc_t;
static void acc_add(acc_t *a, int n)
{
    int i, w;
    for (i = 0; i < n; i++) {
        a->pr += (double)rl[i] * rl[i] + (double)rr[i] * rr[i];
        a->pf += (double)fl[i] * fl[i] + (double)fr[i] * fr[i];
    }
    bands_add(rl, rr, n, a->br);
    bands_add(fl, fr, n, a->bf);
    a->nw = n / WIN < NWIN ? n / WIN : NWIN;
    for (w = 0; w < a->nw; w++)
        for (i = w * WIN; i < (w + 1) * WIN; i++) {
            a->wr[w] += (double)rl[i] * rl[i] + (double)rr[i] * rr[i];
            a->wf[w] += (double)fl[i] * fl[i] + (double)fr[i] * fr[i];
        }
}
typedef struct { double level, band, shape, rms; } cmp_t;
static cmp_t acc_cmp(const acc_t *a, int n, int seeds)
{
    cmp_t c;
    double bmax = 0, wmax = 0;
    int i;
    c.rms = 10.0 * log10(a->pr / seeds / n / 2 + 1e-30);
    c.level = 10.0 * log10((a->pf + 1e-30) / (a->pr + 1e-30));
    for (i = 0; i < NB; i++) if (a->br[i] > bmax) bmax = a->br[i];
    c.band = 0;
    for (i = 0; i < NB; i++) if (a->br[i] > bmax * 1e-4) { double d = fabs(10.0 * log10((a->bf[i] + 1e-30) / a->br[i])); if (d > c.band) c.band = d; }
    for (i = 0; i < a->nw; i++) if (a->wr[i] > wmax) wmax = a->wr[i];
    c.shape = 0;
    for (i = 0; i < a->nw; i++) if (a->wr[i] > wmax * 1e-3) { double d = fabs(10.0 * log10((a->wf[i] + 1e-30) / a->wr[i])); if (d > c.shape) c.shape = d; }
    return c;
}

/* ---- WAV, 16-bit stereo */
static FILE *wav_open(const char *path)
{
    FILE *f = fopen(path, "wb");
    static const unsigned char hdr[44] = {0};
    if (f) fwrite(hdr, 1, 44, f);
    return f;
}
static void wav_put(FILE *f, const float *l, const float *r, int n)
{
    for (int i = 0; i < n; i++) {
        float v[2] = {l ? l[i] : 0.0f, r ? r[i] : 0.0f};
        for (int c = 0; c < 2; c++) {
            long s = lrintf(v[c] * 32767.0f * 1.5f);   /* +3.5 dB: the drums alone are quiet */
            short x = (short)(s > 32767 ? 32767 : s < -32767 ? -32767 : s);
            fwrite(&x, 2, 1, f);
        }
    }
}
static void wav_close(FILE *f)
{
    long n = ftell(f) - 44;
    unsigned char h[44];
    unsigned int v;
    memcpy(h, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\0\0\0\0\0\0\0\0\x04\0\x10\0data\0\0\0\0", 44);
    v = (unsigned)(n + 36); memcpy(h + 4, &v, 4);
    v = FS; memcpy(h + 24, &v, 4);
    v = FS * 4; memcpy(h + 28, &v, 4);
    v = (unsigned)n; memcpy(h + 40, &v, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
    fclose(f);
}

static uint32_t seed_of(int k) { return 0x2545F491u + 0x9E3779B9u * (uint32_t)k; }

int main(void)
{
    static dr_engine e;
    static float gap[FS / 2];
    static acc_t A;
    int kit, slot, vi, fails = 0, n = 0, k;
    double worst_level = 0, worst_band = 0, worst_shape = 0;
    const int vels[2] = {127, 88};
    mkdir("build", 0777);
    mkdir("build/drums", 0777);
    printf("%-10s %-14s %3s %8s %7s %7s %7s\n", "kit", "sound", "vel", "rms dB", "level", "band", "shape");
    for (kit = 0; kit < DR_KITS; kit++) {
        char path[256];
        FILE *w;
        snprintf(path, sizeof path, "build/drums/%d-%s.wav", kit + 1, dr_kit_get(kit)->name);
        for (char *p = path + 12; *p; p++) if (*p == ' ') *p = '_';
        w = wav_open(path);
        for (slot = 0; slot < DR_SLOTS; slot++) {
            if (!(DRM_SOUND[kit][slot].flags & ~DRM_F_THERE)) continue;
            for (vi = 0; vi < 2; vi++) {
                cmp_t c;
                int bad;
                int seeds = (DRM_SOUND[kit][slot].flags & DRM_F_RJIT) ? 64 : SEEDS;   /* random tick times need more to average out */
                memset(&A, 0, sizeof A);
                for (k = 0; k < seeds; k++) {
                    dr_init(&e, FS);
                    e.rng = seed_of(k);
                    dr_set_kit(&e, kit);
                    fixed_reset((uint32_t)kit, seed_of(k));
                    clear(rl, LEN); clear(rr, LEN);
                    dr_trigger(&e, slot, vels[vi]);
                    drm_trigger((uint32_t)slot, (uint32_t)vels[vi]);
                    render_both(&e, LEN, 0);
                    acc_add(&A, LEN);
                    if (k == 0 && vi == 0 && w) {
                        wav_put(w, rl, rr, FS * 6 / 5); wav_put(w, gap, gap, FS / 4);
                        wav_put(w, fl, fr, FS * 6 / 5); wav_put(w, gap, gap, FS / 2);
                    }
                }
                c = acc_cmp(&A, LEN, seeds);
                bad = fabs(c.level) > 0.3 || c.band > 1.0 || c.shape > 1.5;
                fails += bad;
                n++;
                if (fabs(c.level) > fabs(worst_level)) worst_level = c.level;
                if (c.band > worst_band) worst_band = c.band;
                if (c.shape > worst_shape) worst_shape = c.shape;
                printf("%-10s %-14.14s %3d %8.1f %+7.3f %7.3f %7.3f%s\n", dr_kit_get(kit)->name, DRM_SOUND_NAME[kit][slot], vels[vi],
                       c.rms, c.level, c.band, c.shape, bad ? "  FAIL" : "");
            }
        }
        if (w) wav_close(w);
    }
    /* a busy bar on each kit: bass drum, snare, hats with an open hat choked by the closed one, toms, rasps */
    for (kit = 0; kit < DR_KITS; kit++) {
        static const struct { int t, slot, vel; } pat[] = {
            {0, DRM_BD, 127}, {0, DRM_CH, 100}, {1, DRM_CH, 80}, {2, DRM_OH, 110}, {3, DRM_CH, 90}, {4, DRM_SD, 127}, {4, DRM_CH, 100},
            {5, DRM_CB, 90}, {6, DRM_BD, 100}, {6, DRM_CH, 80}, {7, DRM_OH, 100}, {8, DRM_BD, 127}, {8, DRM_CY, 110}, {9, DRM_CL, 90},
            {10, DRM_LT, 110}, {10, DRM_MA, 90}, {11, DRM_HT, 110}, {12, DRM_SD, 127}, {12, DRM_TB, 100}, {13, DRM_EX, 100},
            {14, DRM_QJ, 100}, {14, DRM_CG, 90}, {15, DRM_S2, 110}, {15, DRM_RS, 100}, {15, DRM_HB, 90}, {15, DRM_LB, 90},
        };
        const int step = 5504, total = step * 16 + 44032;   /* about 120 bpm sixteenths (on 16-sample steps), then a second of tails */
        cmp_t c;
        int bad;
        memset(&A, 0, sizeof A);
        for (k = 0; k < SEEDS; k++) {
            int i, t = 0, p = 0;
            dr_init(&e, FS);
            e.rng = seed_of(k);
            dr_set_kit(&e, kit);
            fixed_reset((uint32_t)kit, seed_of(k));
            clear(rl, total); clear(rr, total);
            for (i = 0; i < 16; i++) {
                while (p < (int)(sizeof pat / sizeof pat[0]) && pat[p].t == i) {
                    dr_trigger(&e, pat[p].slot, pat[p].vel);
                    drm_trigger((uint32_t)pat[p].slot, (uint32_t)pat[p].vel);
                    p++;
                }
                render_both(&e, step, t);
                t += step;
            }
            render_both(&e, total - t, t);
            acc_add(&A, total);
        }
        c = acc_cmp(&A, total, SEEDS);
        bad = fabs(c.level) > 0.3 || c.band > 1.0 || c.shape > 1.5;
        fails += bad;
        printf("%-10s %-14s %3s %8.1f %+7.3f %7.3f %7.3f%s\n", dr_kit_get(kit)->name, "(pattern)", "", c.rms, c.level, c.band, c.shape, bad ? "  FAIL" : "");
    }
    printf("\n%d sounds x 2 velocities: worst level %+.3f dB, worst band %.3f dB, worst shape %.3f dB; %d failed; %ld overflows\n",
           n / 2, worst_level, worst_band, worst_shape, fails, overflows);
    printf("drum_tab.h: %u bytes of sounds (%u kits)\n", (unsigned)sizeof DRM_SOUND, (unsigned)DRM_KITS);
    return fails || overflows ? 1 : 0;
}
