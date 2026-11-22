// Game-free regression for the Lossless Scaling compatibility mode's profile table (gpu/vk/ls_compat.h).
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "../ls_compat.h"

namespace {
int failures = 0;

void check(const char* name, bool ok) {
    if (ok) return;
    std::fprintf(stderr, "FAILED: %s\n", name);
    failures++;
}

std::map<std::string, std::string> env;
const char* get(const char* n) {
    auto it = env.find(n);
    return it == env.end() ? nullptr : it->second.c_str();
}

bool same(const mkwvk::LsEffective& a, const mkwvk::LsEffective& b) {
    return a.profile == b.profile && a.internal_fg == b.internal_fg && a.window_filter == b.window_filter && a.repeats == b.repeats &&
           a.fse == b.fse && a.present == b.present && a.images == b.images && a.fast_sync == b.fast_sync;
}

}  // namespace

int main() {
    using namespace mkwvk;
    const LsEffective as_before{};

    // Off is the renderer as before, whatever the parts say
    env = {{"RX_VK_LS_INTERNAL_FG", "0"}, {"RX_VK_LS_IMAGES", "2"}, {"RX_VK_LS_FSE", "disallow"}, {"RX_VK_LS_PRESENT", "fifo"}};
    check("off ignores the parts", same(ls_resolve(LS_OFF, get), as_before));
    check("out of range is off", same(ls_resolve(9, get), as_before) && same(ls_resolve(-1, get), as_before));
    env.clear();

    const LsEffective sc = ls_resolve(LS_SCALING, get);
    check("scaling: window filter off", !sc.window_filter);
    check("scaling: internal FG still allowed", sc.internal_fg && sc.repeats);
    check("scaling: FSE disallowed", sc.fse == LS_FSE_DISALLOW);
    check("scaling: presents and images as before", sc.present == LS_PRESENT_AUTO && sc.images == 0 && sc.fast_sync);

    const LsEffective fg = ls_resolve(LS_LSFG, get);
    check("lsfg: no internal FG, no repeats", !fg.internal_fg && !fg.repeats);
    check("lsfg: window filter kept", fg.window_filter);
    check("lsfg: FSE disallowed", fg.fse == LS_FSE_DISALLOW);

    const LsEffective both = ls_resolve(LS_LSFG_SCALING, get);
    check("lsfg+scaling: both", !both.internal_fg && !both.repeats && !both.window_filter && both.present == LS_PRESENT_AUTO);

    const LsEffective perf = ls_resolve(LS_PERFORMANCE, get);
    check("performance: immediate, 3 images", perf.present == LS_PRESENT_IMMEDIATE && perf.images == 3);
    check("performance: as lsfg+scaling otherwise", !perf.internal_fg && !perf.repeats && !perf.window_filter && perf.fast_sync);

    const LsEffective compat = ls_resolve(LS_COMPAT, get);
    check("compat: fifo, 3 images, fast sync off", compat.present == LS_PRESENT_FIFO && compat.images == 3 && !compat.fast_sync);

    // parts set alone win over the profile
    env = {{"RX_VK_LS_INTERNAL_FG", "1"}, {"RX_VK_LS_FSE", "default"}, {"RX_VK_LS_IMAGES", "4"}, {"RX_VK_LS_PRESENT", "auto"},
           {"RX_VK_LS_FAST_SYNC", "1"}};
    const LsEffective o = ls_resolve(LS_COMPAT, get);
    check("override: internal FG back", o.internal_fg);
    check("override: FSE default", o.fse == LS_FSE_DEFAULT);
    check("override: images 4, present auto, fast sync", o.images == 4 && o.present == LS_PRESENT_AUTO && o.fast_sync);
    env = {{"RX_VK_LS_IMAGES", "7"}, {"RX_VK_LS_PRESENT", "bogus"}, {"RX_VK_LS_FSE", "bogus"}};
    const LsEffective bad = ls_resolve(LS_PERFORMANCE, get);
    check("bad values leave the profile's", bad.images == 3 && bad.present == LS_PRESENT_IMMEDIATE && bad.fse == LS_FSE_DISALLOW);
    env.clear();

    // names
    check("parse numbers", ls_parse_profile("0") == 0 && ls_parse_profile("5") == 5 && ls_parse_profile("6") == -1);
    check("parse names", ls_parse_profile("lsfg") == LS_LSFG && ls_parse_profile("LSFG_Scaling") == LS_LSFG_SCALING &&
                             ls_parse_profile("compat") == LS_COMPAT && ls_parse_profile("normal") == LS_OFF &&
                             ls_parse_profile("performance") == LS_PERFORMANCE);
    check("parse junk", ls_parse_profile("") == -1 && ls_parse_profile(nullptr) == -1 && ls_parse_profile("lsfgx") == -1 &&
                            ls_parse_profile("ls") == -1);
    check("names", !strcmp(ls_profile_name(LS_OFF), "Off") && !strcmp(ls_profile_name(LS_COMPAT), "LS Max Compatibility"));

    // image count within the surface's limits
    check("images: mode's", ls_image_count(4, 3, 2, 8) == 3);
    check("images: renderer's when 0", ls_image_count(4, 0, 2, 8) == 4);
    check("images: surface min", ls_image_count(4, 2, 3, 8) == 3);
    check("images: surface max", ls_image_count(4, 4, 2, 3) == 3);
    check("images: no max", ls_image_count(4, 4, 2, 0) == 4);

    if (!failures) std::puts("ls_compat_test: all passed");
    return failures ? 1 : 0;
}
