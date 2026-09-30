/* Copyright (c) 2026, Kai Wang. Part of the QUASI88 MS-DOS port.
   SPDX-License-Identifier: BSD-3-Clause (see LICENSE). */
/* DMA audio output for the DOS target. Sound Blaster-compatible cards play
   16-bit on an SB16 (DSP 4.xx), otherwise 8-bit mono. With -doswss, a
   Windows Sound System (AD1848/CS4231-compatible) codec plays 16-bit PCM;
   an Aztech AZT2320 is switched from Sound Blaster to WSS mode for this and
   back on exit. The 16-bit modes play stereo unless -dosmono is given or
   the 64 KiB DMA ring cannot be allocated. */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mame-quasi88.h"
#include "getconf.h"
#include "wait.h"

/* The DMA ring is 64 KiB (the most one 8-bit DMA transfer can address),
   or 32 KiB when conventional memory is short; stereo needs the larger
   ring to keep the target lead. */
#define SB_DMA_BYTES_MAX 65536UL
#define SB_DMA_BYTES_MIN 32768UL
/* DSP command 40h uses an integer time constant. 211 gives 22,222 Hz;
   233 gives 43,478 Hz for an optional high-rate mono comparison. */
#define SB_RATE_22K 22222U
#define SB_RATE_44K 43478U
/* SB16 DSP command 41h and the WSS codec's crystal dividers give these
   rates exactly. */
#define SB16_RATE_22K 22050U
#define SB16_RATE_44K 44100U
#define SB_GAIN 2
/* Rate control: the core renders more or fewer samples per frame (at most
   SB_RATE_ADJUST_MAX, 5%) to hold the DMA lead near its target, absorbing a
   card clock that differs from the PIT and an emulator that falls behind real
   time. The error is low-pass filtered so frame jitter does not modulate
   pitch. */
#define SB_RATE_ADJUST_MAX 0.05
#define SB_RATE_ERROR_SMOOTH 16.0
#define SB_DMA_START_CHECK_MS 200
/* Frames kept queued: 0.37 s at 22 kHz. The physical AZT2320 run dipped
   about 5,000 frames below target, so a smaller lead would underrun. Every
   ring holds at least 16,384 frames, which leaves room above the target to
   tell a late writer from an early one. */
#define SB_TARGET_LEAD 8192U

/* WSS codec direct registers (relative to the codec base) and indexed
   registers, as used by WSSTEST on the physical AZT2320. */
#define WSS_INDEX 0
#define WSS_DATA 1
#define WSS_STATUS 2
#define WSS_INDEX_MCE 0x40
#define WSS_INDEX_INIT 0x80
#define WSS_I_RIGHT_INPUT 1
#define WSS_I_AUX1_LEFT 2
#define WSS_I_AUX2_RIGHT 5
#define WSS_I_LEFT_DAC 6
#define WSS_I_RIGHT_DAC 7
#define WSS_I_FORMAT 8
#define WSS_I_IFACE 9
#define WSS_I_PIN 10
#define WSS_I_TEST_INIT 11
#define WSS_I_MODE_ID 12
#define WSS_I_UPPER_COUNT 14
#define WSS_I_LOWER_COUNT 15
#define WSS_I_VERSION 25
#define WSS_FORMAT_16BIT 0x40
#define WSS_FORMAT_STEREO 0x10
#define WSS_RATE_22050 0x07        /* XTAL2 (16.9344 MHz) / 768 */
#define WSS_RATE_44100 0x0b        /* XTAL2 / 384 */
#define WSS_IFACE_PEN 0x01
#define WSS_IFACE_SDC 0x04
#define WSS_IFACE_ACAL 0x08
#define WSS_PIN_IEN 0x02
#define WSS_TEST_ACI 0x20
#define WSS_MODE2 0x40
#define WSS_CALIBRATION_MS 500
/* Aux inputs 1-2 (I2-I5) and DAC (I6/I7) after an F8 boot of the AZT2320:
   unmuted. The card's Sound Blaster output passes through the codec, so
   these must be restored before returning to SB mode (the Windows 98
   driver leaves the DAC and aux 2 muted). */
#define WSS_AUX_POWER_ON 0x0c
#define WSS_DAC_POWER_ON 0x08
/* While playing: DAC at 0 dB attenuation for the best level above analog
   hiss, and the unused aux inputs (SB DSP and FM paths) muted. */
#define WSS_DAC_PLAY 0x00
#define WSS_AUX_MUTE 0x80

enum { OUT_SB8, OUT_SB16, OUT_WSS };

extern int dos_pcm_zero;
extern int dos_sb_filter;
extern int dos_sb_44k;
extern int dos_sb_8bit;
extern int dos_wss;
extern int dos_mono;

static const unsigned wss_candidates[] = {
    0x534, 0x530, 0x608, 0x604, 0xe84, 0xe80, 0xf44, 0xf40
};

static unsigned sb_base = 0x220;
static unsigned sb_irq = 5;
static unsigned sb_dma = 1;
static unsigned sb_hdma = 5;
static int sb_dsp_present;
static unsigned sb_dsp_major, sb_dsp_minor;
static unsigned wss_base;             /* 0 when no codec is in use */
static unsigned wss_irq, wss_dma;
static int out_mode;
static unsigned out_channels;         /* 1 = mono, 2 = interleaved stereo */
static int force_mono;                /* set when WSS stereo setup failed */
static unsigned long sb_ring_bytes;   /* SB_DMA_BYTES_MAX or _MIN */
/* Read by the IRQ handler: the active IRQ line, the DSP acknowledge port
   (0Eh for 8-bit, 0Fh for 16-bit), and the WSS status port (0 for SB). */
static unsigned out_irq;
static unsigned sb_ack_port;
static unsigned wss_status_port;
/* Ring positions below count frames: one sample per channel, one or two
   bytes each. */
