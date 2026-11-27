/* SPDX-License-Identifier: GPL-3.0-only */
/* B3+DRUM for the M-VAVE FM-1: R-Jeb's B3 ORGAN (a fixed-point tonewheel organ on Dinghy's platform) with a rhythm section
 * from the Cheesebox 931 (CB-931) on the way. Fork by Skipper.
 * B3 ORGAN for the M-VAVE FM-1: a fixed-point tonewheel organ on Dinghy's platform.
 *
 * The model follows setBfree (pantherb/setBfree, GPL-2.0-or-later, so it combines with Dinghy's GPL-3.0):
 *   - 91 tonewheels with the 60 Hz gear ratios of setBfree's tonegen.c (tables from tools/gen_organ.py)
 *   - 9 drawbars with setBfree's footage-to-wheel mapping and its linear level (setting / 8)
 *   - percussion on the 2nd or 3rd harmonic: single trigger, fast/slow decay (-60 dB in 1 s / 4 s), soft/normal,
 *     the 1' drawbar muted while it is on
 *   - a vibrato/chorus scanner at 7.25 Hz (V1..V3, C1..C3), an overdrive and a "Leslie-lite"
 * It is NOT setBfree itself: no float, no malloc, no whirl convolution, three manuals (upper, lower, pedal).
 *
 * B3+DRUM 0.2: one manual (an attached keyboard plays it), the CR-78 rhythm section always to hand. The same on every page:
 *   keys        play the organ (key 0 = F3)            MASTER   volume (platform.c)        PRESETS  setBfree's registrations
 *   PLAY        drums start/stop    REC  intro (stopped) / fill (running)    OCT- / OCT+  main A / B    SEQ  outro
 *   FX          vibrato on/off      LFO  Leslie stop/slow/fast    SEL  overdrive on/off
 *   ENV         percussion on/off   ARP  2nd/3rd    EDIT  fast/slow    GLO  soft/normal    HOME  next page    SAVE  free
 *   KNOB1..4    rhythm, tempo, drum level, vibrato/chorus type (V1 C1 V2 C2 V3 C3)
 *   SELECT      pick on the page    ALGORITHM  set it (MAIN: the drawbars; ORGAN, DRUMS: a list; EQ: bass, middle, treble)
 *   USB-MIDI    and the TRS MIDI IN jack: every channel plays the organ but 10 (the KeyLab's pads: silent); program change
 *               (setBfree's default.pgm); the KeyLab Essential mk3 on CB-931's User Program (kl_cc); setBfree's controllers
 *               (CC 1, 7, 11, 30, 31, 64..66, 91, 92, 95, NRPN; the drawbars' 70..78, the percussion's 80..83 and the drive's 93
 *               give way to the KeyLab's)
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division. */
#define APP_NAME "B3+DRUM"                      /* the rescue and the crash screen say this */
#include "platform.c"
#include "organ_tab.h"

#ifdef DINGHY_HOST
static uint32_t fm1_ticks(void) { return 0; }
#define FM1_TICKS_PER_US 24u
#endif

#define ORGAN_POOL __attribute__((section(".pool")))   /* the reverb and Leslie delay lines: zeroed at boot */
#include "drum_play.h"                         /* B3+DRUM: the drums (ORGAN_DRUMS in organ_core.h) */
#include "organ_core.h"
#include "organ_logo.h"                        /* the logo (tools/gen_logo.py) */

/* ---- the screen */
#define C_ACC RGB(255, 190, 90)                 /* tungsten */
#define C_OFF RGB(110, 110, 110)
#define C_BAR RGB(230, 230, 230)
static const uint16_t BARC[NB] = {RGB(190, 120, 60), RGB(190, 120, 60), RGB(235, 235, 235), RGB(235, 235, 235),
                                  RGB(90, 90, 100), RGB(235, 235, 235), RGB(90, 90, 100), RGB(90, 90, 100),
                                  RGB(235, 235, 235)};
#define C_TRK RGB(34, 34, 34)

static char *sp(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *su(char *p, uint32_t v)
{
    char t[10];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    return p;
}

#define C_RED RGB(230, 70, 60)
#define C_GRN RGB(120, 220, 140)
static void draw_battery(uint32_t st)           /* st: bars 0 .. 4, bit 3 = a USB host has it (a bolt instead) */
{
    static const char *const BOLT[7] = {"...XX.", "..XX..", ".XX...", "XXXXXX", "...XX.", "..XX..", ".XX..."};
    const uint32_t x = 212, y = 222, bars = st & 7u;
    uint32_t r, c, k;
    uint16_t line = bars ? C_BAR : C_RED;
    lcd_fill(x, y, 28, 16, C_BLACK);
    if (st & 8u) {
        for (r = 0; r < 7u; r++)
            for (c = 0; c < 6u; c++)
                if (BOLT[r][c] == 'X') lcd_fill(x + 8u + c * 2u, y + 1u + r * 2u, 2, 2, C_GRN);
        return;
    }
    lcd_fill(x + 1u, y + 2u, 24, 1, line);      /* the cell: outline, the nub, then a bar for each step */
    lcd_fill(x + 1u, y + 13u, 24, 1, line);
    lcd_fill(x + 1u, y + 2u, 1, 12, line);
    lcd_fill(x + 24u, y + 2u, 1, 12, line);
    lcd_fill(x + 25u, y + 5u, 2, 6, line);
    for (k = 0; k < bars; k++)
        lcd_fill(x + 3u + k * 5u, y + 4u, 4, 8, bars <= 1u ? C_RED : C_ACC);
}

/* ---- B3+DRUM 0.2: one manual, the drums always to hand, big type (FONT_M, Terminus 24 px; pop-ups FONT_XL, 32 px).
 * Six pages, HOME (or the KeyLab's main encoder click) steps through them:
 *   0 MAIN     the drawbars (SELECT picks one, ALGORITHM sets it), the switches, a drum strip (beats, rhythm, tempo), the program
 *   1 ORGAN    the organ's settings as a list (SELECT picks, ALGORITHM sets): vibrato, Leslie, overdrive, reverb, percussion, volume
 *   2 DRUMS    the beats of the bar, then rhythm, tempo, swing, level (SELECT picks, ALGORITHM sets)
 *   3 EQ       the output equalizer's bass, middle, treble
 *   4 TEST 1, 5 TEST 2   the load figures, the limiters, MIDI
 * The header's right side shows what the drums play, on every page. Whatever the buttons and knobs set that the page does not show
 * comes up big for a moment (popup()), over the drawbars' place; SELECT or ALGORITHM puts it away at once. Each page is built from
 * bands (a canvas of 240 x h, at most 64 rows on the FM-1: taller bands are drawn in strips with cv_oy), drawn only when what they show
 * has changed. */
#define FM (&FONT_M)
#define FW 12                                   /* FONT_M is 12 x 24, fixed */
enum { PG_MAIN = 0, PG_ORGAN, PG_DRUMS, PG_EQ, PG_TEST, PG_TEST2, NSCREENS };
#define SCR_TEST PG_TEST                        /* (and PG_TEST2) */
#define PGM_MAIN (1u << PG_MAIN)                /* page masks: where a setting shows (no pop-up there) */
#define PGM_ORGAN (1u << PG_ORGAN)
#define PGM_DRUMS (1u << PG_DRUMS)
#define PGM_EQ (1u << PG_EQ)
#define Y_HDR 0
#define Y_BARS 28                               /* the drawbars (or the equalizer): 92 rows */
#define H_BARS 92
#define CAP 26                                  /* a drawbar's cap: the digit and a pixel round it */
#define STEP 8                                  /* the cap goes down 8 px a step: 0..8 */
#define TRACK (CAP + 8 * STEP)
#define Y_CTRL 124                              /* two rows of switches, 28 apart */
#define H_ROW 28
#define Y_STRIP 180                             /* MAIN: the drum strip */
#define Y_LIST 28                               /* ORGAN and TEST: rows of text, 26 apart */
#define H_LIST 26
#define LIST_VIS 7u                             /* ORGAN: the rows that fit above the program */
#define Y_PRESET 212
static uint32_t screen, sel0 = 2, eq_sel, org_sel, drp_field;   /* the drawbar picked; the equalizer's band; the ORGAN row; the DRUMS field */
#ifdef DINGHY_HOST
#define CORES() 2u
#else
#define CORES() (fm1_c1_on ? 2u : 1u)                  /* the second core runs (fm1_cpu1.h), or it was not there / was given up */
#endif
static uint32_t load_peak, load_now, load_wheels, ld_org, ld_od, ld_rv, ld_le, ld_pk, ld_g, ld_go, ld_q, ld_dr, ld_dv;
static uint32_t midi_ch, midi_kind, midi_cnt, midi_t, midi_d1;   /* the last message that came in: channel 1..16, kind 0 none 1 on 2 off 3 CC 4 PC 5 other */

/* the volume: 1 dB a step, -48..+6 dB, under the MASTER knob (KeyLab knob 1; ORGAN's last row) */
#define VOL_MIN (-48)
#define VOL_MAX 6
static const uint16_t VOL_Q12[VOL_MAX - VOL_MIN + 1] = {
    16, 18, 21, 23, 26, 29, 33, 37, 41, 46, 52, 58, 65, 73, 82, 92, 103, 115, 130, 145, 163, 183, 205, 230, 258, 290, 325, 365,
    410, 460, 516, 579, 649, 728, 817, 917, 1029, 1154, 1295, 1453, 1631, 1830, 2053, 2303, 2584, 2900, 3254, 3651, 4096, 4596,
    5157, 5786, 6492, 7284, 8173};
static int32_t vol_db;
static void vol_set(int32_t db)
{
    vol_db = db < VOL_MIN ? VOL_MIN : db > VOL_MAX ? VOL_MAX : db;
    organ_vol = VOL_Q12[vol_db - VOL_MIN];
}

static uint32_t hmix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t colx(uint32_t i) { return 3u + 26u * i; }
static void cv_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c); cv_rect(x, y + h - 1, w, 1, c); cv_rect(x, y, 1, h, c); cv_rect(x + w - 1, y, 1, h, c);
}
static int32_t chip(int32_t x, int32_t y, const char *t, int on)     /* a switch: boxed, tungsten when it is on */
{
    int32_t w = text_w(FM, t) + 8;
    cv_rect(x + 1, y + 1, w - 2, 24, C_BLACK);
    cv_box(x, y, w, 26, on ? C_ACC : C_OFF);
    cv_text(x + 4, y + 1, FM, t, on ? C_ACC : C_BAR);
    return x + w;
}
static void draw_bars(int32_t y0, const uint8_t *v, uint32_t selected)
{
    uint32_t k;
    for (k = 0; k < NB; k++) {
        int32_t x = (int32_t)colx(k), cy = y0 + (int32_t)v[k] * STEP;
        char d[2] = {(char)('0' + v[k]), 0};
        cv_rect(x + 7, y0, 10, TRACK, C_TRK);
        if (v[k]) cv_rect(x + 9, y0, 6, cy - y0, RGB(70, 70, 70));
        cv_rect(x, cy, 24, CAP, BARC[k]);
        if (k == selected) { cv_box(x - 1, cy - 1, 26, CAP + 2, C_ACC); cv_box(x - 2, cy - 2, 28, CAP + 4, C_ACC); }
        cv_text(x + 6, cy + 1, FM, d, BARC[k] > RGB(150, 150, 150) ? C_BLACK : C_BAR);
    }
}
/* the equalizer: three sliders, up is more, the middle 0 dB */
#define EQ_X(k) (28 + 72 * (int32_t)(k))
#define EQ_CAP 10
static const char *const EQN[3] = {"BASS", "MID", "TREB"};
static void eq_label(char *t, uint32_t g)       /* gain index -> "+4.5", "-9", "0" */
{
    uint32_t a = g >= 18u ? g - 18u : 18u - g;
    if (a) *t++ = g > 18u ? '+' : '-';
    if (a / 2u >= 10u) *t++ = '1';
    *t++ = (char)('0' + a / 2u % 10u);
    if (a & 1u) { *t++ = '.'; *t++ = '5'; }
    *t = 0;
}
static void draw_eq(int32_t y0, uint32_t selected)
{
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        int32_t x = EQ_X(k), g = (int32_t)eq_p[2u * k], way = H_BARS - 2 - EQ_CAP, cy = y0 + 1 + (36 - g) * way / 36,
                mid = y0 + 1 + way / 2 + EQ_CAP / 2;
        cv_rect(x + 15, y0, 10, H_BARS, C_TRK);
        cv_rect(x - 4, mid, 48, 1, C_OFF);                       /* 0 dB: wider than the cap, so it shows beside it */
        if (cy + EQ_CAP / 2 < mid) cv_rect(x + 17, cy + EQ_CAP / 2, 6, mid - cy - EQ_CAP / 2, RGB(70, 70, 70));
        else if (cy + EQ_CAP / 2 > mid) cv_rect(x + 17, mid + 1, 6, cy + EQ_CAP / 2 - mid, RGB(70, 70, 70));
        cv_rect(x, cy, 40, EQ_CAP, g == 18 ? RGB(120, 120, 120) : C_BAR);
        if (k == selected) { cv_box(x - 1, cy - 1, 42, EQ_CAP + 2, C_ACC); cv_box(x - 2, cy - 2, 44, EQ_CAP + 4, C_ACC); }
    }
}

