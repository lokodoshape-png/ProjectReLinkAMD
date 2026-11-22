#pragma once
// (relink-amd, 0.7.9.4) Lossless Scaling compatibility mode: what the renderer changes while Lossless Scaling (LS, an external
// program that captures the game's window and scales it and/or makes frames between its own, LSFG) runs over the game.
// Nothing here hooks or talks to LS: the renderer only presents in a way a window capture takes well. docs/lossless_scaling.md
// explains each profile in Portuguese.
//
// Profiles (RX_VK_LOSSLESS_SCALING=<0..5 or a name> over the Insert menu's "Lossless Scaling" row, lossless_scaling in
// relink_options.ini):
//   0 off / normal      the renderer as without this file: nothing changed, no extension enabled
//   1 scaling           LS scales the window: the picture goes to the window 1:1 (no Window scaling filter, no Sharpness)
//   2 lsfg              LS makes the frames: no internal frame generation, no repeated presents of the "30, smooth" cap
//   3 lsfg_scaling      both
//   4 performance       3, plus immediate presents and 3 swapchain images (the least delay; LS keeps its own sync)
//   5 compat            3, plus FIFO presents, 3 swapchain images and fast sync off (the old wait): the most predictable
// Every LS profile asks the driver not to take the window as exclusive fullscreen (VK_EXT_full_screen_exclusive, DISALLOWED):
// a borderless window at the monitor's size may otherwise be presented exclusively, which DXGI Desktop Duplication and LS's
// overlay cannot work with.
// Each part can be set alone over the profile: RX_VK_LS_INTERNAL_FG=0/1 (1: the internal frame generation allowed),
// RX_VK_LS_WINDOW_FILTER=0/1, RX_VK_LS_REPEATS=0/1, RX_VK_LS_FSE=default|disallow|allow, RX_VK_LS_PRESENT=auto|fifo|immediate,
// RX_VK_LS_IMAGES=0|2|3|4 (0: as without the mode), RX_VK_LS_FAST_SYNC=0/1.
// The options the player saved are never rewritten: the profile only changes what is in effect, so Off is the renderer as before.
//
// This header is pure logic (no Vulkan, no globals) so gpu/vk/tests/ls_compat_test.cpp can check it without a GPU.
#include <cstdlib>
#include <cstring>

