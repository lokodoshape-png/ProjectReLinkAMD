// (relink-amd 0.7.9.8, AMD MAX PERFORMANCE stage 1) The problems known on AMD GPUs and drivers, in one table: what each is, where it
// was seen, which GPUs it is applied to today and what turns it off. The renderer's own code still decides each of them where it
// always did (the file and the switch are named below); this table describes them for the log ("vk: AMD MAX: known problem ..."),
// so a report shows which ones a start applies. A later stage makes these the place the decisions are read from, by generation,
// driver and driver version, once a test on that generation says so.
//
// Pure: gpu/vk/tests/amd_caps_test.cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "amd_caps.h"

namespace amdmax {

enum GenMask : uint32_t {
    kGcn = 1u << (int)Gen::gcn,
    kRdna1 = 1u << (int)Gen::rdna1,
    kRdna2 = 1u << (int)Gen::rdna2,
    kRdna3 = 1u << (int)Gen::rdna3,
    kRdna35 = 1u << (int)Gen::rdna35,
    kRdna4 = 1u << (int)Gen::rdna4,
    kRdna3Later = 1u << (int)Gen::rdna3_or_later,
    kUnknownGen = 1u << (int)Gen::unknown,
    kAnyAmd = kGcn | kRdna1 | kRdna2 | kRdna3 | kRdna35 | kRdna4 | kRdna3Later | kUnknownGen,
};

struct Quirk {
    const char* key;
    const char* what;        // what the renderer does about it
    uint32_t gens;           // the generations it is applied to today
    uint32_t driver_id;      // 0: any driver
    bool integrated_only;
    const char* seen_on;     // where it was seen (proof); generations outside it are applied to by caution only
    uint32_t seen_gens;      // the generations of seen_on
    const char* off;         // the switch that turns it off
};

inline const std::vector<Quirk>& quirks() {
    static const std::vector<Quirk> table = {
        {"mesh_no_optimizer", "task and mesh pipelines built without the driver's optimizer (vk_draw.cpp)", kAnyAmd, 0, false,
         "RX 6600: device lost in Kokiri when the optimizer built them (issues 461, 484)", kRdna2, "RX_VK_MESH_OPT=1"},
        {"compiler_fail_13", "a pipeline the AMD compiler fails on (-13) is built again without the optimizer (vk_draw.cpp)", kAnyAmd,
         kDriverAmdProprietary, false, "RX 6600: the Market's and Kakariko's dogs (0.7.9.1)", kRdna2, "RX_VK_NOOPT=1 builds all so"},
        {"wave64_drawing", "drawing stages translated for 64-lane groups where the driver holds only compute to 32 (vk_main.cpp)",
         kAnyAmd, 0, false, "RX 6600: the dogs drawn without textures", kRdna2, "RX_VK_NO_BIG_WARP=1"},
        {"uneven_bc_levels", "BC levels whose block grid is not level 0's halved are sampled wrong: such ASTC stays RGBA8 (vk_texture.cpp)",
         kAnyAmd, 0, false, "RX 6600 XT, RX 6700 XT: dark lines on the terrain (Lichii #448)", kRdna2, "RX_VK_UNEVEN_BC=1 resizes them"},
        {"fifo_at_startup", "the start-up screen presents without FIFO; the game's first frame makes the swapchain again (vk_main.cpp)",
         kAnyAmd, 0, false, "RX 6600 XT: shader preparation from 3 minutes to hours with FIFO", kRdna2, "(none)"},
        {"igpu_wait_gpu", "each present waits for its frame's GPU work (vk_replay.cpp)", kAnyAmd, kDriverAmdProprietary, true,
         "Radeon 890M: HUD icons flashing", kRdna35, "RX_VK_WAIT_GPU=-1"},
        {"igpu_astc_cpu", "ASTC decoded on the CPU, unswizzle on the GPU (vk_texture.cpp)", kAnyAmd, kDriverAmdProprietary, true,
         "Radeon 890M, ROG Xbox Ally X", kRdna35, "RX_VK_ASTC_CPU=0, RX_VK_GPU_UNSWIZZLE=0"},
    };
    return table;
}

struct Applied {
    const Quirk* q;
    bool proven_here;   // seen on this generation (not only applied by caution)
};

inline std::vector<Applied> applied_quirks(const Caps& c, Gen g) {
    std::vector<Applied> out;
    if (c.vendor_id != 0x1002) return out;
    const uint32_t bit = 1u << (int)g;
    for (const Quirk& q : quirks()) {
        if (!(q.gens & bit)) continue;
        if (q.driver_id && q.driver_id != c.driver_id) continue;
        if (q.integrated_only && !c.integrated) continue;
        const std::string key = q.key;
        if (key == "wave64_drawing" && !c.big_warp) continue;   // (the driver holds the drawing stages to 32 lanes: nothing to do)
        if (key == "mesh_no_optimizer" && !c.mesh) continue;    // (no mesh pipelines: the compute fallback has none)
        out.push_back({&q, (q.seen_gens & bit) != 0});
    }
    return out;
}

inline std::string quirk_line(const Applied& a) {
    return std::string(a.q->key) + ": " + a.q->what + "; seen on " + a.q->seen_on +
           (a.proven_here ? "" : " (applied here by caution: not seen on this generation)") + "; off: " + a.q->off;
}

} // namespace amdmax
