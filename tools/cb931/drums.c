/*
 * drums - see drums.h.
 *
 * Kit values: CT-403, Mini Pops and TR-606 from measurements of the user's
 * recordings (ref/ct403, ref/minipops, ref/tr606); CR-78 from Roland's
 * factory alignment table (service notes p. 30) as modelled by CW-78
 * (ref/cr78). Sounds a kit's source didn't have are designed to suit it.
 */
#include "drums.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LN20DB 2.302585f    /* a 20 dB fall is e^-2.3026 */

static const char *const slot_names[DR_SLOTS] = {
    "Bass drum", "Snare", "Rim shot", "Closed hat", "Open hat", "Cymbal", "Low tom", "High tom",
    "Claves", "Maracas", "High bongo", "Low bongo", "Conga", "Cowbell", "Tambourine", "Extra",
    "Snare 2", "Quijada"
};

const char *dr_slot_name(int slot) { return slot >= 0 && slot < DR_SLOTS ? slot_names[slot] : ""; }

/* shared Latin percussion, from the CR-78's alignment table */
#define CR78_MA  { .name = "Maracas",    .level_db = -6.4f, .pan =  0.3f, .n_level = 1, .n_centre = 12000, .n_q = 0.8f, .n_decay = 19 }
#define CR78_HB  { .name = "High bongo", .level_db = -4.2f, .pan = -0.25f, .t_level = 1, .t_freq = 618, .t_decay = 41 }
#define CR78_LB  { .name = "Low bongo",  .level_db = -4.4f, .pan = -0.1f, .t_level = 1, .t_freq = 412, .t_decay = 41 }
#define CR78_CG  { .name = "Conga",      .level_db = -3.1f, .pan =  0.15f, .t_level = 1, .t_freq = 214, .t_decay = 149, .t_attack = 2 }
#define CR78_CB  { .name = "Cowbell",    .level_db = -10.1f, .pan =  0.2f, .t_level = 1, .t_freq = 805, .t_freq2 = 555, .t_level2 = 0.7f, .t_decay = 51 }
#define CR78_TB  { .name = "Tambourine", .level_db = -6.2f, .pan =  0.35f, .n_level = 0.7f, .n_centre = 4500, .n_q = 1.0f, .n_decay = 169, .m_level = 0.4f, .m_set = 1, .m_centre = 6000, .m_q = 1.2f, .m_decay = 120 }

/* CT-403 sounds the MT-40 and MT-18 kits borrow (no samples of theirs) */
#define CT403_RS { .name = "Rim shot",   .level_db =  -9.3f, .t_level = 1, .t_freq = 1500, .t_decay = 6, .t_click = 0.3f }
#define CT403_CY { .name = "Cymbal",     .level_db = -11.3f, .pan = -0.3f, .n_level = 1, .n_centre = 8500, .n_q = 0.9f, .n_decay = 120, \
    .tail_level = 0.25f, .tail_decay = 600 }
#define CT403_LT { .name = "Low tom",    .level_db =  -2.8f, .pan = -0.2f, .t_level = 1, .t_freq = 214, .t_decay = 60 }
#define CT403_HT { .name = "High tom",   .level_db =  -4.1f, .pan =  0.2f, .t_level = 1, .t_freq = 300, .t_decay = 55 }

