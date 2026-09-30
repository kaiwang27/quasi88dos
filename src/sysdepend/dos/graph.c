/* Copyright (c) 2026, Kai Wang. Part of the QUASI88 MS-DOS port.
   SPDX-License-Identifier: BSD-3-Clause (see LICENSE). */
/* VGA mode 12h, planar write mode 0, or with -dosvesa a VESA 640x480
 * 256-color mode (VBE, banked window). Headless RGB565 path retained.
 * The frame buffer holds logical colors (one byte per pixel). In mode 12h
 * each maps to one of the 16 DAC entries, which are reprogrammed as the
 * palette changes; in VESA mode the logical color is the DAC entry.
 * CauseWay flat model maps conventional memory at its linear address
 * (installed cw.pdf, "Using the flat memory model"). VGA register layout:
 * IBM VGA/XGA Technical Reference, May 1992, sections 2-51 and 2-84.
 * No custom video timings or interrupts. BIOS restores the original mode.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <i86.h>
#include <conio.h>
#include "quasi88.h"
#include "graph.h"
#include "device.h"

static T_GRAPH_SPEC spec;
static T_GRAPH_INFO info;
static int active, saved_mode, restored = TRUE;
/* Logical colors requested by the core, and the 16 hardware DAC entries.
 * The core's emulator palette (screen/color.c emu_palette) arrives as 16
 * colors, or 136 with the half-size blend colors; its first 16 (the PC-88
 * graphics and text palettes) get exact DAC entries first. Menu colors (12,
 * or 78) and blend colors use any remaining entries, else the nearest. */
#define LOGICAL_COLORS 256
#define DAC_COLORS 16
static struct {
    unsigned char r, g, b;
    unsigned char used, priority;
} logical[LOGICAL_COLORS];
static unsigned char logical_to_dac[LOGICAL_COLORS];
static unsigned char dac[DAC_COLORS][3];
static int dac_count;
/* Set when the DAC contents or the logical-to-DAC mapping changed. VRAM
   still holds pixels for the old mapping, so the next update loads the DAC
   and redraws the whole frame from the buffer; until then the old DAC and
   old VRAM stay consistent. */
static int remap_pending;
/* Palette statistics for the exit report. */
static unsigned long palette_rebuilds, palette_inexact_rebuilds;
static int palette_last_exact, palette_last_total;
static volatile unsigned char *vram = (volatile unsigned char *)0xa0000;

/* VESA state. Logical colors 254 and 255 are reserved for the pointer,
   which QUASI88 draws itself because DOS mouse drivers often cannot draw
   one in VESA modes. */
#define CURSOR_BLACK 254
#define CURSOR_WHITE 255
#define CURSOR_W 12
#define CURSOR_H 19
static int vesa_active;
static int vesa_used;                 /* for the exit report */
static unsigned vesa_mode, vesa_pitch, vesa_bank = 0xffffU;
static unsigned long vesa_granularity, vesa_window_size;
static volatile unsigned char *vesa_window;
static int vesa_readable;
static int cursor_x = -1, cursor_y = -1, cursor_shown;
static const char *const cursor_shape[CURSOR_H] = {
    "B...........", "BB..........", "BWB.........", "BWWB........",
    "BWWWB.......", "BWWWWB......", "BWWWWWB.....", "BWWWWWWB....",
    "BWWWWWWWB...", "BWWWWWWWWB..", "BWWWWWWWWWB.", "BWWWWWWBBBBB",
    "BWWWBWWB....", "BWWBBWWB....", "BWB..BWWB...", "BB...BWWB...",
    "B.....BWWB..", "......BWWB..", ".......BB..."
};

static int video_mode(void)
{
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0f00;
    int386(0x10, &r, &r);
    return r.h.al & 0x7f;
}
static void set_mode(int mode)
{
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.w.ax = (unsigned short)mode;
    int386(0x10, &r, &r);
}
static void set_planar_write_mode(void)
{
    outpw(0x3ce, 0x0001); /* Disable set/reset. */
    outpw(0x3ce, 0x0003); /* No rotate, replace operation. */
    outpw(0x3ce, 0x0005); /* Write/read mode 0. */
    outpw(0x3ce, 0xff08); /* All pixel bits writable. */
    outpw(0x3c4, 0x0f02); /* Enable writes to all planes. */
}
static void restore_mode(void)
{
    if (active) {
        set_mode(saved_mode);
        restored = video_mode() == saved_mode;
        active = FALSE;
    }
}
int dos_graph_restored(void) { return restored; }