/* what the drums play, short: "STOP", "A", "FILL", "A>B", "B>END" */
static const char *const PARTN[DRP_PARTS] = {"INTRO", "A", "FILL", "B", "FILL", "OUTRO"};
static const char *const PARTS[DRP_PARTS] = {"INTRO", "A", "FILL", "B", "FILL", "END"};   /* short, with a change waiting */
static char *drum_state(char *q)
{
    if (!drp_running) return sp(q, "STOP");
    if (drp_pending < DRP_PARTS) return sp(sp(sp(q, drp_part == DRP_OUTRO ? "OUTRO" : PARTS[drp_part % DRP_PARTS]), ">"), PARTS[drp_pending]);
    return sp(q, PARTN[drp_part % DRP_PARTS]);
}
static void band_header(uint32_t scr)
{
    static const char *const NAME[NSCREENS] = {"MAIN", "ORGAN", "DRUMS", "EQ", "TEST 1", "TEST 2"};
    char t[24], *q = t;
    cv_begin(240, 26, C_BLACK);
    cv_text(4, 1, FM, NAME[scr < NSCREENS ? scr : 0], C_OFF);
    q = drum_state(q);
    *q = 0;
    cv_text(236 - text_w(FM, t), 1, FM, t, drp_running ? C_ACC : C_OFF);
    cv_rect(0, 25, 240, 1, C_TRK);
    cv_blit(0, Y_HDR);
}
static void band_bars(uint32_t scr)             /* MAIN: the drawbars; EQ: the equalizer. In strips. */
{
    uint32_t o;
    for (o = 0; o < H_BARS; o += 46u) {
        cv_begin(240, 46, C_BLACK);
        cv_oy = -(int32_t)o;
        if (scr == PG_EQ) draw_eq(0, eq_sel);
        else draw_bars(1, (const uint8_t *)P.db, sel0);
        cv_oy = 0;
        cv_blit(0, Y_BARS + o);
    }
}
static const char *const VIB[6] = {"V1", "C1", "V2", "C2", "V3", "C3"};
static const char *const LES[3] = {"STOP", "SLOW", "FAST"};
static void band_ctrl(uint32_t scr, uint32_t r)  /* MAIN: the switches under the drawbars; EQ: the names and gains */
{
    int32_t x;
    char t[24], *q;
    cv_begin(240, H_ROW, C_BLACK);
    if (scr == PG_EQ) {
        uint32_t k;
        for (k = 0; k < 3u; k++) {              /* row 0 the names, row 1 the gains (dB) */
            if (r == 0) { q = sp(t, EQN[k]); *q = 0; } else eq_label(t, eq_p[2u * k]);
            cv_text(EQ_X(k) + 20 - text_w(FM, t) / 2, 1, FM, t, eq_sel == k ? C_ACC : (r ? C_BAR : C_OFF));
        }
    } else if (r == 0) {                        /* vibrato (its type, lit when on), the Leslie, the overdrive */
        x = chip(0, 0, VIB[P.vtype % 6u], P.vup) + 6;
        q = sp(sp(t, "LES "), LES[P.les % 3u]); *q = 0;
        x = chip(x, 0, t, P.les != 0) + 6;
        chip(x, 0, "OD", P.od);
    } else {                                    /* percussion */
        x = chip(0, 0, "PERC", P.perc) + 4;
        x = chip(x, 0, P.perc3 ? "3RD" : "2ND", P.perc) + 4;
        x = chip(x, 0, P.fast ? "FAST" : "SLOW", P.perc) + 4;
        chip(x, 0, P.soft ? "SOFT" : "NORM", P.perc);
    }
    cv_blit(0, Y_CTRL + H_ROW * r);
}
static void beat_boxes(int32_t x, int32_t y, int32_t w, int32_t h)   /* the beats of the bar: the one playing filled */
{
    uint32_t i, nb = drp_beats < 3u ? 3u : drp_beats > 4u ? 4u : drp_beats;
    for (i = 0; i < nb; i++) {
        int32_t bx = x + (w + 4) * (int32_t)i;
        if (drp_running && i == drp_beat) cv_rect(bx, y, w, h, i == 0 ? C_ACC : C_BAR);
        else cv_box(bx, y, w, h, drp_running ? C_OFF : C_TRK);
    }
}
static void band_strip(void)                    /* MAIN: the beats, the rhythm (K1), the tempo (K2) */
{
    char t[40], *q = t;
    int32_t room;
    cv_begin(240, H_ROW, C_BLACK);
    cv_rect(0, 0, 240, 1, C_TRK);
    beat_boxes(2, 5, 14, 20);
    q = su(q, drp_bpm); *q = 0;
    cv_text(238 - text_w(FM, t), 3, FM, t, C_BAR);
    room = 238 - text_w(FM, t) - 8 - 78;
    q = sp(t, drp_style_get(drp_style)->name); *q = 0;
    while (q > t && text_w(FM, t) > room) *--q = 0;
    cv_text(78, 3, FM, t, C_ACC);
    cv_blit(0, Y_STRIP);
}

/* ---- the DRUMS page: the beats of the bar and the part (row 0), then four fields (SELECT picks, ALGORITHM sets) */
#define DRP_FIELDS 4u
#define Y_DRUMS 30
#define H_DRUMS 30
static char *level_txt(char *q, uint32_t lv)    /* "+6 dB", "-1.5 dB", "OFF" */
{
    int32_t h;                                  /* half dB x 3: (lv - 16) x 1.5 dB */
    if (!lv) return sp(q, "OFF");
    h = ((int32_t)lv - (int32_t)DRP_LEVEL_0DB) * 3;   /* in half dB */
    if (h) *q++ = h < 0 ? '-' : '+';
    if (h < 0) h = -h;
    q = su(q, (uint32_t)h / 2u);
    if (h & 1) q = sp(q, ".5");
    return sp(q, " dB");
}
static void band_drums(uint32_t r)              /* r 0: beats and part; 1..4: rhythm, tempo, swing, level */
{
    char t[40], *q = t;
    cv_begin(240, H_DRUMS, C_BLACK);
    if (r == 0) {
        beat_boxes(4, 2, 26, 24);
        if (!drp_running) q = sp(q, drp_part == DRP_MAIN_B ? "B" : "A");
        else q = drum_state(q);
        *q = 0;
        cv_text(236 - text_w(FM, t), 2, FM, t, drp_running ? C_ACC : C_OFF);
    } else {
        static const char *const LAB[DRP_FIELDS] = {"", "TEMPO", "SWING", "LEVEL"};
        uint32_t f = r - 1u, on = f == drp_field;
        switch (f) {
        case 0: q = sp(sp(su(q, drp_style + 1u), " "), drp_style_get(drp_style)->name); break;
        case 1: q = su(q, drp_bpm); break;
        case 2: q = drp_swing <= 50u ? sp(q, "OFF") : sp(su(q, drp_swing), " %"); break;
        default: q = level_txt(q, drp_level); break;
        }
        *q = 0;
        if (on) cv_box(0, 0, 240, H_DRUMS - 2, C_ACC);
        if (f) cv_text(6, 2, FM, LAB[f], on ? C_ACC : C_OFF);
        cv_text(f ? 6 + 6 * FW : 6, 2, FM, t, on ? C_ACC : C_BAR);
    }
    cv_blit(0, Y_DRUMS + H_DRUMS * r);
}

