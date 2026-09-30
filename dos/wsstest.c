/* Copyright (c) 2026, Kai Wang. Part of the QUASI88 MS-DOS port.
   SPDX-License-Identifier: BSD-3-Clause (see LICENSE). */
/* WSSTEST: probe a Windows Sound System (AD1848/CS4231-compatible) codec
   and play a 16-bit test tone through it. With /AZT, an Aztech AZT2320 in
   Sound Blaster mode is first switched to WSS mode (DSP commands 09h, 00h,
   as in the Linux ALSA azt2320 driver) and afterwards back to Sound Blaster
   mode (09h, 01h, the SB8 mode of the ALSA Aztech Sound Galaxy driver).
   This is a hardware bring-up tool for the planned QUASI88 WSS backend;
   see dos/README.md.

   Usage: WSSTEST [/AZT] [/SB]
   /SB only returns an AZT2320 to Sound Blaster mode: it restores the codec
   mixer if the card is in WSS mode, sends 09h, 01h, logs the SB Pro mixer,
   and checks that 8-bit SB DMA moves.
   BLASTER supplies the Sound Blaster port and default IRQ/DMA. Q88WSS may
   name the codec port, IRQ, and 8-bit DMA channel, e.g. SET Q88WSS=A530 I5 D1.
 */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wait.h"

#define RING_BYTES 32768U
#define RING_SAMPLES (RING_BYTES / 2U)
#define TONE_PERIOD 32U          /* 22,050 / 32 = 689 Hz; whole periods fill the ring */
#define TONE_AMPLITUDE 6000.0
#define PLAY_MS 3000
#define DMA_START_MS 200
#define CALIBRATION_MS 500

/* Codec direct registers, relative to the codec base. */
#define R_INDEX 0
#define R_DATA 1
#define R_STATUS 2
#define INDEX_MCE 0x40            /* mode change enable */
#define INDEX_INIT 0x80           /* read: codec is initializing */

/* Indexed registers. */
#define I_RIGHT_INPUT 1
#define I_LEFT_DAC 6
#define I_RIGHT_DAC 7
#define I_FORMAT 8
#define I_IFACE 9
#define I_PIN 10
#define I_TEST_INIT 11
#define I_MODE_ID 12
#define I_UPPER_COUNT 14
#define I_LOWER_COUNT 15
#define I_VERSION 25

#define FORMAT_16BIT_22050_MONO 0x47   /* 16-bit linear, XTAL2 / 768 */
#define IFACE_PEN 0x01
#define IFACE_SDC 0x04
#define IFACE_ACAL 0x08
#define PIN_IEN 0x02
#define TEST_ACI 0x20
#define MODE_ID_MODE2 0x40

static const unsigned candidates[] = {
    0x530, 0x534, 0x604, 0x608, 0xe80, 0xe84, 0xf40, 0xf44
};

static unsigned sb_base = 0x220;
static unsigned sb_irq = 5;
static unsigned sb_dma = 1;
static unsigned irq = 5;                  /* WSS codec IRQ */
static unsigned dma = 1;                  /* WSS codec 8-bit DMA channel */
static unsigned codec_base;               /* read by the IRQ handler */
static volatile unsigned long irq_count;  /* written by the IRQ handler */
static unsigned irq_number;               /* read by the IRQ handler */
static unsigned dma_segment, dma_selector, dma_offset;
static unsigned char __far *dma_buffer;
static void (__interrupt __far *previous_irq)(void);
static unsigned irq_vector;
static int irq_installed;
static unsigned char pic_master_mask, pic_slave_mask;

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

static unsigned parse_value(const char **text, int base)
{
    const char *p = *text;
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
    *text = p;
    return value;
}

