/* Mods, in the layout emulators use, so RomFS mods made for them work as they are:
     <mods>/<name>/romfs/...          files that replace (or add to) the game's RomFS
   <mods> is the "mods" folder beside the executable, or HOS_MODS=<dir>. A mod may sit deeper, as emulators keep them
   (<mods>/contents/<title id>/<name>/romfs): any folder holding a romfs or exefs folder is a mod. Folders whose names
   start with '-' or '_' are left out (off). When two mods have the same file, the one whose path sorts last wins (name
   them 01-..., 02-... to order them).
   Every mod file is indexed once at the start (no disk lookups per open); nn_fs.c asks hos_mods_romfs for each rom:
   path and lists a folder's mod-only entries with the game's own (hos_mods_dir_extras). HOS_FS_OVERLAY (nn_fs.c) is
   looked at after this, so a test overlay wins over mods.
   exefs patches (<mods>/<name>/exefs/<build id>.ips or <file>.pchtxt) are applied when the game is built, not here: the code is translated
   ahead of time (nx2/exefs_mods.py patches a copy of the modules before the translation). At the start the game hashes the
   patch files there now (mods_exefs.c) and compares that with relink_exefs_mods.txt, which the build wrote: when they
   differ, the player is told once that the game must be built again from the launcher for them to take effect.
   From Skybound's loader (NotRexed/SkyboundTotk a64-kit/hos/mods.c, MIT): its RomFS part. */
#include "hos.h"
#include "hos_lang.h"   /* (relink-amd: the boxes in Portuguese) */
#include "hos_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { char *key; char *host; uint64_t size; int is_dir, logged; } Entry;   /* key: romfs path, lower case, '/' */
static Entry *table;
static size_t table_cap, table_n;

static uint64_t hash_str(const char *s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 0x100000001b3ull;
    return h;
}
static void lower_slash(char *s) {
    for (; *s; s++) *s = *s == '\\' ? '/' : (char)tolower((unsigned char)*s);
}
static const char *last_sep(const char *s) {   /* (a host path: '\' on Windows, '/' from host/posix) */
    const char *b = strrchr(s, '\\'), *f = strrchr(s, '/');
    return b > f ? b : f;
}
static Entry *find(const char *key) {
    if (!table_cap) return NULL;
    for (size_t i = hash_str(key) & (table_cap - 1);; i = (i + 1) & (table_cap - 1)) {
        if (!table[i].key) return NULL;
        if (!strcmp(table[i].key, key)) return &table[i];
    }
}
static void put(const char *key, const char *host, uint64_t size, int is_dir) {
    if ((table_n + 1) * 2 > table_cap) {   /* (grow: rehash) */
        Entry *old = table;
        const size_t old_cap = table_cap;
        table_cap = table_cap ? table_cap * 2 : 1024;
        table = (Entry *)calloc(table_cap, sizeof *table);
        table_n = 0;
        for (size_t i = 0; i < old_cap; i++)
            if (old[i].key) {
                size_t j = hash_str(old[i].key) & (table_cap - 1);
                while (table[j].key) j = (j + 1) & (table_cap - 1);
                table[j] = old[i];
                table_n++;
            }
        free(old);
    }
    size_t i = hash_str(key) & (table_cap - 1);
    while (table[i].key && strcmp(table[i].key, key)) i = (i + 1) & (table_cap - 1);
    if (!table[i].key) table[i].key = _strdup(key), table_n++;
    else free(table[i].host);
    table[i].host = _strdup(host);
    table[i].size = size;
    table[i].is_dir = is_dir;
}

/* a mod's romfs folder, recursively: rel is the path inside the RomFS so far ("" at its root) */
static int index_romfs(const char *host_dir, const char *rel) {
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\*", host_dir);
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    int files = 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char host[MAX_PATH], key[MAX_PATH];
        snprintf(host, sizeof host, "%s\\%s", host_dir, fd.cFileName);
        snprintf(key, sizeof key, "%s%s%s", rel, *rel ? "/" : "", fd.cFileName);
        lower_slash(key);
        const int is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        put(key, host, ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow, is_dir);
        if (is_dir) {
            char sub_rel[MAX_PATH];
            snprintf(sub_rel, sizeof sub_rel, "%s%s%s", rel, *rel ? "/" : "", fd.cFileName);
            files += index_romfs(host, sub_rel);
        } else {
            files++;
        }
    } while (FindNextFileA(f, &fd));
    FindClose(f);
    return files;
}

