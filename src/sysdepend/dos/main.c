/* Headless, bounded machine bring-up. No DOS hardware state is modified. */
#include <stdio.h>
#include "quasi88.h"
#include "getconf.h"
#include "file-op.h"
#include "pc88main.h"
#include "memory.h"
#include "menu.h"
#include "suspend.h"

static int frame_limit = 3;
static int check_fixture;
static const T_CONFIG_TABLE options[] = {
    {300, "dosframes", X_INT, &frame_limit, 1, 600, NULL, NULL},
    {301, "doscheck", X_FIX, &check_fixture, TRUE, 0, NULL, NULL},
    {0, NULL, X_INV, NULL, 0, 0, NULL, NULL}
};

static void help(FILE *fp)
{
    fputs("  DOS bring-up: headless, no sound, no PC-88 keyboard, unthrottled.\n"
          "  -dosframes <1..600>  Exit after this many frames (default 3).\n"
          "  -doscheck           Check synthetic test-ROM RAM markers.\n", fp);
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
    int frames = 0, loops = 0, status, fixture_ok = TRUE;
    puts("QUASI88 DOS: headless machine bring-up (no sound)");
    if (!config_init(argc, argv, options, help, NULL)) return 1;
    quasi88_atexit(config_exit);
    if (!check_main_rom()) { config_exit(); return 1; }
    quasi88_start();
    while (frames < frame_limit && loops++ < frame_limit * 10000) {
        status = quasi88_loop();
        if (status == QUASI88_LOOP_EXIT) break;
        if (status == QUASI88_LOOP_ONE) ++frames;
    }
    if (check_fixture) {
        fixture_ok = main_ram[0x9000] == 0x5a && sub_romram[0x4000] == 0xa5;
        printf("DOS: synthetic CPU markers main=%02X sub=%02X: %s\n",
               main_ram[0x9000], sub_romram[0x4000], fixture_ok ? "PASS" : "FAIL");
    }
    quasi88_stop();
    config_exit();
    printf("DOS: completed %d/%d frames; clean shutdown\n", frames, frame_limit);
    return frames == frame_limit && fixture_ok ? 0 : 1;
}

int stateload_system(void) { return TRUE; }
int statesave_system(void) { return TRUE; }
int menu_about_osd_msg(int japanese, int *code, const char *message[])
{
    (void)japanese; (void)code; (void)message;
    return FALSE;
}
