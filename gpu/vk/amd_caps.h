// (relink-amd 0.7.9.8, AMD MAX PERFORMANCE stage 1) What an AMD GPU and its driver can do, in one place, and the known
// problems that apply to it (amd_quirks.h). Filled once after the device is made (vk_main.cpp amd_caps_fill) and written to the
// log as the "vk: AMD MAX:" lines. This stage only describes: no decision of the renderer reads it yet, so nothing a start
// does changes. Later stages (amd_policy.h) choose paths from it.
//
// Pure (no Vulkan header, no OS call): gpu/vk/tests/amd_caps_test.cpp checks it without a GPU.
//
// Decisions are always taken from the capabilities the driver reports. The generation (RDNA 2, 3, 4...) is only for the log
// and for the known-problems table: it comes from the device ID when the ID is in the table below, and from the capabilities
// otherwise. A device ID missing from the table is never a reason to leave a feature off.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace amdmax {

enum class Gen : uint8_t { not_amd, gcn, rdna1, rdna2, rdna3, rdna35, rdna4, rdna3_or_later, unknown };

inline const char* gen_name(Gen g) {
    switch (g) {
    case Gen::not_amd: return "not AMD";
    case Gen::gcn: return "GCN";
    case Gen::rdna1: return "RDNA 1";
    case Gen::rdna2: return "RDNA 2";
    case Gen::rdna3: return "RDNA 3";
    case Gen::rdna35: return "RDNA 3.5";
    case Gen::rdna4: return "RDNA 4";
    case Gen::rdna3_or_later: return "RDNA 3 or later";
    case Gen::unknown: return "unknown";
    }
    return "unknown";
}

// Vulkan's VkDriverId values the table needs (vulkan_core.h), kept here so the header needs no Vulkan include
enum : uint32_t { kDriverAmdProprietary = 1, kDriverAmdOpenSource = 2, kDriverMesaRadv = 3 };
// VkShaderStageFlagBits
enum : uint32_t { kStageVertex = 0x1, kStageFragment = 0x10, kStageCompute = 0x20, kStageTask = 0x40, kStageMesh = 0x80 };

struct Caps {
    // identity
    uint32_t vendor_id = 0, device_id = 0, driver_id = 0, driver_version = 0, api_version = 0;
    std::string device_name, driver_name, driver_info;
    bool integrated = false;
    // memory
    uint64_t vram_bytes = 0;         // the largest DEVICE_LOCAL heap
    uint64_t ram_total_bytes = 0;    // the system's
    uint64_t ram_free_bytes = 0;     // at the start
    bool memory_budget = false;      // VK_EXT_memory_budget
    // subgroups
    uint32_t sg_default = 0, sg_min = 0, sg_max = 0;
    uint32_t sg32_stages = 0;        // stages a pipeline can hold to 32 lanes (requiredSubgroupSizeStages)
    bool big_warp = false;           // drawing stages may run 64 wide: translated for it (vk_main.cpp)
    // features
    bool mesh = false, mesh_fallback = false;
    bool coop_matrix = false;                 // VK_KHR_cooperative_matrix with its feature
    bool coop_16x8x16_f16 = false;            // the console's shape: what the native multiply path needs (vk_coopmat.cpp)
    bool coop_16x16x16_f16 = false;           // WMMA's shape (RDNA 3 and later)
    bool native_mma = false;                  // the native path is on (the test passed)
    bool mma_software = false;                // the multiplies run on subgroup shuffles
    bool f16 = false, float_atomic_add = false, anti_lag = false, bc = false, astc = false;
    // CPU
    uint32_t cpu_threads = 0, cpu_perf_cores = 0, cpu_eff_cores = 0;   // (cores by efficiency class; 0: not known)
};