static unsigned sb_ring_samples;
static unsigned sb_dma_selector;
static unsigned sb_dma_segment;
static unsigned sb_dma_offset;
static unsigned char __far *sb_dma_buffer;
static unsigned sb_write_pos;
static unsigned long pcm_samples_total;
static unsigned long pcm_samples_non_silent;
static unsigned long pcm_dma_samples_non_silent;
static unsigned long pcm_clipped_samples;
static unsigned long pcm_lr_differ_frames;  /* core frames with left != right */
static unsigned long pcm_peak;
static volatile unsigned long pcm_irq_count;
static unsigned pcm_min_dma_lead;
static unsigned long pcm_low_dma_lead_frames;
static unsigned long pcm_dma_underrun_frames;
static void (__interrupt __far *sb_previous_irq)(void);
static unsigned sb_vector;
static int sb_irq_installed;
static unsigned char sb_pic_master_mask;
static unsigned char sb_pic_slave_mask;
static int sb_pic_masks_saved;
static int sb_active;
static int sb_atexit_registered;
static unsigned source_rate;
static unsigned source_samples;
static double source_phase;
static double sb_rate_scale, sb_rate_error, sb_rate_scale_min, sb_rate_scale_max;
static unsigned sb_target_lead;
/* Unwrapped DMA cursors. Their difference is the lead, which also tells an
   underrun (playback passed the writer) from an overrun (writer lapped the
   playback cursor); a ring-position difference alone cannot. */
static unsigned long sb_written_abs, sb_consumed_abs;
static unsigned sb_last_dma_pos;
static unsigned long pcm_frames, pcm_underrun_resyncs, pcm_overrun_resyncs;
static union wait_time sb_start_time;
static long filter_state_1_q4;
static long filter_state_2_q4;

static int out_16bit(void) { return out_mode != OUT_SB8; }
static unsigned frame_bytes(void) { return (out_16bit() ? 2U : 1U) * out_channels; }

static unsigned sb_rate(void)
{
    if (out_16bit()) return dos_sb_44k ? SB16_RATE_44K : SB16_RATE_22K;
    return dos_sb_44k ? SB_RATE_44K : SB_RATE_22K;
}

static unsigned sb_time_constant(void)
{
    return dos_sb_44k ? 233U : 211U;
}

static unsigned next_frame_samples(void)
{
    unsigned samples;
    source_phase += sb_rate() * sb_rate_scale;
    samples = (unsigned)(source_phase / Machine->refresh_rate);
    source_phase -= samples * Machine->refresh_rate;
    return samples;
}

int xmame_config_init(void) { return TRUE; }
void xmame_config_exit(void) { }
const T_CONFIG_TABLE *xmame_config_get_opt_tbl(void) { return NULL; }
void xmame_config_show_option(FILE *fp) { (void)fp; }
int xmame_config_check_option(char *a, char *b, int priority)
{ (void)a; (void)b; (void)priority; return 0; }
int xmame_config_save_option(void (*write_option)(const char *, const char *))
{ (void)write_option; return 0; }
T_SNDDRV_CONFIG *xmame_config_get_sndopt_tbl(void) { return NULL; }
int xmame_has_audiodevice(void) { return sb_active; }
int xmame_has_mastervolume(void) { return FALSE; }
void osd_set_mastervolume(int attenuation) { (void)attenuation; }
int osd_get_mastervolume(void) { return -32; }

static void pause_ms(unsigned ms)
{
    union wait_time start;
    wait_get_current_time(&start);
    while ((unsigned)wait_calc_elasped_time_ms(&start) < ms) { }
}

static int lock_interrupt_memory(void *address, unsigned long length)
{
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = 0x0000ff1cUL;       /* CauseWay LockMem32 */
    regs.x.esi = (unsigned long)address;
    regs.x.ecx = length;
    int386(0x31, &regs, &regs);
    return !regs.x.cflag;
}

static void __interrupt __far sb_irq_handler(void)
{
    ++pcm_irq_count;
    if (wss_status_port) outp(wss_status_port, 0);   /* any write clears it */
    else (void)inp(sb_ack_port);      /* acknowledge the DSP's 8/16-bit IRQ */
    if (out_irq >= 8) outp(0xa0, 0x20);
    outp(0x20, 0x20);
}

static int install_sb_irq(void)
{
    unsigned long handler = (unsigned long)sb_irq_handler;
    if (!lock_interrupt_memory((void *)handler, 512) ||
        !lock_interrupt_memory(&sb_ack_port, sizeof(sb_ack_port)) ||
        !lock_interrupt_memory(&wss_status_port, sizeof(wss_status_port)) ||
        !lock_interrupt_memory(&out_irq, sizeof(out_irq)) ||
        !lock_interrupt_memory((void *)&pcm_irq_count, sizeof(pcm_irq_count))) return FALSE;
    sb_vector = out_irq < 8 ? out_irq + 8 : out_irq + 0x68;
    sb_previous_irq = _dos_getvect(sb_vector);
    _dos_setvect(sb_vector, sb_irq_handler);
    /* DOS may leave IRQ7 masked when no parallel port is installed.  Save and
       open the card's line (and the slave cascade when needed). */
    sb_pic_master_mask = inp(0x21);
    sb_pic_slave_mask = inp(0xa1);
    sb_pic_masks_saved = TRUE;
    if (out_irq < 8)
        outp(0x21, sb_pic_master_mask & (unsigned char)~(1U << out_irq));
    else {
        outp(0xa1, sb_pic_slave_mask & (unsigned char)~(1U << (out_irq - 8)));
        outp(0x21, sb_pic_master_mask & (unsigned char)~(1U << 2));
    }
    sb_irq_installed = TRUE;
    return TRUE;
}

