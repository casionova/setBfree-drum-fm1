/*
 * rhythm - the rhythm section's style player.
 *
 * A style has six parts, as on an arranger keyboard: Intro, Main A,
 * Fill into B, Main B, Fill into A, Outro. Patterns run on a 12-ticks-per-
 * beat grid (48 per 4/4 bar, 36 per 3/4 bar), so sixteenths and triplets
 * are both exact. Every hit names a kit slot (drums.h) and a velocity
 * (accented hits 127).
 *
 * Commands (buttons): Start/Stop, Intro/Fill, A, B, Outro. Intro/Fill is
 * one button (RH_CMD_INTRO and RH_CMD_FILL do the same): stopped, it starts
 * with the intro; playing, it plays the fill into the other variation at
 * once, from the current position to the end of the bar (in the last beat
 * of a bar, the whole next bar). A and B switch variation at the next bar
 * line with no fill. Outro plays the ending at the next bar line and stops.
 * After a fill, the next part opens with a cymbal.
 *
 * Plain C99, no allocation.
 */
#ifndef RHYTHM_H
#define RHYTHM_H

#include <stdint.h>
#include "drums.h"

enum { RH_INTRO = 0, RH_MAIN_A, RH_FILL_B, RH_MAIN_B, RH_FILL_A, RH_OUTRO, RH_PARTS };
enum { RH_CMD_START_STOP = 0, RH_CMD_INTRO, RH_CMD_FILL, RH_CMD_A, RH_CMD_B, RH_CMD_OUTRO, RH_CMDS };

#define RH_TICKS_PER_BEAT 12

typedef struct { uint16_t tick; uint8_t slot, vel; } rh_hit;
typedef struct { uint8_t bars; uint16_t n; const rh_hit *hits; } rh_part;
typedef struct {
    const char *name;
    uint8_t beats;                 /* beats per bar: 4 or 3 */
    rh_part part[RH_PARTS];
    uint8_t swing;                 /* 0.10.7: the style's own swing, per cent (0 = straight, 50) */
} rh_style;

int             rh_style_count(void);
const rh_style *rh_style_get(int i);
/* 0.10.7: a style's own swing, 50..75 % */
static inline int rh_style_swing(const rh_style *s) { return s && s->swing >= 50 ? s->swing : 50; }

typedef struct {
    const rh_style *style, *next_style;
    int    running;
    int    part;                   /* current part */
    int    variation;              /* RH_MAIN_A or RH_MAIN_B: where fills land */
    int    pending;                /* part queued for the next bar line, -1 none */
    int    stop_after;             /* stop when the current part ends (outro) */
    int    intro_armed;
    int    crash;                  /* add a cymbal at the start of the next part */
    double pos;                    /* ticks into the current part */
    double swing;                  /* 0.10.7: 0.5 straight .. 0.75: where the off-beat 16ths fall in their 8th */
    void (*on_hit)(void *user, int slot, int vel);   /* optional: called for every hit */
    void  *user;
} rh_player;

void rh_init(rh_player *p, const rh_style *s);
void rh_set_style(rh_player *p, const rh_style *s);       /* at the next bar line if running */
void rh_command(rh_player *p, int cmd);
void rh_stop(rh_player *p);
void rh_start(rh_player *p);
/* Move to a position in the bar (beats from the bar line), e.g. to follow the host */
void rh_set_bar_phase(rh_player *p, double beats);
double rh_bar_phase(const rh_player *p);                   /* beats into the current bar */

/* Advance by `ticks` (fractional), triggering hits into the drum engine.
 * Call per block or per sample run; hits are placed at the block start. */
void rh_advance(rh_player *p, double ticks, dr_engine *d);

/* Advance across a block of n samples at the given tempo, rendering the
 * drums with hits placed at the right sample. Adds into l/r. */
void rh_run(rh_player *p, dr_engine *d, double bpm, double fs, float *l, float *r, int n);

#endif