static const dr_kit kits[DR_KITS] = {
    /* 0.10.7: bass drum, snare, snare 2, claves and hats matched to Skipper's CT-403
     * samples (tools/ct403_sounds.h); 0.10.8: levels matched to the other
     * kits' loudness (dr_loudness), as his recording's were too quiet */
    { "CT-403", {
        { .name = "Bass drum",  .level_db =   2.1f, .t_level = 1, .t_freq = 73, .t_decay = 30, .t_attack = 5, .t_drop = 1.6f, .t_drop_ms = 6, .t_click = 0.15f },
        { .name = "Snare",      .level_db =  -4.4f, .t_level = 1, .t_freq = 305, .t_decay = 80, .t_attack = 3, .t_freq2 = 110, .t_level2 = 0.2f, .t_click = 0.2f,
                                .n_level = 0.4f, .n_centre = 3500, .n_q = 0.5f, .n_decay = 150 },
        CT403_RS,
        { .name = "Closed hat", .level_db =  -9.6f, .pan = 0.25f, .n_level = 1, .n_centre = 7000, .n_q = 0.6f, .n_decay = 28, .choke = 1 },
        { .name = "Open hat",   .level_db =  -9.0f, .pan = 0.25f, .n_level = 1, .n_centre = 9500, .n_q = 1.3f, .n_decay = 45, .tail_level = 0.06f, .tail_decay = 300, .choke = -1 },
        CT403_CY,
        CT403_LT,
        CT403_HT,
        { .name = "Claves",     .level_db =  -5.8f, .pan =  0.3f, .t_level = 1, .t_freq = 1094, .t_decay = 14,
                                .n_level = 0.16f, .n_centre = 7000, .n_q = 0.5f, .n_decay = 80 },
        CR78_MA, CR78_HB, CR78_LB, CR78_CG, CR78_CB, CR78_TB,
        { .name = "Snare 2",    .level_db =  -9.3f, .t_level = 1, .t_freq = 214, .t_decay = 60, .t_attack = 3, .t_click = 0.1f,
                                .n_level = 0.2f, .n_centre = 7000, .n_q = 0.5f, .n_decay = 80 },
    } },
    /* 0.10.7: the Mini Pops 7's 15 sounds, matched to Iain McCurdy's synthesised
     * imitations (Freesound pack 35537, CC-BY 4.0; tools/minipops7_sounds.h).
     * It had no hi-hats or toms: its cymbals stand in for the hats and its bongos
     * for the toms. */
    { "Mini Pops", {
        { .name = "Kick",       .level_db =  0.0f, .t_level = 1, .t_freq = 64, .t_decay = 50, .t_attack = 9 },
        { .name = "Snare 1",    .level_db = -0.1f, .t_level = 0.12f, .t_freq = 174, .t_decay = 20,
                                .n_level = 1, .n_centre = 4500, .n_q = 0.9f, .n_decay = 55, .tail_level = 0.15f, .tail_decay = 200 },
        { .name = "Rim shot",   .level_db = -9.5f, .t_level = 1, .t_freq = 1700, .t_decay = 2.6f, .t_click = 0.25f },
        { .name = "Cymbal 1",   .level_db = -12.7f, .pan = 0.25f, .n_level = 1, .n_centre = 7500, .n_q = 1.6f, .n_decay = 82, .choke = 1 },
        { .name = "Cymbal 2",   .level_db = -14.7f, .pan = 0.25f, .n_level = 1, .n_centre = 6500, .n_q = 1.4f, .n_decay = 230,
                                .tail_level = 0.2f, .tail_decay = 550, .choke = -1 },
        { .name = "Cymbal 2",   .level_db = -14.7f, .pan = -0.3f, .n_level = 1, .n_centre = 6500, .n_q = 1.4f, .n_decay = 230,
                                .tail_level = 0.2f, .tail_decay = 550 },
        { .name = "Bongo 3",    .level_db = -3.8f, .pan = -0.2f, .t_level = 1, .t_freq = 194, .t_decay = 305, .t_attack = 2.5f },
        { .name = "Bongo 2",    .level_db = -4.5f, .pan =  0.2f, .t_level = 1, .t_freq = 399, .t_decay = 54, .t_attack = 1.5f },
        { .name = "Claves",     .level_db = -14.0f, .pan =  0.3f, .t_level = 1, .t_freq = 400, .t_decay = 34,
                                .r_level = 4.0f, .r_rate = 1, .r_freq = 2180, .r_len = 1, .r_ring = 2.6f, .r_decay = 9999 },
        { .name = "Maracas",    .level_db = -5.0f, .pan =  0.3f, .n_level = 1, .n_centre = 7800, .n_q = 1.3f, .n_decay = 24 },
        { .name = "Bongo 1",    .level_db = -4.0f, .pan = -0.25f, .t_level = 1, .t_freq = 630, .t_decay = 39, .t_attack = 1 },
        { .name = "Bongo 2",    .level_db = -4.5f, .pan = -0.1f, .t_level = 1, .t_freq = 399, .t_decay = 54, .t_attack = 1.5f },
        { .name = "Bongo 3",    .level_db = -3.8f, .pan =  0.15f, .t_level = 1, .t_freq = 194, .t_decay = 305, .t_attack = 2.5f },
        { .name = "Cowbell",    .level_db = -8.8f, .pan =  0.2f, .t_level = 1, .t_freq = 849, .t_decay = 62, .t_freq2 = 4708, .t_level2 = 0.04f },
        { .name = "Tambourine", .level_db = -5.8f, .pan =  0.35f, .r_level = 1, .r_rate = 285, .r_freq = 4600, .r_len = 70, .r_ring = 1.2f,
                                .r_decay = 80, .r_jitter = 0.35f, .r_attack = 5, .n_level = 0.25f, .n_centre = 8000, .n_q = 1.5f, .n_decay = 55 },
        { .name = "Guiro",      .level_db =  3.0f, .pan = -0.3f, .r_level = 1, .r_rate = 66.7f, .r_freq = 3200, .r_len = 232, .r_ring = 0.12f,
                                .r_decay = 9999, .r_accel = 1.09f, .r_attack = 6 },
        { .name = "Snare 2",    .level_db =  0.5f, .t_level = 0.06f, .t_freq = 292, .t_decay = 18,
                                .n_level = 1, .n_centre = 4300, .n_q = 0.9f, .n_decay = 45, .tail_level = 0.15f, .tail_decay = 150 },
        { .name = "Quijada",    .level_db = -3.6f, .pan =  0.3f, .r_level = 1, .r_rate = 22.7f, .r_freq = 2740, .r_len = 380, .r_ring = 1.0f,
                                .r_decay = 350, .r_jitter = 0.05f },
    } },
    { "TR-606", {
        { .name = "Bass drum",  .level_db =  0.0f, .t_level = 1, .t_freq = 55, .t_drop = 1.95f, .t_drop_ms = 45, .t_decay = 300, .t_click = 0.15f },
        { .name = "Snare",      .level_db = -3.5f, .t_level = 1, .t_freq = 180, .t_freq2 = 900, .t_level2 = 0.25f, .t_decay = 70,
                                .n_level = 0.35f, .n_centre = 5500, .n_q = 0.8f, .n_decay = 130 },
        { .name = "Rim shot",   .level_db = -9.0f, .t_level = 1, .t_freq = 1700, .t_decay = 6, .t_click = 0.3f },
        { .name = "Closed hat", .level_db = -12.4f, .pan = 0.25f, .m_level = 1, .m_set = 0, .m_centre = 7000, .m_q = 1.2f, .m_decay = 75, .choke = 1 },
        { .name = "Open hat",   .level_db = -12.8f, .pan = 0.25f, .m_level = 1, .m_set = 0, .m_centre = 7000, .m_q = 1.2f, .m_decay = 360,
                                .hold_ms = 380, .choke = -1 },
        { .name = "Cymbal",     .level_db = -14.6f, .pan = -0.3f, .m_level = 1, .m_set = 0, .m_centre = 5000, .m_q = 0.9f, .m_decay = 120,
                                .tail_level = 0.2f, .tail_decay = 650 },
        { .name = "Low tom",    .level_db = -4.9f, .pan = -0.2f, .t_level = 1, .t_freq = 140, .t_drop = 1.9f, .t_drop_ms = 57, .t_decay = 95 },
        { .name = "High tom",   .level_db = -5.0f, .pan =  0.2f, .t_level = 1, .t_freq = 330, .t_decay = 65 },
        { .name = "Claves",     .level_db = -8.0f, .pan =  0.3f, .t_level = 1, .t_freq = 2500, .t_decay = 15 },
        CR78_MA, CR78_HB, CR78_LB, CR78_CG, CR78_CB, CR78_TB,
        { .name = "Snare 2",    .level_db = -3.7f, .t_level = 1, .t_freq = 217, .t_freq2 = 388, .t_level2 = 0.3f, .t_decay = 49,
                                .n_level = 0.45f, .n_centre = 6000, .n_q = 0.8f, .n_decay = 226 },
    } },
    { "CR-78", {
        { .name = "Bass drum",  .level_db =  0.0f, .t_level = 1, .t_freq = 62.5f, .t_decay = 104, .t_attack = 5 },
        { .name = "Snare",      .level_db = -2.8f, .t_level = 1, .t_freq = 340, .t_decay = 60,
                                .n_level = 0.3f, .n_centre = 7000, .n_q = 0.7f, .n_decay = 60 },
        { .name = "Rim shot",   .level_db = -5.9f, .t_level = 1, .t_freq = 1486, .t_decay = 6 },
        { .name = "Hi-hat",     .level_db = -6.7f, .pan = 0.25f, .n_level = 1, .n_centre = 10000, .n_q = 0.7f, .n_mode = 1, .n_decay = 60, .choke = 1 },
        { .name = "Open hat",   .level_db = -7.4f, .pan = 0.25f, .n_level = 1, .n_centre = 9000, .n_q = 0.7f, .n_mode = 1, .n_decay = 250, .choke = -1 },
        { .name = "Cymbal",     .level_db = -4.6f, .pan = -0.3f, .n_level = 1, .n_centre = 9100, .n_q = 1.5f, .n_decay = 293 },
        { .name = "Low tom",    .level_db = -2.0f, .pan = -0.2f, .t_level = 1, .t_freq = 160, .t_decay = 120 },
        { .name = "High tom",   .level_db = -3.0f, .pan =  0.2f, .t_level = 1, .t_freq = 300, .t_decay = 80 },
        { .name = "Claves",     .level_db = -8.0f, .pan =  0.3f, .t_level = 1, .t_freq = 2645, .t_decay = 18 },
        CR78_MA, CR78_HB, CR78_LB, CR78_CG, CR78_CB, CR78_TB,
        { .name = "Guiro",      .level_db = -4.5f, .pan = -0.3f, .r_level = 1, .r_rate = 125, .r_freq = 6126, .r_len = 180 },
    } },
    /* 0.10.8: the Casio MT-40, fitted to Skipper's samples (tools/drumfit); rim shot and toms from the CT-403 */
    { "MT-40", {
        { .name = "Bass drum", .level_db = 5.3f, .t_level = 1.0f, .t_freq = 102.7f, .t_drop = 4.671f, .t_drop_ms = 0.8948f, .t_decay = 38.46f, .t_attack = 20.0f, .t_click = 0.1816f },
        { .name = "Snare", .level_db = -5.6f, .t_level = 1.0f, .t_freq = 317.2f, .t_freq2 = 318.8f, .t_level2 = 1.192f, .t_decay = 28.48f, .t_attack = 13.08f, .t_click = 0.7284f, .n_level = 0.7953f, .n_centre = 8346.0f, .n_q = 1.205f, .n_decay = 64.71f },
        CT403_RS,
        { .name = "Closed hat", .level_db = -10.3f, .pan = 0.25f, .n_level = 1.0f, .n_centre = 12754.0f, .n_q = 1.436f, .n_decay = 89.19f, .hold_ms = 33.2f, .choke = 1 },
        { .name = "Open hat", .level_db = -9.5f, .pan = 0.25f, .n_level = 1.0f, .n_centre = 11289.0f, .n_q = 1.308f, .n_decay = 115.2f, .hold_ms = 333.1f, .tail_level = 0.4933f, .tail_decay = 30.22f, .choke = -1 },
        { .name = "Cymbal", .level_db = -11.6f, .pan = -0.3f, .n_level = 1.0f, .n_centre = 11977.0f, .n_q = 1.177f, .n_decay = 301.6f, .hold_ms = 376.8f, .tail_level = 0.2555f, .tail_decay = 301.7f },
        CT403_LT, CT403_HT,
        { .name = "Claves", .level_db = -1.0f, .pan = 0.3f, .t_level = 1.0f, .t_freq = 264.8f, .t_decay = 2.632f, .n_level = 2.577f, .n_centre = 5804.0f, .n_q = 8.0f, .n_decay = 26.37f },
        CR78_MA, CR78_HB, CR78_LB, CR78_CG, CR78_CB, CR78_TB,
        { .name = "Snare 2", .level_db = -5.7f, .t_level = 1.0f, .t_freq = 105.6f, .t_freq2 = 308.2f, .t_level2 = 0.4221f, .t_decay = 43.07f, .t_attack = 19.31f, .t_click = 0.8521f, .n_level = 0.4114f, .n_centre = 9546.0f, .n_q = 0.6847f, .n_decay = 61.87f },
    } },
    /* 0.10.8: the Casio MT-18, fitted to Skipper's samples (tools/drumfit); rim shot and cymbal from the CT-403, claves from the MT-40 */
    { "MT-18", {
        { .name = "Bass drum", .level_db = -2.1f, .t_level = 1.0f, .t_freq = 73.71f, .t_drop = 3.221f, .t_drop_ms = 1.894f, .t_decay = 87.66f, .t_attack = 8.426f, .t_click = 0.275f, .t_square = 1.0f, .t_lp = 253.4f },
        { .name = "Snare", .level_db = 0.5f, .t_level = 1.0f, .t_freq = 453.7f, .t_freq2 = 468.5f, .t_level2 = 0.3901f, .t_decay = 2.212f, .t_attack = 2.813f, .t_click = 1.423f, .t_square = 0.4357f, .t_lp = 799.8f, .n_level = 0.01789f, .n_centre = 1099.0f, .n_q = 0.3f, .n_decay = 117.2f },
        CT403_RS,
        { .name = "Closed hat", .level_db = -14.2f, .pan = 0.25f, .n_level = 1.0f, .n_centre = 10944.0f, .n_q = 0.3f, .n_decay = 123.7f, .hold_ms = 10.81f, .choke = 1 },
        { .name = "Open hat", .level_db = -13.3f, .pan = 0.25f, .n_level = 1.0f, .n_centre = 10801.0f, .n_q = 0.4837f, .n_decay = 417.3f, .hold_ms = 651.9f, .tail_level = 0.9f, .tail_decay = 152.5f, .choke = -1 },
        CT403_CY,
        { .name = "Low tom", .level_db = -3.8f, .pan = -0.2f, .t_level = 1.0f, .t_freq = 145.1f, .t_decay = 81.34f, .t_attack = 1.0f, .t_click = 0.1191f, .t_square = 0.9701f, .t_lp = 209.7f },
        { .name = "High tom", .level_db = 2.9f, .pan = 0.2f, .t_level = 1.0f, .t_freq = 296.0f, .t_decay = 67.36f, .t_attack = 1.0f, .t_click = 0.0607f, .t_square = 0.7878f, .t_lp = 102.2f },
        { .name = "Claves", .level_db = -1.0f, .pan = 0.3f, .t_level = 1.0f, .t_freq = 264.8f, .t_decay = 2.632f, .n_level = 2.577f, .n_centre = 5804.0f, .n_q = 8.0f, .n_decay = 26.37f },
        CR78_MA, CR78_HB, CR78_LB, CR78_CG, CR78_CB, CR78_TB,
    } },
    /* 0.16: the Casio VL-1's three sounds (PolyValens' VL1 emulator, Rhythm.cpp): Po, a 780 Hz square falling in
     * 15 steps over 30 cycles (38 ms); Pi, the same twice as fast; Sha, white noise falling in steps over about
     * 180 ms (fast for the first 40 ms). Square tones, no attack, no filtering, as the chip. The other slots
     * borrow them at other pitches and lengths, so any style plays on the kit */
    { "VL-1", {
        { .name = "Po",          .level_db = -2.0f, .t_level = 1, .t_freq = 780, .t_decay = 40, .t_square = 1.0f },
        { .name = "Pi",          .level_db = -2.5f, .t_level = 1, .t_freq = 1560, .t_decay = 20, .t_square = 1.0f },
        { .name = "Pi (rim)",    .level_db = -5.0f, .t_level = 1, .t_freq = 1560, .t_decay = 8, .t_square = 1.0f },
        { .name = "Sha",         .level_db = -9.0f, .n_level = 1, .n_centre = 600, .n_q = 0.5f, .n_mode = 1, .n_decay = 70,
                                 .tail_level = 0.55f, .tail_decay = 260, .choke = 1 },
        { .name = "Sha (long)",  .level_db = -10.0f, .n_level = 1, .n_centre = 600, .n_q = 0.5f, .n_mode = 1, .n_decay = 90,
                                 .tail_level = 0.55f, .tail_decay = 320, .choke = -1 },
        { .name = "Sha (crash)", .level_db = -11.0f, .n_level = 1, .n_centre = 600, .n_q = 0.5f, .n_mode = 1, .n_decay = 120,
                                 .tail_level = 0.5f, .tail_decay = 600 },
        { .name = "Po (low)",    .level_db = -2.0f, .t_level = 1, .t_freq = 390, .t_decay = 45, .t_square = 1.0f },
        { .name = "Po (high)",   .level_db = -2.5f, .t_level = 1, .t_freq = 1040, .t_decay = 35, .t_square = 1.0f },
        { .name = "Pi (claves)", .level_db = -3.5f, .t_level = 1, .t_freq = 2080, .t_decay = 12, .t_square = 1.0f },
        { .name = "Sha (short)", .level_db = -10.0f, .n_level = 1, .n_centre = 600, .n_q = 0.5f, .n_mode = 1, .n_decay = 20 },
        { .name = "Pi (high)",   .level_db = -4.0f, .t_level = 1, .t_freq = 1950, .t_decay = 18, .t_square = 1.0f },
        { .name = "Pi (low)",    .level_db = -3.0f, .t_level = 1, .t_freq = 1170, .t_decay = 18, .t_square = 1.0f },
        { .name = "Po (conga)",  .level_db = -2.0f, .t_level = 1, .t_freq = 585, .t_decay = 40, .t_square = 1.0f },
        { .name = "Pi (bell)",   .level_db = -4.5f, .t_level = 1, .t_freq = 1300, .t_decay = 30, .t_square = 1.0f },
        { .name = "Sha (tamb)",  .level_db = -10.5f, .n_level = 1, .n_centre = 600, .n_q = 0.5f, .n_mode = 1, .n_decay = 40 },
        { .name = "Sha (extra)", .level_db = -10.5f, .n_level = 1, .n_centre = 600, .n_q = 0.5f, .n_mode = 1, .n_decay = 40 },
        { .name = "Pi (snare 2)", .level_db = -3.0f, .t_level = 1, .t_freq = 1300, .t_decay = 15, .t_square = 1.0f },
        { .name = "Sha (quijada)", .level_db = -10.5f, .n_level = 1, .n_centre = 600, .n_q = 0.5f, .n_mode = 1, .n_decay = 60 },
    } },
};