/* Parse BLASTER-style "A220 I5 D1" settings. Returns the A value or 0. */
static unsigned parse_settings(const char *name, unsigned *irq_out, unsigned *dma_out)
{
    const char *p = getenv(name);
    unsigned address = 0;
    if (!p) return 0;
    while (*p) {
        char key = *p++;
        if (key == 'A' || key == 'a') address = parse_value(&p, 16);
        else if (key == 'I' || key == 'i') *irq_out = parse_value(&p, 10);
        else if (key == 'D' || key == 'd') *dma_out = parse_value(&p, 10);
        else (void)parse_value(&p, 10);
        while (*p == ' ' || *p == '\t') ++p;
    }
    return address;
}

/* ---- Sound Blaster DSP, used for reporting and the AZT2320 switch ---- */

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

static int sb_read(unsigned char *value)
{
    unsigned i;
    for (i = 0; i < 100000U; ++i)
        if (inp(sb_base + 0x0e) & 0x80) {
            *value = (unsigned char)inp(sb_base + 0x0a);
            return 1;
        }
    return 0;
}

/* Reset the DSP and read its version. Returns 0 if it does not answer. */
static int sb_probe(unsigned *major, unsigned *minor)
{
    unsigned i;
    unsigned char a, b;
    outp(sb_base + 6, 1);
    for (i = 0; i < 16U; ++i) (void)inp(sb_base + 6);   /* >= 3 us of ISA bus time */
    outp(sb_base + 6, 0);
    for (i = 0; i < 100000U; ++i)
        if (inp(sb_base + 0x0e) & 0x80) break;
    if (i == 100000U || inp(sb_base + 0x0a) != 0xaa) return 0;
    *major = *minor = 0;
    if (sb_write(0xe1) && sb_read(&a) && sb_read(&b)) {
        *major = a;
        *minor = b;
    }
    return 1;
}

/* ---- Codec register access ---- */

/* Wait until the codec stops reporting INIT; it does so while it
   resynchronizes after reset, power-up, or a format change. */
static int codec_ready(unsigned base)
{
    union wait_time start;
    wait_get_current_time(&start);
    while (inp(base + R_INDEX) == INDEX_INIT)
        if (elapsed_ms(&start) > CALIBRATION_MS) return 0;
    return 1;
}

static unsigned char codec_in(unsigned index)
{
    outp(codec_base + R_INDEX, index);
    return (unsigned char)inp(codec_base + R_DATA);
}

static void codec_out(unsigned index, unsigned char value)
{
    outp(codec_base + R_INDEX, index);
    outp(codec_base + R_DATA, value);
}

/* Look for a codec at one port: the index register must hold what was
   written, and the right input control register (I1) must keep a test
   value. The original I1 value is restored. */
static int codec_probe(unsigned base)
{
    unsigned char saved, readback;
    if (inp(base + R_INDEX) == 0xff) return 0;
    if (!codec_ready(base)) return 0;
    outp(base + R_INDEX, I_RIGHT_INPUT);
    if ((inp(base + R_INDEX) & 0x1f) != I_RIGHT_INPUT) return 0;
    saved = (unsigned char)inp(base + R_DATA);
    outp(base + R_DATA, 0x45);
    readback = (unsigned char)inp(base + R_DATA);
    outp(base + R_DATA, saved);
    /* Some AD1847-class parts force the mic gain bit (20h) on. */
    if (readback != 0x45 && (readback & ~0x20) != 0x45) return 0;
    /* Other hardware can echo an index/data pair too (the GUS GF1 register
       select at base+4 does). AD1848- and CS4231-compatible codecs report
       ID 1010b in the low bits of I12. */
    outp(base + R_INDEX, I_MODE_ID);
    readback = (unsigned char)inp(base + R_DATA);
    if ((readback & 0x0f) != 0x0a) {
        printf("  probe %03X: index/data registers respond, but I12=%02X is not a WSS codec ID\n",
               base, readback);
        return 0;
    }
    return 1;
}