/* the mod folders under root, sorted (a mod: a folder holding romfs or exefs) */
typedef struct { char path[MAX_PATH]; char name[MAX_PATH]; } Mod;
static Mod *mods;
static int nmods, cap_mods;
static void find_mods(const char *dir, const char *name, int depth) {
    char probe[MAX_PATH];
    snprintf(probe, sizeof probe, "%s\\romfs", dir);
    const int has_romfs = GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES;
    snprintf(probe, sizeof probe, "%s\\exefs", dir);
    const int has_exefs = GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES;
    if ((has_romfs || has_exefs) && depth > 0) {
        if (nmods == cap_mods) mods = (Mod *)realloc(mods, sizeof *mods * (size_t)(cap_mods = cap_mods ? cap_mods * 2 : 16));
        snprintf(mods[nmods].path, MAX_PATH, "%s", dir);
        snprintf(mods[nmods].name, MAX_PATH, "%s", name);
        nmods++;
        return;
    }
    if (depth >= 4) return;
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        if (fd.cFileName[0] == '-' || fd.cFileName[0] == '_') {
            hos_log("mods: %s\\%s is off (its name starts with %c)", dir, fd.cFileName, fd.cFileName[0]);
            continue;
        }
        char sub[MAX_PATH], sub_name[MAX_PATH];
        snprintf(sub, sizeof sub, "%s\\%s", dir, fd.cFileName);
        snprintf(sub_name, sizeof sub_name, "%s%s%s", name, *name ? "/" : "", fd.cFileName);
        find_mods(sub, sub_name, depth + 1);
    } while (FindNextFileA(f, &fd));
    FindClose(f);
}
static int mod_cmp(const void *a, const void *b) { return _stricmp(((const Mod *)a)->name, ((const Mod *)b)->name); }

/* ---- exefs patches: built in or not */
int hos_exefs_patch_hash(const char *root, uint64_t *hash, void (*each)(void *arg, const char *rel, uint64_t size), void *arg);

/* the game folder's file name as a UTF-8 path (the mods root's own is mods_root_utf8) */
static int folder_file_utf8(const char *name, char *out, size_t cap) {
#ifdef _WIN32
    wchar_t w[MAX_PATH * 2];
    DWORD n = hos_game_path_w(w, MAX_PATH * 2);
    if (!n || n >= MAX_PATH * 2) return 0;
    wchar_t *slash = w + n;
    while (slash > w && slash[-1] != L'\\' && slash[-1] != L'/') slash--;
    *slash = 0;
    char dir[MAX_PATH * 4];
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, dir, sizeof dir, NULL, NULL)) return 0;
    snprintf(out, cap, "%s%s", dir, name);
#else
    char dir[MAX_PATH];
    DWORD n = hos_game_path_a(dir, sizeof dir);
    if (!n || n >= sizeof dir) return 0;
    char *slash = (char *)last_sep(dir);
    if (!slash) return 0;
    slash[1] = 0;
    snprintf(out, cap, "%s%s", dir, name);
#endif
    return 1;
}

static FILE *open_utf8(const char *path, const char *mode) {
#ifdef _WIN32
    wchar_t w[MAX_PATH * 2], m[8];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, MAX_PATH * 2) || !MultiByteToWideChar(CP_UTF8, 0, mode, -1, m, 8)) return NULL;
    return _wfopen(w, m);
#else
    return fopen(path, mode);
#endif
}

static void mods_root_utf8(char *out, size_t cap) {
#ifdef _WIN32
    const wchar_t *env = _wgetenv(L"HOS_MODS");
    if (env && *env) {
        if (!WideCharToMultiByte(CP_UTF8, 0, env, -1, out, (int)cap, NULL, NULL)) out[0] = 0;
        return;
    }
#else
    if (getenv("HOS_MODS")) { snprintf(out, cap, "%s", getenv("HOS_MODS")); return; }
#endif
    if (!folder_file_utf8("mods", out, cap)) out[0] = 0;
}

