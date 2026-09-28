/* DOS file backend for QUASI88. Distributed under the repository license.
 * Uses the Open Watcom DOS runtime; no LFN or host OS services required.
 * Initial filename support is ASCII 8.3 only, with DOS drive semantics.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <direct.h>
#include <dirent.h>
#include <sys/stat.h>

#include "quasi88.h"
#include "file-op.h"
#include "q8tk.h"

#define DOS_PATH_MAX 260
#define MAX_STREAM 16

struct OSD_FILE_STRUCT {
    FILE *fp;
    int type;
    char path[DOS_PATH_MAX];
    char mode[4];
};

struct T_DIR_INFO_STRUCT {
    DIR *dir;
    T_DIR_ENTRY entry;
    char name[13];
};

static OSD_FILE streams[MAX_STREAM];
static char directories[8][DOS_PATH_MAX];

static int separator(char c)
{
    return c == '\\' || c == '/';
}

/* Reject names DOS might silently truncate or interpret as a wildcard. */
static int valid_path(const char *path)
{
    const char *p, *start, *dot;
    size_t len;
    if (!path || strlen(path) >= DOS_PATH_MAX) return FALSE;
    p = path;
    if (isalpha((unsigned char)p[0]) && p[1] == ':') p += 2;
    while (*p) {
        if (separator(*p)) { ++p; continue; }
        start = p;
        dot = NULL;
        while (*p && !separator(*p)) {
            unsigned char c = (unsigned char)*p;
            if (c <= 32 || c >= 127 || strchr("\"*+,/:;<=>?[\\]|", c))
                return FALSE;
            if (*p == '.') {
                if (dot && !(p == start + 1 && start[0] == '.')) return FALSE;
                dot = p;
            }
            ++p;
        }
        len = (size_t)(p - start);
        if ((len == 1 && start[0] == '.') ||
            (len == 2 && start[0] == '.' && start[1] == '.')) continue;
        if (start[0] == '.') return FALSE;
        if (dot) {
            if (dot - start > 8 || p - dot - 1 == 0 || p - dot - 1 > 3)
                return FALSE;
        } else if (len > 8) return FALSE;
    }
    return TRUE;
}

int osd_path_normalize(const char *path, char resolved[], int size)
{
    char full[DOS_PATH_MAX];
    size_t len;
    char *p;
    if (size <= 0 || !valid_path(path) ||
        !_fullpath(full, path, sizeof(full))) return FALSE;
    for (p = full; *p; ++p) if (*p == '/') *p = '\\';
    len = strlen(full);
    while (len > 3 && separator(full[len - 1])) full[--len] = '\0';
    if (len >= (size_t)size) return FALSE;
    memcpy(resolved, full, len + 1);
    return TRUE;
}

int osd_path_join(const char *dir, const char *file, char path[], int size)
{
    char joined[DOS_PATH_MAX];
    size_t d, f;
    int add;
    if (size <= 0 || !valid_path(dir) || !valid_path(file)) return FALSE;
    d = strlen(dir);
    f = strlen(file);
    if (separator(file[0]) || (f >= 2 && file[1] == ':')) d = 0;
    add = d && f && !separator(dir[d - 1]) && dir[d - 1] != ':';
    if (d + f + add >= sizeof(joined) || d + f + add >= (size_t)size)
        return FALSE;
    memcpy(joined, dir, d);
    if (add) joined[d++] = '\\';
    memcpy(joined + d, file, f + 1);
    memcpy(path, joined, d + f + 1);
    return TRUE;
}

int osd_path_split(const char *path, char dir[], char file[], int size)
{
    char tmp[DOS_PATH_MAX];
    size_t len, split, i, dlen;
    if (size <= 0 || !valid_path(path)) return FALSE;
    strcpy(tmp, path);
    len = strlen(tmp);
    while (len > 1 && separator(tmp[len - 1]) &&
           !(len == 3 && tmp[1] == ':')) tmp[--len] = '\0';
    split = len >= 2 && tmp[1] == ':' ? 2 : 0;
    for (i = 0; i < len; ++i) if (separator(tmp[i])) split = i + 1;
    dlen = split;
    if (dlen > 1 && separator(tmp[dlen - 1]) &&
        !(dlen == 3 && tmp[1] == ':')) --dlen;
    if (dlen >= (size_t)size || len - split >= (size_t)size) return FALSE;
    memcpy(dir, tmp, dlen);
    dir[dlen] = '\0';
    strcpy(file, tmp + split);
    return TRUE;
}

#define DIRECTORY_ACCESSORS(name, index) \
const char *osd_dir_##name(void) { return directories[index]; } \
int osd_set_dir_##name(const char *path) { \
    return osd_path_normalize(path, directories[index], DOS_PATH_MAX); \
}
DIRECTORY_ACCESSORS(cwd, 0)
DIRECTORY_ACCESSORS(rom, 1)
DIRECTORY_ACCESSORS(disk, 2)
DIRECTORY_ACCESSORS(tape, 3)
DIRECTORY_ACCESSORS(snap, 4)
DIRECTORY_ACCESSORS(state, 5)
DIRECTORY_ACCESSORS(gcfg, 6)
DIRECTORY_ACCESSORS(lcfg, 7)