const dr_kit *dr_kit_get(int kit) { return &kits[kit < 0 ? 0 : (kit >= DR_KITS ? DR_KITS - 1 : kit)]; }

/* metal oscillator banks */
static const float metal_606[6] = { 2373, 2579, 2783, 3056, 3614, 3955 };
static const float metal_cr78[3] = { 6170, 5620, 4080 };

/* ---------------------------------------------------------------- engine */

void dr_init(dr_engine *e, double fs)
{
    memset(e, 0, sizeof *e);
    e->fs = fs > 0 ? fs : 48000.0;
    e->rng = 0x2545F491u;
}

void dr_set_kit(dr_engine *e, int kit) { e->kit = kit < 0 ? 0 : (kit >= DR_KITS ? DR_KITS - 1 : kit); }

void dr_all_off(dr_engine *e)
{
    for (int i = 0; i < DR_VOICES; i++) e->v[i].active = 0;
}

static inline float rnd(uint32_t *s)
{
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return (float)(int32_t)*s * (1.0f / 2147483648.0f);
}

static void svf_coef(float fc, float q, double fs, float *g, float *k)
{
    if (fc > 0.45f * (float)fs) fc = 0.45f * (float)fs;
    *g = (float)tan(M_PI * fc / fs);
    *k = 1.0f / (q > 0.1f ? q : 0.1f);
}

