/* CauseWay IRQ1 scan-code capture; translation runs outside the interrupt. */
#include <bios.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>
#include "quasi88.h"
#include "event.h"
#include "keyboard.h"
#include "device.h"
static void (__interrupt __far *previous_keyboard_irq)(void);
volatile struct {
    unsigned char bytes[128];
    unsigned char head;
    unsigned char tail;
    unsigned long dropped;
} keyboard_queue;
extern void __interrupt __far keyboard_irq(void);
static int keyboard_irq_installed;
static int pressed_keys[256];
static int left_shift, right_shift, control, alt, caps_lock;
static int extended_prefix, pause_bytes;
/* Pause sends a make-only sequence (E1 1D 45 E1 9D C5). It is mapped to the
   PC-88 STOP key, released after a few polls so the key scan sees it. */
#define PAUSE_STOP_POLLS 4
static int pause_stop_polls;
/* Key positions follow the PC-88 (JIS) layout, as in the SDL2 port;
   -keyboard selects how the extra keys map: 1 = Japanese 106, 2 = US 101. */
int keyboard_type = 1;
static int bios_pressed_key, bios_remaining, bios_shift, bios_control;
static int mouse_available;
static int gameport_joystick;
static unsigned int gameport_buttons;
static unsigned int gameport_directions;
static unsigned int gameport_center[2];
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
static void log_key(unsigned int scan, int extended, int down, int keycode)
{
    FILE *fp;
    if (!dos_key_log) return;
    fp = fopen("KEYS.LOG", "a");
    if (fp) {
        fprintf(fp, "scan=%02X extended=%d %s key=%d shift=%d ctrl=%d alt=%d\n",
                scan, extended, down ? "down" : "up", keycode,
                left_shift || right_shift, control, alt);
        fclose(fp);
    }
}
unsigned long dos_key_count(void) { return delivered; }
void dos_mouse_video_update_begin(void)
{
    union REGS regs;
    if (!mouse_available || dos_graph_vesa_active()) return;
    memset(&regs, 0, sizeof(regs));
    regs.w.ax = 2;
    int386(0x33, &regs, &regs);
}
void dos_mouse_video_update_end(void)
{
    union REGS regs;
    if (!mouse_available || dos_graph_vesa_active()) return;
    memset(&regs, 0, sizeof(regs));
    regs.w.ax = 1;
    int386(0x33, &regs, &regs);
}
static void release_key(void)
{
    int i;
    for (i = 0; i < 256; ++i) {
        if (pressed_keys[i]) {
            quasi88_key(pressed_keys[i], FALSE);
            pressed_keys[i] = 0;
        }
    }
    left_shift = right_shift = control = alt = 0;
}
static int lock_interrupt_memory(void *address, unsigned long length)
{
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = 0x0000ff1cUL; /* CauseWay LockMem32 */
    regs.x.esi = (unsigned long)address;
    regs.x.ecx = length;
    int386(0x31, &regs, &regs);
    return !regs.x.cflag;
}
static int install_keyboard_irq(void)
{
    unsigned long code = (unsigned long)keyboard_irq;
    if (!lock_interrupt_memory((void *)code, 512) ||
        !lock_interrupt_memory((void *)&keyboard_queue,
                               sizeof(keyboard_queue))) return FALSE;
    previous_keyboard_irq = _dos_getvect(9);
    _dos_setvect(9, keyboard_irq);
    keyboard_irq_installed = TRUE;
    return TRUE;
}
static void remove_keyboard_irq(void)
{
    if (keyboard_irq_installed) {
        _dos_setvect(9, previous_keyboard_irq);
        keyboard_irq_installed = FALSE;
    }
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
#define GAMEPORT_AXIS_TIMEOUT 5965U /* 5 ms at the 1.193182 MHz PIT rate */

static unsigned int read_pit_counter(void)
{
    unsigned int count;
    outp(0x43, 0); /* Latch channel 0 without changing its mode or rate. */
    count = (unsigned int)inp(0x40);
    count |= (unsigned int)inp(0x40) << 8;
    return count;
}

static void sample_gameport_axes(unsigned int axes[2])
{
    unsigned int start, current, status, pending, elapsed;
    int i;

    for (i = 0; i < 2; ++i) axes[i] = GAMEPORT_AXIS_TIMEOUT;
    outp(0x201, 0); /* Start the first joystick's X/Y RC timers. */
    start = read_pit_counter();
    pending = (unsigned int)inp(0x201) & 0x03;
    while (pending) {
        status = (unsigned int)inp(0x201);
        current = read_pit_counter();
        elapsed = (start - current) & 0xffff;
        for (i = 0; i < 2; ++i) {
            if ((pending & (1U << i)) && !(status & (1U << i)))
                axes[i] = elapsed;
        }
        pending &= status;
        if (elapsed >= GAMEPORT_AXIS_TIMEOUT) break;
    }
}

static int probe_gameport(void)
{
    unsigned int axes[2];
    unsigned int total_x = 0, total_y = 0;
    int sample;

    sample_gameport_axes(axes);
    if (axes[0] >= GAMEPORT_AXIS_TIMEOUT ||
        axes[1] >= GAMEPORT_AXIS_TIMEOUT) return FALSE;
    total_x = axes[0];
    total_y = axes[1];
    for (sample = 1; sample < 4; ++sample) {
        sample_gameport_axes(axes);
        total_x += axes[0];
        total_y += axes[1];
    }
    gameport_center[0] = total_x / 4;
    gameport_center[1] = total_y / 4;
    return TRUE;
}
static void poll_gameport(void)
{
    static const int direction_keys[4] = {
        KEY88_PAD1_LEFT, KEY88_PAD1_RIGHT, KEY88_PAD1_UP, KEY88_PAD1_DOWN
    };
    unsigned int axes[2], status, buttons, changed, directions;
    unsigned int dead_x, dead_y;
    int i;
    if (!gameport_joystick || mouse_mode != MOUSE_JOYSTICK) return;
    sample_gameport_axes(axes);
    status = (unsigned int)inp(0x201);
    buttons = (unsigned int)((~status >> 4) & 0x03);
    changed = buttons ^ gameport_buttons;
    for (i = 0; i < 2; ++i) {
        if (changed & (1U << i))
            quasi88_pad(KEY88_PAD1_A + i, (buttons & (1U << i)) != 0);
    }
    gameport_buttons = buttons;

    if (axes[0] >= GAMEPORT_AXIS_TIMEOUT) axes[0] = gameport_center[0];
    if (axes[1] >= GAMEPORT_AXIS_TIMEOUT) axes[1] = gameport_center[1];
    dead_x = gameport_center[0] / 4;
    dead_y = gameport_center[1] / 4;
    if (dead_x < 238) dead_x = 238; /* At least 200 us around center. */
    if (dead_y < 238) dead_y = 238;
    directions = 0;
    if (axes[0] + dead_x < gameport_center[0]) directions |= 1U;
    if (axes[0] > gameport_center[0] + dead_x) directions |= 2U;
    if (axes[1] + dead_y < gameport_center[1]) directions |= 4U;
    if (axes[1] > gameport_center[1] + dead_y) directions |= 8U;
    changed = directions ^ gameport_directions;
    for (i = 0; i < 4; ++i) {
        if (changed & (1U << i))
            quasi88_pad(direction_keys[i], (directions & (1U << i)) != 0);
    }
    gameport_directions = directions;
}
static void release_gameport(void)
{
    static const int direction_keys[4] = {
        KEY88_PAD1_LEFT, KEY88_PAD1_RIGHT, KEY88_PAD1_UP, KEY88_PAD1_DOWN
    };
    int i;
    for (i = 0; i < 2; ++i) {
        if (gameport_buttons & (1U << i))
            quasi88_pad(KEY88_PAD1_A + i, FALSE);
    }
    for (i = 0; i < 4; ++i) {
        if (gameport_directions & (1U << i))
            quasi88_pad(direction_keys[i], FALSE);
    }
    gameport_buttons = 0;
    gameport_directions = 0;
}
void event_init(void)
{
    FILE *fp;
    memset((void *)&keyboard_queue, 0, sizeof(keyboard_queue));
    memset(pressed_keys, 0, sizeof(pressed_keys));
    keyboard_irq_installed = FALSE;
    left_shift = right_shift = control = alt = caps_lock = 0;
    extended_prefix = pause_bytes = 0;
    bios_pressed_key = bios_remaining = bios_shift = bios_control = 0;
    mouse_available = FALSE;
    gameport_joystick = FALSE;
    gameport_buttons = 0;
    gameport_directions = 0;
    gameport_center[0] = gameport_center[1] = 0;
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
            /* Pin the coordinate range: after a VESA mode set some drivers
               assume a different screen size. */
            memset(&regs, 0, sizeof(regs));
            regs.w.ax = 7;
            regs.w.dx = 639;
            int386(0x33, &regs, &regs);
            memset(&regs, 0, sizeof(regs));
            regs.w.ax = 8;
            regs.w.dx = 479;
            int386(0x33, &regs, &regs);
            memset(&regs, 0, sizeof(regs));
            regs.w.ax = 4;                  /* start at the screen center */
            regs.w.cx = 320;
            regs.w.dx = 240;
            int386(0x33, &regs, &regs);
            if (dos_graph_vesa_active()) {
                dos_graph_mouse_moved(320, 240);   /* QUASI88 draws the pointer */
            } else {
                memset(&regs, 0, sizeof(regs));
                regs.w.ax = 1;
                int386(0x33, &regs, &regs);
            }
        }
    }
    gameport_joystick = probe_gameport();
    if (gameport_joystick) {
        printf("DOS: game-port joystick detected (center PIT counts X=%u Y=%u)\n",
               gameport_center[0], gameport_center[1]);
        puts("DOS: analog axes and two buttons available with -joystick");
    } else {
        puts("DOS: game-port joystick not detected");
    }
    if (mouse_mode == MOUSE_JOYSTICK) puts("DOS: PC-88 joystick mode selected");
    if (dos_mouse_log) {
        fp = fopen("MOUSE.LOG", "a");
        if (fp) {
            fprintf(fp, "driver=%s\n", mouse_available ? "installed" : "not-installed");
            fclose(fp);
        }
    }
    delivered = 0;
    if (install_keyboard_irq()) {
        puts("DOS: IRQ1 make/break keyboard input installed");
    } else {
        puts("DOS: IRQ1 unavailable; using BIOS keyboard polling fallback");
    }
    if (dos_key_log && (fp = fopen("KEYS.LOG", "w")) != NULL) {
        fputs("DOS IRQ1 keyboard event log\n", fp);
        fclose(fp);
    }
}
void event_exit(void)
{
    release_key();
    release_mouse();
    release_gameport();
    remove_keyboard_irq();
}
void event_switch(void)
{
    release_key();
    release_mouse();
    release_gameport();
    bios_pressed_key = bios_remaining = bios_shift = bios_control = 0;
    keyboard_queue.tail = keyboard_queue.head;
    extended_prefix = pause_bytes = 0;
}
static int map_keyboard_scan(unsigned int scan, int extended)
{
    static const int number_keys[10] = {
        KEY88_1, KEY88_2, KEY88_3, KEY88_4, KEY88_5,
        KEY88_6, KEY88_7, KEY88_8, KEY88_9, KEY88_0
    };
    static const int shifted_numbers[10] = {
        KEY88_EXCLAM, KEY88_QUOTEDBL, KEY88_NUMBERSIGN, KEY88_DOLLAR,
        KEY88_PERCENT, KEY88_AMPERSAND, KEY88_APOSTROPHE,
        KEY88_PARENLEFT, KEY88_PARENRIGHT, 0
    };
    static const unsigned char letter_scans[26] = {
        0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
        0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
        0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c
    };
    int i, shifted = left_shift || right_shift;
    if (extended) {
        switch (scan) {
        case 0x1c: return KEY88_RETURNR;
        case 0x1d: return keyboard_type == 2 ? KEY88_UNDERSCORE : KEY88_CTRL;
        case 0x35: return KEY88_KP_DIVIDE;
        case 0x37: return KEY88_COPY;          /* PrintScreen */
        case 0x38: return KEY88_KANA;
        case 0x47: return KEY88_HOME;
        case 0x48: return KEY88_UP;
        case 0x49: return KEY88_ROLLDOWN;
        case 0x4b: return KEY88_LEFT;
        case 0x4d: return KEY88_RIGHT;
        case 0x4f: return KEY88_HELP;
        case 0x50: return KEY88_DOWN;
        case 0x51: return KEY88_ROLLUP;
        case 0x52: return KEY88_INS;
        case 0x53: return KEY88_DEL;
        case 0x5b: return KEY88_SYS_MENU;
        case 0x5d: return keyboard_type == 2 ? 0 : KEY88_ZENKAKU;   /* Application */
        default: return 0;
        }
    }
    if (scan >= 0x02 && scan <= 0x0b) {
        i = scan == 0x0b ? 9 : scan - 2;
        return shifted && shifted_numbers[i] ? shifted_numbers[i] : number_keys[i];
    }
    for (i = 0; i < 26; ++i) {
        if (letter_scans[i] == scan) {
            shifted ^= caps_lock;
            return (shifted ? 'A' : 'a') + i;
        }
    }
    switch (scan) {
    case 0x01: return KEY88_ESC;
    case 0x0c: return shifted ? KEY88_EQUAL : KEY88_MINUS;
    case 0x0d: return shifted ? KEY88_TILDE : KEY88_CARET;
    case 0x0e: return KEY88_BS;
    case 0x29:                                 /* JP: hankaku/zenkaku; US: ` */
        if (keyboard_type == 2) return shifted ? KEY88_BAR : KEY88_YEN;
        return 0;
    case 0x0f: return KEY88_TAB;
    case 0x1a: return shifted ? KEY88_BACKQUOTE : KEY88_AT;
    case 0x1b: return shifted ? KEY88_BRACELEFT : KEY88_BRACKETLEFT;
    case 0x1c: return KEY88_RETURNL;
    case 0x27: return shifted ? KEY88_PLUS : KEY88_SEMICOLON;
    case 0x28: return shifted ? KEY88_ASTERISK : KEY88_COLON;
    case 0x2a: return KEY88_SHIFTL;
    case 0x2b: return shifted ? KEY88_BRACERIGHT : KEY88_BRACKETRIGHT;
    case 0x33: return shifted ? KEY88_LESS : KEY88_COMMA;
    case 0x34: return shifted ? KEY88_GREATER : KEY88_PERIOD;
    case 0x35: return shifted ? KEY88_QUESTION : KEY88_SLASH;
    case 0x36: return KEY88_SHIFTR;
    case 0x37: return KEY88_KP_MULTIPLY;
    case 0x38: return KEY88_GRAPH;
    case 0x39: return KEY88_SPACE;
    case 0x3a: return KEY88_CAPS;
    case 0x3b: return KEY88_F1;
    case 0x3c: return KEY88_F2;
    case 0x3d: return KEY88_F3;
    case 0x3e: return KEY88_F4;
    case 0x3f: return KEY88_F5;
    case 0x40: return KEY88_F6;
    case 0x41: return KEY88_F7;
    case 0x42: return KEY88_F8;
    case 0x43: return KEY88_F9;
    case 0x44: return KEY88_F10;
    case 0x46: return KEY88_STOP;              /* ScrollLock */
    case 0x47: return KEY88_KP_7;
    case 0x48: return KEY88_KP_8;
    case 0x49: return KEY88_KP_9;
    case 0x4a: return KEY88_KP_SUB;
    case 0x4b: return KEY88_KP_4;
    case 0x4c: return KEY88_KP_5;
    case 0x4d: return KEY88_KP_6;
    case 0x4e: return KEY88_KP_ADD;
    case 0x4f: return KEY88_KP_1;
    case 0x50: return KEY88_KP_2;
    case 0x51: return KEY88_KP_3;
    case 0x52: return KEY88_KP_0;
    case 0x53: return KEY88_KP_PERIOD;
    case 0x57: return KEY88_SYS_STATUS;
    case 0x58: return KEY88_SYS_MENU;
    /* Japanese 106-key extras; hiragana/katakana (70h) stays unmapped, as
       in the SDL2 port. */
    case 0x73: return keyboard_type == 2 ? 0 : KEY88_UNDERSCORE;      /* ro */
    case 0x79: return keyboard_type == 2 ? 0 : KEY88_HENKAN;          /* henkan */
    case 0x7b: return keyboard_type == 2 ? 0 : KEY88_KETTEI;          /* muhenkan */
    case 0x7d:                                                        /* yen */
        if (keyboard_type == 2) return 0;
        return shifted ? KEY88_BAR : KEY88_YEN;
    default: return 0;
    }
}
static void process_keyboard_scan(unsigned char scan)
{
    unsigned int code, index;
    int down, keycode;
    if (pause_bytes) { --pause_bytes; return; }
    if (scan == 0xe1) {
        pause_bytes = 5;
        extended_prefix = 0;
        if (!pause_stop_polls) {
            quasi88_key(KEY88_STOP, TRUE);
            ++delivered;
            log_key(0x45, 2, TRUE, KEY88_STOP);
        }
        pause_stop_polls = PAUSE_STOP_POLLS;
        return;
    }
    if (scan == 0xe0) { extended_prefix = 1; return; }
    down = (scan & 0x80) == 0;
    code = scan & 0x7f;
    index = code + (extended_prefix ? 128 : 0);
    if (extended_prefix && (code == 0x2a || code == 0x36)) {
        extended_prefix = 0;
        return;
    }
    if (down && pressed_keys[index]) {
        extended_prefix = 0;
        return;
    }
    if (!down) {
        keycode = pressed_keys[index];
        if (keycode) {
            quasi88_key(keycode, FALSE);
            pressed_keys[index] = 0;
            if (keycode == KEY88_SHIFTL) left_shift = 0;
            if (keycode == KEY88_SHIFTR) right_shift = 0;
            if (keycode == KEY88_CTRL) control = 0;
            if (keycode == KEY88_GRAPH || keycode == KEY88_KANA) alt = 0;
            log_key(code, extended_prefix, FALSE, keycode);
        }
        extended_prefix = 0;
        return;
    }
    if (!extended_prefix && code == 0x3a) caps_lock = !caps_lock;
    if (!extended_prefix && (code == 0x2a || code == 0x36))
        keycode = code == 0x2a ? KEY88_SHIFTL : KEY88_SHIFTR;
    else if (!extended_prefix && code == 0x1d) keycode = KEY88_CTRL;
    else if (!extended_prefix && code == 0x38) keycode = KEY88_GRAPH;
    else keycode = map_keyboard_scan(code, extended_prefix);
    if (keycode) {
        if (!extended_prefix && code == 0x10 && control &&
            (keycode == KEY88_q || keycode == KEY88_Q)) {
            puts(alt ? "DOS: Ctrl+Alt+Q received; returning to DOS." :
                       "DOS: Ctrl+Q received; returning to DOS.");
            quasi88_quit();
            extended_prefix = 0;
            return;
        }
        pressed_keys[index] = keycode;
        quasi88_key(keycode, TRUE);
        if (keycode == KEY88_SHIFTL) left_shift = 1;
        if (keycode == KEY88_SHIFTR) right_shift = 1;
        if (keycode == KEY88_CTRL) control = 1;
        if (keycode == KEY88_GRAPH || keycode == KEY88_KANA) alt = 1;
        ++delivered;
        log_key(code, extended_prefix, TRUE, keycode);
    }
    extended_prefix = 0;
}
static void poll_bios_keyboard(void)
{
    unsigned int key, shift_status;
    int ascii, scan, code = 0;
    if (bios_remaining) {
        if (--bios_remaining == 1 && bios_pressed_key) {
            quasi88_key(bios_pressed_key, FALSE);
            if (bios_shift) quasi88_key(KEY88_SHIFT, FALSE);
            if (bios_control) quasi88_key(KEY88_CTRL, FALSE);
            bios_pressed_key = bios_shift = bios_control = 0;
        }
        return;
    }
    if (!_bios_keybrd(_NKEYBRD_READY)) return;
    key = _bios_keybrd(_NKEYBRD_READ);
    shift_status = _bios_keybrd(_NKEYBRD_SHIFTSTATUS);
    ascii = key & 255;
    scan = key >> 8;
    if (scan == 0x10 && (ascii == 17 ||
        (ascii == 0 && (shift_status & 0x0c) == 0x0c))) {
        quasi88_quit();
        return;
    }
    if (scan == 0x57 || scan == 0x85) code = KEY88_SYS_STATUS;
    else if (scan == 0x58 || scan == 0x86) code = KEY88_SYS_MENU;
    else if (ascii >= 32 && ascii <= 126) code = ascii;
    else switch (ascii) {
    case 8: code = KEY88_BS; break;
    case 9: code = KEY88_TAB; break;
    case 13: code = KEY88_RETURN; break;
    case 27: code = KEY88_ESC; break;
    default:
        if (ascii >= 1 && ascii <= 26) {
            code = 'A' + ascii - 1;
            bios_control = TRUE;
        } else switch (scan) {
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
    if (!code) return;
    bios_shift = (ascii >= 32 && ascii <= 126 &&
                  strchr("!\"#$%&'()=+*<>?_`{|}~", ascii) != NULL);
    if (bios_shift) quasi88_key(KEY88_SHIFT, TRUE);
    if (bios_control) quasi88_key(KEY88_CTRL, TRUE);
    quasi88_key(code, TRUE);
    bios_pressed_key = code;
    bios_remaining = 3;
    ++delivered;
}
static void poll_keyboard(void)
{
    unsigned char scan;
    unsigned char tail;
    if (!keyboard_irq_installed) {
        poll_bios_keyboard();
        return;
    }
    while ((tail = keyboard_queue.tail) != keyboard_queue.head) {
        scan = keyboard_queue.bytes[tail];
        keyboard_queue.tail = (tail + 1) & 127;
        process_keyboard_scan(scan);
    }
    if (keyboard_queue.dropped) {
        printf("DOS: keyboard scan queue overflowed (%lu bytes dropped)\n",
               keyboard_queue.dropped);
        keyboard_queue.dropped = 0;
        release_key();
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
    dos_graph_mouse_moved(mouse_x, mouse_y);
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
    if (pause_stop_polls && --pause_stop_polls == 0) {
        quasi88_key(KEY88_STOP, FALSE);
        log_key(0x45, 2, FALSE, KEY88_STOP);
    }
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