typedef struct { char text[1024]; int n; } PatchNames;
static void patch_named(void *arg, const char *rel, uint64_t size) {
    PatchNames *p = (PatchNames *)arg;
    (void)size;
    const size_t used = strlen(p->text);
    if (used + strlen(rel) + 8 < sizeof p->text) snprintf(p->text + used, sizeof p->text - used, "%s%s", p->n ? ", " : "", rel);
    else if (used + 6 < sizeof p->text && !strstr(p->text, "...")) strcat(p->text, ", ...");
    p->n++;
}

/* The patch files in mods now beside the ones the game was built with (relink_exefs_mods.txt's first line: their hash, or
   none). The same: one log line with what the build applied. Not the same (one added, changed or removed since, or a folder
   built before exefs mods were read, with some there now): a log line, and the player is told once for that set of files
   (relink_exefs_mods_told.txt keeps which; HOS_HEADLESS=1 never) that they take effect after a build from the launcher */
static void exefs_check(void) {
    char root[MAX_PATH * 4], report[MAX_PATH * 4], told_path[MAX_PATH * 4];
    mods_root_utf8(root, sizeof root);
    if (!root[0] || !folder_file_utf8("relink_exefs_mods.txt", report, sizeof report) ||
        !folder_file_utf8("relink_exefs_mods_told.txt", told_path, sizeof told_path))
        return;
    uint64_t h = 0;
    PatchNames names = {{0}, 0};
    const int n = hos_exefs_patch_hash(root, &h, patch_named, &names);
    char now[32];
    if (n) snprintf(now, sizeof now, "%016llx", (unsigned long long)h);
    else snprintf(now, sizeof now, "none");
    char built[64] = "", applied[1024] = "", line[1024];
    int have = 0, left_out = 0, warnings = 0;
    FILE *f = open_utf8(report, "rb");
    if (f) {
        have = 1;
        if (fgets(built, sizeof built, f)) built[strcspn(built, "\r\n")] = 0;
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "applied ", 8)) {
                const char *what = line + 8;
                while (*what == ' ') what++;
                const size_t used = strlen(applied);
                if (used + strlen(what) + 8 < sizeof applied) snprintf(applied + used, sizeof applied - used, "%s%s", used ? "; " : "", what);
            } else if (!strncmp(line, "left out ", 9)) {
                left_out++;
            } else if (line[0] && line[0] != '#' && strncmp(line, "note ", 5)) {
                warnings++;
            }
        }
        fclose(f);
    }
    if (!strcmp(now, built) || (!n && !have)) {
        if (n)
            hos_log("mods: exefs: the game was built with these %d patch files (%s): applied: %s; %d left out, %d warnings "
                    "(relink_exefs_mods.txt)", n, now, applied[0] ? applied : "none", left_out, warnings);
        return;
    }
    if (n)
        hos_log("mods: exefs: the patch files in %s (%d, hash %s: %s) are not the ones the game was built with (%s): they take "
                "effect after the game is built again from the launcher", root, n, now, names.text,
                have ? built : "no relink_exefs_mods.txt");
    else
        hos_log("mods: exefs: no patch file in %s now, and the game was built with some (%s): it keeps them until it is built "
                "again from the launcher", root, built);
    char told[64] = "";
    if ((f = open_utf8(told_path, "rb"))) {
        if (fgets(told, sizeof told, f)) told[strcspn(told, "\r\n")] = 0;
        fclose(f);
    }
    if (getenv("HOS_HEADLESS") || !strcmp(told, now)) return;
    if ((f = open_utf8(told_path, "wb"))) {
        fprintf(f, "%s\n", now);
        fclose(f);
    }
#ifdef _WIN32
    char text[2048];
    if (n)
        snprintf(text, sizeof text, HOS_T("The code patches (exefs) in the mods folder have changed since this game was built: %d patch "
                 "file%s there now (%s).\n\nReLink translates the game's code when it builds the game, so a code patch takes effect "
                 "only once the game is built again: open ReLink and choose Build again on its first page (it builds with the "
                 "patches in the mods folder). The game starts now as it was built. RomFS mods work as they are.",
                 "Os patches de código (exefs) na pasta mods mudaram desde que este jogo foi compilado: %d arquivo%s de patch "
                 "lá agora (%s).\n\nO ReLink traduz o código do jogo quando compila o jogo, então um patch de código só vale "
                 "depois que o jogo for compilado de novo: abra o ReLink e escolha Compilar de novo na primeira página (ele compila "
                 "com os patches da pasta mods). O jogo abre agora como foi compilado. Os mods de RomFS funcionam como estão."),
                 n, n == 1 ? "" : "s", names.text);
    else
        snprintf(text, sizeof text, "%s", HOS_T("The code patches (exefs) this game was built with are no longer in the mods folder.\n\n"
                 "ReLink translates the game's code when it builds the game, so the game keeps them until it is built again: "
                 "open ReLink and choose Build again on its first page. The game starts now as it was built.",
                 "Os patches de código (exefs) com que este jogo foi compilado não estão mais na pasta mods.\n\n"
                 "O ReLink traduz o código do jogo quando compila o jogo, então o jogo os mantém até ser compilado de novo: "
                 "abra o ReLink e escolha Compilar de novo na primeira página. O jogo abre agora como foi compilado."));
    wchar_t wtext[2048];   /* (the mods' names are UTF-8) */
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, 2048))
        MessageBoxW(NULL, wtext, L"Project ReLink: mods", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
#endif
}

