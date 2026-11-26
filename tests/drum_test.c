/* SPDX-License-Identifier: GPL-3.0-only */
/* B3+DRUM: the FM-1's integer drums (firmware/src/drum_core.h) against the Cheesebox 931's float drums
 * (tools/cb931/drums.c), every sound of every kit, at full velocity and at 88, and a busy pattern with chokes.
 * Both use the same noise sequence, so they can be compared sample by sample:
 *   level   the difference in loudness (dB)
 *   snr     the reference against what differs (dB)
 *   band    the largest difference in any octave band within 40 dB of the loudest (dB)
 * It writes build/drums/<kit>.wav: each sound twice, CB-931's then the FM-1's, for listening.
 * The busy pattern is judged on level and bands (see there).
 *   cc -O1 -Ifirmware/src -Itools/cb931 -o build/drum_test tests/drum_test.c tools/cb931/drums.c -lm && build/drum_test */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "drums.h"
#include "drum_core.h"

#define FS 44100
#define LEN (FS * 3 / 2)                        /* 1.5 s a sound */
#define BLK 256

static float rl[LEN * 4], rr[LEN * 4], fl[LEN * 4], fr[LEN * 4];
static void clear(float *a, int n) { while (n-- > 0) *a++ = 0.0f; }

static void fixed_reset(uint32_t kit)
{
    memset(drm_v, 0, sizeof drm_v);
    memset(drm_age, 0, sizeof drm_age);
    drm_agec = 0;
    drm_rng = 0x2545F491u;
    drm_set_kit(kit);
}