/* Load the DAC entries in use (6 bits per component). */
static void program_dac(void)
{
    int i;
    if (!active) return;
    if (vesa_active) {
        outp(0x3c8, 0);
        for (i = 0; i < LOGICAL_COLORS; ++i) {
            unsigned char r = logical[i].r, g = logical[i].g, b = logical[i].b;
            if (i == CURSOR_BLACK) r = g = b = 0;
            else if (i == CURSOR_WHITE) r = g = b = 255;
            else if (!logical[i].used) r = g = b = 0;
            outp(0x3c9, r >> 2);
            outp(0x3c9, g >> 2);
            outp(0x3c9, b >> 2);
        }
        return;
    }
    outp(0x3c8, 0);
    for (i = 0; i < dac_count; ++i) {
        outp(0x3c9, dac[i][0] >> 2);
        outp(0x3c9, dac[i][1] >> 2);
        outp(0x3c9, dac[i][2] >> 2);
    }
}

/* Choose the DAC contents: distinct high-priority colors first, then the
   rest while entries remain. Every logical color then maps to its exact or
   nearest DAC entry. */
static void rebuild_dac(void)
{
    int pass, i, j, best;
    long distance, closest, dr, dg, db;
    dac_count = 0;
    for (pass = 1; pass >= 0; --pass) {
        for (i = 0; i < LOGICAL_COLORS && dac_count < DAC_COLORS; ++i) {
            if (!logical[i].used || logical[i].priority != pass) continue;
            for (j = 0; j < dac_count; ++j)
                if (dac[j][0] == logical[i].r && dac[j][1] == logical[i].g &&
                    dac[j][2] == logical[i].b) break;
            if (j == dac_count) {
                dac[dac_count][0] = logical[i].r;
                dac[dac_count][1] = logical[i].g;
                dac[dac_count][2] = logical[i].b;
                ++dac_count;
            }
        }
    }
    if (!dac_count) {
        dac[0][0] = dac[0][1] = dac[0][2] = 0;
        dac_count = 1;
    }
    palette_last_exact = palette_last_total = 0;
    for (i = 0; i < LOGICAL_COLORS; ++i) {
        if (!logical[i].used) continue;
        closest = 0x7fffffffL;
        best = 0;
        for (j = 0; j < dac_count; ++j) {
            dr = (long)logical[i].r - dac[j][0];
            dg = (long)logical[i].g - dac[j][1];
            db = (long)logical[i].b - dac[j][2];
            distance = dr * dr + dg * dg + db * db;
            if (distance < closest) { closest = distance; best = j; }
        }
        logical_to_dac[i] = (unsigned char)best;
        if (logical[i].priority) {
            ++palette_last_total;
            if (closest == 0) ++palette_last_exact;
        }
    }
    ++palette_rebuilds;
    if (palette_last_exact < palette_last_total) ++palette_inexact_rebuilds;
    remap_pending = TRUE;
}

void dos_graph_palette_report(void)
{
    if (vesa_used) {
        int i, used = 0;
        for (i = 0; i < LOGICAL_COLORS; ++i) used += logical[i].used;
        printf("DOS: VESA palette: %d logical colors, all exact; %lu palette changes\n",
               used, palette_rebuilds);
        return;
    }
    printf("DOS: VGA palette: %d of 16 DAC entries in use; emulator colors exact %d/%d;"
           " %lu of %lu palette changes approximated emulator colors\n",
           dac_count, palette_last_exact, palette_last_total,
           palette_inexact_rebuilds, palette_rebuilds);
}

/* ---- VESA BIOS Extension ---- */

/* DPMI 0300h real-mode call structure. */
typedef struct {
    unsigned long edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    unsigned short flags, es, ds, fs, gs, ip, cs, sp, ss;
} RM_REGS;

