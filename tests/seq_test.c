/* SPDX-License-Identifier: GPL-3.0-only */
/* B3+DRUM: the rhythm player (firmware/src/drum_play.h) against CB-931's (tools/cb931/rhythm.c). Both step 16 samples at a
 * time, as the FM-1 does, get the same button presses at the same steps, and every hit is noted with its step. For every
 * style, at two tempos and two swings, through a script that uses every command (intro, fill early and late in a bar, A,
 * B, a new style while playing, outro, stop), the two lists must be the same hits in the same order, each within one step.
 *   cc -O1 -Ifirmware/src -Itools/cb931 -o build/seq_test tests/seq_test.c tools/cb931/rhythm.c tools/cb931/factory_styles.c tools/cb931/drums.c -lm */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static uint32_t fm1_ticks(void) { return 0; }
#include "drum_play.h"
#include "rhythm.h"

#define FS 44100.0
#define MAXH 4096
typedef struct { int step, slot, vel; } hit_t;
static hit_t ha[MAXH], hb[MAXH];
static int na, nb, cur_step;
static void on_hit(void *u, int slot, int vel) { (void)u; if (na < MAXH) { ha[na].step = cur_step; ha[na].slot = slot; ha[na].vel = vel; na++; } }

typedef struct { int beat10; int cmd; } press_t;   /* at beat (x 10) from the start: a command (-1 - n: change to style n) */

static int run(int style, int other, double bpm, int swing, const press_t *script, int np, int *steps_out)
{
    rh_player p;
    int step, k = 0, maxsteps = (int)(FS * 60.0 / bpm * 48 / 16.0);   /* 48 beats at most */
    const double tps = bpm / 60.0 * RH_TICKS_PER_BEAT / FS * 16.0;
    rh_init(&p, rh_style_get(style));
    p.on_hit = on_hit;
    p.swing = swing / 100.0;
    memset(&P_, 0, sizeof P_);
    P_.s = drp_style_get((uint32_t)style); P_.want = (uint32_t)style; P_.next = -1; P_.part = P_.variation = DRP_MAIN_A; P_.pending = -1;
    P_.swing = (uint32_t)swing;
    na = nb = 0;
    for (step = 0; step < maxsteps; step++) {
        double beat = step * 16.0 / FS * bpm / 60.0;
        uint32_t num = (uint32_t)bpm * 12582912u, inc, q;
        while (k < np && script[k].beat10 <= beat * 10.0) {
            int c = script[k].cmd;
            if (c < 0) { rh_set_style(&p, rh_style_get(other)); drp_set_style((uint32_t)other); }
            else {
                static const int map[5] = {RH_CMD_START_STOP, RH_CMD_INTRO, RH_CMD_A, RH_CMD_B, RH_CMD_OUTRO};
                rh_command(&p, map[c]);
                drp_command((uint32_t)c);
            }
            k++;
        }
        cur_step = step;
        if (p.running) rh_advance(&p, tps, NULL);
        P_.qn = 0;
        if (P_.running) {
            inc = num / 2646000u;
            P_.rem += num % 2646000u;
            if (P_.rem >= 2646000u) { P_.rem -= 2646000u; inc++; }
            drp_advance(inc);
        }
        for (q = 0; q < P_.qn && nb < MAXH; q++) { hb[nb].step = step; hb[nb].slot = P_.qs[q]; hb[nb].vel = P_.qv[q]; nb++; }
        if (k >= np && !p.running && !P_.running) break;
    }
    *steps_out = step;
    if (na != nb) return -1;
    {                                           /* same hits, same order, each within a step */
        int i;
        for (i = 0; i < na; i++)
            if (ha[i].slot != hb[i].slot || ha[i].vel != hb[i].vel || abs(ha[i].step - hb[i].step) > 1) return i + 1;
    }
    return 0;
}

int main(void)
{
    /* every command: intro, fill mid-bar and in a bar's last beat, B, A, a new style, outro; and a plain start in B, stop */
    static const press_t script1[] = {
        {0, DRP_CMD_INTRO_FILL}, {95, DRP_CMD_INTRO_FILL}, {200, DRP_CMD_B}, {275, DRP_CMD_INTRO_FILL},
        {335, DRP_CMD_INTRO_FILL}, {420, DRP_CMD_A}, {470, -1}, {560, DRP_CMD_OUTRO},
    };
    static const press_t script2[] = {
        {0, DRP_CMD_B}, {1, DRP_CMD_START_STOP}, {130, DRP_CMD_A}, {250, DRP_CMD_START_STOP},
    };
    const double tempos[2] = {120.0, 97.0};
    const int swings[2] = {50, 66};
    int s, ti, si, fails = 0, runs = 0, hits = 0, steps;
    int n = rh_style_count();
    if (n != (int)DRS_STYLES) { printf("style count: CB-931 %d, B3+DRUM %u\n", n, DRS_STYLES); return 1; }
    for (s = 0; s < n; s++)
        for (ti = 0; ti < 2; ti++)
            for (si = 0; si < 2; si++) {
                int r1 = run(s, (s + 7) % n, tempos[ti], swings[si], script1, (int)(sizeof script1 / sizeof script1[0]), &steps);
                hits += na;
                int r2 = run(s, 0, tempos[ti], swings[si], script2, (int)(sizeof script2 / sizeof script2[0]), &steps);
                hits += na;
                runs += 2;
                if (r1 || r2) {
                    fails++;
                    printf("FAIL %-14s %5.1f bpm swing %d: script 1 %d, script 2 %d (CB-931 %d hits, B3+DRUM %d)\n",
                           rh_style_get(s)->name, tempos[ti], swings[si], r1, r2, na, nb);
                }
            }
    printf("%d runs of %d styles, %d hits: %d failed\n", runs, n, hits, fails);
    return fails ? 1 : 0;
}