static void remove_sb_irq(void)
{
    if (sb_irq_installed) {
        _dos_setvect(sb_vector, sb_previous_irq);
        if (sb_pic_masks_saved) {
            unsigned char mask;
            unsigned irq_bit;
            if (out_irq < 8) {
                irq_bit = (unsigned)(1U << out_irq);
                mask = inp(0x21);
                outp(0x21, (mask & (unsigned char)~irq_bit) |
                     (sb_pic_master_mask & irq_bit));
            } else {
                irq_bit = (unsigned)(1U << (out_irq - 8));
                mask = inp(0xa1);
                outp(0xa1, (mask & (unsigned char)~irq_bit) |
                     (sb_pic_slave_mask & irq_bit));
                mask = inp(0x21);
                outp(0x21, (mask & (unsigned char)~0x04) |
                     (sb_pic_master_mask & 0x04));
            }
            sb_pic_masks_saved = FALSE;
        }
        sb_irq_installed = FALSE;
    }
}

static unsigned char dma_page_port(unsigned channel)
{
    static const unsigned char ports[8] =
        {0x87, 0x83, 0x81, 0x82, 0x8f, 0x8b, 0x89, 0x8a};
    return ports[channel & 7];
}

/* 8237 register ports for the active channel: DMA 0-3 on the first
   controller (SB 8-bit and WSS), 16-bit DMA 5-7 on the second
   (word-addressed) controller (SB16). */
static int dma_16bit_controller(void) { return out_mode == OUT_SB16; }
static unsigned dma_channel(void)
{
    if (out_mode == OUT_SB16) return sb_hdma;
    return out_mode == OUT_WSS ? wss_dma : sb_dma;
}
static unsigned dma_mask_port(void) { return dma_16bit_controller() ? 0xd4 : 0x0a; }
static unsigned dma_mode_port(void) { return dma_16bit_controller() ? 0xd6 : 0x0b; }
static unsigned dma_flipflop_port(void) { return dma_16bit_controller() ? 0xd8 : 0x0c; }
static unsigned dma_address_port(void)
{
    return dma_16bit_controller() ? 0xc0 + (sb_hdma & 3) * 4 : (dma_channel() & 3) * 2;
}
static unsigned dma_count_port(void)
{
    return dma_address_port() + (dma_16bit_controller() ? 2 : 1);
}
/* Transfer units in the ring: bytes on the 8-bit controller, words on the
   16-bit one. WSS moves each 16-bit sample as two bytes. */
static unsigned dma_ring_units(void)
{
    return (unsigned)(dma_16bit_controller() ? sb_ring_bytes / 2 : sb_ring_bytes);
}

static int sb_wait_write(void)
{
    unsigned i;
    for (i = 0; i < 100000U; ++i)
        if (!(inp(sb_base + 0x0c) & 0x80)) return TRUE;
    return FALSE;
}

static int sb_write(unsigned char value)
{
    if (!sb_wait_write()) return FALSE;
    outp(sb_base + 0x0c, value);
    return TRUE;
}

static int sb_read(unsigned char *value)
{
    unsigned i;
    for (i = 0; i < 100000U; ++i)
        if (inp(sb_base + 0x0e) & 0x80) {
            *value = (unsigned char)inp(sb_base + 0x0a);
            return TRUE;
        }
    return FALSE;
}

/* Remaining transfers minus one, in DMA units. */
static unsigned read_dma_count_raw(void)
{
    unsigned port = dma_count_port();
    unsigned low, high;
    outp(dma_flipflop_port(), 0);
    low = inp(port);
    high = inp(port);
    return low | (high << 8);
}

/* The count is read as two bytes while the transfer runs. If the low byte
   wraps between them, the value is off by 256 and looks like almost a whole
   extra ring pass. Accept a read only when the next agrees within the
   normal decrement. */
static unsigned read_dma_count(void)
{
    unsigned a = read_dma_count_raw(), b = a, i;
    for (i = 0; i < 8U; ++i) {
        b = read_dma_count_raw();
        if (((a - b) & 0xffffU) <= 2U) return b;
        a = b;
    }
    return b;
}

static int sb_reset(void)
{
    unsigned i;
    outp(sb_base + 6, 1);
    /* The DSP needs a reset pulse of at least 3 us. An ISA port read takes
       about 1 us of bus time regardless of CPU speed, so 16 reads give a
       safe pulse without a CPU-speed-dependent delay loop. */
    for (i = 0; i < 16U; ++i) (void)inp(sb_base + 6);
    outp(sb_base + 6, 0);
    for (i = 0; i < 100000U; ++i) {
        if (inp(sb_base + 0x0e) & 0x80)
            return inp(sb_base + 0x0a) == 0xaa;
    }
    return FALSE;
}

/* Parse BLASTER-style settings ("A220 I5 D1 H5"). Only the letters whose
   pointers are non-NULL are stored. */
