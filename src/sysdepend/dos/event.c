/* BIOS polling, no IRQ hook. Three emulated frames pressed, then three
 * released: BASIC typing only, not independent held keys/game controls.
 */
#include <bios.h>
#include <conio.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>
#include "quasi88.h"
#include "event.h"
#include "keyboard.h"
#include "device.h"
#include "wait.h"
static int pressed, shift, control, remaining;
static int mouse_available;
static int gameport_joystick;
static unsigned int gameport_buttons;
static unsigned int mouse_buttons;
static int mouse_x = 320, mouse_y = 240;
static int last_mouse_x = 320, last_mouse_y = 240;
static void log_mouse_state(void)
{
    FILE *fp;
    if (!dos_mouse_log) return;
    fp = fopen("MOUSE.LOG", "a");
    if (fp) {
        fprintf(fp, "x=%d y=%d buttons=%u\n", mouse_x, mouse_y, mouse_buttons);
        fclose(fp);
    }
}
static unsigned long delivered;
static void log_key(unsigned int key, unsigned int shift_status)
{
    FILE *fp;
    if (!dos_key_log) return;
    fp = fopen("KEYS.LOG", "a");
    if (fp) {
        fprintf(fp, "scan=%02X ascii=%02X shift=%04X\n",
                key >> 8, key & 255, shift_status);
        fclose(fp);
    }
}
unsigned long dos_key_count(void) { return delivered; }
void dos_mouse_video_update_begin(void)
{
    union REGS regs;
    if (!mouse_available) return;
    memset(&regs, 0, sizeof(regs));
    regs.w.ax = 2;
    int386(0x33, &regs, &regs);
}
void dos_mouse_video_update_end(void)
{
    union REGS regs;
    if (!mouse_available) return;
    memset(&regs, 0, sizeof(regs));
    regs.w.ax = 1;
    int386(0x33, &regs, &regs);
}
static void release_key(void)
{
    if (pressed) quasi88_key(pressed, FALSE);
    if (shift) quasi88_key(KEY88_SHIFT, FALSE);
    if (control) quasi88_key(KEY88_CTRL, FALSE);
    pressed = shift = control = 0;
}
static void release_mouse(void)
{
    static const int codes[3] = {KEY88_MOUSE_L, KEY88_MOUSE_R, KEY88_MOUSE_M};
    int i;
    for (i = 0; i < 3; ++i) {
        if (mouse_buttons & (1U << i)) quasi88_mouse(codes[i], FALSE);
    }
    mouse_buttons = 0;
}
static int probe_gameport(void)
{
    union wait_time start;
    unsigned int status;
    unsigned long polls = 0;
    outp(0x201, 0);
    wait_get_current_time(&start);
    status = (unsigned int)inp(0x201);
    if ((status & 0x03) != 0x03) return FALSE;
    do {
        status = (unsigned int)inp(0x201);
        if ((status & 0x03) != 0x03) return TRUE;
        if ((++polls & 255UL) == 0 &&
            wait_calc_elasped_time_ms(&start) >= 10) break;
    } while (TRUE);
    return FALSE;
}
static void poll_gameport(void)
{
    unsigned int status, buttons, changed;
    int i;
    if (!gameport_joystick) return;
    status = (unsigned int)inp(0x201);
    buttons = (unsigned int)((~status >> 4) & 0x03);
    changed = buttons ^ gameport_buttons;
    for (i = 0; i < 2; ++i) {
        if (changed & (1U << i))
            quasi88_pad(KEY88_PAD1_A + i, (buttons & (1U << i)) != 0);
    }
    gameport_buttons = buttons;
}
static void release_gameport(void)
{
    int i;
    for (i = 0; i < 2; ++i) {
        if (gameport_buttons & (1U << i))
            quasi88_pad(KEY88_PAD1_A + i, FALSE);
    }
    gameport_buttons = 0;
}
void event_init(void)
{
    FILE *fp;
    pressed = shift = control = remaining = 0;
    mouse_available = FALSE;
    gameport_joystick = FALSE;
    gameport_buttons = 0;
    mouse_buttons = 0;
    mouse_x = 320;
    mouse_y = 240;
    last_mouse_x = 320;
    last_mouse_y = 240;
    if (dos_mouse_log && (fp = fopen("MOUSE.LOG", "w")) != NULL) {
        fputs("DOS INT 33h mouse log\n", fp);
        fclose(fp);
    }
    if (dos_vga) {
        union REGS regs;
        memset(&regs, 0, sizeof(regs));
        regs.w.ax = 0;
        int386(0x33, &regs, &regs);
        if (regs.w.ax == 0xffff) {
            mouse_available = TRUE;
            memset(&regs, 0, sizeof(regs));
            regs.w.ax = 1;
            int386(0x33, &regs, &regs);
        }
    }
    gameport_joystick = probe_gameport();
    printf("DOS: game-port joystick %s (two buttons only)\n",
           gameport_joystick ? "detected" : "not detected");
    if (dos_mouse_log) {
        fp = fopen("MOUSE.LOG", "a");
        if (fp) {
            fprintf(fp, "driver=%s\n", mouse_available ? "installed" : "not-installed");
            fclose(fp);
        }
    }
    delivered = 0;
    if (dos_key_log && (fp = fopen("KEYS.LOG", "w")) != NULL) {
        fputs("DOS BIOS keyboard scan log\n", fp);
        fclose(fp);
    }
}
void event_exit(void) { release_key(); release_mouse(); release_gameport(); }
void event_switch(void)
{
    release_key(); release_mouse(); release_gameport(); remaining = 0;
}
static void poll_keyboard(void)
{
    unsigned int key, shift_status;
    int ascii, scan, code = 0;
    if (remaining) {
        if (--remaining == 3) release_key();
        return;
    }
    /* AH=10h/11h expose enhanced keyboard keys including F11 and F12. */
    if (!_bios_keybrd(_NKEYBRD_READY)) return;
    key = _bios_keybrd(_NKEYBRD_READ);
    shift_status = _bios_keybrd(_NKEYBRD_SHIFTSTATUS);
    ascii = key & 255;
    scan = key >> 8;
    log_key(key, shift_status);
    if (scan == 0x10 &&
        (ascii == 17 || (ascii == 0 && (shift_status & 0x0c) == 0x0c))) {
        puts((shift_status & 0x08) ?
             "DOS: Ctrl+Alt+Q received; returning to DOS." :
             "DOS: Ctrl+Q received; returning to DOS.");
        quasi88_quit();
        return;
    }
    if (scan == 0x57 || scan == 0x85) code = KEY88_SYS_STATUS;
    else if (scan == 0x58 || scan == 0x86) code = KEY88_SYS_MENU;
    else if (ascii >= 32 && ascii <= 126) {
        code = ascii;
        if (strchr("!\"#$%&'()=+*<>?_`{|}~", ascii)) shift = TRUE;
    } else {
        switch (ascii) {
        case 8: code = KEY88_BS; break;
        case 9: code = KEY88_TAB; break;
        case 13: code = KEY88_RETURN; break;
        case 27: code = KEY88_ESC; break;
        default:
            if (ascii >= 1 && ascii <= 26) { code = 'A' + ascii - 1; control = TRUE; }
            else switch (scan) {
                case 0x3b: code = KEY88_F1; break;
                case 0x3c: code = KEY88_F2; break;
                case 0x3d: code = KEY88_F3; break;
                case 0x3e: code = KEY88_F4; break;
                case 0x3f: code = KEY88_F5; break;
                case 0x40: code = KEY88_F6; break;
                case 0x41: code = KEY88_F7; break;
                case 0x42: code = KEY88_F8; break;
                case 0x43: code = KEY88_F9; break;
                case 0x44: code = KEY88_F10; break;
                case 0x47: code = KEY88_HOME; break;
                case 0x48: code = KEY88_UP; break;
                case 0x4b: code = KEY88_LEFT; break;
                case 0x4d: code = KEY88_RIGHT; break;
                case 0x50: code = KEY88_DOWN; break;
                case 0x52: code = KEY88_INS; break;
                case 0x53: code = KEY88_DEL; break;
            }
        }
    }
    if (code) {
        if (shift) quasi88_key(KEY88_SHIFT, TRUE);
        if (control) quasi88_key(KEY88_CTRL, TRUE);
        quasi88_key(code, TRUE);
        pressed = code;
        remaining = 6;
        ++delivered;
    }
}
static void poll_mouse(void)
{
    union REGS regs;
    unsigned int buttons;
    static const int codes[3] = {KEY88_MOUSE_L, KEY88_MOUSE_R, KEY88_MOUSE_M};
    int i, changed;
    if (!mouse_available) return;
    memset(&regs, 0, sizeof(regs));
    regs.w.ax = 3;
    int386(0x33, &regs, &regs);
    mouse_x = regs.w.cx;
    mouse_y = regs.w.dx;
    buttons = regs.w.bx & 7;
    changed = mouse_x != last_mouse_x || mouse_y != last_mouse_y || buttons != mouse_buttons;
    last_mouse_x = mouse_x;
    last_mouse_y = mouse_y;
    quasi88_mouse_moved_abs(mouse_x, mouse_y);
    for (i = 0; i < 3; ++i) {
        if (((buttons ^ mouse_buttons) & (1U << i)) != 0) {
            quasi88_mouse(codes[i], (buttons & (1U << i)) != 0);
        }
    }
    mouse_buttons = buttons;
    if (changed) log_mouse_state();
}
void event_update(void)
{
    poll_keyboard();
    poll_mouse();
    poll_gameport();
}
void event_get_mouse_pos(int *x, int *y)
{
    *x = mouse_x;
    *y = mouse_y;
}
int event_keylayout_change(void) { return FALSE; }
void event_keylayout_revert(void) {}
int event_get_joystick_num(void) { return gameport_joystick ? 1 : 0; }