/* ---- the ORGAN page: a list, SELECT picks a row (it scrolls), ALGORITHM sets it */
enum { OR_VIB = 0, OR_VIBON, OR_LES, OR_OD, OR_DRIVE, OR_REV, OR_PERC, OR_HARM, OR_DECAY, OR_PVOL, OR_VOL, OR_N };
static const uint8_t REVS[9] = {0, 1, 2, 4, 8, 16, 32, 64, 127};   /* reverb mix, like CC 91: a doubling a step, fine at the bottom */
static uint32_t rev_step(uint32_t rev, int32_t e)
{
    int32_t j = 0;
    while (j < 8 && REVS[j + 1] <= rev) j++;    /* the step at or below the value (CC 91 may sit between) */
    if (REVS[j] < rev && e < 0) e++;            /* between two steps: down lands on the one below, up on the next */
    j += e;
    return REVS[j < 0 ? 0 : j > 8 ? 8 : j];
}
static uint8_t clampu(int32_t v, int32_t hi) { return (uint8_t)(v < 0 ? 0 : v > hi ? hi : v); }
static void org_set(uint32_t r, int32_t e)
{
    switch (r) {
    case OR_VIB: P.vtype = clampu((int32_t)P.vtype + e, 5); break;
    case OR_VIBON: P.vup = e > 0 ? 1u : 0u; break;
    case OR_LES: P.les = clampu((int32_t)P.les + e, 2); break;          /* (0 stop, 1 slow, 2 fast) */
    case OR_OD: P.od = e > 0 ? 1u : 0u; break;
    case OR_DRIVE: P.drv = clampu((int32_t)P.drv + e, OD_DRIVE_MAX); break;
    case OR_REV: P.rev = (uint8_t)rev_step(P.rev, e); break;
    case OR_PERC: P.perc = e > 0 ? 1u : 0u; break;
    case OR_HARM: P.perc3 = e > 0 ? 1u : 0u; break;
    case OR_DECAY: P.fast = e > 0 ? 1u : 0u; break;
    case OR_PVOL: P.soft = e > 0 ? 1u : 0u; break;
    default: vol_set(vol_db + e); break;
    }
}
static char *org_txt(char *q, uint32_t r)
{
    switch (r) {
    case OR_VIB: return sp(q, VIB[P.vtype % 6u]);
    case OR_VIBON: return sp(q, P.vup ? "ON" : "OFF");
    case OR_LES: return sp(q, LES[P.les % 3u]);
    case OR_OD: return sp(q, P.od ? "ON" : "OFF");
    case OR_DRIVE: return su(q, P.drv);
    case OR_REV: return su(q, P.rev);
    case OR_PERC: return sp(q, P.perc ? "ON" : "OFF");
    case OR_HARM: return sp(q, P.perc3 ? "3RD" : "2ND");
    case OR_DECAY: return sp(q, P.fast ? "FAST" : "SLOW");
    case OR_PVOL: return sp(q, P.soft ? "SOFT" : "NORMAL");
    default:
        if (vol_db < 0) *q++ = '-'; else if (vol_db > 0) *q++ = '+';
        return sp(su(q, (uint32_t)(vol_db < 0 ? -vol_db : vol_db)), " dB");
    }
}
static uint32_t org_top(void) { return org_sel < LIST_VIS ? 0u : org_sel - LIST_VIS + 1u; }   /* the first row shown */
static uint32_t org_vis(uint32_t r) { return r >= org_top() && r < org_top() + LIST_VIS ? PGM_ORGAN : 0u; }   /* ORGAN shows row r */
static void band_org(uint32_t row)              /* row on screen 0..LIST_VIS-1 */
{
    static const char *const LAB[OR_N] = {"VIBRATO", "VIB ON", "LESLIE", "OVERDRV", "DRIVE", "REVERB", "PERC", "HARMONIC",
                                          "DECAY", "PERC VOL", "VOLUME"};
    char t[24], *q = t;
    uint32_t r = org_top() + row, on = r == org_sel;
    cv_begin(240, H_LIST, C_BLACK);
    if (r < OR_N) {
        q = org_txt(q, r); *q = 0;
        if (on) cv_box(0, 0, 240, H_LIST, C_ACC);
        cv_text(6, 1, FM, LAB[r], on ? C_ACC : C_OFF);
        cv_text(6 + 9 * FW, 1, FM, t, on ? C_ACC : C_BAR);
    }
    cv_blit(0, Y_LIST + H_LIST * row);
}

/* ---- the TEST pages: rows of text (8 each) */
#define TEST_ROWS 8u
static uint32_t list_vals(uint32_t scr, uint32_t r, uint32_t *a, uint32_t *b)   /* what row r shows; returns 1 for a warning */
{
    *a = *b = 0;
    switch ((scr - SCR_TEST) * TEST_ROWS + r) {
    case 0: *a = load_now; return 0;
    case 1: *a = load_peak; return 0;
    case 2: *a = ld_org; return 0;
    case 3: *a = ld_od; return 0;
    case 4: *a = ld_rv; return 0;
    case 5: *a = ld_le; return 0;
    case 6: *a = drp_running ? ld_dr : 0u; *b = drp_running; return 0;
    case 7: *a = ld_dv; *b = drp_running; return 0;
    case 8: *a = load_wheels; return 0;
    case 9: *a = CORES(); return CORES() != 2u;
    case 10: *a = ld_pk; return ld_pk > 100u;
    case 11: *a = ld_g; return ld_g < 100u;
    case 12: *a = ld_q; return ld_q > 100u;
    case 13: *a = ld_go; return ld_go < 100u;
    case 14: *a = midi_ch * 16u + midi_kind; *b = midi_d1; return 0;
#if FELUCCA_UART
    case 15: *a = um.drops + um.resync; return um.drops + um.resync != 0u;
#endif
    }
    return 0;
}
static void band_list(uint32_t scr, uint32_t r)
{
    char t[40], *q = t;
    uint32_t a, b, warn = list_vals(scr, r, &a, &b);
    cv_begin(240, H_LIST, C_BLACK);
    switch ((scr - SCR_TEST) * TEST_ROWS + r) {
    case 0: q = sp(su(sp(q, "LOAD "), a), " us"); break;
    case 1: q = sp(su(sp(q, "PEAK "), a), " us"); break;
    case 2: q = sp(su(sp(q, "ORGAN "), a), " us"); break;
    case 3: q = sp(su(sp(q, "PREAMP "), a), " us"); break;
    case 4: q = sp(su(sp(q, "REVERB "), a), " us"); break;
    case 5: q = sp(su(sp(q, "LESLIE "), a), " us"); break;
    case 6: q = b ? sp(su(sp(q, "DRUMS "), a), " us") : sp(q, "DRUMS OFF (PLAY)"); break;
    case 7: q = b ? su(sp(q, "VOICES "), a) : sp(q, "VOICES -"); break;
    case 8: q = su(sp(q, "WHEELS "), a); break;
    case 9: q = su(sp(q, "CORES "), a); break;
    case 10: q = sp(su(sp(q, "WHEEL SUM "), a > 999u ? 999u : a), " %"); break;
    case 11: q = sp(su(sp(q, "SUM LIMIT "), a), " %"); break;
    case 12: q = sp(su(sp(q, "OUT PEAK "), a > 999u ? 999u : a), " %"); break;
    case 13: q = sp(su(sp(q, "OUT LIMIT "), a), " %"); break;
    case 14: {
        static const char *const KN[6] = {"--", "ON", "OFF", "CC", "PC", "OTHER"};
        q = a & 15u ? sp(su(sp(q, "MIDI "), a >> 4), " ") : sp(q, "MIDI - ");
        q = sp(q, KN[(a & 15u) < 6u ? a & 15u : 5u]);
        if ((a & 15u) == 3u || (a & 15u) == 4u || (a & 15u) == 1u) q = su(sp(q, " "), b);
        break; }
    default: q = su(sp(q, "MIDI ERRORS "), a > 99u ? 99u : a); break;
    }
    *q = 0;
    cv_text(4, 1, FM, t, warn ? C_ACC : C_BAR);
    cv_blit(0, Y_LIST + H_LIST * r);
}

/* ---- the logo: at the start, for LOGO_MS or until the first key, knob or button (which still does its job) */
#define LOGO_MS 2500u
static void draw_logo(void)                     /* in strips as high as the canvas holds (CV_MAX: 64 rows on the FM-1, more on the PC) */
{
    uint32_t r0, i = 0, left = 0, c = 0;
    for (r0 = 0; r0 < LOGO_H; r0 += cv_h) {
        uint32_t p = 0, n, h = LOGO_H - r0;
        if (h > CV_MAX / 240u) h = CV_MAX / 240u;
        cv_begin(240, h, C_BLACK);
        n = 240u * cv_h;                        /* (never more than the canvas: cv_begin clamps cv_h) */
        while (p < n) {
            if (!left) {
                if (i >= sizeof LOGO_RLE) break;
                c = LOGO_RLE[i] >> 4; left = (LOGO_RLE[i] & 15u) + 1u; i++;
            }
            for (; left && p < n; left--) cv_px[p++] = swap16(LOGO_PAL[c]);
        }
        if (r0 == 0) cv_text(6, 4, FM, "v" APP_VER, RGB(150, 128, 104));
        cv_blit(0, r0);
    }
}

static void band_preset(void)
{
    char s[40], *p = s;
    if (P.pgm && pgm_name(P.pgm)) { p = su(p, P.pgm); p = sp(p, " "); p = sp(p, pgm_name(P.pgm)); }
    *p = 0;
    while (p > s && text_w(FM, s) > 206) *--p = 0;     /* the corner is the battery's */
    draw_text_box(0, Y_PRESET, 208, FM, s, C_ACC, 0);
}

