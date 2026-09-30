/* Copyright (c) 2026, Kai Wang. Part of the QUASI88 MS-DOS port.
   SPDX-License-Identifier: BSD-3-Clause (see LICENSE). */
/* DOS entry point: bounded headless tests or an interactive VGA session. */
#include <stdio.h>
#include "quasi88.h"
#include "getconf.h"
#include "file-op.h"
#include "event.h"
#include "pc88main.h"
#include "intr.h"
#include "drive.h"
#include "fdc.h"
#include "image.h"
#include "memory.h"
#include "menu.h"
#include "suspend.h"
#include "snapshot.h"
#include "device.h"

/* Interactive VGA is the default; -dosnovga and -dosframes N are for
   bounded headless test runs. */
int dos_vga = TRUE;
int dos_vesa;
int dos_key_log;
int dos_mouse_log;
extern int use_sound;
static int frame_limit = 0;
extern int keyboard_type;
static int check_fixture;
static int check_video;
static int check_disk;
static int check_state;
static int check_snapshot;
static int check_config;
static int dos_no_sound;
int dos_pcm_zero;
int dos_sb_filter;
int dos_sb_44k;
int dos_sb_8bit;
int dos_wss;
int dos_mono;
static const T_CONFIG_TABLE options[] = {
    {300, "dosframes", X_INT, &frame_limit, 0, 36000, NULL, NULL},
    {301, "doscheck", X_FIX, &check_fixture, TRUE, 0, NULL, NULL},
    {302, "dosvga", X_FIX, &dos_vga, TRUE, 0, NULL, NULL},
    {302, "dosnovga", X_FIX, &dos_vga, FALSE, 0, NULL, NULL},
    {303, "dosvideochk", X_FIX, &check_video, TRUE, 0, NULL, NULL},
    {304, "doskeylog", X_FIX, &dos_key_log, TRUE, 0, NULL, NULL},
    {305, "dosdiskchk", X_FIX, &check_disk, TRUE, 0, NULL, NULL},
    {306, "dosmouselog", X_FIX, &dos_mouse_log, TRUE, 0, NULL, NULL},
    {307, "dosstatechk", X_FIX, &check_state, TRUE, 0, NULL, NULL},
    {308, "dossnapchk", X_FIX, &check_snapshot, TRUE, 0, NULL, NULL},
    {309, "doscfgchk", X_FIX, &check_config, TRUE, 0, NULL, NULL},
    {310, "dosnosound", X_FIX, &dos_no_sound, TRUE, 0, NULL, NULL},
    {311, "dospcmzero", X_FIX, &dos_pcm_zero, TRUE, 0, NULL, NULL},
    {312, "dossbfilter", X_FIX, &dos_sb_filter, TRUE, 0, NULL, NULL},
    {313, "dossb44k", X_FIX, &dos_sb_44k, TRUE, 0, NULL, NULL},
    {314, "dossb8", X_FIX, &dos_sb_8bit, TRUE, 0, NULL, NULL},
    /* Saved to QUASI88.INI: the AZT2320 WSS mode is a per-machine choice. */
    {315, "doswss", X_FIX, &dos_wss, TRUE, 0, NULL, OPT_SAVE},
    {315, "nodoswss", X_FIX, &dos_wss, FALSE, 0, NULL, OPT_SAVE},
    {316, "dosmono", X_FIX, &dos_mono, TRUE, 0, NULL, NULL},
    /* Same meaning as the SDL2 port's -keyboard: 1 = JP106, 2 = US101. */
    {317, "keyboard", X_INT, &keyboard_type, 1, 2, NULL, OPT_SAVE},
    /* Saved: VESA support depends on the video card. */
    {318, "dosvesa", X_FIX, &dos_vesa, TRUE, 0, NULL, OPT_SAVE},
    {318, "nodosvesa", X_FIX, &dos_vesa, FALSE, 0, NULL, OPT_SAVE},
    {0, NULL, X_INV, NULL, 0, 0, NULL, NULL}
};