/* TPT state-variable filter: mode 0 band-pass (normalised), 1 high-pass */
static inline float svf_run(float x, float g, float k, float *s1, float *s2, int mode)
{
    const float hp = (x - (k + g) * *s1 - *s2) / (1.0f + g * (g + k));
    const float bp = g * hp + *s1;
    *s1 = g * hp + bp;
    const float lp = g * bp + *s2;
    *s2 = g * bp + lp;
    return mode ? hp : bp * k;
}

static inline float decay_k(float ms, double fs)
{
    if (ms <= 0) return 0.0f;
    return (float)exp(-LN20DB / (ms * 0.001 * fs));
}

void dr_trigger(dr_engine *e, int slot, int velocity)
{
    if (slot < 0 || slot >= DR_SLOTS) return;
    const dr_sound *s = &kits[e->kit].sound[slot];
    if (!s->name && slot == DR_S2) {                                   /* 0.10.7: no snare 2 slot: the kit's */
        const dr_sound *ex = &kits[e->kit].sound[DR_EX];               /* "Snare 2" in Extra, else its snare */
        s = ex->name && !strcmp(ex->name, "Snare 2") ? ex : &kits[e->kit].sound[DR_SD];
    }
    if (!s->name) return;                                              /* a slot the kit doesn't have */
    dr_trigger_sound(e, s, slot, velocity);
}