/* ---- the pop-up: what a knob or a button set, big, for a moment, over the drawbars' place (every band redraws after) */
#define POP_MS 1500u
#define Y_POP 40
#define H_POP 56
static char pop_t[16];
static uint32_t pop_until, pop_on, pop_drawn;
static void popup(const char *a, const char *b) /* a, a space and b, cut to fit pop_t (a program's name can be longer) */
{
    uint32_t i = 0;
    while (*a && i < sizeof pop_t - 1u) pop_t[i++] = *a++;
    if (b && i < sizeof pop_t - 1u) { pop_t[i++] = ' '; while (*b && i < sizeof pop_t - 1u) pop_t[i++] = *b++; }
    pop_t[i] = 0;
    pop_on = 1; pop_drawn = 0; pop_until = fm1_ms + POP_MS;
}
static void pop_off(void) { if (pop_on) pop_until = fm1_ms; }   /* away at the next look (and every band again) */
static void pop_if(uint32_t shown, const char *a, const char *b)   /* only where the page does not show it */
{
    if (!(shown >> screen & 1u)) popup(a, b);
}
static void pop_num(uint32_t shown, const char *a, int32_t v, int sign, const char *unit)
{
    char t[12], *q = t;
    if (v < 0) { *q++ = '-'; v = -v; } else if (sign && v > 0) *q++ = '+';
    q = su(q, (uint32_t)v);
    if (unit) q = sp(q, unit);
    *q = 0;
    pop_if(shown, a, t);
}
static void draw_popup(void)
{
    char t[16];
    uint32_t i;
    for (i = 0; pop_t[i] && i < 13u; i++) t[i] = pop_t[i];   /* 13 x 16 px fits the box */
    t[i] = 0;
    cv_begin(240, H_POP, C_BLACK);
    cv_box(6, 0, 228, H_POP, C_ACC);
    cv_box(7, 1, 226, H_POP - 2, C_ACC);
    cv_text(120 - text_w(&FONT_XL, t) / 2, (H_POP - 32) / 2, &FONT_XL, t, C_ACC);
    cv_blit(0, Y_POP);
}

/* ---- what the controls do (the FM-1's own and the KeyLab's): the same on every page */
static void do_rhythm(int32_t e)                /* K1 */
{
    drp_style = clampu((int32_t)drp_style + e, (int32_t)DRS_STYLES - 1);
    pop_if(PGM_MAIN | PGM_DRUMS, drp_style_get(drp_style)->name, 0);
}
static void tempo_set(int32_t v) { drp_bpm = (uint32_t)(v < 40 ? 40 : v > 240 ? 240 : v); }
static void do_tempo(int32_t e) { tempo_set((int32_t)drp_bpm + e); pop_num(PGM_MAIN | PGM_DRUMS, "TEMPO", (int32_t)drp_bpm, 0, 0); }   /* K2 */
static void pop_level(void)
{
    char t[12], *q = level_txt(t, drp_level);
    *q = 0;
    pop_if(PGM_DRUMS, "DRUMS", t);
}
static void do_level(int32_t e) { drp_level = clampu((int32_t)drp_level + e, (int32_t)DRP_LEVELS - 1); pop_level(); }   /* K3 */
static void do_vib(int32_t e) { P.vtype = clampu((int32_t)P.vtype + e, 5); pop_if(PGM_MAIN | org_vis(OR_VIB), "VIBRATO", VIB[P.vtype % 6u]); }   /* K4 */
static void do_preset(int32_t e)                /* PRESETS: setBfree's registrations, one after the other */
{
    uint32_t n = P.pgm ? P.pgm : 5u;
    char pt[8];
    for (; e > 0; e--) n = pgm_step(n, 1);
    for (; e < 0; e++) n = pgm_step(n, -1);
    organ_program(n);
    su(pt, n)[0] = 0;
    pop_if(PGM_MAIN | PGM_ORGAN | PGM_DRUMS | PGM_EQ, pt, pgm_name(n) ? pgm_name(n) : "");
}
static void do_page(void) { screen = (screen + 1u) % NSCREENS; pop_off(); }
static uint32_t tap_t[4], tap_n;
static void do_tap(void)                        /* the KeyLab's Tap: the tempo from the last taps (a gap over 2 s starts again) */
{
    uint32_t now = fm1_ms, i, sum;
    if (tap_n && now - tap_t[(tap_n - 1u) & 3u] > 2000u) tap_n = 0;
    tap_t[tap_n & 3u] = now; tap_n++;
    if (tap_n < 2u) { pop_if(0, "TAP", 0); return; }
    i = tap_n - 1u > 3u ? 3u : tap_n - 1u;     /* intervals: up to 3 */
    sum = now - tap_t[(tap_n - 1u - i) & 3u];
    if (sum) tempo_set((int32_t)((60000u * i + sum / 2u) / sum));
    pop_num(PGM_MAIN | PGM_DRUMS, "TEMPO", (int32_t)drp_bpm, 0, 0);
}

/* ---- the KeyLab Essential mk3 on its CB-931 User Program (CB-931-FM1.keylabessential3): the same controls as CB-931 on the FM-1.
 * Knobs (endless, Relative 2: 64 + n; two's complement read too): 1 volume, 2 SELECT, 3 PRESETS, 4 ALGORITHM, 5-8 K1-K4 (rhythm,
 * tempo, drum level, vibrato). Main encoder: turn PRESETS, click HOME. Fader 2: drum level, with pickup. Transport: Play start/stop,
 * Stop outro, Record intro/fill, Rewind A, Fast forward B, Tap tap tempo. Any channel. The other controllers it sends (pads CC 3,
 * faders 72..85) do nothing here; setBfree's drawbar, percussion and drive controllers (70..78, 80..83, 93) give way to it. */
static int32_t kl_enc[8], kl_pre;               /* knob turns waiting for the main loop, by ENC id; PRESETS */
static uint32_t kl_picked, kl_lastv = 255u, kl_lastset = 255u;   /* fader 2's pickup */
static int32_t kl_rel(uint32_t v)               /* as CB-931's relDelta */
{
    int32_t x = (int32_t)v;
    return v == 0u || v == 64u ? 0 : v > 64u && v < 96u ? x - 64 : v >= 32u && v < 64u ? x - 64 : v < 32u ? x : x - 128;
}
static uint32_t kl_lev(uint32_t v) { return (v * (DRP_LEVELS - 1u) + 63u) / 127u; }
static int kl_cc(uint32_t cc, uint32_t v)       /* 1: the KeyLab's (taken) */
{
    switch (cc) {
    case 74: if (kl_rel(v)) { vol_set(vol_db + kl_rel(v)); pop_num(org_vis(OR_VOL), "VOLUME", vol_db, 1, " dB"); } return 1;
    case 71: kl_enc[ENC_SELECT] += kl_rel(v); return 1;
    case 76: case 114: kl_pre += kl_rel(v); return 1;
    case 77: kl_enc[ENC_ALGORITHM] += kl_rel(v); return 1;
    case 93: kl_enc[ENC_KNOB1] += kl_rel(v); return 1;
    case 18: kl_enc[ENC_KNOB2] += kl_rel(v); return 1;
    case 19: kl_enc[ENC_KNOB3] += kl_rel(v); return 1;
    case 16: kl_enc[ENC_KNOB4] += kl_rel(v); return 1;
    case 17: case 3: return 1;                  /* knob 9, the pads: nothing */
    case 75: {                                  /* fader 2: the drums' level; it takes over once it reaches or passes the level */
        uint32_t want = kl_lev(v), cur = drp_level, was = kl_lastv > 127u ? want : kl_lev(kl_lastv);
        if (cur != kl_lastset) kl_picked = 0;   /* first move, or the level was set elsewhere (K3, the DRUMS page) */
        if (!kl_picked) kl_picked = want == cur || (want < cur) != (was < cur) || was == cur;
        kl_lastv = v;
        if (!kl_picked) { popup("FADER 2", want < cur ? "UP" : "DOWN"); return 1; }
        drp_level = want; kl_lastset = want;
        pop_level();
        return 1; }
    case 115: if (v >= 64u) do_page(); return 1;
    case 107: if (v >= 64u) drp_cmd(DRP_CMD_START_STOP); return 1;
    case 106: if (v >= 64u) drp_cmd(DRP_CMD_OUTRO); return 1;
    case 108: if (v >= 64u) drp_cmd(DRP_CMD_INTRO_FILL); return 1;
    case 103: if (v >= 64u) drp_cmd(DRP_CMD_A); return 1;
    case 104: if (v >= 64u) drp_cmd(DRP_CMD_B); return 1;
    case 109: if (v >= 64u) do_tap(); return 1;
    case 102: case 105: return 1;               /* Loop, Metronome: free */
    case 70: case 72: case 73: case 78: case 79: case 80: case 81: case 82: case 83: case 85: return 1;   /* the KeyLab's faders */
    default: return 0;
    }
}

static uint32_t state_key(int32_t oct, uint32_t *status)   /* what is worth saving; *status: what the screen shows */
{
    uint32_t i, dbk = 0, k = P.vtype | P.vup << 3 | P.les << 4 | P.perc << 6 | P.perc3 << 7 | P.fast << 8 | P.soft << 9 |
                             P.od << 10 | P.pgm << 15 | (uint32_t)P.rev << 22 | P.vlo << 29 | P.keys << 30;
    for (i = 0; i < NB; i++) dbk = dbk * 9u + P.db[i];
    for (i = 0; i < NB; i++) dbk = dbk * 9u + P.dbl[i];
    dbk = dbk * 9u + P.dbp[0]; dbk = dbk * 9u + P.dbp[1];
    dbk = dbk * 21u + P.drv;                    /* the drive 0..20 does not fit the bits above */
    dbk = dbk * 128u + P.chr;
    for (i = 0; i < LES_NP; i++) dbk = dbk * 31u + les_p[i];
    for (i = 0; i < TON_NP; i++) dbk = dbk * 31u + ton_p[i];
    for (i = 0; i < EQ_NP; i++) dbk = dbk * 31u + eq_p[i];
    dbk = hmix(hmix(hmix(hmix(hmix(dbk, drp_style), drp_bpm), drp_swing), drp_level), (uint32_t)(vol_db + 64));   /* B3+DRUM 0.2 */
    *status = k;
    return (k ^ dbk * 2654435761u) + (uint32_t)(oct + 2);
}

