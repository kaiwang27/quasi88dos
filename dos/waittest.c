/* Copyright (c) 2026, Kai Wang. Part of the QUASI88 MS-DOS port.
   SPDX-License-Identifier: BSD-3-Clause (see LICENSE). */
#include <stdio.h>
#include "wait.h"

#define TEST_FRAMES 60
#define TEST_PERIOD_US 18050L
#define MINIMUM_ELAPSED_MS 1080

int main(void)
{
    union wait_time start;
    int i, elapsed;
    wait_vsync_init();
    wait_vsync_setup(TEST_PERIOD_US, 0);
    wait_get_current_time(&start);
    for (i = 0; i < TEST_FRAMES; ++i) wait_vsync_update();
    elapsed = wait_calc_elasped_time_ms(&start);
    wait_vsync_exit();
    printf("DOS: PIT pacing %d x %ldus = %dms; %s\n", TEST_FRAMES,
           TEST_PERIOD_US, elapsed,
           elapsed >= MINIMUM_ELAPSED_MS ? "PASS" : "FAIL");
    return elapsed >= MINIMUM_ELAPSED_MS ? 0 : 1;
}
