/* DOS entry point: bounded headless tests or an interactive VGA session. */
#include <stdio.h>
#include "quasi88.h"
#include "getconf.h"
#include "file-op.h"
#include "pc88main.h"
#include "memory.h"
#include "menu.h"
#include "suspend.h"
#include "device.h"

int dos_vga;
int dos_key_log;
static int frame_limit = 3;
static int check_fixture;
static int check_video;
static const T_CONFIG_TABLE options[] = {
    {300, "dosframes", X_INT, &frame_limit, 0, 36000, NULL, NULL},
    {301, "doscheck", X_FIX, &check_fixture, TRUE, 0, NULL, NULL},
    {302, "dosvga", X_FIX, &dos_vga, TRUE, 0, NULL, NULL},
    {303, "dosvideochk", X_FIX, &check_video, TRUE, 0, NULL, NULL},
    {304, "doskeylog", X_FIX, &dos_key_log, TRUE, 0, NULL, NULL},
    {0, NULL, X_INV, NULL, 0, 0, NULL, NULL}
};

static void help(FILE *fp)
{
    fputs("  DOS: no sound; frame pacing enabled; default is headless.\n"
          "  -dosframes <0..36000>  Frame limit (default 3); 0 runs until quit.\n"
          "  -dosvga               VGA 640x480, 16-color approximated palette.\n"
          "  -dosvideochk          Verify VGA planes before restoring text mode.\n"
          "  -doskeylog            Log BIOS keyboard scan/ASCII codes to KEYS.LOG.\n"
          "  Ctrl+Alt+Q             Emergency quit to DOS.\n"
          "  -doscheck             Check synthetic test-ROM RAM markers.\n", fp);
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
    int frames = 0, status, fixture_ok = TRUE, video_ok = TRUE;
    unsigned long loops = 0;
    puts("QUASI88 DOS (no sound)");
    if (!config_init(argc, argv, options, help, NULL)) return 1;
    quasi88_atexit(config_exit);
    if (check_video && !dos_vga) { puts("DOS: -dosvideochk requires -dosvga"); config_exit(); return 1; }
    if (!check_main_rom()) { config_exit(); return 1; }
    quasi88_start();
    while ((!frame_limit || frames < frame_limit) &&
           (!frame_limit || loops++ < (unsigned long)frame_limit * 10000UL)) {
        status = quasi88_loop();
        if (status == QUASI88_LOOP_EXIT) break;
        if (status == QUASI88_LOOP_ONE && frames < 2147483647) ++frames;
    }
    if (check_fixture) {
        fixture_ok = main_ram[0x9000] == 0x5a && sub_romram[0x4000] == 0xa5;
    }
    if (check_video) video_ok = dos_graph_verify();
    quasi88_stop();
    config_exit();
    if (check_fixture) printf("DOS: synthetic CPU markers: %s\n", fixture_ok ? "PASS" : "FAIL");
    if (check_video) printf("DOS: VGA plane readback: %s\n", video_ok ? "PASS" : "FAIL");
    if (dos_vga) printf("DOS: original video mode restored: %s; keys delivered: %lu\n",
                       dos_graph_restored() ? "PASS" : "FAIL", dos_key_count());
    printf("DOS: completed %d/%d frames; clean shutdown\n", frames, frame_limit);
    return (!frame_limit || frames == frame_limit) && fixture_ok && video_ok && dos_graph_restored() ? 0 : 1;
}

int stateload_system(void) { return TRUE; }
int statesave_system(void) { return TRUE; }
int menu_about_osd_msg(int japanese, int *code, const char *message[])
{
    (void)japanese; (void)code; (void)message;
    return FALSE;
}
