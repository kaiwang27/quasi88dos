/* Copyright (c) 2026, Kai Wang. Part of the QUASI88 MS-DOS port.
   SPDX-License-Identifier: BSD-3-Clause (see LICENSE). */
/* DOS bring-up tests using the unchanged QUASI88 Z80 core and OSD API. */
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "quasi88.h"
#include "z80.h"
#include "file-op.h"

static byte memory[65536];
static z80arch cpu;
static int output_count;
static byte output_port, output_value;
int verbose_z80 = 1;
typedef char dos_type_check[(sizeof(byte) == 1 && sizeof(word) == 2 &&
    sizeof(bit32) == 4 && sizeof(void *) == 4 && offsetof(pair, B.l) == 0) ? 1 : -1];

/* Synthetic program, not a PC-8801 ROM:
 * LD SP,FF00; LD A,7F; ADD A,1; LD (8000),A; PUSH AF; POP BC;
 * LD IX,8100; LD (IX),55; RLC (IX); LD A,(IX); OUT (10),A;
 * IN A,(11); LD (8001),A; HALT.
 */
static const byte program[] = {
    0x31,0x00,0xff, 0x3e,0x7f, 0xc6,0x01, 0x32,0x00,0x80,
    0xf5,0xc1, 0xdd,0x21,0x00,0x81, 0xdd,0x36,0x00,0x55,
    0xdd,0xcb,0x00,0x06, 0xdd,0x7e,0x00, 0xd3,0x10,
    0xdb,0x11, 0x32,0x01,0x80, 0x76
};

static byte read_memory(word addr) { return memory[addr]; }
static void write_memory(word addr, byte value) { memory[addr] = value; }
static byte read_port(byte port) { return port == 0x11 ? 0x42 : 0xff; }
static void write_port(byte port, byte value)
{
    ++output_count;
    output_port = port;
    output_value = value;
}
static void update_interrupt(void) { cpu.icount = 10000; }
static int acknowledge_interrupt(void) { return -1; }

#define CHECK(condition, message) do { \
    if (!(condition)) { printf("FAIL: %s (line %d)\n", message, __LINE__); goto done; } \
} while (0)