/* hos_main.c, once the image is loaded (image_path: unused; exefs patches are applied when the game is built) */
void hos_mods_init(const char *image_path) {
    (void)image_path;
    exefs_check();
    char root[MAX_PATH];
    if (getenv("HOS_MODS")) {
        snprintf(root, sizeof root, "%s", getenv("HOS_MODS"));
    } else {
        hos_game_path_a(root, sizeof root);
        char *slash = (char *)last_sep(root);
        if (!slash) return;
        snprintf(slash + 1, sizeof root - (size_t)(slash + 1 - root), "mods");
    }
    if (GetFileAttributesA(root) == INVALID_FILE_ATTRIBUTES) return;
    find_mods(root, "", 0);
    if (!nmods) {
        hos_log("mods: none in %s (a mod: <name>\\romfs, or <name>\\exefs for code patches)", root);
        return;
    }
    qsort(mods, (size_t)nmods, sizeof *mods, mod_cmp);
    for (int i = 0; i < nmods; i++) {
        char dir[MAX_PATH];
        snprintf(dir, sizeof dir, "%s\\romfs", mods[i].path);
        const int files = index_romfs(dir, "");
        hos_log("mods: %s: %d RomFS files", mods[i].name, files);
    }
    hos_log("mods: %d mods from %s: %zu RomFS entries", nmods, root, table_n);
}

/* nn_fs.c: the mod file for a RomFS path ("UI/x.zs", no mount, no leading slash), or 0 */
int hos_mods_romfs(const char *rel, char *out, size_t cap) {
    if (!table_n) return 0;
    char key[MAX_PATH];
    snprintf(key, sizeof key, "%s", rel);
    lower_slash(key);
    Entry *e = find(key);
    if (!e || e->is_dir) return 0;
    snprintf(out, cap, "%s", e->host);
    static volatile LONG logged;
    if (!e->logged && InterlockedIncrement(&logged) <= 200) hos_log("mods: %s <- %s", rel, e->host);
    e->logged = 1;
    return 1;
}

/* nn_fs.c, listing a RomFS folder: fn(arg, name, is_dir, size) for each mod entry directly in it that the game's own
   folder (base_dir, a host path; may not exist) does not have. Returns how many. */
int hos_mods_dir_extras(const char *rel, const char *base_dir, void (*fn)(void *, const char *, int, uint64_t),
                        void *arg) {
    if (!table_n) return 0;
    char prefix[MAX_PATH];
    snprintf(prefix, sizeof prefix, "%s", rel);
    lower_slash(prefix);
    size_t pn = strlen(prefix);
    while (pn && prefix[pn - 1] == '/') prefix[--pn] = 0;
    int n = 0;
    for (size_t i = 0; i < table_cap; i++) {
        const Entry *e = &table[i];
        if (!e->key) continue;
        const char *k = e->key;
        if (pn && (strncmp(k, prefix, pn) || k[pn] != '/')) continue;
        const char *name = pn ? k + pn + 1 : k;
        if (!*name || strchr(name, '/')) continue;   /* (deeper) */
        const char *real = last_sep(e->host);   /* (the name as the mod spells it) */
        real = real ? real + 1 : e->host;
        char base[MAX_PATH];
        snprintf(base, sizeof base, "%s\\%s", base_dir, real);
        if (GetFileAttributesA(base) != INVALID_FILE_ATTRIBUTES) continue;
        fn(arg, real, e->is_dir, e->size);
        n++;
    }
    return n;
}