/* Call INT 10h in real mode; VBE 4F00h/4F01h need a real-mode buffer. */
static int vbe_call(RM_REGS *rm)
{
    union REGS r;
    struct SREGS sr;
    memset(&r, 0, sizeof(r));
    segread(&sr);
    sr.es = sr.ds;
    r.x.eax = 0x0300;
    r.x.ebx = 0x0010;
    r.x.ecx = 0;
    r.x.edi = (unsigned)rm;
    int386x(0x31, &r, &r, &sr);
    return !r.x.cflag && (rm->eax & 0xffffUL) == 0x004f;
}

static unsigned read16(const unsigned char *p) { return p[0] | (p[1] << 8); }

/* Check one mode: 640x480, 8 bits per pixel, packed pixel, supported,
   color graphics, VGA compatible (so the DAC ports and a window at
   A000h work), windowed access, and a writable window A. */
static int vbe_mode_usable(unsigned mode, unsigned segment, unsigned offset,
                           const unsigned char *info_block)
{
    RM_REGS rm;
    unsigned attributes;
    memset(&rm, 0, sizeof(rm));
    rm.eax = 0x4f01;
    rm.ecx = mode;
    rm.es = (unsigned short)segment;
    rm.edi = offset;
    if (!vbe_call(&rm)) return FALSE;
    attributes = read16(info_block);
    if ((attributes & 0x19) != 0x19 || (attributes & 0x60)) return FALSE;
    if (read16(info_block + 18) != 640 || read16(info_block + 20) != 480 ||
        info_block[25] != 8 || info_block[27] != 4) return FALSE;
    if ((info_block[2] & 0x05) != 0x05) return FALSE;       /* window A writable */
    if (!read16(info_block + 4) || !read16(info_block + 6)) return FALSE;
    vesa_mode = mode;
    vesa_granularity = (unsigned long)read16(info_block + 4) * 1024UL;
    vesa_window_size = (unsigned long)read16(info_block + 6) * 1024UL;
    vesa_window = (volatile unsigned char *)((unsigned long)read16(info_block + 8) << 4);
    vesa_readable = (info_block[2] & 0x02) != 0;
    vesa_pitch = read16(info_block + 16);
    return vesa_window != NULL && vesa_pitch >= 640;
}

/* Find a usable 640x480x256 mode: 101h first, then the BIOS mode list.
   Prints the reason when none is found. */
static int vbe_find_mode(void)
{
    union REGS r;
    RM_REGS rm;
    unsigned segment, selector, i;
    unsigned char *block;
    int found = FALSE;
    memset(&r, 0, sizeof(r));
    r.x.eax = 0x0100;                       /* DPMI allocate DOS memory */
    r.x.ebx = 64;                           /* 1 KiB for VBE info blocks */
    int386(0x31, &r, &r);
    if (r.x.cflag) {
        puts("DOS: VESA unavailable: no DOS memory for VBE calls");
        return FALSE;
    }
    segment = r.w.ax;
    selector = r.w.dx;
    block = (unsigned char *)((unsigned long)segment << 4);
    memset(block, 0, 1024);
    memcpy(block, "VBE2", 4);
    memset(&rm, 0, sizeof(rm));
    rm.eax = 0x4f00;
    rm.es = (unsigned short)segment;
    rm.edi = 0;
    if (!vbe_call(&rm) || memcmp(block, "VESA", 4)) {
        puts("DOS: VESA unavailable: no VBE BIOS");
    } else {
        unsigned version = read16(block + 4);
        const unsigned short *modes = (const unsigned short *)
            (((unsigned long)read16(block + 16) << 4) + read16(block + 14));
        /* Mode info goes to the second half of the buffer, so it cannot
           overwrite the info block, which may hold the mode list. */
        unsigned char *mode_info = block + 512;
        found = vbe_mode_usable(0x101, segment, 512, mode_info);
        for (i = 0; !found && i < 256 && modes[i] != 0xffffU; ++i)
            if (modes[i] != 0x101) found = vbe_mode_usable(modes[i], segment, 512, mode_info);
        if (!found)
            printf("DOS: VESA unavailable: VBE %u.%u has no usable 640x480 256-color mode\n",
                   version >> 8, version & 0xff);
    }
    memset(&r, 0, sizeof(r));
    r.x.eax = 0x0101;                       /* DPMI free DOS memory */
    r.w.dx = (unsigned short)selector;
    int386(0x31, &r, &r);
    return found;
}

