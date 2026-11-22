/* (relink-amd, 0.7.9.6) The game's own boxes (a missing AV1 extension, Windows out of memory, game files of another version...)
   in Brazilian Portuguese when the game speaks it: the same choice as the Insert menu's (gpu/vk/menu_lang.h). RELINK_MENU_LANG
   (pt or en) for one run, else HOS_LANGUAGE, else language.txt's code (host/nn_applet.c reads it the same way). Header only,
   C and C++. hos_box shows UTF-8 text (the accents) with the wide call on Windows. */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hos_paths.h"

static inline int hos_lang_is_pt(const char *s) {
    return s && (s[0] == 'p' || s[0] == 'P') && (s[1] == 't' || s[1] == 'T');
}
/* True when the game's language is Portuguese: decided once */
static inline int hos_lang_pt(void) {
    static int said = -1;
    if (said >= 0) return said;
    const char *e = getenv("RELINK_MENU_LANG");
    if (e && *e) return said = hos_lang_is_pt(e);
    e = getenv("HOS_LANGUAGE");
    if (e && *e) return said = hos_lang_is_pt(e);
    char path[MAX_PATH + 16];
    DWORD n = hos_game_path_a(path, MAX_PATH);
    if (!n || n >= MAX_PATH) return said = 0;
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/') n--;
    snprintf(path + n, sizeof path - n, "language.txt");
    said = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof line, f)) {
            const char *p = line;
            if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;   /* (a BOM) */
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == '\r' || *p == '\n' || !*p) continue;
            said = hos_lang_is_pt(p);
            break;
        }
        fclose(f);
    }
    return said;
}
/* The Portuguese when the game speaks it, else the English */
#define HOS_T(en, pt) (hos_lang_pt() ? (pt) : (en))

/* A message box of UTF-8 text */
static inline int hos_box(const char *text, const char *caption, unsigned type) {
#ifdef _WIN32
    wchar_t wt[4096], wc[256];
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wt, 4096) && MultiByteToWideChar(CP_UTF8, 0, caption, -1, wc, 256))
        return MessageBoxW(NULL, wt, wc, type);
#endif
    return MessageBoxA(NULL, text, caption, type);
}