/// The generation the device ID says, when it is in the table (Gen::unknown otherwise). Ranges of PCI device IDs by chip;
/// a hint for the log and the known-problems table only.
inline Gen gen_by_device_id(uint32_t vendor, uint32_t id) {
    if (vendor != 0x1002) return Gen::not_amd;
    struct Range { uint16_t lo, hi; Gen g; };
    static const Range table[] = {
        {0x7310, 0x731F, Gen::rdna1},   // Navi 10 (RX 5600 / 5700)
        {0x7340, 0x734F, Gen::rdna1},   // Navi 14 (RX 5500)
        {0x7360, 0x736F, Gen::rdna1},   // Navi 12
        {0x73A0, 0x73BF, Gen::rdna2},   // Navi 21 (RX 6800 / 6900 / 6950)
        {0x73C0, 0x73DF, Gen::rdna2},   // Navi 22 (RX 6700 / 6750)
        {0x73E0, 0x73FF, Gen::rdna2},   // Navi 23 (RX 6600 / 6650)
        {0x7420, 0x743F, Gen::rdna2},   // Navi 24 (RX 6400 / 6500)
        {0x1680, 0x1681, Gen::rdna2},   // Rembrandt (Radeon 680M)
        {0x163F, 0x163F, Gen::rdna2},   // Van Gogh (Steam Deck)
        {0x7440, 0x745F, Gen::rdna3},   // Navi 31 (RX 7900)
        {0x7470, 0x747F, Gen::rdna3},   // Navi 32 (RX 7700 / 7800)
        {0x7480, 0x749F, Gen::rdna3},   // Navi 33 (RX 7600)
        {0x15BF, 0x15BF, Gen::rdna3},   // Phoenix (Radeon 780M)
        {0x15C8, 0x15C8, Gen::rdna3},   // Phoenix 2
        {0x150E, 0x150E, Gen::rdna35},  // Strix Point (Radeon 880M / 890M)
        {0x1586, 0x1586, Gen::rdna35},  // Strix Halo
        {0x7550, 0x755F, Gen::rdna4},   // Navi 48 (RX 9070)
        {0x7590, 0x759F, Gen::rdna4},   // Navi 44 (RX 9060)
    };
    for (const Range& r : table)
        if (id >= r.lo && id <= r.hi) return r.g;
    return Gen::unknown;
}

/// The generation the capabilities say: 64-lane-only subgroups are GCN; wave32 without mesh shaders RDNA 1; mesh shaders without
/// WMMA's matrices RDNA 2 (or an RDNA 3 whose driver does not offer them); with them RDNA 3 or later
inline Gen gen_by_caps(const Caps& c) {
    if (c.vendor_id != 0x1002) return Gen::not_amd;
    if (c.sg_min == 64 && c.sg_max == 64) return Gen::gcn;
    if (!c.mesh) return Gen::rdna1;
    if (c.coop_16x16x16_f16) return Gen::rdna3_or_later;
    return Gen::rdna2;
}

struct GenGuess {
    Gen gen = Gen::unknown;
    const char* basis = "";   // "device ID" or "capabilities"
    bool disagree = false;    // the ID's generation is not one the capabilities allow (an old driver, or a table to fix)
};

inline GenGuess guess_gen(const Caps& c) {
    GenGuess g;
    const Gen by_caps = gen_by_caps(c);
    const Gen by_id = gen_by_device_id(c.vendor_id, c.device_id);
    if (by_caps == Gen::not_amd) {
        g.gen = Gen::not_amd, g.basis = "vendor ID";
        return g;
    }
    if (by_id == Gen::unknown) {
        g.gen = by_caps, g.basis = "capabilities";
        return g;
    }
    g.gen = by_id, g.basis = "device ID";
    const bool id_new = by_id == Gen::rdna3 || by_id == Gen::rdna35 || by_id == Gen::rdna4;
    switch (by_caps) {
    case Gen::gcn: g.disagree = by_id != Gen::gcn; break;
    case Gen::rdna1: g.disagree = by_id != Gen::rdna1; break;   // (a newer chip without mesh shaders: its driver lacks them)
    case Gen::rdna2: g.disagree = id_new; break;                 // (an RDNA 3/4 chip without WMMA's matrices: old driver)
    case Gen::rdna3_or_later: g.disagree = !id_new; break;
    default: break;
    }
    return g;
}

inline bool is_rdna3_or_later(Gen g) {
    return g == Gen::rdna3 || g == Gen::rdna35 || g == Gen::rdna4 || g == Gen::rdna3_or_later;
}

inline std::string mb(uint64_t bytes) {
    char b[32];
    std::snprintf(b, sizeof b, "%llu MB", (unsigned long long)(bytes >> 20));
    return b;
}

