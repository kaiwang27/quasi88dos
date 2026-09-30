/* Copyright (c) 2026, Kai Wang. Part of the QUASI88 MS-DOS port.
   SPDX-License-Identifier: BSD-3-Clause (see LICENSE). */
/* AZTSB: return an Aztech AZT2320 to Sound Blaster mode, for DOSSTART.BAT.

   The Windows 98 driver leaves the card in Windows Sound System (WSS) mode
   with the codec's DAC and aux-2 input muted; DOS programs then get an SB
   DSP that answers but whose DMA never moves, or no audible output. This
   tool restores the codec mixer to the unmuted values seen after an F8
   boot, sends DSP commands 09h, 01h (Sound Blaster mode, as in the ALSA
   Aztech Sound Galaxy driver), resets the DSP, and checks that 8-bit SB DMA
   moves. It is the WSSTEST /SB sequence without the diagnostics.

   Usage: AZTSB [/V]
   BLASTER supplies the SB port and 8-bit DMA channel (default A220 D1).
   Q88WSS A names the codec port; otherwise the usual ports are probed.
   Errorlevel: 0 SB mode works, 1 no SB DSP, 2 SB DMA did not move.
 */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wait.h"

#define DMA_CHECK_MS 200

static const unsigned candidates[] = {
    0x534, 0x530, 0x608, 0x604, 0xe84, 0xe80, 0xf44, 0xf40
};

static unsigned sb_base = 0x220;
static unsigned sb_dma = 1;
static int verbose;

static unsigned elapsed_ms(const union wait_time *start)
{
    return (unsigned)wait_calc_elasped_time_ms(start);
}

static void pause_ms(unsigned ms)
{
    union wait_time start;
    wait_get_current_time(&start);
    while (elapsed_ms(&start) < ms) { }
}

