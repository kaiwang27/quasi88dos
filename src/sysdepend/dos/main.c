/* DOS entry point: bounded headless tests or an interactive VGA session. */
#include <stdio.h>
#include "quasi88.h"
#include "getconf.h"
#include "file-op.h"
#include "pc88main.h"
#include "drive.h"
#include "fdc.h"
#include "image.h"
#include "memory.h"
#include "menu.h"
#include "suspend.h"
#include "device.h"

int dos_vga;
int dos_key_log;
int dos_mouse_log;
static int frame_limit = 3;
static int check_fixture;
static int check_video;
static int check_disk;
static int check_state;
static const T_CONFIG_TABLE options[] = {
    {300, "dosframes", X_INT, &frame_limit, 0, 36000, NULL, NULL},
    {301, "doscheck", X_FIX, &check_fixture, TRUE, 0, NULL, NULL},
    {302, "dosvga", X_FIX, &dos_vga, TRUE, 0, NULL, NULL},
    {303, "dosvideochk", X_FIX, &check_video, TRUE, 0, NULL, NULL},
    {304, "doskeylog", X_FIX, &dos_key_log, TRUE, 0, NULL, NULL},
    {305, "dosdiskchk", X_FIX, &check_disk, TRUE, 0, NULL, NULL},
    {306, "dosmouselog", X_FIX, &dos_mouse_log, TRUE, 0, NULL, NULL},
    {307, "dosstatechk", X_FIX, &check_state, TRUE, 0, NULL, NULL},
    {0, NULL, X_INV, NULL, 0, 0, NULL, NULL}
};

static void help(FILE *fp)
{
    fputs("  DOS: no sound; frame pacing enabled; default is headless.\n"
          "  -dosframes <0..36000>  Frame limit (default 3); 0 runs until quit.\n"
          "  -dosvga               VGA 640x480, 16-color approximated palette.\n"
          "  -dosvideochk          Verify VGA planes before restoring text mode.\n"
          "  -doskeylog            Log BIOS keyboard scan/ASCII codes to KEYS.LOG.\n"
          "  -dosmouselog          Log INT 33h availability and state to MOUSE.LOG.\n"
          "  Ctrl+Alt+Q             Emergency quit to DOS.\n"
          "  -dosdiskchk           Test mounted drive 1 (test image is modified).\n"
          "  -dosstatechk          Save/load emulator state (requires -doscheck).\n"
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
    int state_ok = TRUE;
    unsigned long loops = 0;
    puts("QUASI88 DOS (no sound)");
    if (!config_init(argc, argv, options, help, NULL)) return 1;
    quasi88_atexit(config_exit);
    if (check_video && !dos_vga) { puts("DOS: -dosvideochk requires -dosvga"); config_exit(); return 1; }
    if (check_state && !check_fixture) { puts("DOS: -dosstatechk requires -doscheck"); config_exit(); return 1; }
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
    if (dos_vga) printf("DOS: original video mode restored: %s; keys delivered: %lu\n",
                       dos_graph_restored() ? "PASS" : "FAIL", dos_key_count());
    printf("DOS: completed %d/%d frames; clean shutdown\n", frames, frame_limit);
    return (!frame_limit || frames == frame_limit) && fixture_ok && video_ok &&
           disk_ok && state_ok && dos_graph_restored() ? 0 : 1;
}

int stateload_system(void) { return TRUE; }
int statesave_system(void) { return TRUE; }
int menu_about_osd_msg(int japanese, int *code, const char *message[])
{
    (void)japanese; (void)code; (void)message;
    return FALSE;
}