static void vbe_set_bank(unsigned bank)
{
    union REGS r;
    if (bank == vesa_bank) return;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x4f05;                        /* window A, set position */
    r.w.bx = 0;
    r.w.dx = (unsigned short)bank;
    int386(0x10, &r, &r);
    vesa_bank = bank;
}

/* Copy bytes to (or from) VRAM through window A. */
static void vesa_copy(unsigned long address, unsigned char *data, unsigned length, int write)
{
    while (length) {
        unsigned bank = (unsigned)(address / vesa_granularity);
        unsigned long offset = address - (unsigned long)bank * vesa_granularity;
        unsigned chunk = (unsigned)MIN((unsigned long)length, vesa_window_size - offset);
        vbe_set_bank(bank);
        /* memcpy compiles to rep movsd: four pixels per bus transfer. Byte
           writes were slower than mode 12h's planar writes on the i740. */
        if (write) memcpy((void *)(vesa_window + offset), data, chunk);
        else memcpy(data, (const void *)(vesa_window + offset), chunk);
        address += chunk;
        data += chunk;
        length -= chunk;
    }
}

/* Redraw the pointer rows from the frame buffer, with the arrow on top
   when draw is set. Pixels outside the frame buffer are black. */
static void cursor_paint(int x0, int y0, int draw)
{
    unsigned char row[CURSOR_W];
    const unsigned char *src = (const unsigned char *)info.buffer;
    int x, y, w;
    if (x0 < 0 || y0 < 0 || !src) return;
    for (y = y0; y < y0 + CURSOR_H && y < 480; ++y) {
        w = MIN(CURSOR_W, 640 - x0);
        if (w <= 0) return;
        for (x = 0; x < w; ++x) {
            char c = draw ? cursor_shape[y - y0][x] : '.';
            if (c == 'B') row[x] = CURSOR_BLACK;
            else if (c == 'W') row[x] = CURSOR_WHITE;
            else if (x0 + x < info.width && y < info.height)
                row[x] = src[y * info.width + x0 + x];
            else row[x] = 0;
        }
        vesa_copy((unsigned long)y * vesa_pitch + x0, row, (unsigned)w, TRUE);
    }
}

/* Called by the mouse code with the new pointer position. */
void dos_graph_mouse_moved(int x, int y)
{
    if (!vesa_active || (x == cursor_x && y == cursor_y && cursor_shown)) return;
    if (cursor_shown) cursor_paint(cursor_x, cursor_y, FALSE);
    cursor_x = x;
    cursor_y = y;
    cursor_paint(cursor_x, cursor_y, TRUE);
    cursor_shown = TRUE;
}

int dos_graph_vesa_active(void) { return vesa_active; }

/* Enter the VESA mode and clear the 640x480 screen. */
static int vesa_enter(void)
{
    union REGS r;
    unsigned char zero[640];
    unsigned y;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x4f02;
    r.w.bx = (unsigned short)vesa_mode;
    int386(0x10, &r, &r);
    if (r.w.ax != 0x004f) return FALSE;
    vesa_bank = 0xffffU;
    memset(zero, 0, sizeof(zero));
    for (y = 0; y < 480; ++y) vesa_copy((unsigned long)y * vesa_pitch, zero, 640, TRUE);
    printf("DOS: VESA mode %03Xh 640x480 256 colors; window %lu KiB, granularity %lu KiB, pitch %u\n",
           vesa_mode, vesa_window_size / 1024UL, vesa_granularity / 1024UL, vesa_pitch);
    return TRUE;
}

const T_GRAPH_SPEC *graph_init(void)
{
    union REGS r;
    memset(&spec, 0, sizeof(spec));
    spec.window_max_width = 640;
    spec.window_max_height = 480;
    if (dos_vga) {
        memset(&r, 0, sizeof(r));
        r.w.ax = 0x1a00;
        int386(0x10, &r, &r);
        if (r.h.al != 0x1a || r.h.bl != 8) {
            fputs("DOS: a color VGA-compatible BIOS/display is required.\n", stderr);
            return NULL;
        }
        saved_mode = video_mode();
        if (saved_mode > 3 && saved_mode != 7) {
            fputs("DOS: start this build from a standard DOS text mode.\n", stderr);
            return NULL;
        }
        if (atexit(restore_mode) != 0) return NULL;
        memset(logical, 0, sizeof(logical));
        memset(logical_to_dac, 0, sizeof(logical_to_dac));
        dac_count = 0;
    }
    return &spec;
}

