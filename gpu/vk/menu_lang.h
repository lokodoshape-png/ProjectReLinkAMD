// (relink-amd, 0.7.9.6) The Insert menu's and the start-up screen's words in Brazilian Portuguese. The menu's text is English in
// the code, as before; each piece of text that reaches GDI (TextOutA, GetTextExtentPoint32A) is looked up whole in
// menu_pt.inc, and drawn in Portuguese when the menu speaks it. Text that is put together (a number in a sentence, a list of
// rows) is translated piece by piece where it is put together (tr). What has no entry stays English.
// Which language: RELINK_MENU_LANG (pt or en) for one run, else the game's own language (HOS_LANGUAGE, else language.txt,
// as host/nn_applet.c reads it): pt-BR gives Portuguese, anything else English.
// The text is UTF-8 (the table's accents), drawn with the wide GDI calls on Windows so the system's code page does not matter;
// a byte that is not UTF-8 (the "\xB7" dots the menu has had) is taken as Latin-1, as before. On POSIX the GDI stand-in
// draws a byte a letter in Latin-1: the text goes to it as Latin-1.
// Include before the code that draws: it redirects TextOutA and GetTextExtentPoint32A (not with MENU_LANG_NO_GDI defined).
#pragma once

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

#include "vk_paths.h"

namespace mkwvk::menu_lang {

struct Pair { const char* en; const char* pt; };
inline constexpr Pair kPortuguese[] = {
#include "menu_pt.inc"
};

inline bool starts_pt(const char* s) {
    return s && (s[0] == 'p' || s[0] == 'P') && (s[1] == 't' || s[1] == 'T');
}
/// The game's language code, as language.txt has it (its first line that is not a note), or HOS_LANGUAGE
inline std::string game_language() {
    if (const char* h = std::getenv("HOS_LANGUAGE"); h && *h) return h;
    char exe[MAX_PATH];
    const DWORD n = game_module_path(exe, MAX_PATH);
    std::string path(exe, n);
    path = path.substr(0, path.find_last_of("\\/") + 1) + "language.txt";
    std::string code;
    if (FILE* f = std::fopen(path.c_str(), "r")) {
        char line[128];
        while (std::fgets(line, sizeof line, f)) {
            const char* p = line;
            if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;   // (a BOM)
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == '\r' || *p == '\n' || !*p) continue;
            code.assign(p, std::strcspn(p, " \t\r\n"));
            break;
        }
        std::fclose(f);
    }
    return code;
}
/// True when the menu speaks Portuguese: decided once, at the first text
inline bool portuguese() {
    static const bool on = [] {
        if (const char* e = std::getenv("RELINK_MENU_LANG"); e && *e) return starts_pt(e);
        return starts_pt(game_language().c_str());
    }();
    return on;
}
inline const std::unordered_map<std::string_view, const char*>& table() {
    static const auto t = [] {
        std::unordered_map<std::string_view, const char*> m;
        m.reserve(sizeof kPortuguese / sizeof kPortuguese[0]);
        for (const Pair& p : kPortuguese) m.emplace(p.en, p.pt);
        return m;
    }();
    return t;
}
/// The Portuguese for this English, when the menu speaks Portuguese and the table has it; else nullptr
inline const char* find(std::string_view en) {
    if (!portuguese() || en.empty()) return nullptr;
    const auto& t = table();
    const auto it = t.find(en);
    return it == t.end() ? nullptr : it->second;
}
/// The menu's words for this English: the Portuguese when there is one, else the English itself
inline const char* tr(const char* en) {
    if (!en) return en;
    const char* pt = find(en);
    return pt ? pt : en;
}
inline std::string tr(const std::string& en) {
    const char* pt = find(en);
    return pt ? std::string(pt) : en;
}

/// The last letter off text (all its bytes, when it is UTF-8): text cut short never ends in half a letter
inline void pop_letter(std::string& t) {
    while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();
    if (!t.empty()) t.pop_back();
}

/// (wrap_help) set while a translated text is drawn in pieces: a piece is never looked up again
inline thread_local bool g_pieces = false;
struct Pieces {
    bool was = g_pieces;
    Pieces() { g_pieces = true; }
    ~Pieces() { g_pieces = was; }
};

/// The code points of UTF-8 text; a byte that does not start a whole UTF-8 letter is taken as Latin-1
template <class F>
inline void each_letter(const char* s, int n, F&& out) {
    const unsigned char* p = (const unsigned char*)s;
    for (int i = 0; i < n;) {
        const unsigned c = p[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool ok = len >= 1 && i + len <= n;
        unsigned cp = len == 1 ? c : len == 2 ? c & 31 : len == 3 ? c & 15 : c & 7;
        for (int k = 1; ok && k < len; k++) {
            if ((p[i + k] & 0xC0) != 0x80) ok = false;
            else cp = cp << 6 | (p[i + k] & 63);
        }
        if (ok && len > 1 && (cp < 0x80 || cp > 0x10FFFF)) ok = false;   // (an overlong form: not UTF-8)
        if (!ok) cp = c, len = 1;
        out(cp);
        i += len;
    }
}
#ifdef _WIN32
inline std::wstring wide(const char* s, int n) {
    std::wstring w;
    w.reserve((size_t)n);
    each_letter(s, n, [&](unsigned cp) {
        if (cp >= 0x10000) cp -= 0x10000, w += (wchar_t)(0xD800 + (cp >> 10)), w += (wchar_t)(0xDC00 + (cp & 0x3FF));
        else w += (wchar_t)cp;
    });
    return w;
}
#else
inline std::string latin1(const char* s, int n) {
    std::string o;
    o.reserve((size_t)n);
    each_letter(s, n, [&](unsigned cp) { o += cp < 256 ? (char)cp : '?'; });
    return o;
}
#endif
/// The text a GDI call draws: the Portuguese for the whole of it when there is one
inline void words(const char*& s, int& n) {
    if (g_pieces || !s || n <= 0) return;
    if (const char* pt = find(std::string_view(s, (size_t)n))) s = pt, n = (int)std::strlen(pt);
}
inline BOOL text_out(HDC dc, int x, int y, const char* s, int n) {
    words(s, n);
#ifdef _WIN32
    const std::wstring w = wide(s, n);
    return TextOutW(dc, x, y, w.c_str(), (int)w.size());
#else
    const std::string l = latin1(s, n);
    return TextOutA(dc, x, y, l.c_str(), (int)l.size());
#endif
}
inline BOOL text_extent(HDC dc, const char* s, int n, SIZE* sz) {
    words(s, n);
#ifdef _WIN32
    const std::wstring w = wide(s, n);
    return GetTextExtentPoint32W(dc, w.c_str(), (int)w.size(), sz);
#else
    const std::string l = latin1(s, n);
    return GetTextExtentPoint32A(dc, l.c_str(), (int)l.size(), sz);
#endif
}

/// A message box of UTF-8 text (vk_main.cpp's: a driver that crashed, a card without mesh shaders, a lost device)
inline int message_box(const std::string& text, const char* caption, UINT type) {
#ifdef _WIN32
    const std::wstring t = wide(text.c_str(), (int)text.size()), c = wide(caption, (int)std::strlen(caption));
    return MessageBoxW(nullptr, t.c_str(), c.c_str(), type);
#else
    return MessageBoxA(nullptr, text.c_str(), caption, type);
#endif
}

} // namespace mkwvk::menu_lang

#ifndef MENU_LANG_NO_GDI   // (vk_main.cpp: only the words and message_box)
#define TextOutA mkwvk::menu_lang::text_out
#define GetTextExtentPoint32A mkwvk::menu_lang::text_extent
#endif
