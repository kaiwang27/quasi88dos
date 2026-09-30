/* Sound Blaster-compatible DMA output for the DOS target: 16-bit on SB16
   (DSP 4.xx), otherwise 8-bit. */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mame-quasi88.h"
#include "getconf.h"
#include "wait.h"

#define SB_DMA_BYTES 32768U
/* DSP command 40h uses an integer time constant. 211 gives 22,222 Hz;
   233 gives 43,478 Hz for an optional high-rate mono comparison. */
#define SB_RATE_22K 22222U
#define SB_RATE_44K 43478U
/* SB16 DSP command 41h takes the output rate directly. */
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

extern int dos_pcm_zero;
extern int dos_sb_filter;
extern int dos_sb_44k;
extern int dos_sb_8bit;

static unsigned sb_base = 0x220;
static unsigned sb_irq = 5;
static unsigned sb_dma = 1;
static unsigned sb_hdma = 5;
static unsigned sb_ack_port;          /* 0Eh for 8-bit, 0Fh for 16-bit IRQs */
static unsigned sb_dsp_major, sb_dsp_minor;
static int sb_16bit;
/* Ring positions below count samples; 16-bit samples use two bytes. */
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

static unsigned sb_rate(void)
{
    if (sb_16bit) return dos_sb_44k ? SB16_RATE_44K : SB16_RATE_22K;
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
    (void)inp(sb_ack_port);           /* acknowledge the DSP's 8/16-bit IRQ */
    if (sb_irq >= 8) outp(0xa0, 0x20);
    outp(0x20, 0x20);
}

