/* VGA mode 12h, planar write mode 0. Headless RGB565 path retained.
 * The frame buffer holds logical colors (one byte per pixel); each maps to
 * one of the 16 DAC entries, which are reprogrammed as the palette changes.
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
    printf("DOS: VGA palette: %d of 16 DAC entries in use; emulator colors exact %d/%d;"
           " %lu of %lu palette changes approximated emulator colors\n",
           dac_count, palette_last_exact, palette_last_total,
           palette_inexact_rebuilds, palette_rebuilds);
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
    if (active) {
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
        while (slot < LOGICAL_COLORS && logical[slot].used) ++slot;
        if (slot == LOGICAL_COLORS) {
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
void graph_update(int count, T_GRAPH_RECT rect[])
{
    int p, n, x, y, left, right, top, bottom;
    const unsigned char *src = (const unsigned char *)info.buffer;
    T_GRAPH_RECT whole;
    if (!active || !src) return;
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
