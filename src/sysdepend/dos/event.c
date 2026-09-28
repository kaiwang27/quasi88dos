/* No emulated keyboard/mouse yet. Escape requests an early diagnostic exit. */
#include <conio.h>
#include "quasi88.h"
#include "event.h"
void event_init(void) {}
void event_exit(void) {}
void event_switch(void) {}
void event_update(void) { if (kbhit() && getch() == 27) quasi88_quit(); }
void event_get_mouse_pos(int *x, int *y) { *x = 0; *y = 0; }
int event_keylayout_change(void) { return FALSE; }
void event_keylayout_revert(void) {}
int event_get_joystick_num(void) { return 0; }