static unsigned find_codec(unsigned only)
{
    unsigned i;
    if (only) {
        int found = codec_probe(only);
        printf("  probe %03X: %s\n", only, found ? "codec found" : "no codec");
        return found ? only : 0;
    }
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        int found = codec_probe(candidates[i]);
        printf("  probe %03X: %s\n", candidates[i], found ? "codec found" : "no codec");
        if (found) return candidates[i];
    }
    return 0;
}

static void report_codec(void)
{
    unsigned char mode_id = codec_in(I_MODE_ID);
    unsigned char mode2;
    printf("WSS codec at %03X: I12=%02X (ID %X)", codec_base, mode_id, mode_id & 0x0f);
    /* CS4231-class codecs accept MODE2 in I12 and then expose I25. Put the
       register back afterwards; this test uses the AD1848-compatible mode. */
    codec_out(I_MODE_ID, (unsigned char)(mode_id | MODE_ID_MODE2));
    mode2 = codec_in(I_MODE_ID);
    if (mode2 & MODE_ID_MODE2) {
        printf(", MODE2 supported, I25=%02X", codec_in(I_VERSION));
        codec_out(I_MODE_ID, (unsigned char)(mode_id & ~MODE_ID_MODE2));
    } else {
        printf(", AD1848-class (no MODE2)");
    }
    printf("\n  I0..I7:");
    {
        unsigned i;
        for (i = 0; i < 8; ++i) printf(" %02X", codec_in(i));
    }
    printf("  I9=%02X I10=%02X I11=%02X\n", codec_in(I_IFACE), codec_in(I_PIN),
           codec_in(I_TEST_INIT));
}

/* ---- DMA buffer, IRQ, and 8237 ---- */

static int lock_memory(void *address, unsigned long length)
{
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = 0x0000ff1cUL;       /* CauseWay LockMem32 */
    regs.x.esi = (unsigned long)address;
    regs.x.ecx = length;
    int386(0x31, &regs, &regs);
    return !regs.x.cflag;
}

static void __interrupt __far codec_irq_handler(void)
{
    ++irq_count;
    outp(codec_base + R_STATUS, 0);   /* any write clears the codec interrupt */
    if (irq_number >= 8) outp(0xa0, 0x20);
    outp(0x20, 0x20);
}

static int install_irq(void)
{
    if (!lock_memory((void *)(unsigned long)codec_irq_handler, 512) ||
        !lock_memory(&codec_base, sizeof(codec_base)) ||
        !lock_memory(&irq_number, sizeof(irq_number)) ||
        !lock_memory((void *)&irq_count, sizeof(irq_count))) return 0;
    irq_number = irq;
    irq_vector = irq < 8 ? irq + 8 : irq + 0x68;
    previous_irq = _dos_getvect(irq_vector);
    _dos_setvect(irq_vector, codec_irq_handler);
    pic_master_mask = (unsigned char)inp(0x21);
    pic_slave_mask = (unsigned char)inp(0xa1);
    if (irq < 8) {
        outp(0x21, pic_master_mask & (unsigned char)~(1U << irq));
    } else {
        outp(0xa1, pic_slave_mask & (unsigned char)~(1U << (irq - 8)));
        outp(0x21, pic_master_mask & (unsigned char)~(1U << 2));
    }
    irq_installed = 1;
    return 1;
}

static void remove_irq(void)
{
    if (!irq_installed) return;
    _dos_setvect(irq_vector, previous_irq);
    outp(0x21, pic_master_mask);
    outp(0xa1, pic_slave_mask);
    irq_installed = 0;
}

static int allocate_dma_buffer(void)
{
    union REGS regs;
    unsigned physical;
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = 0x0100;             /* DPMI allocate DOS memory block */
    /* Up to 64 KiB extra aligns the ring so it cannot cross a DMA page. */
    regs.x.ebx = (RING_BYTES + 65535U + 15U) >> 4;
    int386(0x31, &regs, &regs);
    if (regs.x.cflag) return 0;
    dma_segment = regs.w.ax;
    dma_selector = regs.w.dx;
    physical = (dma_segment << 4) & 0xffffU;
    dma_offset = (0x10000U - physical) & 0xffffU;
    dma_buffer = (unsigned char __far *)MK_FP(dma_selector, dma_offset);
    return 1;
}