static void parse_card_settings(const char *name, unsigned *address, unsigned *irq,
                                unsigned *dma, unsigned *hdma)
{
    const char *p = getenv(name);
    if (!p) return;
    while (*p) {
        char key = *p++;
        unsigned value = 0;
        int base = (key == 'A' || key == 'a') ? 16 : 10;
        while (*p == ' ' || *p == '\t') ++p;
        if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
        while (*p) {
            int digit;
            if (*p >= '0' && *p <= '9') digit = *p - '0';
            else if (base == 16 && *p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
            else if (base == 16 && *p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
            else break;
            value = value * (unsigned)base + (unsigned)digit;
            ++p;
        }
        if ((key == 'A' || key == 'a') && address) *address = value;
        else if ((key == 'I' || key == 'i') && irq) *irq = value;
        else if ((key == 'D' || key == 'd') && dma) *dma = value;
        else if ((key == 'H' || key == 'h') && hdma) *hdma = value;
        while (*p == ' ' || *p == '\t') ++p;
    }
}

/* ---- WSS codec ---- */

/* Wait while the codec reports INIT, which it does while resynchronizing
   after power-up or a format change. */
static int wss_ready(unsigned base)
{
    union wait_time start;
    wait_get_current_time(&start);
    while (inp(base + WSS_INDEX) == WSS_INDEX_INIT)
        if (wait_calc_elasped_time_ms(&start) > WSS_CALIBRATION_MS) return FALSE;
    return TRUE;
}

static unsigned char wss_in(unsigned index)
{
    outp(wss_base + WSS_INDEX, index);
    return (unsigned char)inp(wss_base + WSS_DATA);
}

static void wss_out(unsigned index, unsigned char value)
{
    outp(wss_base + WSS_INDEX, index);
    outp(wss_base + WSS_DATA, value);
}

/* A codec keeps the index it was given, keeps a test value in I1 (restored
   afterwards), and reports ID 1010b in I12. The ID check rejects other
   hardware that echoes an index/data pair. */
static int wss_probe(unsigned base)
{
    unsigned char saved, readback;
    if (inp(base + WSS_INDEX) == 0xff || !wss_ready(base)) return FALSE;
    outp(base + WSS_INDEX, WSS_I_RIGHT_INPUT);
    if ((inp(base + WSS_INDEX) & 0x1f) != WSS_I_RIGHT_INPUT) return FALSE;
    saved = (unsigned char)inp(base + WSS_DATA);
    outp(base + WSS_DATA, 0x45);
    readback = (unsigned char)inp(base + WSS_DATA);
    outp(base + WSS_DATA, saved);
    if (readback != 0x45 && (readback & ~0x20) != 0x45) return FALSE;
    outp(base + WSS_INDEX, WSS_I_MODE_ID);
    return (inp(base + WSS_DATA) & 0x0f) == 0x0a;
}

static unsigned wss_find(unsigned requested)
{
    unsigned i;
    if (requested) return wss_probe(requested) ? requested : 0;
    for (i = 0; i < sizeof(wss_candidates) / sizeof(wss_candidates[0]); ++i)
        if (wss_probe(wss_candidates[i])) return wss_candidates[i];
    return 0;
}

/* Leave the codec in the unmuted F8 power-on mixer state and, when a
   Sound Blaster DSP is present, return the AZT2320 to SB mode (DSP 09h,
   01h; the SB8 mode of the ALSA Aztech Sound Galaxy driver, confirmed on
   the physical card). */
static void wss_leave(void)
{
    unsigned i;
    if (!wss_base) return;
    for (i = WSS_I_AUX1_LEFT; i <= WSS_I_AUX2_RIGHT; ++i) wss_out(i, WSS_AUX_POWER_ON);
    wss_out(WSS_I_LEFT_DAC, WSS_DAC_POWER_ON);
    wss_out(WSS_I_RIGHT_DAC, WSS_DAC_POWER_ON);
    if (sb_dsp_present) {
        (void)sb_write(0x09);
        (void)sb_write(0x01);
        pause_ms(20);
        (void)sb_reset();
    }
    wss_base = 0;
    wss_status_port = 0;
}

/* Set 16-bit mono or stereo at the output rate with single-channel DMA, then wait for
   the autocalibration that follows a mode change. MODE2 is cleared so the
   AD1848-compatible register set (I14/I15 playback count) applies. */
static int wss_configure(void)
{
    union wait_time start;
    unsigned char format = (unsigned char)(WSS_FORMAT_16BIT |
        (out_channels == 2 ? WSS_FORMAT_STEREO : 0) |
        (dos_sb_44k ? WSS_RATE_44100 : WSS_RATE_22050));
    unsigned i;
    wss_out(WSS_I_MODE_ID, (unsigned char)(wss_in(WSS_I_MODE_ID) & ~WSS_MODE2));
    wss_out(WSS_I_IFACE, (unsigned char)(wss_in(WSS_I_IFACE) & ~WSS_IFACE_PEN));
    outp(wss_base + WSS_INDEX, WSS_INDEX_MCE | WSS_I_FORMAT);
    outp(wss_base + WSS_DATA, format);
    if (!wss_ready(wss_base)) {
        printf("DOS: WSS codec stayed busy after format %02X\n", format);
        return FALSE;
    }
    outp(wss_base + WSS_INDEX, WSS_INDEX_MCE | WSS_I_IFACE);
    outp(wss_base + WSS_DATA, WSS_IFACE_ACAL | WSS_IFACE_SDC);
    outp(wss_base + WSS_INDEX, WSS_I_IFACE);   /* leave mode change */
    if (!wss_ready(wss_base)) {
        printf("DOS: WSS codec stayed busy after leaving mode change (format %02X)\n",
               format);
        return FALSE;
    }
    pause_ms(2);                               /* let ACI rise before polling */
    wait_get_current_time(&start);
    while (wss_in(WSS_I_TEST_INIT) & WSS_TEST_ACI)
        if (wait_calc_elasped_time_ms(&start) > WSS_CALIBRATION_MS) {
            printf("DOS: WSS codec calibration timed out (format %02X, I9 %02X, I11 %02X)\n",
                   format, wss_in(WSS_I_IFACE), wss_in(WSS_I_TEST_INIT));
            return FALSE;
        }
    for (i = WSS_I_AUX1_LEFT; i <= WSS_I_AUX2_RIGHT; ++i)
        wss_out(i, (unsigned char)(WSS_AUX_POWER_ON | WSS_AUX_MUTE));
    wss_out(WSS_I_LEFT_DAC, WSS_DAC_PLAY);
    wss_out(WSS_I_RIGHT_DAC, WSS_DAC_PLAY);
    if (wss_in(WSS_I_FORMAT) != format) {
        printf("DOS: WSS codec format wrote %02X, read %02X (I9 %02X, I11 %02X, I12 %02X)\n",
               format, wss_in(WSS_I_FORMAT), wss_in(WSS_I_IFACE),
               wss_in(WSS_I_TEST_INIT), wss_in(WSS_I_MODE_ID));
        return FALSE;
    }
    return TRUE;
}

/* ---- DMA buffer and output start/stop ---- */

static int allocate_dma_buffer(unsigned long bytes)
{
    union REGS regs;
    unsigned physical, remainder;
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = 0x0100;             /* DPMI allocate DOS memory block */
    /* Up to 64 KiB extra aligns the ring to a 64 KiB boundary, so it never
       crosses an 8-bit (64 KiB) or 16-bit (128 KiB) DMA page. */
    regs.x.ebx = (unsigned)((bytes + 65535UL + 15UL) >> 4);
    int386(0x31, &regs, &regs);
    if (regs.x.cflag) return FALSE;
    sb_ring_bytes = bytes;
    sb_dma_segment = regs.w.ax;
    sb_dma_selector = regs.w.dx;
    physical = (sb_dma_segment << 4) & 0xffffU;
    remainder = (0x10000U - physical) & 0xffffU;
    sb_dma_offset = remainder;
    sb_dma_buffer = (unsigned char __far *)MK_FP(sb_dma_selector, sb_dma_offset);
    return TRUE;
}

static void free_dma_buffer(void)
{
    union REGS regs;
    if (!sb_dma_selector) return;
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = 0x0101;             /* DPMI free DOS memory block */
    regs.w.dx = (unsigned short)sb_dma_selector;
    int386(0x31, &regs, &regs);
    sb_dma_selector = sb_dma_segment = sb_dma_offset = 0;
    sb_dma_buffer = NULL;
}

static void start_dma(void)
{
    unsigned physical = (sb_dma_segment << 4) + sb_dma_offset;
    unsigned channel = dma_channel() & 3;
    unsigned address_port = dma_address_port();
    unsigned count_port = dma_count_port();
    /* The 16-bit controller takes a word address; bit 0 of its page is
       ignored. The buffer is 64 KiB aligned, so it cannot cross a page. */
    unsigned address = dma_16bit_controller() ? physical >> 1 : physical;
    /* DMA and SB DSP lengths count bytes or 16-bit words (both channels);
       the WSS codec counts frames. */
    unsigned last = dma_ring_units() - 1;
    unsigned samples_last = sb_ring_samples - 1;
    outp(dma_mask_port(), 0x04 | channel);   /* mask DMA channel */
    outp(dma_mode_port(), 0x58 | channel);   /* single, auto-init, read */
    outp(dma_flipflop_port(), 0);
    outp(address_port, address & 0xff);
    outp(address_port, (address >> 8) & 0xff);
    outp(dma_page_port(dma_channel()), (physical >> 16) & 0xff);
    outp(dma_flipflop_port(), 0);
    outp(count_port, last & 0xff);
    outp(count_port, last >> 8);
    if (out_mode == OUT_WSS) {
        /* The codec interrupts once per ring pass; its count is in samples. */
        wss_out(WSS_I_LOWER_COUNT, (unsigned char)(samples_last & 0xff));
        wss_out(WSS_I_UPPER_COUNT, (unsigned char)(samples_last >> 8));
        wss_out(WSS_I_PIN, (unsigned char)(wss_in(WSS_I_PIN) | WSS_PIN_IEN));
        outp(wss_base + WSS_STATUS, 0);
        outp(dma_mask_port(), channel);      /* enable DMA before playback */
        wss_out(WSS_I_IFACE, WSS_IFACE_ACAL | WSS_IFACE_SDC | WSS_IFACE_PEN);
    } else if (out_mode == OUT_SB16) {
        unsigned rate = sb_rate();
        if (!sb_write(0xd1) || !sb_write(0x41) ||
            !sb_write((unsigned char)(rate >> 8)) ||
            !sb_write((unsigned char)(rate & 0xff))) return;
        outp(dma_mask_port(), channel);      /* enable DMA before DRQ */
        /* B6h: 16-bit output, auto-init, FIFO; mode 10h signed mono or
           30h signed stereo. */
        if (!sb_write(0xb6) || !sb_write(out_channels == 2 ? 0x30 : 0x10) ||
            !sb_write(last & 0xff) || !sb_write(last >> 8)) return;
    } else {
        if (!sb_write(0x40) || !sb_write((unsigned char)sb_time_constant()) ||
            !sb_write(0x48) || !sb_write(last & 0xff) ||
            !sb_write(last >> 8) || !sb_write(0xd1)) return;
        outp(dma_mask_port(), channel);      /* enable DMA before DRQ */
        if (!sb_write(0x1c)) return;
    }
    sb_active = TRUE;
}

static void stop_output(void)
{
    if (out_mode == OUT_WSS) {
        wss_out(WSS_I_IFACE, WSS_IFACE_ACAL | WSS_IFACE_SDC);   /* playback off */
        wss_out(WSS_I_PIN, (unsigned char)(wss_in(WSS_I_PIN) & ~WSS_PIN_IEN));
        outp(dma_mask_port(), 0x04 | (dma_channel() & 3));
        outp(wss_base + WSS_STATUS, 0);
        return;
    }
    sb_write(out_mode == OUT_SB16 ? 0xd5 : 0xd0);   /* pause 16/8-bit DMA */
    sb_write(0xd3);                          /* turn off the DSP speaker */
    outp(dma_mask_port(), 0x04 | (dma_channel() & 3));
    (void)inp(sb_ack_port);
}

static const char *out_name(void)
{
    if (out_mode == OUT_WSS) return "WSS 16-bit";
    return out_mode == OUT_SB16 ? "Sound Blaster 16-bit" : "Sound Blaster 8-bit";
}

/* The playback cursor in frames. */
static unsigned read_dma_position(void)
{
    unsigned units = dma_ring_units();
    unsigned pos = (units - read_dma_count() - 1U) & (units - 1U);
    return pos * (dma_16bit_controller() ? 2U : 1U) / frame_bytes();
}

/* A card can answer its setup without its DMA transfer running, e.g. a Plug
   and Play card that was not configured for DOS or a D/H value that does
   not match the card. Wait briefly for the 8237 cursor to move before
   trusting the device. */
static int sb_dma_started(void)
{
    union wait_time start;
    unsigned first = read_dma_count(), last = first;
    wait_get_current_time(&start);
    while (wait_calc_elasped_time_ms(&start) < SB_DMA_START_CHECK_MS)
        if ((last = read_dma_count()) != first) return TRUE;
    printf("DOS: %s DMA %u did not start (count %04X -> %04X, IRQs %lu)\n",
           out_name(), dma_channel(), first, last, pcm_irq_count);
    return FALSE;
}

/* Configure the ring and cursors for the current output mode, start it,
   and confirm that DMA is moving. On failure the output is stopped (and an
   SB DSP reset), leaving the IRQ handler and buffer for another attempt. */
static int start_output(void)
{
    unsigned long i;
    sb_ack_port = sb_base + (out_mode == OUT_SB16 ? 0x0f : 0x0e);
    wss_status_port = out_mode == OUT_WSS ? wss_base + WSS_STATUS : 0;
    /* Stereo needs 16-bit output and the 64 KiB ring for the target lead. */
    out_channels = (out_16bit() && !dos_mono && !force_mono &&
                    sb_ring_bytes == SB_DMA_BYTES_MAX) ? 2U : 1U;
    sb_ring_samples = (unsigned)(sb_ring_bytes / frame_bytes());
    /* Unsigned 8-bit silence is 80h; signed 16-bit silence is 0000h. */
    for (i = 0; i < sb_ring_bytes; ++i) sb_dma_buffer[i] = out_16bit() ? 0 : 0x80;
    sb_target_lead = SB_TARGET_LEAD;
    sb_write_pos = sb_target_lead;
    sb_written_abs = sb_target_lead;
    sb_consumed_abs = 0;
    sb_last_dma_pos = 0;
    pcm_irq_count = 0;
    if (out_mode == OUT_WSS && !wss_configure()) return FALSE;
    start_dma();
    if (sb_active && sb_dma_started()) return TRUE;
    sb_active = FALSE;
    stop_output();
    if (out_mode != OUT_WSS) sb_reset();
    return FALSE;
}

/* The core does not call osd_stop_audio_stream() when its own sound setup
   fails after this backend started DMA (for example, a failed YM chip
   allocation). Stop the card and restore the IRQ vector at process exit so
   the card never interrupts into freed program memory. */
static void sb_exit_cleanup(void)
{
    if (sb_active || sb_irq_installed || sb_dma_selector || wss_base)
        osd_stop_audio_stream();
}

/* Try the WSS codec when -doswss is given. The codec is probed first; if it
   does not answer and an SB DSP is present, the AZT2320 switch to WSS mode
   (DSP 09h, 00h, from the Linux ALSA azt2320 driver) is sent and the codec
   probed again. The Windows 98 driver can leave the card in WSS mode, in
   which case no switch is needed. */
static void wss_detect(void)
{
    unsigned requested = 0;
    wss_irq = sb_irq;
    wss_dma = sb_dma;
    parse_card_settings("Q88WSS", &requested, &wss_irq, &wss_dma, NULL);
    if (wss_irq > 15 || wss_dma > 3 || wss_dma == 2) {
        puts("DOS: invalid Q88WSS IRQ or DMA (WSS needs 8-bit DMA 0, 1, or 3)");
        return;
    }
    wss_base = wss_find(requested);
    if (!wss_base && sb_dsp_present) {
        (void)sb_write(0x09);
        (void)sb_write(0x00);
        pause_ms(20);
        wss_base = wss_find(requested);
        if (!wss_base) {
            /* Undo the switch in case the card accepted it. */
            (void)sb_write(0x09);
            (void)sb_write(0x01);
            pause_ms(20);
            (void)sb_reset();
        }
    }
    if (!wss_base) {
        puts("DOS: no WSS codec found; using Sound Blaster output");
        return;
    }
    {
        unsigned char id = wss_in(WSS_I_MODE_ID);
        wss_out(WSS_I_MODE_ID, (unsigned char)(id | WSS_MODE2));
        if (wss_in(WSS_I_MODE_ID) & WSS_MODE2)
            printf("DOS: WSS codec at %03X (I12 %02X, I25 %02X)\n", wss_base, id,
                   wss_in(WSS_I_VERSION));
        else
            printf("DOS: WSS codec at %03X (I12 %02X, AD1848-class)\n", wss_base, id);
        wss_out(WSS_I_MODE_ID, id);
    }
}

int osd_start_audio_stream(int stereo)
{
    unsigned physical;
    (void)stereo;
    out_mode = OUT_SB8;
    out_channels = 1;
    force_mono = FALSE;
    source_phase = 0.0;
    filter_state_1_q4 = 0;
    filter_state_2_q4 = 0;
    pcm_samples_total = 0;
    pcm_samples_non_silent = 0;
    pcm_dma_samples_non_silent = 0;
    pcm_clipped_samples = 0;
    pcm_lr_differ_frames = 0;
    pcm_peak = 0;
    pcm_irq_count = 0;
    pcm_min_dma_lead = 0xffffU;
    pcm_low_dma_lead_frames = 0;
    pcm_dma_underrun_frames = 0;
    pcm_frames = 0;
    pcm_underrun_resyncs = 0;
    pcm_overrun_resyncs = 0;
    sb_rate_scale = sb_rate_scale_min = sb_rate_scale_max = 1.0;
    sb_rate_error = 0.0;
    sb_active = FALSE;
    sb_dma_selector = 0;
    wss_base = 0;
    wss_status_port = 0;
    parse_card_settings("BLASTER", &sb_base, &sb_irq, &sb_dma, &sb_hdma);
    if (sb_dma > 3 || sb_irq > 15 || sb_base < 0x200 || sb_base > 0x3f0)
        goto no_device;
    sb_dsp_present = sb_reset();
    sb_dsp_major = sb_dsp_minor = 0;
    if (sb_dsp_present) {
        if (sb_write(0xe1)) {
            unsigned char major, minor;
            if (sb_read(&major) && sb_read(&minor)) {
                sb_dsp_major = major;
                sb_dsp_minor = minor;
            }
        }
        printf("DOS: Sound Blaster DSP %u.%02u at %03X IRQ %u DMA %u HDMA %u\n",
               sb_dsp_major, sb_dsp_minor, sb_base, sb_irq, sb_dma, sb_hdma);
    }
    if (dos_wss) wss_detect();
    if (!sb_dsp_present && !wss_base) goto no_device;
    if (!allocate_dma_buffer(SB_DMA_BYTES_MAX) && !allocate_dma_buffer(SB_DMA_BYTES_MIN)) {
        wss_leave();
        goto no_device;
    }

    physical = (sb_dma_segment << 4) + sb_dma_offset;
    if ((physical & 0xffffU) + sb_ring_bytes > 0x10000UL) {
        free_dma_buffer();
        wss_leave();
        goto no_device;
    }

    if (wss_base) {
        out_mode = OUT_WSS;
        out_irq = wss_irq;
        if (install_sb_irq() && !start_output() && out_channels == 2) {
            /* Keep 16-bit output if only the stereo setup failed. */
            puts("DOS: WSS stereo did not start; retrying WSS in mono");
            force_mono = TRUE;
            (void)start_output();
        }
        if (!sb_active) {
            puts("DOS: WSS output did not start; returning to Sound Blaster output");
            remove_sb_irq();
            wss_leave();
        }
    }
    if (!sb_active && sb_dsp_present) {
        /* Only DSP 4.xx is an SB16 with 16-bit high DMA. Anything else,
           including an implausible version reply, -dossb8, or a missing or
           invalid H setting, uses the 8-bit path. If 16-bit DMA does not
           start, fall back to 8-bit before giving up. */
        out_mode = (sb_dsp_major == 4 && !dos_sb_8bit &&
                    sb_hdma >= 5 && sb_hdma <= 7) ? OUT_SB16 : OUT_SB8;
        out_irq = sb_irq;
        if (!install_sb_irq()) {
            free_dma_buffer();
            goto no_device;
        }
        if (!start_output() && out_mode == OUT_SB16) {
            out_mode = OUT_SB8;
            if (!sb_reset() || !start_output()) sb_active = FALSE;
        }
        if (!sb_active) {
            printf("DOS: Sound Blaster DMA did not start; check BLASTER I/D/H and"
                   " the card's DOS setup\n");
            remove_sb_irq();
        }
    }
    if (!sb_active) {
        free_dma_buffer();
        goto no_device;
    }
    /* The DOS backend replaces xmame's Unix audio setup, so it must set the
       core sample rate before the YM chips and mixer are started. */
    Machine->sample_rate = sb_rate();
    source_rate = sb_rate();
    source_samples = next_frame_samples();
    if (!sb_atexit_registered) sb_atexit_registered = atexit(sb_exit_cleanup) == 0;
    if (out_mode == OUT_WSS)
        printf("DOS: WSS codec PCM at %03X IRQ %u DMA %u, %u Hz 16-bit %s\n",
               wss_base, out_irq, dma_channel(), sb_rate(),
               out_channels == 2 ? "stereo" : "mono");
    else
        printf("DOS: Sound Blaster DSP %u.%02u PCM at %03X IRQ %u DMA %u, %u Hz %s %s\n",
               sb_dsp_major, sb_dsp_minor, sb_base, out_irq, dma_channel(),
               sb_rate(), out_16bit() ? "16-bit" : "8-bit",
               out_channels == 2 ? "stereo" : "mono");
    printf("DOS: audio ring %lu KiB, %u frames, target lead %u frames\n",
           sb_ring_bytes / 1024UL, sb_ring_samples, sb_target_lead);
    wait_get_current_time(&sb_start_time);
    return (int)source_samples;

no_device:
    Machine->sample_rate = 0;
    source_rate = 0;
    source_samples = 0;
    puts("DOS: Sound Blaster unavailable; continuing silently.");
    return 0;
}

/* Restart the writer a target lead ahead of playback. The samples between
   the playback cursor and the new write position are stale ring data from a
   previous pass, so replace them with silence: a brief gap instead of
   crackling. */
static void sb_resync(unsigned dma_pos)
{
    unsigned i, j, pos = dma_pos, bytes = frame_bytes();
    for (i = 0; i < sb_target_lead; ++i) {
        for (j = 0; j < bytes; ++j)
            sb_dma_buffer[(unsigned long)pos * bytes + j] = out_16bit() ? 0 : 0x80;
        pos = (pos + 1) & (sb_ring_samples - 1U);
    }
    sb_write_pos = pos;
    sb_written_abs = sb_consumed_abs + sb_target_lead;
}

int osd_update_audio_stream(INT16 *buffer)
{
    unsigned i, dma_pos, dma_lead;
    long lead;
    double error;
    if (!sb_active || !source_rate) return (int)source_samples;
    ++pcm_frames;
    /* Advance the unwrapped playback cursor by the distance the 8237 moved
       since the last frame. A frame longer than a whole ring pass aliases;
       the resulting false lead is caught below as an overrun and resynced. */
    dma_pos = read_dma_position();
    sb_consumed_abs += (dma_pos - sb_last_dma_pos) & (sb_ring_samples - 1U);
    sb_last_dma_pos = dma_pos;
    lead = (long)(sb_written_abs - sb_consumed_abs);
    if (lead < (long)source_samples) {
        /* Playback caught or passed the writer. */
        if (lead < 0) {
            ++pcm_underrun_resyncs;
            sb_resync(dma_pos);
        }
        ++pcm_dma_underrun_frames;
    } else if (lead > (long)(sb_ring_samples - source_samples * 2U)) {
        /* The writer would overwrite audio that has not played yet. */
        ++pcm_overrun_resyncs;
        sb_resync(dma_pos);
    }
    lead = (long)(sb_written_abs - sb_consumed_abs);
    dma_lead = lead < 0 ? 0 : (unsigned)lead;
    if (dma_lead < pcm_min_dma_lead) pcm_min_dma_lead = dma_lead;
    if (dma_lead < source_samples * 2U) ++pcm_low_dma_lead_frames;
    /* Positive error: the lead is short, so render slightly more samples. */
    error = ((double)sb_target_lead - (double)dma_lead) / (double)sb_target_lead;
    if (error > 1.0) error = 1.0;
    else if (error < -1.0) error = -1.0;
    sb_rate_error += (error - sb_rate_error) / SB_RATE_ERROR_SMOOTH;
    sb_rate_scale = 1.0 + SB_RATE_ADJUST_MAX * sb_rate_error;
    if (sb_rate_scale < sb_rate_scale_min) sb_rate_scale_min = sb_rate_scale;
    if (sb_rate_scale > sb_rate_scale_max) sb_rate_scale_max = sb_rate_scale;
    for (i = 0; i < source_samples; ++i) {
        int left = (int)buffer[i * 2] * SB_GAIN;
        int right = (int)buffer[i * 2 + 1] * SB_GAIN;
        unsigned long offset = (unsigned long)sb_write_pos * frame_bytes();
        unsigned channel;
        if (out_channels == 1) {
            /* The downmix averages the channels, so a centered sound keeps
               the same level in mono and stereo. */
            left = (((int)buffer[i * 2] + (int)buffer[i * 2 + 1]) / 2) * SB_GAIN;
            if (dos_sb_filter) {
                /* Two fixed-point one-pole stages roll off high-frequency
                   hiss before reducing the signal to 8-bit PCM. */
                long input_q4 = (long)(left < -32768 ? -32768 : left > 32767 ? 32767 : left) * 16;
                filter_state_1_q4 += ((input_q4 - filter_state_1_q4) * 230) / 256;
                filter_state_2_q4 += ((filter_state_1_q4 - filter_state_2_q4) * 230) / 256;
                left = (int)(filter_state_2_q4 / 16);
            }
        }
        ++pcm_samples_total;
        if (buffer[i * 2] != 0 || buffer[i * 2 + 1] != 0) ++pcm_samples_non_silent;
        if (buffer[i * 2] != buffer[i * 2 + 1]) ++pcm_lr_differ_frames;
        for (channel = 0; channel < out_channels; ++channel) {
            int value = channel ? right : left;
            unsigned magnitude;
            if (value > 32767) {
                value = 32767;
                ++pcm_clipped_samples;
            } else if (value < -32768) {
                value = -32768;
                ++pcm_clipped_samples;
            }
            magnitude = (unsigned)(value < 0 ? -value : value);
            if (magnitude > pcm_peak) pcm_peak = magnitude;
            if (dos_pcm_zero) value = 0;
            if (out_16bit()) {
                /* Signed little-endian 16-bit PCM keeps the core's resolution. */
                if (value != 0) ++pcm_dma_samples_non_silent;
                sb_dma_buffer[offset] = (unsigned char)(value & 0xff);
                sb_dma_buffer[offset + 1] = (unsigned char)((value >> 8) & 0xff);
                offset += 2;
            } else {
                /* Round to the nearest 8-bit step. Truncating with a plain
                   shift floors every small negative value to -1 LSB, turning
                   a quiet or decaying tone into a lingering half-wave buzz. */
                int rounded = (value + 128) >> 8;
                unsigned char sample;
                if (rounded > 127) rounded = 127;
                sample = (unsigned char)(rounded + 128);
                if (sample != 128) ++pcm_dma_samples_non_silent;
                sb_dma_buffer[offset] = sample;
                offset += 1;
            }
        }
        sb_write_pos = (sb_write_pos + 1) & (sb_ring_samples - 1);
    }
    sb_written_abs += source_samples;
    source_samples = next_frame_samples();
    return (int)source_samples;
}

void osd_stop_audio_stream(void)
{
    unsigned dma_count = sb_active ? read_dma_count() : 0xffffU;
    if (sb_active) {
        printf("DOS: audio PCM input: %lu samples, %lu non-silent; output peak=%lu; clipped=%lu; L/R differ=%lu; DMA non-center=%lu; DMA count=%04X; IRQs=%lu; minimum DMA lead=%u samples; low-lead frames=%lu; underrun frames=%lu\n",
               pcm_samples_total, pcm_samples_non_silent, pcm_peak,
               pcm_clipped_samples, pcm_lr_differ_frames,
               pcm_dma_samples_non_silent, dma_count, pcm_irq_count,
               pcm_min_dma_lead, pcm_low_dma_lead_frames,
               pcm_dma_underrun_frames);
        {
            /* Measured against the PIT: the card's real playback rate and
               the emulator's speed relative to real time. Together they show
               whether drift came from the card clock or from slow frames. */
            int ms = wait_calc_elasped_time_ms(&sb_start_time);
            double seconds = ms > 0 ? ms / 1000.0 : 0.0;
            printf("DOS: audio timing: %.1f s; card played %.0f Hz; emulation %.1f%% of real time; target lead=%u; resyncs underrun=%lu overrun=%lu; rate scale %.4f..%.4f\n",
                   seconds,
                   seconds > 0.0 ? sb_consumed_abs / seconds : 0.0,
                   seconds > 0.0 ? pcm_frames * 100.0 / (seconds * Machine->refresh_rate) : 0.0,
                   sb_target_lead, pcm_underrun_resyncs, pcm_overrun_resyncs,
                   sb_rate_scale_min, sb_rate_scale_max);
        }
    }
    if (sb_active) {
        stop_output();
        remove_sb_irq();
        sb_active = FALSE;
    }
    remove_sb_irq();
    wss_leave();
    free_dma_buffer();
}

void osd_update_video_and_audio(void) { }
void osd_sound_enable(int enable) { (void)enable; }
