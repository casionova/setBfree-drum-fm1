/*
 * rhythm - see rhythm.h.
 */
#include "rhythm.h"
#include <math.h>
#include <string.h>

static int bar_ticks(const rh_style *s) { return s->beats * RH_TICKS_PER_BEAT; }
static int part_ticks(const rh_style *s, int part)
{
    const int bars = s->part[part].bars > 0 ? s->part[part].bars : 1;
    return bars * bar_ticks(s);
}

void rh_init(rh_player *p, const rh_style *s)
{
    memset(p, 0, sizeof *p);      /* on_hit cleared too: set it after rh_init */
    p->style = s;
    p->part = RH_MAIN_A;
    p->variation = RH_MAIN_A;
    p->pending = -1;
}

void rh_set_style(rh_player *p, const rh_style *s)
{
    if (!s || s == p->style) { p->next_style = 0; return; }
    if (p->running) p->next_style = s;
    else { p->style = s; p->next_style = 0; p->pos = 0; }
}

void rh_start(rh_player *p)
{
    p->running = 1;
    p->part = p->intro_armed ? RH_INTRO : p->variation;
    p->intro_armed = 0;
    p->pending = -1;
    p->stop_after = (p->part == RH_OUTRO);
    p->crash = 0;
    p->pos = 0;
}

void rh_stop(rh_player *p)
{
    p->running = 0;
    p->pending = -1;
    p->stop_after = 0;
    p->pos = 0;
}

/* Fill now: from the current position in the bar to its end, so the fill
 * starts at once (as on Casio keyboards). Pressed in the last beat of a bar,
 * it plays the whole of the next bar instead. */
static void fill_now(rh_player *p)
{
    if (p->part != RH_MAIN_A && p->part != RH_MAIN_B) return;   /* intro, fill, outro: ignored */
    if (p->pending == RH_OUTRO) return;
    const int fill = (p->part == RH_MAIN_A) ? RH_FILL_B : RH_FILL_A;
    const int bt = p->style->beats * RH_TICKS_PER_BEAT;
    const double in_bar = fmod(p->pos, bt);
    if (bt - in_bar < RH_TICKS_PER_BEAT) { p->pending = fill; return; }
    p->part = fill;
    p->pos = in_bar;
    p->pending = -1;
    p->stop_after = 0;
}

void rh_command(rh_player *p, int cmd)
{
    switch (cmd) {
    case RH_CMD_START_STOP:
        if (p->running) rh_stop(p); else rh_start(p);
        break;
    case RH_CMD_INTRO:            /* Intro/Fill: one button */
    case RH_CMD_FILL:
        if (!p->running) { p->intro_armed = 1; rh_start(p); }
        else fill_now(p);
        break;
    case RH_CMD_A:
    case RH_CMD_B: {
        const int v = (cmd == RH_CMD_A) ? RH_MAIN_A : RH_MAIN_B;
        if (!p->running) { p->variation = v; break; }
        if (p->pending == RH_OUTRO) break;
        /* no fill: the other variation takes over at the next bar line (or
         * when the intro or fill playing now ends) */
        if (p->part == v) p->pending = -1;
        else p->pending = v;
        break;
    }
    case RH_CMD_OUTRO:
        if (p->running) p->pending = RH_OUTRO;
        break;
    }
}

double rh_bar_phase(const rh_player *p)
{
    const int bt = bar_ticks(p->style);
    return fmod(p->pos, bt) / RH_TICKS_PER_BEAT;
}

void rh_set_bar_phase(rh_player *p, double beats)
{
    const int bt = bar_ticks(p->style);
    double b = fmod(beats, (double)p->style->beats);
    if (b < 0) b += p->style->beats;
    p->pos = floor(p->pos / bt) * bt + b * RH_TICKS_PER_BEAT;
}

/* 0.10.7: swing moves the off-beat 16ths (tick 3 of each 8th) later: at 0.5
 * they are halfway through their 8th, at 0.75 three quarters of the way */
static double swung(const rh_player *p, int tick)
{
    const int in8 = tick % (RH_TICKS_PER_BEAT / 2);
    if (in8 != RH_TICKS_PER_BEAT / 4 || p->swing <= 0.5) return tick;
    const double sw = p->swing > 0.75 ? 0.75 : p->swing;
    return tick + (sw - 0.5) * (RH_TICKS_PER_BEAT / 2);
}

static void fire(const rh_player *p, double from, double to, dr_engine *d)
{
    const rh_part *pt = &p->style->part[p->part];
    for (int i = 0; i < pt->n; i++) {
        const double t = swung(p, pt->hits[i].tick);
        if (t >= from && t < to) {
            if (d) dr_trigger(d, pt->hits[i].slot, pt->hits[i].vel);
            if (p->on_hit) p->on_hit(p->user, pt->hits[i].slot, pt->hits[i].vel);
        }
    }
}

static int default_next(const rh_player *p)
{
    switch (p->part) {
    case RH_INTRO:  return p->variation;
    case RH_FILL_A: return RH_MAIN_A;
    case RH_FILL_B: return RH_MAIN_B;
    case RH_MAIN_A: return RH_MAIN_A;
    case RH_MAIN_B: return RH_MAIN_B;
    default:        return -1;     /* outro: stop */
    }
}

static void enter(rh_player *p, int next, dr_engine *d)
{
    const int from_fill = p->part == RH_FILL_A || p->part == RH_FILL_B;
    if (p->next_style) { p->style = p->next_style; p->next_style = 0; }
    p->part = next;
    p->pos = 0;
    p->pending = -1;
    if (next == RH_MAIN_A || next == RH_MAIN_B) p->variation = next;
    p->stop_after = (next == RH_OUTRO);
    if (from_fill && next != RH_OUTRO) {        /* land the fill */
        if (d) dr_trigger(d, DR_CY, 120);
        if (p->on_hit) p->on_hit(p->user, DR_CY, 120);
    }
}

void rh_advance(rh_player *p, double ticks, dr_engine *d)
{
    int guard = 64;
    while (p->running && ticks > 1e-9 && guard-- > 0) {
        const int bt = bar_ticks(p->style);
        const int pt = part_ticks(p->style, p->part);
        double next_bar = (floor(p->pos / bt + 1e-9) + 1.0) * bt;
        if (next_bar > pt) next_bar = pt;
        double step = next_bar - p->pos;
        if (step > ticks) step = ticks;
        fire(p, p->pos, p->pos + step, d);
        p->pos += step;
        ticks -= step;
        if (p->pos < next_bar - 1e-9) break;          /* mid-bar */
        /* at a bar line */
        const int is_main = p->part == RH_MAIN_A || p->part == RH_MAIN_B;
        if (p->pos >= pt - 1e-9) {
            if (p->stop_after) { rh_stop(p); break; }
            const int next = p->pending >= 0 ? p->pending : default_next(p);
            if (next < 0) { rh_stop(p); break; }
            enter(p, next, d);
        } else if (is_main && p->pending >= 0) {
            enter(p, p->pending, d);
        } else if (p->next_style && is_main) {
            /* a new style takes over at the bar line, in the same variation */
            p->style = p->next_style;
            p->next_style = 0;
            p->pos = 0;
        }
    }
}

void rh_run(rh_player *p, dr_engine *d, double bpm, double fs, float *l, float *r, int n)
{
    const int chunk = 16;
    const double tps = bpm / 60.0 * RH_TICKS_PER_BEAT / fs;
    for (int i = 0; i < n; i += chunk) {
        const int m = n - i < chunk ? n - i : chunk;
        if (p->running) rh_advance(p, tps * m, d);
        dr_render(d, l + i, r + i, m);
    }
}