/* ---- the web editor's SysEx (docs/editor): F0 7D 42 33 CMD ... F7 ("B3"). README_ORGAN.md has the list. */
static uint32_t up_pend, up_dirty, up_gen;     /* slots waiting for the flash (bit k: program 112 + k), anything to save, a count for the screen */
static void ed_send(uint32_t cmd, const uint8_t *d, uint32_t n)
{
    uint8_t m[104];
    uint32_t i, w = 0;
    m[w++] = 0xF0; m[w++] = 0x7D; m[w++] = 0x42; m[w++] = 0x33; m[w++] = (uint8_t)cmd;
    for (i = 0; i < n && w < sizeof m - 1u; i++) m[w++] = d[i] & 0x7Fu;
    m[w++] = 0xF7;
    sysex_send(m, w);
}
static void ed_state(void)                      /* 41: the sound now, then the version */
{
    uint8_t d[96];
    uint32_t i, w = 0;
    const char *v = APP_VER;
    for (i = 0; i < NB; i++) d[w++] = P.db[i];
    for (i = 0; i < NB; i++) d[w++] = P.dbl[i];
    d[w++] = P.dbp[0]; d[w++] = P.dbp[1];
    d[w++] = P.vtype; d[w++] = P.vup; d[w++] = P.vlo; d[w++] = P.perc; d[w++] = P.perc3; d[w++] = P.fast; d[w++] = P.soft;
    d[w++] = P.les; d[w++] = P.od; d[w++] = P.drv; d[w++] = P.chr; d[w++] = P.rev; d[w++] = P.swell; d[w++] = P.pgm; d[w++] = P.keys;
    for (i = 0; i < LES_NP; i++) { d[w++] = (uint8_t)(les_p[i] >> 7); d[w++] = (uint8_t)(les_p[i] & 127u); }   /* the Leslie's, 14 bits: MSB, LSB */
    for (i = 0; i < TON_NP; i++) { d[w++] = (uint8_t)(ton_p[i] >> 7); d[w++] = (uint8_t)(ton_p[i] & 127u); }   /* tuning, key click */
    for (i = 0; i < EQ_NP; i++) { d[w++] = (uint8_t)(eq_p[i] >> 7); d[w++] = (uint8_t)(eq_p[i] & 127u); }       /* the equalizer */
    while (*v && w < sizeof d) d[w++] = (uint8_t)*v++;
    ed_send(0x41, d, w);
}
static void ed_slot(uint32_t n)                 /* 42: one of the user's programs: number, used, name (12 bytes) */
{
    uint8_t d[14];
    const organ_user_t *u = user_pgm(n);
    uint32_t i;
    d[0] = (uint8_t)n; d[1] = u != 0;
    for (i = 0; i < 12u; i++) d[2 + i] = u ? (uint8_t)u->name[i] : 0;
    ed_send(0x42, d, 14);
}
#define UP_BYTES 44u                            /* a user program on the wire: name 12, db 9, dbl 9, dbp 2, then 12 switches and values */
static void up_pack(const organ_user_t *u, uint8_t *d)
{
    uint32_t i, w = 0;
    for (i = 0; i < 12u; i++) d[w++] = (uint8_t)u->name[i] & 0x7Fu;
    for (i = 0; i < NB; i++) d[w++] = u->db[i];
    for (i = 0; i < NB; i++) d[w++] = u->dbl[i];
    d[w++] = u->dbp[0]; d[w++] = u->dbp[1];
    d[w++] = u->vtype; d[w++] = u->vup; d[w++] = u->vlo; d[w++] = u->perc; d[w++] = u->perc3; d[w++] = u->fast; d[w++] = u->soft;
    d[w++] = u->les; d[w++] = u->od; d[w++] = u->drv; d[w++] = u->chr; d[w++] = u->rev;
}
static void up_unpack(organ_user_t *u, const uint8_t *d)   /* the name made printable; user_ok checks the rest */
{
    uint32_t i, r = 0, end = 0;
    u->used = 1;
    for (i = 0; i < 12u; i++, r++) {
        if (!d[r]) end = 1;
        u->name[i] = end ? 0 : d[r] >= 32u && d[r] < 127u ? (char)d[r] : '?';
    }
    u->name[12] = 0;
    for (i = 0; i < NB; i++) u->db[i] = d[r++];
    for (i = 0; i < NB; i++) u->dbl[i] = d[r++];
    u->dbp[0] = d[r++]; u->dbp[1] = d[r++];
    u->vtype = d[r++]; u->vup = d[r++] & 1u; u->vlo = d[r++] & 1u; u->perc = d[r++] & 1u; u->perc3 = d[r++] & 1u; u->fast = d[r++] & 1u;
    u->soft = d[r++] & 1u; u->les = d[r++]; u->od = d[r++] & 1u; u->drv = d[r++]; u->chr = d[r++]; u->rev = d[r++];
    u->rsv[0] = UP_DRV_NEW; u->rsv[1] = 0;      /* the editor sends the drive in 0.6's scale (it converts older backups) */
    if (!u->name[0]) { u->name[0] = 'U'; u->name[1] = 'S'; u->name[2] = 'E'; u->name[3] = 'R'; }
}
static void ed_sysex(const uint8_t *p, uint32_t n)   /* p: the bytes between F0 and F7 */
{
    uint32_t k, slot;
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x42 || p[2] != 0x33) return;
    slot = n > 4u ? p[4] : 0;
    switch (p[3]) {
    case 0x01: ed_state(); break;                                       /* 01: send the state */
    case 0x02: for (k = UP_FIRST; k < UP_FIRST + UP_N; k++) ed_slot(k); break;   /* 02: send the user's programs */
    case 0x05:                                                          /* 05 slot: send all of it (a backup): 45 slot used + UP_BYTES */
        if (slot >= UP_FIRST && slot < UP_FIRST + UP_N) {
            uint8_t d[2 + UP_BYTES];
            d[0] = (uint8_t)slot; d[1] = user_pgm(slot) != 0;
            up_pack(&UPGM[slot - UP_FIRST], d + 2);
            if (!d[1]) for (k = 2; k < sizeof d; k++) d[k] = 0;
            ed_send(0x45, d, sizeof d);
        }
        break;
    case 0x07:                                                          /* 07: the equalizer: 6 values (14 bits); the FM-1 computes the filters */
        if (n >= 4u + 12u) {                                            /* (the first editor sent coefficients after them: ignored) */
            uint16_t ep[EQ_NP];
            uint8_t d[1];
            const uint8_t *q = p + 4;
            for (k = 0; k < EQ_NP; k++, q += 2) ep[k] = (uint16_t)(q[0] << 7 | q[1]);
            d[0] = eq_set(ep) ? 1 : 0;
            ed_send(0x47, d, 1);                                        /* 47 status: 0 taken, 1 out of range */
        }
        break;
    case 0x06:                                                          /* 06 slot + UP_BYTES: write it (a restore); answer as 03 */
        if (slot < UP_FIRST || slot >= UP_FIRST + UP_N || n < 5u + UP_BYTES) { uint8_t d[2] = {(uint8_t)slot, 1}; ed_send(0x43, d, 2); break; }
        {
            organ_user_t u;
            up_unpack(&u, p + 5);
            if (!user_ok(&u)) { uint8_t d[2] = {(uint8_t)slot, 3}; ed_send(0x43, d, 2); break; }   /* 3: out of range */
            UPGM[slot - UP_FIRST] = u;
        }
        up_pend |= 1u << (slot - UP_FIRST); up_dirty = 1; up_gen++;
        break;
    case 0x03:                                                          /* 03 slot name[12]: store the sound now in it */
    case 0x04:                                                          /* 04 slot: delete it */
        if (slot < UP_FIRST || slot >= UP_FIRST + UP_N) { uint8_t d[2] = {(uint8_t)slot, 1}; ed_send(0x43, d, 2); break; }
        if (p[3] == 0x03u) {
            uint8_t name[12] = {0};
            for (k = 0; k < 12u && 5u + k < n; k++) name[k] = p[5 + k];
            user_store(slot, name);
        } else {
            UPGM[slot - UP_FIRST].used = 0;
            if (P.pgm == slot) P.pgm = 0;
        }
        up_pend |= 1u << (slot - UP_FIRST); up_dirty = 1; up_gen++;   /* saved when nothing sounds (an erase stops the sound) */
        break;
    default: break;
    }
}

#if BOARD_SMK37                                 /* 37 keys, keybed key 0 = C3 (fm1_in.keys); the FM-1: 27, key 0 = F3 */
#define NKEYS 37u
#define KEY0 48
typedef uint64_t keys_t;
#define KEYS_NOW() ((keys_t)fm1_in.keys)
#else
#define NKEYS 27u
#define KEY0 53
typedef uint32_t keys_t;
#define KEYS_NOW() fm1_in.notes
#endif

#if BOARD_SMK37
/* ---- the SMK-37's own controls (README_SMK37.md). The pads are the drawbars 16' .. 1 1/3' of a manual, one column under each
 * knob: the top pad pushes the drawbar in (down to 0), the bottom pad pulls it out (up to 8); held, it goes on. One bank switch for
 * pads and faders (PATCH, or KNOB BANK or FADER BANK; the screen follows): bank 1 the upper manual, the faders its 1' and the
 * equalizer's bass, middle, treble; bank 2 the lower manual, the faders its 1', -, the pedal's 16' and 8'. A drawbar's fader works as
 * a drawbar: up is in (0), down is out (8); the equalizer's: up is more. A fader takes over only once it passes the value it sets
 * (no jump after a bank, a preset or the editor changed it). The mod wheel: down slow, middle stop, up fast.
 * The pedal jack: the swell, once a pedal has moved. */
#define SMK_REP_MS 400u                         /* a pad held: the first repeat, then every SMK_REP_EVERY ms */
#define SMK_REP_EVERY 120u
static uint8_t pad_bank, fad_bank, fad_live[4];   /* (pads and faders switch together: the same bank) */
static int16_t fad_last[4] = {-1, -1, -1, -1};
static uint32_t pad_held, pad_t[16];
static volatile uint8_t *smk_bars(uint32_t bank) { return bank ? P.dbl : P.db; }
/* the colour of drawbar c (0 = 16' .. 7 = 1 1/3'): brown, white, black as on the B3; the LEDs cannot show black: a cold blue */
static const uint8_t SMK_DBC[8][3] = {{150, 60, 10}, {150, 60, 10}, {150, 150, 140}, {150, 150, 140},
                                      {30, 50, 150}, {150, 150, 140}, {30, 50, 150}, {30, 50, 150}};