const T_GRAPH_INFO *graph_setup(int width, int height, int fullscreen, double aspect)
{
    void *buffer;
    int i, bytes = dos_vga ? 1 : 2;
    union REGS r;
    (void)fullscreen; (void)aspect;
    if (width <= 0 || width > 640 || (width & 7) || height <= 0 || height > 480) return NULL;
    buffer = calloc((size_t)width * height, bytes);
    if (!buffer) return NULL;
    if (dos_vga && !active && dos_vesa && vbe_find_mode()) {
        active = TRUE;
        restored = FALSE;
        if (vesa_enter()) {
            vesa_active = vesa_used = TRUE;
            program_dac();
            remap_pending = FALSE;
        } else {
            puts("DOS: VESA mode set failed; using VGA mode 12h");
            active = FALSE;
        }
    }
    if (dos_vga && !active) {
        active = TRUE;
        restored = FALSE;
        set_mode(0x12);
        if (video_mode() != 0x12) { free(buffer); restore_mode(); return NULL; }
        for (i = 0; i < 16; ++i) {
            memset(&r, 0, sizeof(r));
            r.w.ax = 0x1000;
            r.h.bl = (unsigned char)i;
            r.h.bh = (unsigned char)i;
            int386(0x10, &r, &r);
        }
        program_dac();
        remap_pending = FALSE;
        set_planar_write_mode();
    }
    if (active && !vesa_active) {
        outpw(0x3c4, 0x0f02);
        for (i = 0; i < 38400; ++i) vram[i] = 0;
    }
    free(info.buffer);
    memset(&info, 0, sizeof(info));
    info.buffer = buffer;
    info.width = width;
    info.height = height;
    info.byte_per_pixel = bytes;
    info.byte_per_line = width * bytes;
    /* The interface permits requested colors to share a pixel value. */
    info.nr_color = dos_vga ? 256 : 65536;
    return &info;
}
void graph_exit(void)
{
    restore_mode();
    vesa_active = FALSE;
    cursor_shown = FALSE;
    free(info.buffer);
    memset(&info, 0, sizeof(info));
}
void graph_add_color(const PC88_PALETTE_T colors[], int count, unsigned long pixels[])
{
    int i, slot = 0, emulator = count == 16 || count == 136;
    for (i = 0; i < count; ++i) {
        if (!dos_vga) {
            pixels[i] = ((unsigned long)(colors[i].red >> 3) << 11) |
                        ((unsigned long)(colors[i].green >> 2) << 5) | (colors[i].blue >> 3);
            continue;
        }
        while (slot < CURSOR_BLACK && logical[slot].used) ++slot;
        if (slot == CURSOR_BLACK) {
            pixels[i] = 0;                 /* the core requests at most 214 */
            continue;
        }
        logical[slot].r = colors[i].red;
        logical[slot].g = colors[i].green;
        logical[slot].b = colors[i].blue;
        logical[slot].used = 1;
        logical[slot].priority = (unsigned char)(emulator && i < 16);
        pixels[i] = (unsigned long)slot;
    }
    if (dos_vga) rebuild_dac();
}
static unsigned char pack_pixels(const unsigned char *src, int plane)
{
    int bit;
    unsigned char value = 0;
    for (bit = 0; bit < 8; ++bit)
        value |= ((logical_to_dac[src[bit]] >> plane) & 1) << (7 - bit);
    return value;
}
/* VESA: the frame buffer bytes are the DAC indices, so updated rows are
   copied as they are. */