void dr_trigger_sound(dr_engine *e, const dr_sound *s, int slot, int velocity)
{
    /* chokes: a closed hat stops an open one */
    if (s->choke > 0)
        for (int i = 0; i < DR_VOICES; i++)
            if (e->v[i].active && e->v[i].s->choke == -s->choke) {
                e->v[i].close = e->v[i].close > 0 ? e->v[i].close : 1.0f;
                e->v[i].close_k = (float)exp(-1.0 / (0.004 * e->fs));
                e->v[i].hold_n = 0;
            }
    /* voice: a free one, else the oldest */
    int vi = -1;
    for (int i = 0; i < DR_VOICES; i++) if (!e->v[i].active) { vi = i; break; }
    if (vi < 0) {
        vi = 0;
        for (int i = 1; i < DR_VOICES; i++) if (e->age[i] < e->age[vi]) vi = i;
    }
    dr_voice *v = &e->v[vi];
    memset(v, 0, sizeof *v);
    v->s = s;
    v->slot = slot;
    v->active = 1;
    e->age[vi] = ++e->age_counter;
    if (velocity < 1) velocity = 1;
    if (velocity > 127) velocity = 127;
    /* accent: full level at 127, about 4 dB down at 88 */
    v->gain = powf(10.0f, (s->level_db - 12.0f * (1.0f - velocity / 127.0f)) / 20.0f);
    const float p = (s->pan + 1.0f) * 0.25f * (float)M_PI;
    v->pl = cosf(p) * 1.41421356f;
    v->pr = sinf(p) * 1.41421356f;

    const double fs = e->fs;
    v->tone_env = 1.0f;
    v->tone_k = decay_k(s->t_decay, fs);
    v->att_env = s->t_attack > 0 ? 0.0f : 1.0f;
    v->tone_a = s->t_attack > 0 ? (float)(1.0 - exp(-3.0 / (s->t_attack * 0.001 * fs))) : 1.0f;
    v->drop_env = s->t_drop > 1.0f ? s->t_drop - 1.0f : 0.0f;
    v->drop_k = s->t_drop_ms > 0 ? (float)exp(-1.0 / (s->t_drop_ms * 0.001 * fs)) : 0.0f;
    v->click = s->t_click;
    v->tlp_a = s->t_lp > 0 ? (float)(1.0 - exp(-2.0 * M_PI * s->t_lp / fs)) : 1.0f;

    const float decay = s->n_level > 0 ? s->n_decay : s->m_decay;
    v->nm_env1 = 1.0f - s->tail_level;
    v->nm_env2 = s->tail_level;
    v->nm_k1 = decay_k(decay, fs);
    v->nm_k2 = decay_k(s->tail_decay > 0 ? s->tail_decay : decay, fs);
    v->hold_n = s->hold_ms > 0 ? (int)(s->hold_ms * 0.001 * fs) : 0;
    v->close = 0;
    if (s->n_level > 0) svf_coef(s->n_centre, s->n_q, fs, &v->nf_g, &v->nf_k);
    if (s->m_level > 0) {
        svf_coef(s->m_centre, s->m_q, fs, &v->mf_g, &v->mf_k);
        for (int i = 0; i < 6; i++) v->mph[i] = rnd(&e->rng) * 0.5 + 0.5;
    }
    if (s->r_level > 0) {
        const double ring = s->r_ring > 0 ? s->r_ring : 1.5;
        const double r = exp(-1.0 / (ring * 0.001 * fs)), w = 2.0 * M_PI * s->r_freq / fs;
        v->rr_a1 = (float)(2.0 * r * cos(w));
        v->rr_a2 = (float)(-r * r);
        v->r_n = v->r_n0 = (int)(s->r_len * 0.001 * fs);
        v->r_env = 1.0f;
        v->r_k = s->r_decay > 0 ? decay_k(s->r_decay, fs) : 1.0f;
        v->r_att = s->r_attack > 0 ? 0.0f : 1.0f;
        v->r_att_k = s->r_attack > 0 ? (float)(1.0 - exp(-3.0 / (s->r_attack * 0.001 * fs))) : 1.0f;
        const int extras = s->r_ring > 0 || s->r_decay > 0 || s->r_accel > 0 || s->r_jitter > 0 || s->r_attack > 0;
        v->r_phase = extras ? 1.0 : 0.0;   /* with the extras, the first tick at once */
        v->r_bp = extras;                  /* and a true band-pass (no low thump from the ticks) */
        v->r_next = 1.0;
    }
}