static int install_sb_irq(void)
{
    unsigned long handler = (unsigned long)sb_irq_handler;
    if (!lock_interrupt_memory((void *)handler, 512) ||
        !lock_interrupt_memory(&sb_ack_port, sizeof(sb_ack_port)) ||
        !lock_interrupt_memory(&sb_irq, sizeof(sb_irq)) ||
        !lock_interrupt_memory((void *)&pcm_irq_count, sizeof(pcm_irq_count))) return FALSE;
    sb_vector = sb_irq < 8 ? sb_irq + 8 : sb_irq + 0x68;
    sb_previous_irq = _dos_getvect(sb_vector);
    _dos_setvect(sb_vector, sb_irq_handler);
    /* DOS may leave IRQ7 masked when no parallel port is installed.  Save and
       open the Sound Blaster line (and the slave cascade when needed). */
    sb_pic_master_mask = inp(0x21);
    sb_pic_slave_mask = inp(0xa1);
    sb_pic_masks_saved = TRUE;
    if (sb_irq < 8)
        outp(0x21, sb_pic_master_mask & (unsigned char)~(1U << sb_irq));
    else {
        outp(0xa1, sb_pic_slave_mask & (unsigned char)~(1U << (sb_irq - 8)));
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
            if (sb_irq < 8) {
                irq_bit = (unsigned)(1U << sb_irq);
                mask = inp(0x21);
                outp(0x21, (mask & (unsigned char)~irq_bit) |
                     (sb_pic_master_mask & irq_bit));
            } else {
                irq_bit = (unsigned)(1U << (sb_irq - 8));
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
   controller, 16-bit DMA 5-7 on the second (word-addressed) controller. */
static unsigned dma_channel(void) { return sb_16bit ? sb_hdma : sb_dma; }
static unsigned dma_mask_port(void) { return sb_16bit ? 0xd4 : 0x0a; }
static unsigned dma_mode_port(void) { return sb_16bit ? 0xd6 : 0x0b; }
static unsigned dma_flipflop_port(void) { return sb_16bit ? 0xd8 : 0x0c; }
static unsigned dma_address_port(void)
{
    return sb_16bit ? 0xc0 + (sb_hdma & 3) * 4 : (sb_dma & 3) * 2;
}
static unsigned dma_count_port(void)
{
    return dma_address_port() + (sb_16bit ? 2 : 1);
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

/* Remaining transfers minus one: bytes on 8-bit DMA, words on 16-bit DMA. */
static unsigned read_dma_count(void)
{
    unsigned port = dma_count_port();
    unsigned low, high;
    outp(dma_flipflop_port(), 0);
    low = inp(port);
    high = inp(port);
    return low | (high << 8);
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

static void sb_parse_blaster(void)
{
    const char *p = getenv("BLASTER");
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
        if (key == 'A' || key == 'a') sb_base = value;
        else if (key == 'I' || key == 'i') sb_irq = value;
        else if (key == 'D' || key == 'd') sb_dma = value;
        else if (key == 'H' || key == 'h') sb_hdma = value;
        while (*p == ' ' || *p == '\t') ++p;
    }
}

static int allocate_dma_buffer(void)
{
    union REGS regs;
    unsigned physical, remainder;
    unsigned i;
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = 0x0100;             /* DPMI allocate DOS memory block */
    /* Up to 64 KiB is needed to align the DMA window without crossing a page. */
    regs.x.ebx = (SB_DMA_BYTES + 65535U + 15U) >> 4;
    int386(0x31, &regs, &regs);
    if (regs.x.cflag) return FALSE;
    sb_dma_segment = regs.w.ax;
    sb_dma_selector = regs.w.dx;
    physical = (sb_dma_segment << 4) & 0xffffU;
    remainder = (0x10000U - physical) & 0xffffU;
    sb_dma_offset = remainder;
    sb_dma_buffer = (unsigned char __far *)MK_FP(sb_dma_selector, sb_dma_offset);
    for (i = 0; i < SB_DMA_BYTES; ++i) sb_dma_buffer[i] = 0x80;
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
    unsigned address = sb_16bit ? physical >> 1 : physical;
    unsigned last = sb_ring_samples - 1;
    outp(dma_mask_port(), 0x04 | channel);   /* mask DMA channel */
    outp(dma_mode_port(), 0x58 | channel);   /* single, auto-init, read */
    outp(dma_flipflop_port(), 0);
    outp(address_port, address & 0xff);
    outp(address_port, (address >> 8) & 0xff);
    outp(dma_page_port(dma_channel()), (physical >> 16) & 0xff);
    outp(dma_flipflop_port(), 0);
    outp(count_port, last & 0xff);
    outp(count_port, last >> 8);
    if (sb_16bit) {
        unsigned rate = sb_rate();
        if (!sb_write(0xd1) || !sb_write(0x41) ||
            !sb_write((unsigned char)(rate >> 8)) ||
            !sb_write((unsigned char)(rate & 0xff))) return;
        outp(dma_mask_port(), channel);      /* enable DMA before DRQ */
        /* B6h: 16-bit output, auto-init, FIFO; mode 10h: signed mono. */
        if (!sb_write(0xb6) || !sb_write(0x10) ||
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

static void stop_dsp_output(void)
{
    sb_write(sb_16bit ? 0xd5 : 0xd0);        /* pause 16/8-bit DMA */
    sb_write(0xd3);                          /* turn off the DSP speaker */
    outp(dma_mask_port(), 0x04 | (dma_channel() & 3));
    (void)inp(sb_ack_port);
}

static unsigned read_dma_position(void)
{
    return (sb_ring_samples - read_dma_count() - 1U) & (sb_ring_samples - 1U);
}

/* A card can answer DSP reset and version commands without its DMA transfer
   running, e.g. a Plug and Play card that was not configured for DOS or a
   BLASTER D/H value that does not match the card. Wait briefly for the 8237
   cursor to move before trusting the device. */
static int sb_dma_started(void)
{
    union wait_time start;
    unsigned first = read_dma_count(), last = first;
    wait_get_current_time(&start);
    while (wait_calc_elasped_time_ms(&start) < SB_DMA_START_CHECK_MS)
        if ((last = read_dma_count()) != first) return TRUE;
    printf("DOS: Sound Blaster %s DMA %u did not start (count %04X -> %04X, IRQs %lu)\n",
           sb_16bit ? "16-bit" : "8-bit", dma_channel(), first, last,
           pcm_irq_count);
    return FALSE;
}

/* Configure the ring and cursors for the current sb_16bit mode, start the
   DSP, and confirm that DMA is moving. On failure the DSP is stopped and
   reset, leaving the IRQ handler and buffer for another attempt. */
static int sb_start_output(void)
{
    unsigned i;
    sb_ack_port = sb_base + (sb_16bit ? 0x0f : 0x0e);
    sb_ring_samples = sb_16bit ? SB_DMA_BYTES / 2 : SB_DMA_BYTES;
    /* Unsigned 8-bit silence is 80h; signed 16-bit silence is 0000h. */
    for (i = 0; i < SB_DMA_BYTES; ++i) sb_dma_buffer[i] = sb_16bit ? 0 : 0x80;
    /* Keep a quarter of the ring queued (0.37 s at 22 kHz 8-bit, 0.19 s at
       22 kHz 16-bit). The remaining three quarters leave room both to absorb
       frame-time spikes and to tell a late writer from an early one. */
    sb_target_lead = sb_ring_samples / 4;
    sb_write_pos = sb_target_lead;
    sb_written_abs = sb_target_lead;
    sb_consumed_abs = 0;
    sb_last_dma_pos = 0;
    pcm_irq_count = 0;
    start_dma();
    if (sb_active && sb_dma_started()) return TRUE;
    sb_active = FALSE;
    stop_dsp_output();
    sb_reset();
    return FALSE;
}

/* The core does not call osd_stop_audio_stream() when its own sound setup
   fails after this backend started DMA (for example, a failed YM chip
   allocation). Stop the DSP and restore the IRQ vector at process exit so
   the card never interrupts into freed program memory. */
static void sb_exit_cleanup(void)
{
    if (sb_active || sb_irq_installed || sb_dma_selector) osd_stop_audio_stream();
}

int osd_start_audio_stream(int stereo)
{
    unsigned physical;
    (void)stereo;
    sb_16bit = FALSE;
    source_phase = 0.0;
    filter_state_1_q4 = 0;
    filter_state_2_q4 = 0;
    pcm_samples_total = 0;
    pcm_samples_non_silent = 0;
    pcm_dma_samples_non_silent = 0;
    pcm_clipped_samples = 0;
    pcm_peak = 0;
    pcm_irq_count = 0;
    pcm_min_dma_lead = SB_DMA_BYTES;
    pcm_low_dma_lead_frames = 0;
    pcm_dma_underrun_frames = 0;
    pcm_frames = 0;
    pcm_underrun_resyncs = 0;
    pcm_overrun_resyncs = 0;
    sb_rate_scale = sb_rate_scale_min = sb_rate_scale_max = 1.0;
    sb_rate_error = 0.0;
    sb_active = FALSE;
    sb_dma_selector = 0;
    sb_parse_blaster();
    if (sb_dma > 3 || sb_irq > 15 || sb_base < 0x200 || sb_base > 0x3f0)
        goto no_device;
    if (!sb_reset()) goto no_device;
    sb_dsp_major = sb_dsp_minor = 0;
    if (sb_write(0xe1)) {
        unsigned char major, minor;
        if (sb_read(&major) && sb_read(&minor)) {
            sb_dsp_major = major;
            sb_dsp_minor = minor;
        }
    }
    printf("DOS: Sound Blaster DSP %u.%02u at %03X IRQ %u DMA %u HDMA %u\n",
           sb_dsp_major, sb_dsp_minor, sb_base, sb_irq, sb_dma, sb_hdma);
    if (!allocate_dma_buffer()) goto no_device;

    physical = (sb_dma_segment << 4) + sb_dma_offset;
    if ((physical & 0xffffU) + SB_DMA_BYTES > 0x10000UL) {
        free_dma_buffer();
        goto no_device;
    }

    if (!install_sb_irq()) {
        free_dma_buffer();
        goto no_device;
    }
    /* Only DSP 4.xx is an SB16 with 16-bit high DMA. Anything else,
       including an implausible version reply, -dossb8, or a missing or
       invalid H setting, uses the 8-bit path. If 16-bit DMA does not start,
       fall back to 8-bit before giving up. */
    sb_16bit = sb_dsp_major == 4 && !dos_sb_8bit &&
               sb_hdma >= 5 && sb_hdma <= 7;
    if (!sb_start_output() && sb_16bit) {
        sb_16bit = FALSE;
        if (!sb_reset() || !sb_start_output()) sb_active = FALSE;
    }
    if (!sb_active) {
        printf("DOS: Sound Blaster DMA did not start; check BLASTER I/D/H and"
               " the card's DOS setup\n");
        remove_sb_irq();
        free_dma_buffer();
        goto no_device;
    }
    /* The DOS backend replaces xmame's Unix audio setup, so it must set the
       core sample rate before the YM chips and mixer are started. */
    Machine->sample_rate = sb_rate();
    source_rate = sb_rate();
    source_samples = next_frame_samples();
    if (!sb_atexit_registered) sb_atexit_registered = atexit(sb_exit_cleanup) == 0;
    printf("DOS: Sound Blaster DSP %u.%02u PCM at %03X IRQ %u DMA %u, %u Hz %s mono\n",
           sb_dsp_major, sb_dsp_minor, sb_base, sb_irq, dma_channel(),
           sb_rate(), sb_16bit ? "16-bit" : "8-bit");
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
    unsigned i, pos = dma_pos;
    for (i = 0; i < sb_target_lead; ++i) {
        if (sb_16bit) {
            sb_dma_buffer[pos * 2U] = 0;
            sb_dma_buffer[pos * 2U + 1U] = 0;
        } else {
            sb_dma_buffer[pos] = 0x80;
        }
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
        int mono = (((int)buffer[i * 2] + (int)buffer[i * 2 + 1]) / 2) * SB_GAIN;
        unsigned magnitude;
        unsigned char sample;
        if (mono > 32767) {
            mono = 32767;
            ++pcm_clipped_samples;
        } else if (mono < -32768) {
            mono = -32768;
            ++pcm_clipped_samples;
        }
        if (dos_sb_filter) {
            /* Two fixed-point one-pole stages roll off high-frequency hiss
               before reducing the signal to the Sound Blaster's 8-bit PCM. */
            long input_q4 = (long)mono * 16;
            filter_state_1_q4 += ((input_q4 - filter_state_1_q4) * 230) / 256;
            filter_state_2_q4 += ((filter_state_1_q4 - filter_state_2_q4) * 230) / 256;
            mono = (int)(filter_state_2_q4 / 16);
        }
        magnitude = (unsigned)(mono < 0 ? -mono : mono);
        if (magnitude > pcm_peak) pcm_peak = magnitude;
        ++pcm_samples_total;
        if (mono != 0) ++pcm_samples_non_silent;
        if (dos_pcm_zero) mono = 0;
        if (sb_16bit) {
            /* Signed little-endian 16-bit PCM keeps the core's resolution. */
            unsigned offset = sb_write_pos * 2U;
            if (mono != 0) ++pcm_dma_samples_non_silent;
            sb_dma_buffer[offset] = (unsigned char)(mono & 0xff);
            sb_dma_buffer[offset + 1] = (unsigned char)((mono >> 8) & 0xff);
        } else {
            /* Round to the nearest 8-bit step. Truncating with a plain shift
               floors every small negative value to -1 LSB, turning a quiet
               or decaying tone into a lingering half-wave buzz. */
            int rounded = (mono + 128) >> 8;
            if (rounded > 127) rounded = 127;
            sample = (unsigned char)(rounded + 128);
            if (sample != 128) ++pcm_dma_samples_non_silent;
            sb_dma_buffer[sb_write_pos] = sample;
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
        printf("DOS: audio PCM input: %lu samples, %lu non-silent; output peak=%lu; clipped=%lu; DMA non-center=%lu; DMA count=%04X; IRQs=%lu; minimum DMA lead=%u samples; low-lead frames=%lu; underrun frames=%lu\n",
               pcm_samples_total, pcm_samples_non_silent, pcm_peak,
               pcm_clipped_samples,
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
        stop_dsp_output();
        remove_sb_irq();
        sb_active = FALSE;
    }
    free_dma_buffer();
}

void osd_update_video_and_audio(void) { }
void osd_sound_enable(int enable) { (void)enable; }
