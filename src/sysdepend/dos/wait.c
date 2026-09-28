/* Bring-up runs unthrottled. clock() is only for elapsed-time bookkeeping.
 * No PIT programming, interrupt hooks, sleeps, or CPU-speed delay loops.
 */
#include <time.h>
#include <limits.h>
#include "quasi88.h"
#include "wait.h"
int wait_vsync_init(void) { return TRUE; }
void wait_vsync_exit(void) {}
void wait_vsync_setup(long usec, int sleep) { (void)usec; (void)sleep; }
int wait_vsync_update(void) { return WAIT_OVER; }
void wait_get_current_time(union wait_time *t) { t->l[0] = (unsigned long)clock(); }
int wait_calc_elasped_time_ms(const union wait_time *t)
{
    unsigned long delta = (unsigned long)clock() - t->l[0];
    double ms = (double)delta * 1000.0 / CLOCKS_PER_SEC;
    return ms > INT_MAX ? INT_MAX : (int)ms;
}