static void help(FILE *fp)
{
    fputs("  DOS: Sound Blaster-compatible audio is optional; frame pacing enabled.\n"
          "  -dosframes <0..36000>  Frame limit for tests (default 0: run until quit).\n"
          "  -dosvga / -dosnovga   VGA 640x480 display (default) / headless test run.\n"
          "  -dosvesa / -nodosvesa VESA 640x480 256 colors (falls back to VGA mode\n"
          "                        12h); saved in QUASI88.INI.\n"
          "  -keyboard <1|2>       Extra keys for a JP106 (1, default) or US101 (2)\n"
          "                        keyboard; saved in QUASI88.INI.\n"
          "  -dosvideochk          Verify VGA planes before restoring text mode.\n"
          "  -doskeylog            Log BIOS keyboard scan/ASCII codes to KEYS.LOG.\n"
          "  -dosmouselog          Log INT 33h availability and state to MOUSE.LOG.\n"
          "  -dosnosound           Disable emulated sound output.\n"
          "  -dospcmzero           Keep Sound Blaster active, but send digital silence.\n"
          "  -dossbfilter          Apply a gentle high-frequency audio roll-off.\n"
          "  -dossb44k             Use 44,100 Hz (SB16) or 43,478 Hz (8-bit) output.\n"
          "  -dossb8               Force 8-bit DSP output even on an SB16.\n"
          "  -doswss / -nodoswss   Use a WSS codec (AZT2320: switch from SB mode);\n"
          "                        saved in QUASI88.INI.\n"
          "  -dosmono              Play mono even when 16-bit stereo is available.\n"
          "  Ctrl+Q / Ctrl+Alt+Q    Emergency quit to DOS.\n"
          "  -dosdiskchk           Test mounted drive 1 (test image is modified).\n"
          "  -dosstatechk          Save/load emulator state (requires -doscheck).\n"
          "  -dossnapchk           Save/check a BMP screenshot (requires -doscheck).\n"
          "  -doscfgchk            Check loaded DOS config speed (fixture uses 77).\n"
          "  -doscheck             Check synthetic test-ROM RAM markers.\n", fp);
}

static int dos_fdc_wait(int *data_from_fdc)
{
    int i;
    unsigned char status;
    for (i = 0; i < 200000; ++i) {
        status = fdc_status();
        if (status & 0x80) {
            *data_from_fdc = (status & 0x40) != 0;
            return TRUE;
        }
        fdc_ctrl(1000);
    }
    printf("DOS: FDC request timeout, status=%02X\n", fdc_status());
    return FALSE;
}

static int dos_fdc_write_byte(unsigned char value)
{
    int data_from_fdc;
    if (!dos_fdc_wait(&data_from_fdc)) return FALSE;
    if (data_from_fdc) return FALSE;
    fdc_write(value);
    return TRUE;
}

static int dos_fdc_read_byte(unsigned char *value)
{
    int data_from_fdc;
    if (!dos_fdc_wait(&data_from_fdc)) return FALSE;
    if (!data_from_fdc) return FALSE;
    *value = fdc_read();
    return TRUE;
}

/* Exercise the same command/data/result phases that the sub-CPU uses at 0xFB. */
static int dos_fdc_rw_sector(int write, unsigned char data[256],
                             unsigned char result[7])
{
    static const unsigned char read_command[9] =
        {0x46, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x2a, 0xff};
    static const unsigned char write_command[9] =
        {0x45, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x2a, 0xff};
    const unsigned char *command = write ? write_command : read_command;
    int i, data_from_fdc;
    for (i = 0; i < 9; ++i) {
        if (!dos_fdc_write_byte(command[i])) return FALSE;
    }
    if (!dos_fdc_wait(&data_from_fdc)) return FALSE;
    if (!write && data_from_fdc && !(fdc_status() & 0x20)) {
        /* The controller rejected the ID/sector and entered result phase. */
        for (i = 0; i < 7; ++i) {
            if (!dos_fdc_read_byte(&result[i])) return FALSE;
        }
        return TRUE;
    }
    if (write && !data_from_fdc) {
        for (i = 0; i < 256; ++i) {
            if (!dos_fdc_write_byte(data[i])) return FALSE;
        }
    } else if (!write && data_from_fdc) {
        for (i = 0; i < 256; ++i) {
            if (!dos_fdc_read_byte(&data[i])) return FALSE;
        }
    }
    /* A rejected write goes straight to the result phase. */
    for (i = 0; i < 7; ++i) {
        if (!dos_fdc_read_byte(&result[i])) return FALSE;
    }
    return TRUE;
}

static int dos_fdc_sector_result_ok(const unsigned char result[7])
{
    return ((result[0] & 0xc0) == 0 && result[1] == 0 && result[2] == 0) ||
           ((result[0] & 0xc0) == 0x40 && result[1] == 0x80 && result[2] == 0);
}