int main(void)
{
    OSD_FILE *fp;
    T_DIR_INFO *dir = NULL;
    T_DIR_ENTRY *entry;
    char path[260], normalized[260], dirname[260], filename[260];
    byte data[sizeof(program)];
    int result = 1, created = 0, found = 0;
    int states;

    puts("QUASI88 DOS bring-up: Z80 core + file backend");
    CHECK(osd_file_config_init(), "file backend initialization");
    CHECK(osd_file_stat(".") == FILE_STAT_DIR, "directory stat");
    CHECK(osd_path_join(osd_dir_cwd(), "IOCHK.TMP", path, sizeof(path)), "path join");
    CHECK(osd_path_normalize(".\\iochk.tmp", normalized, sizeof(normalized)), "normalize relative path");
    CHECK(stricmp(path, normalized) == 0, "normalized path identity");
    CHECK(osd_path_split(path, dirname, filename, sizeof(filename)), "path split");
    CHECK(strcmp(filename, "IOCHK.TMP") == 0, "split filename");
    CHECK(!osd_path_join("ROM", "TEST.BIN", normalized, 4), "join overflow rejection");
    CHECK(!osd_path_normalize("TOOLONGNAME.BIN", normalized, sizeof(normalized)), "8.3 rejection");
    CHECK(osd_path_join("C:", "TEST.BIN", normalized, sizeof(normalized)) &&
          strcmp(normalized, "C:TEST.BIN") == 0, "drive-relative path join");
    CHECK(osd_path_split("C:\\", dirname, filename, sizeof(filename)) &&
          strcmp(dirname, "C:\\") == 0 && filename[0] == '\0', "drive root split");
    CHECK(osd_fopen(FTYPE_ROM, "NOFILE.ROM", "rb") == NULL, "missing ROM fails gracefully");

    /* Never overwrite an existing file, even in the test directory. */
    CHECK(osd_file_stat(path) == FILE_STAT_NOEXIST, "scratch file must not already exist");
    fp = osd_fopen(FTYPE_WRITE, path, "wb");
    CHECK(fp != NULL, "create scratch file");
    created = 1;
    CHECK(osd_fwrite(program, 1, sizeof(program), fp) == sizeof(program), "binary write");
    CHECK(osd_fflush(fp) == 0 && osd_ftell(fp) == sizeof(program), "flush and tell");
    CHECK(osd_fclose(fp) == 0, "close output");
    CHECK(osd_file_stat(path) == FILE_STAT_FILE, "file stat");

    fp = osd_fopen(FTYPE_DISK, path, "rb");
    CHECK(fp != NULL, "open disk-style stream");
    CHECK(osd_fopen(FTYPE_DISK, ".\\iochk.tmp", "rb") == fp, "case-insensitive duplicate disk handle");
    CHECK(osd_fopen(FTYPE_DISK, path, "r+b") == NULL, "conflicting disk mode rejected");
    CHECK(osd_fopen(FTYPE_WRITE, path, "wb") == NULL, "conflicting write rejected");
    CHECK(osd_fread(data, 1, sizeof(data), fp) == sizeof(data) &&
          memcmp(data, program, sizeof(data)) == 0, "binary read round trip");
    CHECK(osd_fgetc(fp) == EOF, "end of file");
    CHECK(osd_fseek(fp, 1L, SEEK_SET) == 0 && osd_fgetc(fp) == program[1], "seek");
    osd_rewind(fp);
    CHECK(osd_ftell(fp) == 0 && osd_fgetc(fp) == program[0], "rewind");
    CHECK(osd_fclose(fp) == 0, "close input");

    dir = osd_opendir(".");
    CHECK(dir != NULL, "open directory");
    while ((entry = osd_readdir(dir)) != NULL)
        if (stricmp(entry->name, "IOCHK.TMP") == 0 && entry->type == FILE_STAT_FILE) found = 1;
    osd_closedir(dir);
    dir = NULL;
    CHECK(found, "enumerate scratch file");
    puts("PASS: DOS file access, paths, directory listing, duplicate protection");

    fp = osd_fopen(FTYPE_ROM, path, "rb");
    CHECK(fp != NULL, "open synthetic ROM");
    CHECK(osd_fread(memory, 1, sizeof(program), fp) == sizeof(program), "load synthetic ROM");
    CHECK(osd_fclose(fp) == 0, "close synthetic ROM");
    cpu.fetch = read_memory;
    cpu.mem_read = read_memory;
    cpu.mem_write = write_memory;
    cpu.io_read = read_port;
    cpu.io_write = write_port;
    cpu.intr_update = update_interrupt;
    cpu.intr_ack = acknowledge_interrupt;
    cpu.break_if_halt = TRUE;
    z80_reset(&cpu);
    states = z80_emu(&cpu, 1000);
    CHECK(cpu.HALT && memory[0x8000] == 0x80 && cpu.BC.W == 0x8094,
          "Z80 arithmetic, flags, memory, stack");
    CHECK(memory[0x8100] == 0xaa && cpu.IX.W == 0x8100, "Z80 indexed and CB-prefixed operations");
    CHECK(output_count == 1 && output_port == 0x10 && output_value == 0xaa &&
          memory[0x8001] == 0x42 && cpu.SP.W == 0xff00, "Z80 I/O and stack balance");
    printf("PASS: unchanged Z80 core (%d emulated states)\n", states);
    result = 0;
done:
    osd_closedir(dir);
    osd_file_config_exit();
    if (created && remove(path) != 0) {
        puts("FAIL: scratch file cleanup");
        result = 1;
    }
    puts(result == 0 ? "PASS: DOS bring-up" : "FAIL: DOS bring-up");
    return result;
}