inline std::string driver_label(uint32_t id) {
    switch (id) {
    case kDriverAmdProprietary: return "AMD proprietary";
    case kDriverAmdOpenSource: return "AMDVLK";
    case kDriverMesaRadv: return "RADV";
    default: {
        char b[24];
        std::snprintf(b, sizeof b, "driver id %u", id);
        return b;
    }
    }
}

inline std::string stage_names(uint32_t s) {
    std::string out;
    const auto add = [&](uint32_t bit, const char* n) {
        if (!(s & bit)) return;
        if (!out.empty()) out += "+";
        out += n;
    };
    add(kStageVertex, "vertex"), add(kStageFragment, "fragment"), add(kStageCompute, "compute"), add(kStageTask, "task"),
        add(kStageMesh, "mesh");
    return out.empty() ? "none" : out;
}

/// The matrix multiplies' path in words, and why
inline std::string matrix_line(const Caps& c) {
    std::string shapes;
    if (c.coop_16x8x16_f16) shapes += "16x8x16 f16";
    if (c.coop_16x16x16_f16) shapes += shapes.empty() ? "16x16x16 f16" : ", 16x16x16 f16";
    std::string s = c.coop_matrix ? "cooperative matrices " + (shapes.empty() ? std::string("without an f16 shape used here") : shapes)
                                  : "no cooperative matrices";
    if (c.native_mma && !c.mma_software) s += "; multiplies native";
    else if (c.coop_16x16x16_f16 && !c.coop_16x8x16_f16) s += "; multiplies in software (the native path needs 16x8x16; WMMA's 16x16x16 not used yet)";
    else s += "; multiplies in software";
    return s;
}

/// The "vk: AMD MAX:" lines (without the prefix), one per entry
inline std::string summary_gpu(const Caps& c, const GenGuess& g) {
    char b[512];
    std::snprintf(b, sizeof b, "%s (%04x:%04x), %s from the %s%s; %s %s, Vulkan %u.%u; %s",
                  c.device_name.c_str(), c.vendor_id, c.device_id, gen_name(g.gen), g.basis,
                  g.disagree ? " (the capabilities say otherwise: an old driver?)" : "", driver_label(c.driver_id).c_str(),
                  c.driver_info.empty() ? "" : c.driver_info.c_str(), (c.api_version >> 22) & 0x7F, (c.api_version >> 12) & 0x3FF,
                  c.integrated ? "integrated" : "discrete");
    return b;
}

inline std::string summary_memory(const Caps& c) {
    return "VRAM " + mb(c.vram_bytes) + ", RAM " + mb(c.ram_total_bytes) + " (" + mb(c.ram_free_bytes) + " free), memory budget " +
           (c.memory_budget ? "reported" : "not reported");
}

inline std::string summary_shaders(const Caps& c) {
    char b[160];
    std::snprintf(b, sizeof b, "subgroups %u lanes (%u-%u), held to 32 on %s", c.sg_default, c.sg_min, c.sg_max,
                  stage_names(c.sg32_stages).c_str());
    std::string s = b;
    s += c.big_warp ? "; drawing stages translated for 64 lanes" : "";
    s += c.mesh ? ", mesh shaders yes" : c.mesh_fallback ? ", mesh shaders no (compute fallback)" : ", mesh shaders no";
    s += ", f16 " + std::string(c.f16 ? "yes" : "no") + ", float atomic add " + (c.float_atomic_add ? "yes" : "no") + ", BC " +
         (c.bc ? "yes" : "no") + ", ASTC " + (c.astc ? "sampled" : "decoded on upload") + ", Anti-Lag 2 " + (c.anti_lag ? "yes" : "no");
    return s;
}

inline std::string summary_cpu(const Caps& c) {
    char b[160];
    if (c.cpu_perf_cores || c.cpu_eff_cores)
        std::snprintf(b, sizeof b, "CPU %u threads, %u performance and %u efficiency cores", c.cpu_threads, c.cpu_perf_cores,
                      c.cpu_eff_cores);
    else
        std::snprintf(b, sizeof b, "CPU %u threads", c.cpu_threads);
    return b;
}

} // namespace amdmax