static int check_main_rom(void)
{
    const char *name = boot_basic == BASIC_N ? "N88N.ROM" : "N88.ROM";
    char path[260];
    OSD_FILE *fp;
    long size;
    if (file_compatrom) {
        puts("DOS: combined ROM format is not supported by this bring-up target.");
        return FALSE;
    }
    if (!osd_path_join(osd_dir_rom(), name, path, sizeof(path))) return FALSE;
    fp = osd_fopen(FTYPE_ROM, path, "rb");
    if (!fp && boot_basic == BASIC_N) {
        if (!osd_path_join(osd_dir_rom(), "N80.ROM", path, sizeof(path))) return FALSE;
        fp = osd_fopen(FTYPE_ROM, path, "rb");
    }
    if (!fp) { printf("DOS: missing required main ROM: %s\n", path); return FALSE; }
    size = osd_fseek(fp, 0, SEEK_END) == 0 ? osd_ftell(fp) : -1;
    osd_fclose(fp);
    if (size != 32768L) {
        printf("DOS: main ROM must be 32768 bytes: %s (%ld)\n", path, size);
        return FALSE;
    }
    printf("DOS: main ROM preflight OK: %s\n", path);
    return TRUE;
}

int main(int argc, char **argv)
{
    int frames = 0, status, fixture_ok = TRUE, video_ok = TRUE, disk_ok = TRUE;
    int state_ok = TRUE, snapshot_ok = TRUE, config_ok = TRUE;
    unsigned long loops = 0;
    puts("QUASI88 DOS (Sound Blaster-compatible audio)");
    if (!config_init(argc, argv, options, help, NULL)) return 1;
    if (dos_no_sound) use_sound = FALSE;
    quasi88_atexit(config_exit);
    if (check_video && !dos_vga) { puts("DOS: -dosvideochk requires -dosvga"); config_exit(); return 1; }
    if (check_state && !check_fixture) { puts("DOS: -dosstatechk requires -doscheck"); config_exit(); return 1; }
    if (check_snapshot && !check_fixture) { puts("DOS: -dossnapchk requires -doscheck"); config_exit(); return 1; }
    if (check_config && !check_fixture) { puts("DOS: -doscfgchk requires -doscheck"); config_exit(); return 1; }
    if (!check_main_rom()) { config_exit(); return 1; }
    quasi88_start();
    if (!frame_limit) {
        puts("DOS: interactive session started; Ctrl+Alt+Q exits.");
        fflush(stdout);
    }
    while ((!frame_limit || frames < frame_limit) &&
           (!frame_limit || loops++ < (unsigned long)frame_limit * 10000UL)) {
        status = quasi88_loop();
        if (status == QUASI88_LOOP_EXIT) break;
        if (status == QUASI88_LOOP_ONE && frames < 2147483647) ++frames;
    }
    if (check_fixture) {
        fixture_ok = main_ram[0x9000] == 0x5a && sub_romram[0x4000] == 0xa5;
    }
    if (check_state) {
        if (!fixture_ok || !statesave()) {
            state_ok = FALSE;
        } else {
            main_ram[0x9000] = 0;
            state_ok = stateload() && main_ram[0x9000] == 0x5a &&
                       sub_romram[0x4000] == 0xa5;
        }
        puts(state_ok ? "DOS: state save/load marker restore: PASS"
                      : "DOS: state save/load marker restore: FAIL");
    }
    if (check_snapshot) {
        char path[QUASI88_MAX_FILENAME + 16];
        unsigned char header[54];
        FILE *fp;
        sprintf(path, "%s0000.BMP", filename_get_snap_base());
        snapshot_ok = fixture_ok && quasi88_screen_snapshot();
        fp = snapshot_ok ? fopen(path, "rb") : NULL;
        if (!fp || fread(header, 1, sizeof(header), fp) != sizeof(header)) {
            snapshot_ok = FALSE;
        } else if (header[0] != 'B' || header[1] != 'M' ||
                   header[18] != 0x80 || header[19] != 0x02 ||
                   header[22] != 0x90 || header[23] != 0x01 ||
                   fseek(fp, 0, SEEK_END) != 0 || ftell(fp) != 768054L) {
            snapshot_ok = FALSE;
        }
        if (fp) fclose(fp);
        puts(snapshot_ok ? "DOS: BMP snapshot output: PASS"
                         : "DOS: BMP snapshot output: FAIL");
        if (snapshot_ok) printf("DOS: snapshot file: %s\n", path);
    }
    if (check_config) {
        config_ok = wait_rate == 77;
        printf("DOS: configuration load speed=%d: %s\n",
               wait_rate, config_ok ? "PASS" : "FAIL");
    }
    if (check_video) video_ok = dos_graph_verify();
    if (check_disk) {
        long before, after, appended;
        unsigned char header[32];
        unsigned char sector[256], result[7];
        int i, sector_ok = TRUE, transfer_ok;
        appended = 32L + 164L * 4L + 84L * 0x1600L;
        if (!disk_image_exist(0) || osd_fseek(drive[0].fp, 0, SEEK_END) != 0 ||
            (before = osd_ftell(drive[0].fp)) < 0) {
            disk_ok = FALSE;
            puts("DOS: D88 mounted-image check: FAIL");
        } else if (drive[0].read_only) {
            if (!dos_fdc_rw_sector(TRUE, sector, result) ||
                (result[0] & 0xc0) != 0x40 || !(result[1] & 0x02) ||
                !dos_fdc_rw_sector(FALSE, sector, result) ||
                !dos_fdc_sector_result_ok(result)) {
                sector_ok = FALSE;
            }
            for (i = 0; sector_ok && i < 256; ++i) {
                if (sector[i] != (unsigned char)i) sector_ok = FALSE;
            }
            puts(sector_ok ? "DOS: FDC read-only sector protection: PASS"
                           : "DOS: FDC read-only sector protection: FAIL");
            if (!sector_ok) disk_ok = FALSE;
        } else if (!(transfer_ok = dos_fdc_rw_sector(FALSE, sector, result)) ||
                   !dos_fdc_sector_result_ok(result)) {
            disk_ok = FALSE;
            if (transfer_ok) {
                printf("DOS: FDC read result %02X %02X %02X\n",
                       result[0], result[1], result[2]);
            }
            puts("DOS: FDC sector read: FAIL");
        } else {
            for (i = 0; i < 256; ++i) {
                if (sector[i] != (unsigned char)i) sector_ok = FALSE;
                sector[i] = (unsigned char)(0xa5 ^ i);
            }
            if (!sector_ok || !dos_fdc_rw_sector(TRUE, sector, result) ||
                !dos_fdc_sector_result_ok(result) ||
                !dos_fdc_rw_sector(FALSE, sector, result) ||
                !dos_fdc_sector_result_ok(result)) {
                disk_ok = FALSE;
                puts("DOS: FDC sector write/read-back: FAIL");
            } else {
                for (i = 0; i < 256; ++i) {
                    if (sector[i] != (unsigned char)(0xa5 ^ i)) sector_ok = FALSE;
                }
                if (!sector_ok) disk_ok = FALSE;
                puts(sector_ok ? "DOS: FDC sector read/write: PASS"
                               : "DOS: FDC sector read/write: FAIL");
            }
            if (d88_append_blank(drive[0].fp, -1) != D88_SUCCESS ||
                   d88_read_header(drive[0].fp, before, header) != D88_SUCCESS ||
                   (((long)header[28] | ((long)header[29] << 8) |
                     ((long)header[30] << 16) | ((long)header[31] << 24)) != appended) ||
                   osd_fseek(drive[0].fp, 0, SEEK_END) != 0 ||
                   (after = osd_ftell(drive[0].fp)) != before + appended) {
                disk_ok = FALSE;
                puts("DOS: D88 append/write check: FAIL");
            } else {
                puts("DOS: D88 append/write check: PASS");
            }
        }
    }
    quasi88_stop();
    config_exit();
    if (check_fixture) printf("DOS: synthetic CPU markers: %s\n", fixture_ok ? "PASS" : "FAIL");
    if (check_video) printf("DOS: VGA plane readback: %s\n", video_ok ? "PASS" : "FAIL");
    if (check_disk) printf("DOS: D88 image check: %s\n", disk_ok ? "PASS" : "FAIL");
    if (check_state) printf("DOS: state file: %s\n", file_state);
    if (dos_vga) dos_graph_palette_report();
    if (dos_vga) printf("DOS: original video mode restored: %s; keys delivered: %lu\n",
                       dos_graph_restored() ? "PASS" : "FAIL", dos_key_count());
    printf("DOS: completed %d/%d frames; clean shutdown\n", frames, frame_limit);
    return (!frame_limit || frames == frame_limit) && fixture_ok && video_ok &&
           disk_ok && state_ok && snapshot_ok && config_ok &&
           dos_graph_restored() ? 0 : 1;
}

int stateload_system(void) { return TRUE; }
int statesave_system(void) { return TRUE; }
int menu_about_osd_msg(int japanese, int *code, const char *message[])
{
    (void)japanese; (void)code; (void)message;
    return FALSE;
}