static void render_both(dr_engine *e, int n, int32_t *il, int32_t *ir, int off)
{
    int i, b;
    for (i = off; i < off + n; i += BLK) {
        b = off + n - i < BLK ? off + n - i : BLK;
        memset(il, 0, sizeof(int32_t) * BLK);
        memset(ir, 0, sizeof(int32_t) * BLK);
        dr_render(e, rl + i, rr + i, b);
        drm_render(il, ir, (uint32_t)b);
        for (int k = 0; k < b; k++) { fl[i + k] = il[k] / 8388608.0f; fr[i + k] = ir[k] / 8388608.0f; }
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
#define NB 10                                   /* octaves from 31.25 Hz to 16 kHz */
static void bands(const float *l, const float *r, int n, double *out)
{
    static double re[65536], im[65536];
    int N = 65536, i;
    for (i = 0; i < N; i++) { re[i] = i < n ? 0.5 * (l[i] + r[i]) : 0.0; im[i] = 0.0; }
    fft(re, im, N);
    for (i = 0; i < NB; i++) out[i] = 1e-30;
    for (i = 1; i < N / 2; i++) {
        double f = (double)i * FS / N, p = re[i] * re[i] + im[i] * im[i];
        int b = (int)floor(log2(f / 22.1));
        if (b >= 0 && b < NB) out[b] += p;
    }
}

typedef struct { double level, snr, band, rms; } cmp_t;
static cmp_t compare(int off, int n)
{
    double sr = 0, sf = 0, sd = 0, br[NB], bf[NB], bmax = 0;
    cmp_t c;
    int i;
    for (i = off; i < off + n; i++) {
        double a = rl[i], b = rr[i], x = fl[i], y = fr[i];
        sr += a * a + b * b; sf += x * x + y * y; sd += (a - x) * (a - x) + (b - y) * (b - y);
    }
    c.rms = 10.0 * log10(sr / n / 2 + 1e-30);
    c.level = 10.0 * log10((sf + 1e-30) / (sr + 1e-30));
    c.snr = sd > 0 ? 10.0 * log10(sr / sd) : 200.0;
    bands(rl + off, rr + off, n, br);
    bands(fl + off, fr + off, n, bf);
    for (i = 0; i < NB; i++) if (br[i] > bmax) bmax = br[i];
    c.band = 0;
    for (i = 0; i < NB; i++)
        if (br[i] > bmax * 1e-4) { double d = fabs(10.0 * log10(bf[i] / br[i])); if (d > c.band) c.band = d; }
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

int main(void)
{
    static dr_engine e;
    static int32_t il[BLK], ir[BLK];
    static float gap[FS / 4];
    int kit, slot, vi, fails = 0, n = 0;
    double worst_level = 0, worst_snr = 200, worst_band = 0;
    const int vels[2] = {127, 88};
    mkdir("build", 0777);
    mkdir("build/drums", 0777);
    printf("%-10s %-14s %3s %8s %7s %7s %7s\n", "kit", "sound", "vel", "rms dB", "level", "snr", "band");
    for (kit = 0; kit < DR_KITS; kit++) {
        char path[256];
        FILE *w;
        snprintf(path, sizeof path, "build/drums/%d-%s.wav", kit + 1, dr_kit_get(kit)->name);
        for (char *p = path + 12; *p; p++) if (*p == ' ') *p = '_';
        w = wav_open(path);
        for (slot = 0; slot < DR_SLOTS; slot++) {
            if (!DRM_SOUND[kit][slot].flags) continue;
            for (vi = 0; vi < 2; vi++) {
                cmp_t c;
                int bad;
                dr_init(&e, FS);
                dr_set_kit(&e, kit);
                fixed_reset((uint32_t)kit);
                clear(rl, LEN); clear(rr, LEN);
                dr_trigger(&e, slot, vels[vi]);
                drm_trigger((uint32_t)slot, (uint32_t)vels[vi]);
                render_both(&e, LEN, il, ir, 0);
                c = compare(0, LEN);
                bad = fabs(c.level) > 0.1 || c.snr < 40.0 || c.band > 0.5;
                fails += bad;
                n++;
                if (fabs(c.level) > fabs(worst_level)) worst_level = c.level;
                if (c.snr < worst_snr) worst_snr = c.snr;
                if (c.band > worst_band) worst_band = c.band;
                printf("%-10s %-14.14s %3d %8.1f %+7.3f %7.1f %7.3f%s\n", dr_kit_get(kit)->name, DRM_SOUND_NAME[kit][slot], vels[vi],
                       c.rms, c.level, c.snr, c.band, bad ? "  FAIL" : "");
                if (vi == 0 && w) {
                    wav_put(w, rl, rr, FS * 6 / 5); wav_put(w, gap, gap, FS / 4);
                    wav_put(w, fl, fr, FS * 6 / 5); wav_put(w, gap, gap, FS / 2);
                }
            }
        }
        if (w) wav_close(w);
    }
    /* a busy bar on each kit: bass drum, snare, hats with an open hat choked by the closed one, toms, a rasp */
    for (kit = 0; kit < DR_KITS; kit++) {
        static const struct { int t, slot, vel; } pat[] = {
            {0, DRM_BD, 127}, {0, DRM_CH, 100}, {1, DRM_CH, 80}, {2, DRM_OH, 110}, {3, DRM_CH, 90}, {4, DRM_SD, 127}, {4, DRM_CH, 100},
            {5, DRM_CB, 90}, {6, DRM_BD, 100}, {6, DRM_CH, 80}, {7, DRM_OH, 100}, {8, DRM_BD, 127}, {8, DRM_CY, 110}, {9, DRM_CL, 90},
            {10, DRM_LT, 110}, {10, DRM_MA, 90}, {11, DRM_HT, 110}, {12, DRM_SD, 127}, {12, DRM_TB, 100}, {13, DRM_EX, 100},
            {14, DRM_QJ, 100}, {14, DRM_CG, 90}, {15, DRM_S2, 110}, {15, DRM_RS, 100}, {15, DRM_HB, 90}, {15, DRM_LB, 90},
        };
        const int step = FS / 8, total = step * 16 + FS;   /* 120 bpm sixteenths, then a second of tails */
        int i, t = 0, p = 0;
        cmp_t c;
        dr_init(&e, FS);
        dr_set_kit(&e, kit);
        fixed_reset((uint32_t)kit);
        clear(rl, total); clear(rr, total);
        for (i = 0; i < 16; i++) {
            while (p < (int)(sizeof pat / sizeof pat[0]) && pat[p].t == i) {
                dr_trigger(&e, pat[p].slot, pat[p].vel);
                drm_trigger((uint32_t)pat[p].slot, (uint32_t)pat[p].vel);
                p++;
            }
            render_both(&e, step, il, ir, t);
            t += step;
        }
        render_both(&e, total - t, il, ir, t);
        c = compare(0, total);
        {
            /* judged on loudness and spectrum only: a voice that ends in a different block (a near-tie with CB-931's
             * end test, e.g. a resonator crossing zero) puts the next hits in other voices, so the noise is drawn in
             * another order: the same sound, not the same samples. The snr is shown for information. */
            int bad = fabs(c.level) > 0.1 || c.band > 0.5;
            fails += bad;
            printf("%-10s %-14s %3s %8.1f %+7.3f %7.1f %7.3f%s\n", dr_kit_get(kit)->name, "(pattern)", "", c.rms, c.level, c.snr, c.band, bad ? "  FAIL" : "");
        }
    }
    printf("\n%d sounds x 2 velocities: worst level %+.3f dB, worst snr %.1f dB, worst band %.3f dB; %d failed\n",
           n / 2, worst_level, worst_snr, worst_band, fails);
    printf("drum_tab.h: %u bytes of sounds (%u kits)\n", (unsigned)sizeof DRM_SOUND, (unsigned)DRM_KITS);
    return fails ? 1 : 0;
}