static void free_dma_buffer(void)
{
    union REGS regs;
    if (!dma_selector) return;
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = 0x0101;             /* DPMI free DOS memory block */
    regs.w.dx = (unsigned short)dma_selector;
    int386(0x31, &regs, &regs);
    dma_selector = 0;
}

static unsigned char dma_page_port(unsigned channel)
{
    static const unsigned char ports[4] = {0x87, 0x83, 0x81, 0x82};
    return ports[channel & 3];
}

static unsigned read_dma_count_raw(unsigned channel)
{
    unsigned port = (channel & 3) * 2 + 1, low, high;
    outp(0x0c, 0);
    low = inp(port);
    high = inp(port);
    return low | (high << 8);
}

/* The 8237 count is read as two bytes while the transfer runs. If the low
   byte wraps between the two reads, the value is off by 256. Accept a read
   only when the next one agrees within the normal decrement. */
static unsigned read_dma_count_on(unsigned channel)
{
    unsigned a = read_dma_count_raw(channel), b = a, i;
    for (i = 0; i < 8U; ++i) {
        b = read_dma_count_raw(channel);
        if (((a - b) & 0xffffU) <= 2U) return b;
        a = b;
    }
    return b;
}

static unsigned read_dma_count(void)
{
    return read_dma_count_on(dma);
}

static void start_dma(void)
{
    unsigned long physical = ((unsigned long)dma_segment << 4) + dma_offset;
    unsigned channel = dma & 3;
    unsigned address_port = channel * 2;
    outp(0x0a, 0x04 | channel);           /* mask */
    outp(0x0b, 0x58 | channel);           /* single, auto-init, memory to device */
    outp(0x0c, 0);
    outp(address_port, (unsigned)(physical & 0xff));
    outp(address_port, (unsigned)((physical >> 8) & 0xff));
    outp(dma_page_port(channel), (unsigned)((physical >> 16) & 0xff));
    outp(0x0c, 0);
    outp(address_port + 1, (RING_BYTES - 1) & 0xff);
    outp(address_port + 1, (RING_BYTES - 1) >> 8);
    outp(0x0a, channel);                  /* unmask */
}

static void fill_tone(void)
{
    unsigned i;
    for (i = 0; i < RING_SAMPLES; ++i) {
        int sample = (int)floor(TONE_AMPLITUDE *
            sin(2.0 * 3.14159265358979323846 * (i % TONE_PERIOD) / TONE_PERIOD) + 0.5);
        dma_buffer[i * 2] = (unsigned char)(sample & 0xff);
        dma_buffer[i * 2 + 1] = (unsigned char)((sample >> 8) & 0xff);
    }
}

/* ---- Playback test ---- */

/* Set 16-bit mono 22,050 Hz single-channel DMA playback, then wait for the
   autocalibration that follows a mode change. */
static int codec_configure(void)
{
    union wait_time start;
    outp(codec_base + R_INDEX, INDEX_MCE | I_FORMAT);
    outp(codec_base + R_DATA, FORMAT_16BIT_22050_MONO);
    if (!codec_ready(codec_base)) return 0;
    outp(codec_base + R_INDEX, INDEX_MCE | I_IFACE);
    outp(codec_base + R_DATA, IFACE_ACAL | IFACE_SDC);
    outp(codec_base + R_INDEX, I_IFACE);      /* leave mode change */
    if (!codec_ready(codec_base)) return 0;
    pause_ms(2);                              /* let ACI rise before polling it */
    wait_get_current_time(&start);
    while (codec_in(I_TEST_INIT) & TEST_ACI)
        if (elapsed_ms(&start) > CALIBRATION_MS) return 0;
    printf("Codec format: I8=%02X I9=%02X (calibrated)\n",
           codec_in(I_FORMAT), codec_in(I_IFACE));
    return 1;
}