/* Parse BLASTER-style "A220 D1" settings; only non-NULL fields are set. */
static void parse_settings(const char *name, unsigned *address, unsigned *dma)
{
    const char *p = getenv(name);
    if (!p) return;
    while (*p) {
        char key = *p++;
        int base = (key == 'A' || key == 'a') ? 16 : 10;
        unsigned value = 0;
        for (;;) {
            int digit;
            if (*p >= '0' && *p <= '9') digit = *p - '0';
            else if (base == 16 && *p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
            else if (base == 16 && *p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
            else break;
            value = value * (unsigned)base + (unsigned)digit;
            ++p;
        }
        if ((key == 'A' || key == 'a') && address) *address = value;
        else if ((key == 'D' || key == 'd') && dma) *dma = value;
        while (*p == ' ' || *p == '\t') ++p;
    }
}

static int sb_write(unsigned char value)
{
    unsigned i;
    for (i = 0; i < 100000U; ++i)
        if (!(inp(sb_base + 0x0c) & 0x80)) {
            outp(sb_base + 0x0c, value);
            return 1;
        }
    return 0;
}

static int sb_reset(void)
{
    unsigned i;
    outp(sb_base + 6, 1);
    for (i = 0; i < 16U; ++i) (void)inp(sb_base + 6);   /* >= 3 us of ISA bus time */
    outp(sb_base + 6, 0);
    for (i = 0; i < 100000U; ++i)
        if (inp(sb_base + 0x0e) & 0x80) return inp(sb_base + 0x0a) == 0xaa;
    return 0;
}

/* A WSS codec keeps the written index, keeps a test value in I1 (restored
   afterwards), and reports ID 1010b in I12. */
static int codec_probe(unsigned base)
{
    unsigned char saved, readback;
    unsigned i;
    if (inp(base) == 0xff) return 0;
    for (i = 0; i < 100000U && inp(base) == 0x80; ++i) { }
    outp(base, 1);
    if ((inp(base) & 0x1f) != 1) return 0;
    saved = (unsigned char)inp(base + 1);
    outp(base + 1, 0x45);
    readback = (unsigned char)inp(base + 1);
    outp(base + 1, saved);
    if (readback != 0x45 && (readback & ~0x20) != 0x45) return 0;
    outp(base, 12);
    return (inp(base + 1) & 0x0f) == 0x0a;
}

static void codec_out(unsigned base, unsigned index, unsigned char value)
{
    outp(base, index);
    outp(base + 1, value);
}

/* Start a short single-cycle 8-bit transfer of silence and check that the
   8237 count moves. The DSP is paused before the block ends, so no SB IRQ
   is raised. */
static int sb_dma_moves(void)
{
    union REGS regs;
    unsigned segment, selector, offset, channel = sb_dma & 3, i;
    unsigned long physical;
    unsigned char __far *buffer;
    unsigned first, last = 0;
    union wait_time start;
    int moved = 0;
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = 0x0100;                 /* DPMI allocate DOS memory */
    regs.x.ebx = (4096U + 4096U + 15U) >> 4;
    int386(0x31, &regs, &regs);
    if (regs.x.cflag) return 0;
    segment = regs.w.ax;
    selector = regs.w.dx;
    /* Use a 4 KiB window that does not cross a 64 KiB DMA page. */
    physical = (unsigned long)segment << 4;
    offset = ((physical & 0xffffUL) + 4096UL > 0x10000UL) ?
             (unsigned)(0x10000UL - (physical & 0xffffUL)) : 0;
    physical += offset;
    buffer = (unsigned char __far *)MK_FP(selector, offset);
    for (i = 0; i < 4096U; ++i) buffer[i] = 0x80;
    outp(0x0a, 0x04 | channel);
    outp(0x0b, 0x48 | channel);          /* single, no auto-init, memory to device */
    outp(0x0c, 0);
    outp(channel * 2, (unsigned)(physical & 0xff));
    outp(channel * 2, (unsigned)((physical >> 8) & 0xff));
    {
        static const unsigned char pages[4] = {0x87, 0x83, 0x81, 0x82};
        outp(pages[channel], (unsigned)((physical >> 16) & 0xff));
    }
    outp(0x0c, 0);
    outp(channel * 2 + 1, 0xff);         /* 4096 bytes */
    outp(channel * 2 + 1, 0x0f);
    outp(0x0a, channel);
    outp(0x0c, 0);
    first = inp(channel * 2 + 1);
    first |= inp(channel * 2 + 1) << 8;
    if (sb_write(0x40) && sb_write(211) &&      /* 22,222 Hz */
        sb_write(0x14) && sb_write(0xff) && sb_write(0x0f)) {
        wait_get_current_time(&start);
        while (elapsed_ms(&start) < DMA_CHECK_MS) {
            outp(0x0c, 0);
            last = inp(channel * 2 + 1);
            last |= inp(channel * 2 + 1) << 8;
            if (last != first) {
                moved = 1;
                break;
            }
        }
    }
    (void)sb_write(0xd0);                /* pause 8-bit DMA */
    outp(0x0a, 0x04 | channel);
    (void)inp(sb_base + 0x0e);           /* clear any pending 8-bit IRQ */
    (void)sb_reset();
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = 0x0101;                 /* DPMI free DOS memory */
    regs.w.dx = (unsigned short)selector;
    int386(0x31, &regs, &regs);
    if (verbose) printf("AZTSB: 8-bit DMA %u count %04X -> %04X\n", sb_dma, first, last);
    return moved;
}

int main(int argc, char **argv)
{
    unsigned requested = 0, codec = 0, i;
    for (i = 1; i < (unsigned)argc; ++i)
        if (!stricmp(argv[i], "/V")) verbose = 1;
    parse_settings("BLASTER", &sb_base, &sb_dma);
    parse_settings("Q88WSS", &requested, NULL);
    if (sb_base < 0x200 || sb_base > 0x3f0 || sb_dma > 3) {
        puts("AZTSB: invalid BLASTER A or D setting");
        return 1;
    }
    wait_vsync_init();
    if (!sb_reset()) {
        printf("AZTSB: no Sound Blaster DSP at %03X\n", sb_base);
        return 1;
    }
    if (requested) {
        if (codec_probe(requested)) codec = requested;
    } else {
        for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]) && !codec; ++i)
            if (codec_probe(candidates[i])) codec = candidates[i];
    }
    if (codec) {
        /* Unmute the codec's aux inputs (I2-I5) and DAC (I6/I7): the SB
           output passes through them on the AZT2320. */
        for (i = 2; i <= 5; ++i) codec_out(codec, i, 0x0c);
        codec_out(codec, 6, 0x08);
        codec_out(codec, 7, 0x08);
        if (verbose) printf("AZTSB: WSS codec at %03X unmuted\n", codec);
    }
    (void)sb_write(0x09);
    (void)sb_write(0x01);
    pause_ms(20);
    if (!sb_reset()) {
        printf("AZTSB: Sound Blaster DSP at %03X stopped answering\n", sb_base);
        return 1;
    }
    if (!sb_dma_moves()) {
        printf("AZTSB: Sound Blaster at %03X answers, but 8-bit DMA %u does not move\n",
               sb_base, sb_dma);
        return 2;
    }
    printf("AZTSB: Sound Blaster mode at %03X%s\n", sb_base,
           codec ? " (was in WSS mode)" : "");
    return 0;
}