static void vesa_update(int count, T_GRAPH_RECT rect[])
{
    int n, y, left, right, top, bottom, cover = FALSE;
    unsigned char *src = (unsigned char *)info.buffer;
    if (remap_pending) {
        program_dac();
        remap_pending = FALSE;
    }
    for (n = 0; n < count; ++n) {
        left = MAX(0, rect[n].x);
        right = MIN(info.width, rect[n].x + rect[n].w);
        top = MAX(0, rect[n].y);
        bottom = MIN(info.height, rect[n].y + rect[n].h);
        if (left >= right) continue;
        for (y = top; y < bottom; ++y)
            vesa_copy((unsigned long)y * vesa_pitch + left, src + y * info.width + left,
                      (unsigned)(right - left), TRUE);
        if (cursor_shown && cursor_x < right && cursor_x + CURSOR_W > left &&
            cursor_y < bottom && cursor_y + CURSOR_H > top) cover = TRUE;
    }
    if (cover) cursor_paint(cursor_x, cursor_y, TRUE);
}

void graph_update(int count, T_GRAPH_RECT rect[])
{
    int p, n, x, y, left, right, top, bottom;
    const unsigned char *src = (const unsigned char *)info.buffer;
    T_GRAPH_RECT whole;
    if (!active || !src) return;
    if (vesa_active) {
        vesa_update(count, rect);
        return;
    }
    if (remap_pending) {
        /* Load the new DAC and redraw everything with the new mapping. */
        program_dac();
        remap_pending = FALSE;
        whole.x = whole.y = 0;
        whole.w = info.width;
        whole.h = info.height;
        count = 1;
        rect = &whole;
    }
    /* INT 33h uses a saved-background software cursor. Hide it while updating
     * planar VRAM so it cannot restore stale pixels over the new toolbar. */
    dos_mouse_video_update_begin();
    set_planar_write_mode();
    for (p = 0; p < 4; ++p) {
        outpw(0x3c4, ((1 << p) << 8) | 2);
        for (n = 0; n < count; ++n) {
            left = MAX(0, rect[n].x) & ~7;
            right = MIN(info.width, rect[n].x + rect[n].w + 7) & ~7;
            top = MAX(0, rect[n].y);
            bottom = MIN(info.height, rect[n].y + rect[n].h);
            for (y = top; y < bottom; ++y)
                for (x = left; x < right; x += 8)
                    vram[y * 80 + x / 8] = pack_pixels(src + y * info.width + x, p);
        }
    }
    outpw(0x3c4, 0x0f02);
    dos_mouse_video_update_end();
}
int dos_graph_verify(void)
{
    int p, x, y, ok = TRUE, lit = FALSE;
    unsigned char expected;
    const unsigned char *src = (const unsigned char *)info.buffer;
    if (!active || !src) return FALSE;
    if (remap_pending) graph_update(0, NULL);   /* apply a deferred remap */
    if (vesa_active) {
        /* Compare VRAM with the buffer, outside the pointer. */
        unsigned char row[640];
        if (!vesa_readable) return TRUE;
        for (y = 0; y < info.height; ++y) {
            vesa_copy((unsigned long)y * vesa_pitch, row, (unsigned)info.width, FALSE);
            for (x = 0; x < info.width; ++x) {
                if (cursor_shown && x >= cursor_x && x < cursor_x + CURSOR_W &&
                    y >= cursor_y && y < cursor_y + CURSOR_H) continue;
                if (src[y * info.width + x]) lit = TRUE;
                if (row[x] != src[y * info.width + x]) ok = FALSE;
            }
        }
        return ok && lit;
    }
    dos_mouse_video_update_begin();
    for (p = 0; p < 4; ++p) {
        outpw(0x3ce, (p << 8) | 4);
        for (y = 0; y < info.height; ++y)
            for (x = 0; x < info.width; x += 8) {
                expected = pack_pixels(src + y * info.width + x, p);
                if (expected) lit = TRUE;
                if (vram[y * 80 + x / 8] != expected) ok = FALSE;
            }
    }
    outpw(0x3ce, 0x0004);
    dos_mouse_video_update_end();
    return ok && lit;
}
void graph_remove_color(int count, unsigned long pixels[])
{
    int i;
    if (!dos_vga) return;
    /* The DAC is rebuilt when the replacement colors are added. */
    for (i = 0; i < count; ++i)
        if (pixels[i] < LOGICAL_COLORS) logical[pixels[i]].used = 0;
}
void graph_set_window_title(const char *title) { (void)title; }
void graph_set_attribute(int mouse, int grab, int repeat, int *show, int *result_grab)
{
    (void)mouse; (void)grab; (void)repeat;
    *show = FALSE;
    *result_grab = FALSE;
}