static int play_tone(void)
{
    unsigned char saved_left, saved_right, saved_pin;
    unsigned first, last_pos, pos, count, block;
    unsigned long consumed = 0;
    union wait_time start;
    unsigned ms = 0;
    int moved = 0;

    if (!allocate_dma_buffer()) {
        puts("FAIL: no DOS memory for the DMA buffer");
        return 0;
    }
    fill_tone();
    if (!install_irq()) {
        puts("FAIL: could not lock memory for the IRQ handler");
        free_dma_buffer();
        return 0;
    }
    saved_left = codec_in(I_LEFT_DAC);
    saved_right = codec_in(I_RIGHT_DAC);
    saved_pin = codec_in(I_PIN);
    if (!codec_configure()) {
        puts("FAIL: codec did not finish initialization/calibration");
        remove_irq();
        free_dma_buffer();
        return 0;
    }
    /* Interrupt twice per ring pass: the count is in samples minus one. */
    block = RING_SAMPLES / 2U - 1U;
    codec_out(I_LOWER_COUNT, (unsigned char)(block & 0xff));
    codec_out(I_UPPER_COUNT, (unsigned char)(block >> 8));
    codec_out(I_LEFT_DAC, 0x00);          /* unmuted, 0 dB attenuation */
    codec_out(I_RIGHT_DAC, 0x00);
    codec_out(I_PIN, (unsigned char)(saved_pin | PIN_IEN));
    outp(codec_base + R_STATUS, 0);
    irq_count = 0;
    start_dma();
    first = last_pos = RING_BYTES - 1U - read_dma_count();
    codec_out(I_IFACE, IFACE_ACAL | IFACE_SDC | IFACE_PEN);
    printf("Playing 689 Hz 16-bit mono tone at 22,050 Hz on IRQ %u DMA %u for %u s...\n",
           irq, dma, PLAY_MS / 1000);
    wait_get_current_time(&start);
    while ((ms = elapsed_ms(&start)) < PLAY_MS) {
        count = read_dma_count();
        pos = (RING_BYTES - 1U - count) & (RING_BYTES - 1U);
        consumed += (pos - last_pos) & (RING_BYTES - 1U);
        if (pos != first) moved = 1;
        last_pos = pos;
        if (!moved && ms > DMA_START_MS) break;
    }
    codec_out(I_IFACE, IFACE_ACAL | IFACE_SDC);  /* playback off */
    codec_out(I_PIN, saved_pin);
    outp(0x0a, 0x04 | (dma & 3));
    outp(codec_base + R_STATUS, 0);
    codec_out(I_LEFT_DAC, saved_left);
    codec_out(I_RIGHT_DAC, saved_right);
    remove_irq();
    free_dma_buffer();

    if (!moved) {
        printf("FAIL: DMA %u did not move within %u ms (IRQs %lu)\n", dma,
               DMA_START_MS, irq_count);
        return 0;
    }
    printf("DMA moved %lu bytes in %u ms: measured rate %.0f Hz; codec IRQs %lu (rate from IRQs about %.0f Hz)\n",
           consumed, ms, consumed / 2.0 * 1000.0 / ms, irq_count,
           irq_count * (RING_SAMPLES / 2.0) * 1000.0 / ms);
    if (!irq_count) printf("WARNING: no codec IRQs; check the IRQ number (tried %u)\n", irq);
    return 1;
}

/* Start a short single-cycle 8-bit Sound Blaster transfer of silence and
   check that the 8237 count moves. The DSP is paused and reset well before
   the block ends, so no SB interrupt is raised. */