static int32_t smk_fad_get(uint32_t f)          /* what fader f sets now, in its own steps; -1: nothing */
{
    if (f == 0u) return smk_bars(fad_bank)[8];
    if (!fad_bank) return eq_p[2u * (f - 1u)];
    return f == 1u ? -1 : P.dbp[f - 2u];
}
static int32_t smk_fad_max(uint32_t f) { return f && !fad_bank ? 36 : 8; }   /* drawbars 0..8, the equalizer 0..36 (18 = 0 dB) */
static int32_t smk_fad_val(uint32_t f, int32_t pos)   /* a position 0..127 (up 127) as the value: a drawbar's is upside down */
{
    int32_t mx = smk_fad_max(f), v = (pos * mx + 63) / 127;
    return f && !fad_bank ? v : mx - v;
}
static void smk_fad_set(uint32_t f, int32_t v)
{
    if (f == 0u) smk_bars(fad_bank)[8] = (uint8_t)v;
    else if (!fad_bank) {
        uint16_t ep[EQ_NP];
        uint32_t k;
        for (k = 0; k < EQ_NP; k++) ep[k] = eq_p[k];
        ep[2u * (f - 1u)] = (uint16_t)v;
        if (eq_p[2u * (f - 1u)] != (uint16_t)v) eq_set(ep);
    } else if (f >= 2u) P.dbp[f - 2u] = (uint8_t)v;
}
static void smk_fad_bank(uint32_t b)            /* a new bank: every fader waits for its value again */
{
    uint32_t f;
    fad_bank = (uint8_t)b;
    for (f = 0; f < 4u; f++) { fad_live[f] = 0; fad_last[f] = -1; }
}
static void smk_faders(void)
{
    uint32_t f;
    for (f = 0; f < 4u; f++) {
        int32_t pos = fm1_in.fader[f], cur = smk_fad_get(f), v = smk_fad_val(f, pos), was;
        if (cur < 0) continue;
        if (fad_last[f] < 0) { fad_last[f] = (int16_t)pos; continue; }
        if (pos == fad_last[f]) {
            if (fad_live[f] && v != cur) fad_live[f] = 0;      /* changed from elsewhere (a preset, a knob, the editor): wait again */
            continue;
        }
        was = smk_fad_val(f, fad_last[f]);
        fad_last[f] = (int16_t)pos;
        if (!fad_live[f] && (v == cur || was == cur || (was < cur) != (v < cur))) fad_live[f] = 1;   /* it sat on the value, reached it or passed it */
        if (fad_live[f] && v != cur) smk_fad_set(f, v);
    }
}
static void smk_pad_step(uint32_t pad)          /* top row: in (less), bottom row: out (more) */
{
    volatile uint8_t *db = smk_bars(pad_bank);
    uint32_t c = pad & 7u;
    db[c] = clampu((int32_t)db[c] + (pad < 8u ? -1 : 1), 8);
}
static void smk_pads(uint32_t now)
{
    int32_t ev;
    uint32_t p;
    while ((ev = smk_pad_take()) >= 0) {
        p = (uint32_t)ev >> 8 & 15u;
        if (ev & 0x7F) { pad_held |= 1u << p; pad_t[p] = now + SMK_REP_MS; smk_pad_step(p); }
        else pad_held &= ~(1u << p);
    }
    for (p = 0; p < 16u; p++)
        if (pad_held >> p & 1u && (int32_t)(now - pad_t[p]) >= 0) { pad_t[p] = now + SMK_REP_EVERY; smk_pad_step(p); }
    for (p = 0; p < 8u; p++) {                  /* the colours: the bottom pad as bright as the drawbar is out, the top one dim */
        uint32_t v = smk_bars(pad_bank)[p], lo = 8u + v * 31u, hi = 24u;
        const uint8_t *c = SMK_DBC[p];
        if (pad_held >> p & 1u) hi = 255u;      /* (a held pad lights up) */
        padled_set(p, (uint8_t)(c[0] * hi / 255u), (uint8_t)(c[1] * hi / 255u), (uint8_t)(c[2] * hi / 255u));
        if (pad_held >> (p + 8u) & 1u) lo = 255u;
        padled_set(p + 8u, (uint8_t)(c[0] * lo / 255u), (uint8_t)(c[1] * lo / 255u), (uint8_t)(c[2] * lo / 255u));
    }
    padled_flush();
}
static void smk_modwheel(void)                  /* the mod wheel in three parts (up = a lower reading): slow, stop, fast */
{
    static int32_t lo = 474, hi = 629, zone = -1;   /* (a unit's travel; it widens to what the wheel reaches) */
    int32_t raw = fm1_in.wheel[1], pos, z;
    if (!raw) return;
    if (raw < lo) lo = raw;
    if (raw > hi) hi = raw;
    pos = (hi - raw) * 300 / (hi - lo);         /* 0 down .. 300 up */
    z = zone < 0 ? (pos < 100 ? 0 : pos < 200 ? 1 : 2) : zone;
    if (zone >= 0) {                            /* a margin of 12 around the borders: no flutter */
        if (pos < 100 - 12) z = 0;
        else if (pos > 100 + 12 && pos < 200 - 12) z = 1;
        else if (pos > 200 + 12) z = 2;
    }
    if (zone >= 0 && z != zone) P.les = (uint8_t)(z == 0 ? 1u : z == 1 ? 0u : 2u);   /* (P.les: 0 stop, 1 slow, 2 fast) */
    zone = z;                                   /* (at power-on the wheel only gives its place: the saved Leslie stays) */
}
static void smk_pedal(void)                     /* an expression pedal on the jack: the swell, once it has moved (no plug detection) */
{
    static int32_t first = -1, lo = 1023, hi = 0, on = 0;
    int32_t raw = fm1_in.pedal, v;
    if (!raw) return;
    v = 1023 - raw;                             /* (as the stock firmware: inverted) */
    if (first < 0) first = v;
    if (!on && (v - first > 64 || first - v > 64)) on = 1;
    if (!on) return;
    if (v < lo) lo = v;
    if (v > hi) hi = v;
    if (hi - lo >= 200) P.swell = (uint8_t)((v - lo) * 127 / (hi - lo));
}
/* the buttons only the SMK has: < > the presets; PATCH (or KNOB BANK, FADER BANK) the bank of pads and faders: upper, lower */
static void smk_buttons(uint32_t b, uint32_t rel)
{
    (void)rel;
    if (b & (1u << BTN_LEFT | 1u << BTN_RIGHT)) {
        uint32_t n = P.pgm ? P.pgm : 5u;
        organ_program(pgm_step(n, b & (1u << BTN_RIGHT) ? 1 : -1));
        fad_live[0] = fad_live[1] = fad_live[2] = fad_live[3] = 0;
    }
    if (b & (1u << BTN_HOME | 1u << BTN_KNOB_BANK | 1u << BTN_FADER_BANK)) {
        pad_bank ^= 1u;
        smk_fad_bank(pad_bank);
    }
    fm1_led_key(BTN_HOME, pad_bank != 0);       /* lit: bank 2, the lower manual */
    fm1_led_key(BTN_KNOB_BANK, pad_bank != 0);
    fm1_led_key(BTN_FADER_BANK, pad_bank != 0);
}
#endif

