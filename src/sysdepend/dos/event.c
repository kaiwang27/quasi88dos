/* BIOS polling, no IRQ hook. Three emulated frames pressed, then three
 * released: BASIC typing only, not independent held keys/game controls.
 */
#include <bios.h>
#include <stdio.h>
#include <string.h>
#include "quasi88.h"
#include "event.h"
#include "keyboard.h"
#include "device.h"
static int pressed, shift, control, remaining;
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
static void release_key(void)
{
    if (pressed) quasi88_key(pressed, FALSE);
    if (shift) quasi88_key(KEY88_SHIFT, FALSE);
    if (control) quasi88_key(KEY88_CTRL, FALSE);
    pressed = shift = control = 0;
}
void event_init(void)
{
    FILE *fp;
    pressed = shift = control = remaining = 0;
    delivered = 0;
    if (dos_key_log && (fp = fopen("KEYS.LOG", "w")) != NULL) {
        fputs("DOS BIOS keyboard scan log\n", fp);
        fclose(fp);
    }
}
void event_exit(void) { release_key(); }
void event_switch(void) { release_key(); remaining = 0; }
void event_update(void)
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
    if ((ascii == 0 || ascii == 17) && scan == 0x10 &&
        (shift_status & 0x0c) == 0x0c) {
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
void event_get_mouse_pos(int *x, int *y) { *x = 0; *y = 0; }
int event_keylayout_change(void) { return FALSE; }
void event_keylayout_revert(void) {}
int event_get_joystick_num(void) { return 0; }