static int sb_dma_moves(void)
{
    unsigned long physical;
    unsigned channel = sb_dma & 3, first, last = 0, i;
    union wait_time start;
    int moved = 0;
    if (!allocate_dma_buffer()) return 0;
    for (i = 0; i < 4096U; ++i) dma_buffer[i] = 0x80;
    physical = ((unsigned long)dma_segment << 4) + dma_offset;
    outp(0x0a, 0x04 | channel);
    outp(0x0b, 0x48 | channel);           /* single, no auto-init, memory to device */
    outp(0x0c, 0);
    outp(channel * 2, (unsigned)(physical & 0xff));
    outp(channel * 2, (unsigned)((physical >> 8) & 0xff));
    outp(dma_page_port(channel), (unsigned)((physical >> 16) & 0xff));
    outp(0x0c, 0);
    outp(channel * 2 + 1, 0xff);          /* 4096 bytes */
    outp(channel * 2 + 1, 0x0f);
    outp(0x0a, channel);
    first = read_dma_count_on(channel);
    if (sb_write(0x40) && sb_write(211) &&        /* 22,222 Hz */
        sb_write(0x14) && sb_write(0xff) && sb_write(0x0f)) {
        wait_get_current_time(&start);
        while (elapsed_ms(&start) < DMA_START_MS)
            if ((last = read_dma_count_on(channel)) != first) {
                moved = 1;
                break;
            }
    }
    (void)sb_write(0xd0);                 /* pause 8-bit DMA */
    outp(0x0a, 0x04 | channel);
    (void)inp(sb_base + 0x0e);            /* clear any pending 8-bit IRQ */
    free_dma_buffer();
    printf("Sound Blaster 8-bit DMA %u test: %s (count %04X -> %04X)\n", sb_dma,
           moved ? "moves" : "does not move", first, last);
    return moved;
}

/* ---- Mixers ---- */

/* SB Pro mixer registers: voice (DAC), mic, input, output/filter, master,
   FM, CD, and line. */
static const unsigned char sb_mixer_regs[] = {0x04, 0x0a, 0x0c, 0x0e, 0x22, 0x26, 0x28, 0x2e};

static void sb_mixer_dump(const char *label)
{
    unsigned i;
    printf("SB Pro mixer %s:", label);
    for (i = 0; i < sizeof(sb_mixer_regs); ++i) {
        outp(sb_base + 4, sb_mixer_regs[i]);
        printf(" %02X=%02X", sb_mixer_regs[i], inp(sb_base + 5));
    }
    printf("\n");
}

/* Put the codec's aux inputs (I2-I5) and DAC (I6/I7) at the unmuted values
   the AZT2320 showed after an F8 boot. On this card the Sound Blaster and
   FM output may pass through these inputs, so a mute left by Windows can
   silence SB mode as well. */
static void codec_mixer_defaults(void)
{
    unsigned i;
    for (i = 2; i <= 5; ++i) codec_out(i, 0x0c);
    codec_out(I_LEFT_DAC, 0x08);
    codec_out(I_RIGHT_DAC, 0x08);
    printf("Codec mixer set to F8 power-on values; I0..I7:");
    for (i = 0; i < 8; ++i) printf(" %02X", codec_in(i));
    printf("\n");
}