static void app_main(void)
{
    struct { uint8_t magic, db[NB], vtype, vup, perc, perc3, fast, soft, les, od, drv, pgm, rev; int8_t oct;
             uint8_t dbl[NB], dbp[2], vlo, keys, chr, lesp[2 * LES_NP], tonp[2 * TON_NP],
             eqp[2 * EQ_NP], eqc[60], b3d[6]; } set;   /* b3d (B3+DRUM 0.2): 0xD3, rhythm, tempo, swing, drum level, volume + 64 */   /* eqc: unused (the first equalizer builds kept the editor's coefficients; the size stays) */   /* les: the Leslie's les_p, low byte first */
    keys_t prev = 0;
    uint32_t k, i, t_change = 0, dirty = 0, t_load = 0, seed = 12345u, saved_key = 0, last_key = 0;
    uint32_t hash[22], drawn_screen = 99, drawn_batt = ~0u, logo_on = 1, logo_drawn = 0, has_b3d;
    uint8_t held[NKEYS], heldm[NKEYS];
#ifdef DINGHY_HOST
    if (getenv("ORGAN_ONE")) organ_two = 0;          /* tests: all the wheels in one call, to compare with the two halves */
#endif
    int32_t oct = 0;
    int set_old;

    for (i = 0; i < NW; i++) { seed = seed * 1664525u + 1013904223u; wph[i] = seed; }   /* the wheels start anywhere */
    for (i = 0; i < NKEYS; i++) { held[i] = 255; heldm[i] = 0; }
    eq_sel = 0;
#if BOARD_SMK37
    smk_pad_events = 1;                         /* the pads are ours (events), not white keys */
#endif
    set.chr = OD_CHARACTER;
    if (data_load(UPGM, sizeof UPGM) == 0) user_check();
    else for (i = 0; i < UP_N; i++) UPGM[i].used = 0;
    for (i = 0; i < LES_NP; i++) { set.lesp[2 * i] = (uint8_t)LES_DEF[i]; set.lesp[2 * i + 1] = (uint8_t)(LES_DEF[i] >> 8); }
    for (i = 0; i < TON_NP; i++) { set.tonp[2 * i] = (uint8_t)TON_DEF[i]; set.tonp[2 * i + 1] = (uint8_t)(TON_DEF[i] >> 8); }
    for (i = 0; i < 2u * EQ_NP; i++) set.eqp[i] = 0;   /* (no equalizer saved: flat) */
    for (i = 0; i < sizeof set.b3d; i++) set.b3d[i] = 0;
#define SET_B3D (sizeof set.b3d)
#define SET_EQ (2u * EQ_NP + 60u + SET_B3D)
    {
        int now = settings_load(&set, sizeof set) == 0;   /* B3+DRUM 0.2's */
        has_b3d = (uint32_t)now;
        if (!now) now = settings_load(&set, sizeof set - SET_B3D) == 0;   /* B3 ORGAN 0.6 .. B3+DRUM 0.1; anything older has the drive 0..16 */
        set_old = !now;
    }
    if ((!set_old || settings_load(&set, sizeof set - SET_EQ) == 0 ||                                      /* 0.5.1: no equalizer */
         settings_load(&set, sizeof set - SET_EQ - 2u * TON_NP) == 0 ||                                      /* without the tuning, click */
         settings_load(&set, sizeof set - SET_EQ - 2u * TON_NP - 2u * LES_NP) == 0 ||                        /* without the Leslie's */
         settings_load(&set, sizeof set - SET_EQ - 2u * TON_NP - 2u * LES_NP - 1u) == 0) && set.chr < 128u && set.magic == 0xB6 && set.oct >= -2 && set.oct <= 2 && set.vtype < 6u &&
        set.les < 3u && set.drv <= OD_DRIVE_MAX && set.pgm < 128u && set.rev < 128u && set.keys < 3u) {
        for (i = 0; i < NB; i++) { P.db[i] = set.db[i] > 8u ? 8u : set.db[i]; P.dbl[i] = set.dbl[i] > 8u ? 8u : set.dbl[i]; }
        P.dbp[0] = set.dbp[0] > 8u ? 8u : set.dbp[0]; P.dbp[1] = set.dbp[1] > 8u ? 8u : set.dbp[1];
        P.vtype = set.vtype; P.vup = set.vup & 1u; P.vlo = set.vlo & 1u; P.perc = set.perc & 1u; P.perc3 = set.perc3 & 1u;
        P.fast = set.fast & 1u; P.soft = set.soft & 1u; P.les = set.les; P.od = set.od & 1u; P.drv = set_old ? (set.drv + 4u > OD_DRIVE_MAX ? OD_DRIVE_MAX : set.drv + 4u) : set.drv;
        P.pgm = set.pgm; P.rev = set.rev; P.keys = 0; P.chr = set.chr;   /* B3+DRUM 0.2: one manual, no octave */
        for (i = 0; i < LES_NP; i++) les_set(i, set.lesp[2 * i] | (uint32_t)set.lesp[2 * i + 1] << 8);
        for (i = 0; i < TON_NP; i++) ton_set(i, set.tonp[2 * i] | (uint32_t)set.tonp[2 * i + 1] << 8);
        if (set.eqp[0] | set.eqp[1]) {          /* an equalizer saved (gain index 0 with frequency 0 is never one) */
            uint16_t ep[EQ_NP];
            for (i = 0; i < EQ_NP; i++) ep[i] = (uint16_t)(set.eqp[2 * i] | set.eqp[2 * i + 1] << 8);
            eq_set(ep);
        }
        if (P.pgm >= UP_FIRST && !user_pgm(P.pgm)) P.pgm = 0;
        if (has_b3d && set.b3d[0] == 0xD3u) {   /* the drums and the volume */
            if (set.b3d[1] < DRS_STYLES) drp_style = set.b3d[1];
            if (set.b3d[2] >= 40u && set.b3d[2] <= 240u) drp_bpm = set.b3d[2];
            if (set.b3d[3] >= 50u && set.b3d[3] <= 75u) drp_swing = set.b3d[3];
            if (set.b3d[4] < DRP_LEVELS) drp_level = set.b3d[4];
            vol_set((int32_t)set.b3d[5] - 64);
        }
    } else
        organ_program(5u);                      /* Standard B, 88 8000 000 */
    last_key = saved_key = state_key(oct, &i);

    for (;;) {
        keys_t keys, down, up;
        uint32_t rel, b;
        int32_t e;
        uint8_t st, d1, d2;
        platform_poll();

        keys = KEYS_NOW();
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < NKEYS; k++) {           /* the FM-1's own keys: the organ, as they lie (no octave shift: OCT- and OCT+ are the drums') */
            if (down >> k & 1u) {
                uint32_t note = (uint32_t)(KEY0 + 12 * oct + (int32_t)k);
                held[k] = (uint8_t)note; heldm[k] = 0;
                organ_note(0, 1u, note, 1, 0);
            }
            if (up >> k & 1u && held[k] != 255) { organ_note(heldm[k], 1u, held[k], 0, 0); held[k] = 255; }
        }

        /* MIDI first (the KeyLab's knobs and buttons queue what the panel's do, below) */
        if (midi_in_overflow) {                 /* a message was lost (queue full): its note off may have been it, so release the MIDI notes */
            midi_in_overflow = 0;
            organ_cc_ch(0, 123, 0);
        }
        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI and the MIDI IN jack: one manual on every channel but 10 */
            uint32_t cmd = st & 0xF0u, ch = st & 0x0Fu;
            if (cmd >= 0x80u && cmd < 0xF0u) {
                midi_ch = ch + 1u; midi_cnt++; midi_t = fm1_ms; midi_d1 = d1;
                midi_kind = cmd == 0x90u && d2 ? 1u : cmd == 0x80u || cmd == 0x90u ? 2u : cmd == 0xB0u ? 3u : cmd == 0xC0u ? 4u : 5u;
            }
            if (ch == 9u && (cmd == 0x80u || cmd == 0x90u || cmd == 0xA0u)) continue;   /* channel 10: the KeyLab's pads (the FM-1 sits on them): silent */
            if (cmd == 0x90u && d2) organ_midi_note(0, d1, 1);
            else if (cmd == 0x80u || cmd == 0x90u) organ_midi_note(0, d1, 0);
            else if (cmd == 0xB0u) { if (!kl_cc(d1, d2)) organ_cc_ch(0, d1, d2); }   /* the KeyLab's, else setBfree's (organ_core.h) */
            else if (cmd == 0xC0u) organ_pc(d1);                /* program change: setBfree's default.pgm */
        }
        {
            const uint8_t *sx;
            uint32_t sxn;
            if (sysex_get(&sx, &sxn)) { ed_sysex(sx, sxn); sysex_done(); }
        }

        b = fm1_input_edges(&rel);
        if (logo_on && (keys || b || fm1_ms > LOGO_MS)) { logo_on = 0; drawn_screen = 99; }   /* the logo goes; what woke it still works */
        /* the buttons: the same on every page */
        if (b & (1u << BTN_PLAY)) drp_cmd(DRP_CMD_START_STOP);
        if (b & (1u << BTN_REC)) drp_cmd(DRP_CMD_INTRO_FILL);
        if (b & (1u << BTN_OCT_DOWN)) drp_cmd(DRP_CMD_A);
        if (b & (1u << BTN_OCT_UP)) drp_cmd(DRP_CMD_B);
        if (b & (1u << BTN_SEQ)) drp_cmd(DRP_CMD_OUTRO);
        if (b & (1u << BTN_FX)) { P.vup ^= 1u; pop_if(PGM_MAIN | org_vis(OR_VIBON), "VIBRATO", P.vup ? "ON" : "OFF"); }
        if (b & (1u << BTN_LFO)) { P.les = (uint8_t)((P.les + 1u) % 3u); pop_if(PGM_MAIN | org_vis(OR_LES), "LESLIE", LES[P.les % 3u]); }
        if (b & (1u << BTN_SEL)) { P.od ^= 1u; pop_if(PGM_MAIN | org_vis(OR_OD), "OD", P.od ? "ON" : "OFF"); }
        if (b & (1u << BTN_ENV)) { P.perc ^= 1u; pop_if(PGM_MAIN | org_vis(OR_PERC), "PERC", P.perc ? "ON" : "OFF"); }
        if (b & (1u << BTN_ARP)) { P.perc3 ^= 1u; pop_if(PGM_MAIN | org_vis(OR_HARM), "PERC", P.perc3 ? "3RD" : "2ND"); }
        if (b & (1u << BTN_EDIT)) { P.fast ^= 1u; pop_if(PGM_MAIN | org_vis(OR_DECAY), "PERC", P.fast ? "FAST" : "SLOW"); }
        if (b & (1u << BTN_GLO)) { P.soft ^= 1u; pop_if(PGM_MAIN | org_vis(OR_PVOL), "PERC", P.soft ? "SOFT" : "NORMAL"); }
#if !BOARD_SMK37
        if (b & (1u << BTN_HOME)) do_page();    /* (the SMK's HOME is its PATCH: the pads' bank) */
#endif

#define enc_in(id) (e = fm1_enc_take(id) + kl_enc[id], kl_enc[id] = 0, e)
        if (enc_in(ENC_SELECT) != 0) {          /* SELECT: pick on the page */
            pop_off();
            if (screen == PG_MAIN) sel0 = clampu((int32_t)sel0 + e, 8);
            else if (screen == PG_ORGAN) org_sel = clampu((int32_t)org_sel + e, OR_N - 1);
            else if (screen == PG_EQ) eq_sel = clampu((int32_t)eq_sel + e, 2);
            else if (screen == PG_DRUMS) drp_field = clampu((int32_t)drp_field + e, (int32_t)DRP_FIELDS - 1);
            else drp_style = clampu((int32_t)drp_style + e, (int32_t)DRP_NSTYLES - 1);   /* TEST: TEST BUSY is the last */
        }
        if (enc_in(ENC_ALGORITHM) != 0) {       /* ALGORITHM: set it */
            pop_off();
            if (screen == PG_MAIN) P.db[sel0] = clampu((int32_t)P.db[sel0] + e, 8);
            else if (screen == PG_ORGAN) org_set(org_sel, e);
            else if (screen == PG_EQ) {         /* the equalizer: 0.5 dB a step */
                uint16_t ep[EQ_NP];
                for (k = 0; k < EQ_NP; k++) ep[k] = eq_p[k];
                ep[2u * eq_sel] = (uint16_t)clampu((int32_t)ep[2u * eq_sel] + e, 36);
                eq_set(ep);
            } else if (screen == PG_DRUMS) {
                switch (drp_field) {
                case 0: drp_style = clampu((int32_t)drp_style + e, (int32_t)DRS_STYLES - 1); break;
                case 1: tempo_set((int32_t)drp_bpm + e); break;
                case 2: { int32_t v = (int32_t)drp_swing + e; drp_swing = (uint32_t)(v < 50 ? 50 : v > 75 ? 75 : v); break; }
                default: drp_level = clampu((int32_t)drp_level + e, (int32_t)DRP_LEVELS - 1); break;
                }
            }
        }
        if (enc_in(ENC_KNOB1) != 0) do_rhythm(e);   /* K1-K4: the same on every page */
        if (enc_in(ENC_KNOB2) != 0) do_tempo(e);
        if (enc_in(ENC_KNOB3) != 0) do_level(e);
        if (enc_in(ENC_KNOB4) != 0) do_vib(e);