/* band-limited square (polyBLEP), phase in cycles */
static inline float blep(double t, double dt)
{
    if (t < dt) { t /= dt; return (float)(t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float)(t * t + t + t + 1.0); }
    return 0.0f;
}

static inline float square(double *ph, double dt)
{
    double t = *ph;
    float y = t < 0.5 ? 1.0f : -1.0f;
    y += blep(t, dt);
    double t2 = t + 0.5; if (t2 >= 1.0) t2 -= 1.0;
    y -= blep(t2, dt);
    t += dt; if (t >= 1.0) t -= 1.0;
    *ph = t;
    return y;
}

void dr_render(dr_engine *e, float *l, float *r, int n)
{
    const double fs = e->fs, inv = 1.0 / fs;
    const float out_gain = 0.35f;
    for (int vi = 0; vi < DR_VOICES; vi++) {
        dr_voice *v = &e->v[vi];
        if (!v->active) continue;
        const dr_sound *s = v->s;
        const float *mf = s->m_set ? metal_cr78 : metal_606;
        const int mn = s->m_set ? 3 : 6;
        for (int i = 0; i < n; i++) {
            float y = 0.0f;
            /* tone */
            if (s->t_level > 0) {
                const float f = s->t_freq * (1.0f + v->drop_env);
                v->drop_env *= v->drop_k;
                v->att_env += (1.0f - v->att_env) * v->tone_a;
                const float a = v->tone_env * v->att_env;
                float t = (float)sin(2.0 * M_PI * v->phase);
                if (s->t_square > 0) {
                    const double dt = f * inv, ph = v->phase;
                    float q = ph < 0.5 ? 1.0f : -1.0f;
                    q += blep(ph, dt);
                    double ph2 = ph + 0.5; if (ph2 >= 1.0) ph2 -= 1.0;
                    q -= blep(ph2, dt);
                    t += s->t_square * (q - t);
                }
                if (s->t_level2 > 0) t += s->t_level2 * (float)sin(2.0 * M_PI * v->phase2);
                v->phase += f * inv; if (v->phase >= 1.0) v->phase -= 1.0;
                v->phase2 += s->t_freq2 * (1.0f + v->drop_env) * inv; if (v->phase2 >= 1.0) v->phase2 -= 1.0;
                if (s->t_lp > 0) { v->tlp_z += (t - v->tlp_z) * v->tlp_a; t = v->tlp_z; }
                y += s->t_level * a * t;
                v->tone_env *= v->tone_k;
                if (v->click > 1e-4f) { y += v->click * rnd(&e->rng); v->click *= 0.92f; }
            }
            /* noise and metal share an envelope */
            if (s->n_level > 0 || s->m_level > 0) {
                float env = v->nm_env1 + v->nm_env2;
                v->nm_env1 *= v->nm_k1;
                v->nm_env2 *= v->nm_k2;
                if (v->hold_n > 0 && --v->hold_n == 0) { v->close = 1.0f; v->close_k = (float)exp(-1.0 / (0.012 * fs)); }
                if (v->close > 0) { env *= v->close; v->close *= v->close_k; }
                if (s->n_level > 0)
                    y += s->n_level * env * svf_run(rnd(&e->rng), v->nf_g, v->nf_k, &v->nf_s1, &v->nf_s2, s->n_mode);
                if (s->m_level > 0) {
                    float m = 0.0f;
                    for (int k = 0; k < mn; k++) m += square(&v->mph[k], mf[k] * inv);
                    m *= 1.0f / mn;
                    m = svf_run(m, v->mf_g, v->mf_k, &v->mf_s1, &v->mf_s2, 0);
                    /* gentle high-pass: the metal's body is above 2 kHz */
                    const float hp = m - v->mhp_z; v->mhp_z += 0.25f * hp;
                    y += s->m_level * env * hp * 1.6f;
                }
            }
            /* rasp: a pulse train ringing a resonator */
            if (s->r_level > 0 && (v->r_n > 0 || fabsf(v->rr_y1) > 1e-5f)) {
                const float pos = v->r_n0 > 0 ? 1.0f - (float)v->r_n / (float)v->r_n0 : 1.0f;
                float x = 0.0f;
                if (v->r_n > 0) {
                    const double rate = s->r_accel > 0 ? s->r_rate * (1.0 + (s->r_accel - 1.0) * pos * pos) : s->r_rate;   /* speeds up towards the end */
                    v->r_phase += rate * inv;
                    if (v->r_phase >= v->r_next) {
                        v->r_phase -= v->r_next;
                        v->r_next = 1.0 + (s->r_jitter > 0 ? s->r_jitter * rnd(&e->rng) : 0.0);
                        x = 1.0f;
                    }
                    v->r_n--;
                }
                float in = x;
                if (v->r_bp) { in = x - v->rx2; v->rx2 = v->rx1; v->rx1 = x; }
                const float yy = in + v->rr_a1 * v->rr_y1 + v->rr_a2 * v->rr_y2;
                v->rr_y2 = v->rr_y1; v->rr_y1 = yy;
                v->r_att += (1.0f - v->r_att) * v->r_att_k;
                const float shape = s->r_decay > 0 ? v->r_env : (0.4f + 0.6f * pos);
                y += s->r_level * 0.67f * yy * shape * v->r_att;
                v->r_env *= v->r_k;
            }
            y *= v->gain * out_gain;
            l[i] += y * v->pl;
            r[i] += y * v->pr;
            v->t += inv;
        }
        /* finished? */
        const int tone_done = s->t_level <= 0 || v->tone_env < 1e-4f;
        const int nm_done = (s->n_level <= 0 && s->m_level <= 0) ||
                            ((v->nm_env1 + v->nm_env2) * (v->close > 0 ? v->close : 1.0f) < 1e-4f && v->hold_n <= 0);
        const int r_done = s->r_level <= 0 || (v->r_n <= 0 && fabsf(v->rr_y1) < 1e-5f);
        if (tone_done && nm_done && r_done) v->active = 0;
    }
}
