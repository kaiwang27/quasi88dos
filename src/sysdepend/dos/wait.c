/* PIT-count pacing. Reads the BIOS timer and channel 0 without changing either. */
#include <i86.h>
#include <conio.h>
#include <limits.h>
#include <string.h>
#include "quasi88.h"
#include "wait.h"

#define PIT_HZ 1193182UL
#define PIT_DAY_OFFSET 0x00b00000UL

static unsigned long last_bios_tick, day_offset;
static unsigned long last_pit_time;
static int pit_started;
static unsigned long period_counts, next_time;
static int late_frames;

/* PIT channel 0 is the BIOS 18.2 Hz clock source. Combine its current
 * down-counter with INT 1Ah ticks for sub-tick timing. Sampling the BIOS tick
 * on both sides avoids a torn sample at the 65536-count reload.
 */
static unsigned long pit_time(void)
{
    union REGS before, after;
    unsigned long ticks_before, ticks_after, raw;
    unsigned int count;
    int tries = 0;

    do {
        memset(&before, 0, sizeof(before));
        before.h.ah = 0;
        int386(0x1a, &before, &before);
        ticks_before = ((unsigned long)before.w.cx << 16) | before.w.dx;
        outp(0x43, 0);
        count = (unsigned int)inp(0x40);
        count |= (unsigned int)inp(0x40) << 8;
        memset(&after, 0, sizeof(after));
        after.h.ah = 0;
        int386(0x1a, &after, &after);
        ticks_after = ((unsigned long)after.w.cx << 16) | after.w.dx;
    } while (ticks_before != ticks_after && ++tries < 4);

    if (ticks_before != ticks_after) ticks_before = ticks_after;
    if (pit_started && ticks_before < last_bios_tick)
        day_offset += PIT_DAY_OFFSET;
    last_bios_tick = ticks_before;
    raw = ((ticks_before & 0xffffUL) << 16) + (0xffffU - count);
    last_pit_time = raw + day_offset;
    pit_started = TRUE;
    return last_pit_time;
}

static unsigned long usec_to_counts(unsigned long usec)
{
    unsigned long seconds = usec / 1000000UL;
    unsigned long remainder = usec % 1000000UL;
    unsigned long counts = seconds * PIT_HZ;
    /* Split the remainder product to stay inside 32-bit arithmetic. */
    counts += (remainder * 1193UL + 500UL) / 1000UL;
    counts += (remainder * 182UL + 500000UL) / 1000000UL;
    return counts ? counts : 1;
}

int wait_vsync_init(void)
{
    pit_started = FALSE;
    day_offset = 0;
    (void)pit_time();
    return TRUE;
}

void wait_vsync_exit(void) {}

void wait_vsync_setup(long vsync_cycle_us, int sleep)
{
    if (vsync_cycle_us < 1) vsync_cycle_us = 1;
    period_counts = usec_to_counts((unsigned long)vsync_cycle_us);
    (void)sleep;
    late_frames = 0;
    next_time = pit_time() + period_counts;
}

int wait_vsync_update(void)
{
    unsigned long now, remaining;
    int on_time = FALSE;

    now = pit_time();
    remaining = next_time - now;
    if (remaining < 0x80000000UL) {
        /* Poll the hardware counter, not a CPU-speed delay loop. DOS has no
         * portable sub-frame sleep API that is reliable across BIOS/extenders.
         */
        do {
            now = pit_time();
            remaining = next_time - now;
        } while (remaining && remaining < 0x80000000UL);
        on_time = TRUE;
        late_frames = 0;
    } else if (++late_frames >= 10) {
        next_time = now + period_counts;
        late_frames = 0;
        return WAIT_OVER;
    }
    next_time += period_counts;
    return on_time ? WAIT_JUST : WAIT_OVER;
}

void wait_get_current_time(union wait_time *time)
{
    time->l[0] = pit_time();
}

int wait_calc_elasped_time_ms(const union wait_time *got_time)
{
    unsigned long counts = pit_time() - got_time->l[0];
    unsigned long seconds = counts / PIT_HZ;
    unsigned long remainder = counts % PIT_HZ;
    unsigned long ms = seconds * 1000UL + remainder * 1000UL / PIT_HZ;
    return ms > INT_MAX ? INT_MAX : (int)ms;
}