#if BOARD_SMK37
        if ((e = fm1_enc_take(ENC_SWELL)) != 0) P.swell = clampu((int32_t)P.swell + 4 * e, 127);   /* K7: the swell, as CC 11 */
        smk_buttons(b, rel);
        smk_pads(fm1_ms);
        smk_faders();
        smk_modwheel();
        smk_pedal();
        if (kl_pre) { do_preset(kl_pre); kl_pre = 0; }
#else
        e = fm1_enc_take(ENC_PRESETS) + kl_pre;
        kl_pre = 0;
        if (e != 0) do_preset(e);
#endif

#if !BOARD_SMK37                                /* (the SMK's keys have no LEDs) */
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), held[k] != 255);
#endif
        {                                       /* the LEDs: the drums (what plays, what waits), the organ's switches */
            uint32_t pt = drp_part, pd = drp_pending;
            fm1_led_key(BTN_OCT_DOWN, drp_running ? (pd < DRP_PARTS ? pd == DRP_MAIN_A : pt == DRP_MAIN_A) : pt != DRP_MAIN_B);
            fm1_led_key(BTN_OCT_UP, drp_running ? (pd < DRP_PARTS ? pd == DRP_MAIN_B : pt == DRP_MAIN_B) : pt == DRP_MAIN_B);
            fm1_led_key(BTN_SEQ, drp_running && (pt == DRP_OUTRO || pd == DRP_OUTRO));
            fm1_led_key(BTN_REC, drp_running && (pt == DRP_INTRO || pt == DRP_FILL_A || pt == DRP_FILL_B));
            fm1_led_key(BTN_PLAY, drp_running != 0);
        }
        fm1_led_key(BTN_FX, P.vup != 0);
        fm1_led_key(BTN_LFO, P.les != 0);
        fm1_led_key(BTN_SEL, P.od != 0);
        fm1_led_key(BTN_ENV, P.perc != 0);
        fm1_led_key(BTN_ARP, P.perc3 != 0);
        fm1_led_key(BTN_EDIT, P.fast != 0);
        fm1_led_key(BTN_GLO, P.soft != 0);

        if (fm1_ms - t_load > 500u) {           /* how close to the 5800 us a block has: the number to watch */
            t_load = fm1_ms;
            load_now = load_us; load_wheels = wheels_on; ld_org = load_org; ld_od = load_od; ld_rv = load_rv; ld_le = load_le;
            ld_dr = load_dr; load_dr = 0; ld_dv = drp_vmax; drp_vmax = 0;   /* B3+DRUM */
            ld_pk = (sum_pk * 100u + 16384u) >> 15; sum_pk = 0; ld_q = (out_pk * 100u + 16384u) >> 15; out_pk = 0; ld_g = ((uint32_t)lim_sum.lo * 100u + 16384u) >> 15; lim_sum.lo = 32768; ld_go = ((uint32_t)lim_out.lo * 100u + 16384u) >> 15; lim_out.lo = 32768;
            if (load_now > load_peak) load_peak = load_now;
            load_us = load_org = load_od = load_rv = load_le = 0;
        }

        if (logo_on) {
            if (!logo_drawn) { logo_drawn = 1; draw_logo(); }
        } else if (screen != drawn_screen) {    /* a new page: clear, and draw every band */
            drawn_screen = screen;
            lcd_fill(0, 0, 240, 240, C_BLACK);
            for (i = 0; i < 22u; i++) hash[i] = 0xFFFFFFFFu;
            drawn_batt = ~0u;
        }
        if (!logo_on) {                         /* each band again when what it shows has changed */
            uint32_t h, r, n, drew = 0;
            h = hmix(hmix(hmix(hmix(hmix(0, screen), drp_running), drp_part), drp_pending), 0);
            if (h != hash[0]) { hash[0] = h; band_header(screen); drew = 1; }
            if (screen == PG_MAIN || screen == PG_EQ) {
                h = hmix(1, screen);
                if (screen == PG_EQ) { for (i = 0; i < 3u; i++) h = hmix(h, eq_p[2u * i]); h = hmix(h, eq_sel); }
                else { for (i = 0; i < NB; i++) h = hmix(h, P.db[i]); h = hmix(h, sel0); }
                if (h != hash[1]) { hash[1] = h; band_bars(screen); drew = 1; }
                for (r = 0; r < 2u; r++) {
                    h = hmix(hmix(2, screen), r);
                    if (screen == PG_EQ) { for (i = 0; i < 3u; i++) h = hmix(h, eq_p[2u * i]); h = hmix(h, eq_sel); }
                    else if (r == 0) h = hmix(hmix(hmix(hmix(h, P.vtype), P.vup), P.les), P.od);
                    else h = hmix(hmix(hmix(hmix(h, P.perc), P.perc3), P.fast), P.soft);
                    if (h != hash[2 + r]) { hash[2 + r] = h; band_ctrl(screen, r); drew = 1; }
                }
                if (screen == PG_MAIN) {
                    h = hmix(hmix(hmix(hmix(hmix(4, drp_running), drp_beat), drp_beats), drp_style), drp_bpm);
                    if (h != hash[4]) { hash[4] = h; band_strip(); drew = 1; }
                }
            } else if (screen == PG_ORGAN) {
                uint32_t vals = hmix(hmix(hmix(hmix(hmix(hmix(hmix(hmix(hmix(hmix(hmix(hmix(7, P.vtype), P.vup), P.les), P.od), P.drv), P.rev),
                                                               P.perc), P.perc3), P.fast), P.soft), (uint32_t)vol_db), org_sel);
                for (r = 0; r < LIST_VIS; r++) {
                    h = hmix(hmix(vals, r), org_top());
                    if (h != hash[6u + r]) { hash[6u + r] = h; band_org(r); drew = 1; }
                }
            } else if (screen == PG_DRUMS) {
                for (r = 0; r <= DRP_FIELDS; r++) {
                    h = hmix(hmix(5, screen), r);
                    if (r == 0) h = hmix(hmix(hmix(hmix(hmix(h, drp_running), drp_part), drp_pending), drp_beat), drp_beats);
                    else h = hmix(hmix(hmix(hmix(hmix(h, drp_field), drp_style), drp_bpm), drp_swing), drp_level);
                    if (h != hash[6u + r]) { hash[6u + r] = h; band_drums(r); drew = 1; }
                }
            } else {
                n = TEST_ROWS;
                for (r = 0; r < n; r++) {
                    uint32_t va, vb;
                    list_vals(screen, r, &va, &vb);
                    h = hmix(hmix(hmix(hmix(9, screen), r), va), vb);
                    if (h != hash[6u + r]) { hash[6u + r] = h; band_list(screen, r); drew = 1; }
                }
            }
            if (screen < SCR_TEST) {
                h = hmix(hmix(3, P.pgm), up_gen);
                if (h != hash[5]) { hash[5] = h; band_preset(); }
            }
            if (pop_on) {                       /* the pop-up: over the drawbars' place; when it is over, every band again */
                if ((int32_t)(fm1_ms - pop_until) >= 0) { pop_on = 0; for (i = 0; i < 22u; i++) hash[i] = 0xFFFFFFFFu; }
                else if (!pop_drawn || drew) { draw_popup(); pop_drawn = 1; }
            }
        }
        {
            uint32_t bs = battery_state(), sk, key = state_key(oct, &sk);
            if (bs != drawn_batt && !logo_on) { drawn_batt = bs; draw_battery(bs); }
            if (key != last_key) { last_key = key; dirty = key != saved_key; t_change = fm1_ms; }
        }

        if (dirty && quiet() && fm1_ms - t_change > 2000u) {   /* an erase stalls the sound: save when silent */
            set.magic = 0xB6;
            for (i = 0; i < NB; i++) { set.db[i] = P.db[i]; set.dbl[i] = P.dbl[i]; }
            set.dbp[0] = P.dbp[0]; set.dbp[1] = P.dbp[1]; set.vlo = P.vlo; set.keys = P.keys;
            set.vtype = P.vtype; set.vup = P.vup; set.perc = P.perc; set.perc3 = P.perc3; set.fast = P.fast;
            set.soft = P.soft; set.les = P.les; set.od = P.od; set.drv = P.drv; set.pgm = P.pgm; set.rev = P.rev; set.oct = (int8_t)oct;
            set.chr = P.chr;
            set.b3d[0] = 0xD3; set.b3d[1] = (uint8_t)drp_style; set.b3d[2] = (uint8_t)drp_bpm; set.b3d[3] = (uint8_t)drp_swing;
            set.b3d[4] = (uint8_t)drp_level; set.b3d[5] = (uint8_t)(vol_db + 64);
            for (i = 0; i < LES_NP; i++) { set.lesp[2 * i] = (uint8_t)les_p[i]; set.lesp[2 * i + 1] = (uint8_t)(les_p[i] >> 8); }
            for (i = 0; i < TON_NP; i++) { set.tonp[2 * i] = (uint8_t)ton_p[i]; set.tonp[2 * i + 1] = (uint8_t)(ton_p[i] >> 8); }
            for (i = 0; i < EQ_NP; i++) { set.eqp[2 * i] = (uint8_t)eq_p[i]; set.eqp[2 * i + 1] = (uint8_t)(eq_p[i] >> 8); }
            settings_save(&set, sizeof set);
            saved_key = last_key;
            dirty = 0;
        }
        if (up_dirty && quiet()) {                 /* the user's programs: when the editor stored or deleted one, and nothing sounds */
            int rc = data_save(UPGM, sizeof UPGM);
            up_dirty = 0;
            for (k = 0; k < UP_N; k++)
                if (up_pend >> k & 1u) { uint8_t d[2] = {(uint8_t)(UP_FIRST + k), rc ? 2u : 0u}; ed_send(0x43, d, 2); ed_slot(UP_FIRST + k); }
            up_pend = 0;
        }
    }
}
