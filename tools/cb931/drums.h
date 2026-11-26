/*
 * drums - the rhythm section's sound engine: four kits of synthesised
 * analogue drums (CT-403, Mini Pops, TR-606, CR-78), no samples.
 *
 * Every sound is built from up to four parts, each optional:
 *   tone   a sine "ping" with its own decay, an optional second tone, a
 *          pitch drop (606 bass drum and toms) and a hard or soft attack;
 *   noise  filtered white noise (band-pass or high-pass);
 *   metal  a bank of square-wave oscillators through a band-pass (606 hats
 *          and cymbal, CR-78 metallic beat);
 *   rasp   a pulse train ringing a resonator (CR-78 guiro).
 * Noise and metal share an envelope that can hold then close (open hat) or
 * fall fast then ring on quietly (cymbal).
 *
 * Kits fill the same 18 slots (DR_*), so any rhythm plays on any kit. A
 * kit without a snare 2 plays its snare there (0.10.7).
 * Decay times are "time to fall 20 dB" (as the references were measured).
 *
 * Plain C99, float maths, no allocation.
 */
#ifndef DRUMS_H
#define DRUMS_H

#include <stdint.h>

enum {
    DR_BD = 0, DR_SD, DR_RS, DR_CH, DR_OH, DR_CY, DR_LT, DR_HT,
    DR_CL, DR_MA, DR_HB, DR_LB, DR_CG, DR_CB, DR_TB, DR_EX,
    DR_S2, DR_QJ,                  /* 0.10.7: snare 2 and quijada (the Mini Pops 7's) */
    DR_SLOTS
};

enum { DR_KIT_CT403 = 0, DR_KIT_MINIPOPS, DR_KIT_TR606, DR_KIT_CR78, DR_KIT_MT40, DR_KIT_MT18, DR_KIT_VL1, DR_KITS };   /* 0.16: the VL-1 */

typedef struct {
    const char *name;
    float level_db, pan;           /* pan -1..1 */
    /* tone */
    float t_level, t_freq, t_freq2, t_level2, t_drop, t_drop_ms, t_decay, t_attack, t_click;
    /* noise: mode 0 band-pass, 1 high-pass */
    float n_level, n_centre, n_q; int n_mode; float n_decay;
    /* metal: set 0 = TR-606 six oscillators, 1 = CR-78 three */
    float m_level; int m_set; float m_centre, m_q, m_decay;
    /* shared noise/metal envelope extras */
    float hold_ms, tail_level, tail_decay;
    /* rasp */
    float r_level, r_rate, r_freq, r_len;
    /* 0.10.7 rasp extras (0 = as before): ring time of each tick (ms, default
     * 1.5), fade (ms to fall 20 dB; 0 = the old rising ramp), rate at the end
     * as a multiple of the start (accelerando), timing wobble (0..1 of a
     * period), attack (ms) */
    float r_ring, r_decay, r_accel, r_jitter, r_attack;
    int   choke;                   /* hits on a sound with choke > 0 silence other sounds with choke == -that */
    /* 0.10.8: tone wave, 0 sine .. 1 square (the MT-18's tones are square-ish) */
    float t_square;
    float t_lp;                    /* 0.10.8: one-pole low-pass on the tone, Hz (0 = off) */
} dr_sound;

typedef struct {
    const char *name;
    dr_sound sound[DR_SLOTS];
} dr_kit;

const dr_kit *dr_kit_get(int kit);
const char   *dr_slot_name(int slot);

#define DR_VOICES 24

typedef struct {
    const dr_sound *s;
    int    slot, active;
    float  gain, pl, pr;
    double t;                      /* seconds since the hit */
    double phase, phase2;
    float  tone_k, tone_a;         /* per-sample decay and attack factors */
    float  tone_env, att_env, drop_env, drop_k;
    float  nm_env1, nm_env2, nm_k1, nm_k2, close, close_k;
    int    hold_n;
    /* noise filter (TPT state variable) */
    float  nf_g, nf_k, nf_s1, nf_s2;
    /* metal: oscillator phases, band-pass and high-pass */
    double mph[6];
    float  mf_g, mf_k, mf_s1, mf_s2, mhp_z;
    /* rasp */
    double r_phase, r_next;
    float  rr_y1, rr_y2, rr_a1, rr_a2;
    float  r_env, r_k, r_att, r_att_k, rx1, rx2;
    int    r_n, r_n0, r_bp;
    float  click;
    float  tlp_a, tlp_z;           /* 0.10.8: the tone's low-pass */
} dr_voice;

typedef struct {
    double fs;
    int    kit;
    uint32_t rng;
    dr_voice v[DR_VOICES];
    uint32_t age[DR_VOICES], age_counter;
} dr_engine;

void dr_init(dr_engine *e, double fs);
void dr_set_kit(dr_engine *e, int kit);
/* velocity 1..127 */
void dr_trigger(dr_engine *e, int slot, int velocity);
/* 0.10.7: play any sound (not from a kit), for auditioning and tests */
void dr_trigger_sound(dr_engine *e, const dr_sound *s, int slot, int velocity);
void dr_all_off(dr_engine *e);
/* adds into the buffers */
void dr_render(dr_engine *e, float *l, float *r, int n);

#endif