int main(int argc, char **argv)
{
    unsigned major = 0, minor = 0, requested, i;
    int azt = 0, sb_only = 0, sb_present, switched = 0, ok;

    for (i = 1; i < (unsigned)argc; ++i) {
        if (!stricmp(argv[i], "/AZT")) azt = 1;
        else if (!stricmp(argv[i], "/SB")) sb_only = 1;
    }
    puts(sb_only ? "WSSTEST /SB: return an AZT2320 to Sound Blaster mode" :
                   "WSSTEST: Windows Sound System codec probe and 16-bit tone test");
    {
        unsigned address = parse_settings("BLASTER", &sb_irq, &sb_dma);
        if (address) sb_base = address;
    }
    /* The WSS codec shares the card's IRQ and first DMA channel unless
       Q88WSS says otherwise. */
    irq = sb_irq;
    dma = sb_dma;
    requested = parse_settings("Q88WSS", &irq, &dma);
    printf("BLASTER=%s\nQ88WSS=%s\n", getenv("BLASTER") ? getenv("BLASTER") : "",
           getenv("Q88WSS") ? getenv("Q88WSS") : "");
    if (irq > 15 || dma > 3 || dma == 2 || sb_dma > 3 || sb_base < 0x200 || sb_base > 0x3f0) {
        puts("FAIL: invalid port, IRQ, or DMA setting (WSS needs 8-bit DMA 0, 1, or 3)");
        return 1;
    }
    wait_vsync_init();
    sb_present = sb_probe(&major, &minor);
    if (sb_present) {
        printf("Sound Blaster DSP %u.%02u at %03X\n", major, minor, sb_base);
        sb_mixer_dump("at start");
    } else {
        printf("No Sound Blaster DSP at %03X\n", sb_base);
    }

    puts("Probing for a WSS codec:");
    codec_base = find_codec(requested);
    if (!codec_base && azt && !sb_only) {
        if (!sb_present) {
            puts("FAIL: /AZT needs the Sound Blaster DSP to send the WSS switch");
            return 1;
        }
        puts("Sending AZT2320 WSS mode switch (DSP 09h, 00h)");
        if (!sb_write(0x09) || !sb_write(0x00)) {
            puts("FAIL: DSP did not accept the switch commands");
            return 1;
        }
        switched = 1;
        pause_ms(20);
        puts("Probing again:");
        codec_base = find_codec(requested);
    }
    if (sb_only) {
        if (codec_base) {
            puts("Card is in WSS mode");
            report_codec();
        } else {
            puts("No WSS codec answers; the card is probably in Sound Blaster mode");
        }
        ok = 1;
    } else if (!codec_base) {
        puts(azt ? "No WSS codec found" :
                   "No WSS codec found; try WSSTEST /AZT on an AZT2320");
        ok = 0;
    } else {
        report_codec();
        ok = play_tone();
    }

    /* With /AZT or /SB, return the card to Sound Blaster mode even when an
       earlier program (such as the Windows 98 driver) left it in WSS mode.
       Restore the codec mixer first, while the codec is still reachable. */
    if ((azt || sb_only) && sb_present) {
        if (codec_base) codec_mixer_defaults();
        puts("Returning to Sound Blaster mode (DSP 09h, 01h)");
        if (!sb_write(0x09) || !sb_write(0x01))
            puts("WARNING: DSP did not accept the SB mode commands");
        pause_ms(20);
    }
    if (sb_present) {
        unsigned after_major = 0, after_minor = 0;
        if (sb_probe(&after_major, &after_minor)) {
            printf("After test: Sound Blaster DSP %u.%02u answers at %03X\n",
                   after_major, after_minor, sb_base);
            /* The SB Pro mixer is only logged. On the AZT2320 it read the
               same after an F8 boot and after Windows 98; the silence after
               Windows came from the codec mutes restored above. A mixer
               reset would lower the voice/master levels and mute CD/line. */
            if (azt || sb_only) sb_mixer_dump("in SB mode");
            if (!sb_dma_moves() && sb_only) ok = 0;
        } else {
            printf("After test: Sound Blaster DSP at %03X does not answer%s\n", sb_base,
                   switched ? " (card may stay in WSS mode until power-off)" : "");
            if (sb_only) ok = 0;
        }
    }
    if (sb_only) puts(ok ? "RESULT: PASS (Sound Blaster mode restored)" : "RESULT: FAIL");
    else puts(ok ? "RESULT: PASS (report whether the tone was clean)" : "RESULT: FAIL");
    return ok ? 0 : 1;
}
