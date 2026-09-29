/* VGA mode 12h, planar write mode 0. Headless RGB565 path retained.
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
static unsigned char palette[16][3];
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

const T_GRAPH_SPEC *graph_init(void)
{
    union REGS r;
    int i;
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
        for (i = 0; i < 16; ++i) {
            palette[i][0] = i < 8 ? ((i & 4) ? 255 : 0) : (i - 7) * 28;
            palette[i][1] = i < 8 ? ((i & 2) ? 255 : 0) : (i - 7) * 28;
            palette[i][2] = i < 8 ? ((i & 1) ? 255 : 0) : (i - 7) * 28;
        }
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
        outp(0x3c8, 0);
        for (i = 0; i < 16; ++i) {
            outp(0x3c9, palette[i][0] >> 2);
            outp(0x3c9, palette[i][1] >> 2);
            outp(0x3c9, palette[i][2] >> 2);
        }
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
    int i, j, best;
    long distance, closest, r, g, b;
    for (i = 0; i < count; ++i) {
        if (!dos_vga) {
            pixels[i] = ((unsigned long)(colors[i].red >> 3) << 11) |
                        ((unsigned long)(colors[i].green >> 2) << 5) | (colors[i].blue >> 3);
        } else {
            closest = 0x7fffffffL;
            best = 0;
            for (j = 0; j < 16; ++j) {
                r = colors[i].red - (int)palette[j][0];
                g = colors[i].green - (int)palette[j][1];
                b = colors[i].blue - (int)palette[j][2];
                distance = r*r + g*g + b*b;
                if (distance < closest) { closest = distance; best = j; }
            }
            pixels[i] = best;
        }
    }
}
static unsigned char pack_pixels(const unsigned char *src, int plane)
{
    int bit;
    unsigned char value = 0;
    for (bit = 0; bit < 8; ++bit)
        value |= ((src[bit] >> plane) & 1) << (7 - bit);
    return value;
}
void graph_update(int count, T_GRAPH_RECT rect[])
{
    int p, n, x, y, left, right, top, bottom;
    const unsigned char *src = (const unsigned char *)info.buffer;
    if (!active || !src) return;
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
void graph_remove_color(int count, unsigned long pixels[]) { (void)count; (void)pixels; }
void graph_set_window_title(const char *title) { (void)title; }
void graph_set_attribute(int mouse, int grab, int repeat, int *show, int *result_grab)
{
    (void)mouse; (void)grab; (void)repeat;
    *show = FALSE;
    *result_grab = FALSE;
}