int osd_file_config_init(void)
{
    int i;
    if (!osd_set_dir_cwd(".")) return FALSE;
    for (i = 1; i < 8; ++i) strcpy(directories[i], directories[0]);
    return osd_set_dir_rom("ROM");
}

void osd_file_config_exit(void)
{
    int i;
    for (i = 0; i < MAX_STREAM; ++i)
        if (streams[i].fp) osd_fclose(&streams[i]);
}

int osd_kanji_code(void) { return Q8TK_KANJI_ANK; }

OSD_FILE *osd_fopen(int type, const char *path, const char *mode)
{
    char full[DOS_PATH_MAX];
    OSD_FILE *slot = NULL;
    int i;
    if (!mode || strlen(mode) >= sizeof(streams[0].mode) ||
        !osd_path_normalize(path, full, sizeof(full))) return NULL;
    for (i = 0; i < MAX_STREAM; ++i) {
        if (!streams[i].fp) {
            if (!slot) slot = &streams[i];
        } else if (stricmp(streams[i].path, full) == 0) {
            /* The core expects one shared handle for duplicate disk opens.
             * Closing that handle invalidates both references, as upstream.
             * Reject every other duplicate to avoid conflicting writes.
             */
            if (type == FTYPE_DISK && streams[i].type == FTYPE_DISK &&
                strcmp(mode, streams[i].mode) == 0) return &streams[i];
            return NULL;
        }
    }
    if (!slot) return NULL;
    slot->fp = fopen(full, mode);
    if (!slot->fp) return NULL;
    slot->type = type;
    strcpy(slot->path, full);
    strcpy(slot->mode, mode);
    return slot;
}

int osd_fclose(OSD_FILE *s)
{
    int result;
    if (!s || !s->fp) return EOF;
    result = fclose(s->fp);
    s->fp = NULL;
    return result;
}
int osd_fflush(OSD_FILE *s) { return s ? (s->fp ? fflush(s->fp) : EOF) : fflush(NULL); }
int osd_fseek(OSD_FILE *s, long off, int whence) { return s && s->fp ? fseek(s->fp, off, whence) : -1; }
long osd_ftell(OSD_FILE *s) { return s && s->fp ? ftell(s->fp) : -1L; }
void osd_rewind(OSD_FILE *s) { if (s && s->fp) rewind(s->fp); }
size_t osd_fread(void *p, size_t size, size_t n, OSD_FILE *s) { return s && s->fp ? fread(p, size, n, s->fp) : 0; }
size_t osd_fwrite(const void *p, size_t size, size_t n, OSD_FILE *s) { return s && s->fp ? fwrite(p, size, n, s->fp) : 0; }
int osd_fputc(int c, OSD_FILE *s) { return s && s->fp ? fputc(c, s->fp) : EOF; }
int osd_fgetc(OSD_FILE *s) { return s && s->fp ? fgetc(s->fp) : EOF; }
char *osd_fgets(char *p, int n, OSD_FILE *s) { return s && s->fp ? fgets(p, n, s->fp) : NULL; }
int osd_fputs(const char *p, OSD_FILE *s) { return s && s->fp ? fputs(p, s->fp) : EOF; }

int osd_file_stat(const char *path)
{
    struct stat st;
    if (!valid_path(path) || stat(path, &st)) return FILE_STAT_NOEXIST;
    return (st.st_mode & S_IFDIR) ? FILE_STAT_DIR : FILE_STAT_FILE;
}

T_DIR_INFO *osd_opendir(const char *path)
{
    T_DIR_INFO *info;
    if (!valid_path(path)) return NULL;
    info = (T_DIR_INFO *)calloc(1, sizeof(*info));
    if (!info) return NULL;
    info->dir = opendir(path);
    if (!info->dir) { free(info); return NULL; }
    return info;
}

T_DIR_ENTRY *osd_readdir(T_DIR_INFO *info)
{
    struct dirent *entry;
    if (!info) return NULL;
    while ((entry = readdir(info->dir)) != NULL) {
        if ((entry->d_attr & _A_VOLID) || !valid_path(entry->d_name)) continue;
        if (strlen(entry->d_name) >= sizeof(info->name)) continue;
        strcpy(info->name, entry->d_name);
        info->entry.type = (entry->d_attr & _A_SUBDIR) ? FILE_STAT_DIR : FILE_STAT_FILE;
        info->entry.name = info->name;
        info->entry.str = info->name;
        return &info->entry;
    }
    return NULL;
}

void osd_closedir(T_DIR_INFO *info)
{
    if (info) { closedir(info->dir); free(info); }
}