namespace mkwvk {

enum LsProfile { LS_OFF = 0, LS_SCALING = 1, LS_LSFG = 2, LS_LSFG_SCALING = 3, LS_PERFORMANCE = 4, LS_COMPAT = 5, LS_COUNT = 6 };
enum LsFse { LS_FSE_DEFAULT = 0, LS_FSE_DISALLOW = 1, LS_FSE_ALLOW = 2 };
enum LsPresent { LS_PRESENT_AUTO = 0, LS_PRESENT_FIFO = 1, LS_PRESENT_IMMEDIATE = 2 };

struct LsEffective {
    int profile = LS_OFF;
    bool internal_fg = true;     // the internal frame generation may run (the menu's Frame generation row still decides)
    bool window_filter = true;   // the Window scaling filter and Sharpness may run
    bool repeats = true;         // the "30, smooth" cap may present a frame more than once
    int fse = LS_FSE_DEFAULT;    // exclusive fullscreen: the driver's choice (no extension enabled), disallowed, allowed
    int present = LS_PRESENT_AUTO;
    int images = 0;              // swapchain images asked for; 0: as without the mode
    bool fast_sync = true;       // RX_VK_FAST_SYNC's default may stand (an explicit RX_VK_FAST_SYNC still wins)
};

inline const char* ls_profile_name(int p) {
    static const char* const names[LS_COUNT] = {"Off", "LS Scaling", "LSFG", "LSFG + Scaling", "LS Performance",
                                                "LS Max Compatibility"};
    return p >= 0 && p < LS_COUNT ? names[p] : "Off";
}

/// A profile from text: a number 0..5 or a name (off, normal, scaling, lsfg, lsfg_scaling, performance, compat). -1: not one
inline int ls_parse_profile(const char* s) {
    if (!s || !*s) return -1;
    if (s[0] >= '0' && s[0] <= '9' && !s[1]) return s[0] - '0' < LS_COUNT ? s[0] - '0' : -1;
    static const struct { const char* name; int p; } names[] = {
        {"off", LS_OFF}, {"normal", LS_OFF}, {"scaling", LS_SCALING}, {"ls_scaling", LS_SCALING}, {"lsfg", LS_LSFG},
        {"lsfg_scaling", LS_LSFG_SCALING}, {"lsfg+scaling", LS_LSFG_SCALING}, {"performance", LS_PERFORMANCE},
        {"ls_performance", LS_PERFORMANCE}, {"compat", LS_COMPAT}, {"max_compat", LS_COMPAT}, {"compatibility", LS_COMPAT}};
    for (const auto& n : names) {
        size_t i = 0;
        while (n.name[i] && s[i] && (s[i] >= 'A' && s[i] <= 'Z' ? s[i] | 0x20 : s[i]) == n.name[i]) i++;
        if (!n.name[i] && !s[i]) return n.p;
    }
    return -1;
}

/// The profile's parts, before any part is set alone
inline LsEffective ls_profile_parts(int p) {
    LsEffective e;
    if (p <= LS_OFF || p >= LS_COUNT) return e;
    e.profile = p;
    e.fse = LS_FSE_DISALLOW;
    const bool scaling = p == LS_SCALING || p >= LS_LSFG_SCALING;
    const bool lsfg = p >= LS_LSFG;
    e.window_filter = !scaling;
    e.internal_fg = !lsfg;
    e.repeats = !lsfg;
    if (p == LS_PERFORMANCE) e.present = LS_PRESENT_IMMEDIATE, e.images = 3;
    if (p == LS_COMPAT) e.present = LS_PRESENT_FIFO, e.images = 3, e.fast_sync = false;
    return e;
}

/// The parts set alone (getenv-like lookup, so a test can hand its own) over the profile's. Only with a profile on: Off stays
/// the renderer as before whatever is set
template <class Lookup>
LsEffective ls_resolve(int profile, Lookup get) {
    LsEffective e = ls_profile_parts(profile);
    if (e.profile == LS_OFF) return e;
    auto flag = [&](const char* name, bool& v) {
        if (const char* s = get(name); s && *s) v = atoi(s) != 0;
    };
    flag("RX_VK_LS_INTERNAL_FG", e.internal_fg);
    flag("RX_VK_LS_WINDOW_FILTER", e.window_filter);
    flag("RX_VK_LS_REPEATS", e.repeats);
    flag("RX_VK_LS_FAST_SYNC", e.fast_sync);
    if (const char* s = get("RX_VK_LS_FSE"); s && *s) {
        if (!strcmp(s, "default") || !strcmp(s, "0")) e.fse = LS_FSE_DEFAULT;
        else if (!strcmp(s, "disallow") || !strcmp(s, "1")) e.fse = LS_FSE_DISALLOW;
        else if (!strcmp(s, "allow") || !strcmp(s, "2")) e.fse = LS_FSE_ALLOW;
    }
    if (const char* s = get("RX_VK_LS_PRESENT"); s && *s) {
        if (!strcmp(s, "auto")) e.present = LS_PRESENT_AUTO;
        else if (!strcmp(s, "fifo")) e.present = LS_PRESENT_FIFO;
        else if (!strcmp(s, "immediate")) e.present = LS_PRESENT_IMMEDIATE;
    }
    if (const char* s = get("RX_VK_LS_IMAGES"); s && *s) {
        const int n = atoi(s);
        if (n == 0 || (n >= 2 && n <= 4)) e.images = n;
    }
    return e;
}

/// Swapchain images: what the renderer would ask for, the mode's choice, within the surface's limits (max 0: no limit)
inline unsigned ls_image_count(unsigned renderer_wants, int mode_images, unsigned surface_min, unsigned surface_max) {
    unsigned n = mode_images > 0 ? (unsigned)mode_images : renderer_wants;
    if (n < surface_min) n = surface_min;
    if (surface_max && n > surface_max) n = surface_max;
    return n;
}

}  // namespace mkwvk
