// Game-free regression for the AMD MAX PERFORMANCE capability matrix and known-problems table (gpu/vk/amd_caps.h, amd_quirks.h).
// g++ -std=c++17 -I gpu/vk gpu/vk/tests/amd_caps_test.cpp -o amd_caps_test && ./amd_caps_test
#include <cstdio>
#include <string>

#include "../amd_caps.h"
#include "../amd_quirks.h"

using namespace amdmax;

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    if (ok) return;
    std::fprintf(stderr, "FAILED: %s\n", name);
    failures++;
}

Caps radeon(uint32_t id, bool mesh, bool wmma, bool integrated = false, uint32_t driver = kDriverAmdProprietary) {
    Caps c;
    c.vendor_id = 0x1002, c.device_id = id, c.driver_id = driver, c.api_version = (1u << 22) | (3u << 12);
    c.sg_default = 64, c.sg_min = 32, c.sg_max = 64, c.sg32_stages = kStageCompute;
    c.big_warp = true, c.mesh = mesh, c.coop_matrix = wmma, c.coop_16x16x16_f16 = wmma, c.integrated = integrated;
    return c;
}
bool has(const std::vector<Applied>& v, const char* key) {
    for (const Applied& a : v)
        if (std::string(a.q->key) == key) return true;
    return false;
}
bool proven(const std::vector<Applied>& v, const char* key) {
    for (const Applied& a : v)
        if (std::string(a.q->key) == key) return a.proven_here;
    return false;
}
}  // namespace

int main() {
    // the reference card: RX 9060 XT (Navi 44)
    {
        const Caps c = radeon(0x7590, true, true);
        const GenGuess g = guess_gen(c);
        check("9060 XT is RDNA 4 by its ID", g.gen == Gen::rdna4 && std::string(g.basis) == "device ID" && !g.disagree);
        const auto q = applied_quirks(c, g.gen);
        check("9060 XT: mesh optimizer off, by caution", has(q, "mesh_no_optimizer") && !proven(q, "mesh_no_optimizer"));
        check("9060 XT: no iGPU rules", !has(q, "igpu_wait_gpu") && !has(q, "igpu_astc_cpu"));
        check("9060 XT: matrices line names WMMA", matrix_line(c).find("16x16x16") != std::string::npos);
    }
    // RDNA 2 (RX 6600): the problems were seen here
    {
        const Caps c = radeon(0x73FF, true, false);
        const GenGuess g = guess_gen(c);
        check("6600 is RDNA 2", g.gen == Gen::rdna2 && !g.disagree);
        const auto q = applied_quirks(c, g.gen);
        check("6600: mesh optimizer off, proven", has(q, "mesh_no_optimizer") && proven(q, "mesh_no_optimizer"));
        check("6600: wave64 drawing", has(q, "wave64_drawing"));
    }
    // RDNA 3 (RX 7900) on an old driver without cooperative matrices: the ID wins, the mismatch is said
    {
        const Caps c = radeon(0x744C, true, false);
        const GenGuess g = guess_gen(c);
        check("7900 old driver: RDNA 3, disagree", g.gen == Gen::rdna3 && g.disagree);
    }
    // an AMD chip not in the table: the capabilities decide
    {
        const Caps c = radeon(0x7FFF, true, true);
        const GenGuess g = guess_gen(c);
        check("unknown ID with WMMA: RDNA 3 or later", g.gen == Gen::rdna3_or_later && std::string(g.basis) == "capabilities");
        check("unknown gen still gets the cautious rules", has(applied_quirks(c, g.gen), "mesh_no_optimizer"));
        const Caps old = radeon(0x7FFE, true, false);
        check("unknown ID, mesh, no WMMA: RDNA 2", guess_gen(old).gen == Gen::rdna2);
        const Caps rdna1 = radeon(0x7FFD, false, false);
        check("unknown ID, no mesh: RDNA 1", guess_gen(rdna1).gen == Gen::rdna1);
        check("no mesh: no mesh rule", !has(applied_quirks(rdna1, Gen::rdna1), "mesh_no_optimizer"));
    }
    // GCN: 64 lanes only
    {
        Caps c = radeon(0x67DF, false, false);
        c.sg_min = c.sg_max = 64;
        check("RX 580 is GCN", guess_gen(c).gen == Gen::gcn);
    }
    // integrated: Radeon 890M on the proprietary driver gets the iGPU rules; on RADV it does not
    {
        const Caps c = radeon(0x150E, true, true, true);
        const GenGuess g = guess_gen(c);
        check("890M is RDNA 3.5", g.gen == Gen::rdna35 && !g.disagree);
        const auto q = applied_quirks(c, g.gen);
        check("890M: iGPU rules", has(q, "igpu_wait_gpu") && has(q, "igpu_astc_cpu"));
        const Caps radv = radeon(0x150E, true, true, true, kDriverMesaRadv);
        check("890M on RADV: no proprietary-only rules", !has(applied_quirks(radv, Gen::rdna35), "igpu_wait_gpu") &&
                                                             !has(applied_quirks(radv, Gen::rdna35), "compiler_fail_13"));
    }
    // a driver that holds the drawing stages to 32 lanes: no wave64 rule
    {
        Caps c = radeon(0x7590, true, true);
        c.big_warp = false, c.sg32_stages = kStageVertex | kStageFragment | kStageCompute;
        check("drawing held to 32: no wave64 rule", !has(applied_quirks(c, Gen::rdna4), "wave64_drawing"));
    }
    // NVIDIA and Intel: nothing applies, whatever the device ID
    {
        Caps nv;
        nv.vendor_id = 0x10DE, nv.device_id = 0x7590, nv.mesh = true;
        check("NVIDIA: not AMD", guess_gen(nv).gen == Gen::not_amd && applied_quirks(nv, Gen::not_amd).empty());
        Caps intel;
        intel.vendor_id = 0x8086, intel.device_id = 0x56A0;
        check("Intel: not AMD", guess_gen(intel).gen == Gen::not_amd && applied_quirks(intel, Gen::not_amd).empty());
    }
    // the log lines say what they should
    {
        Caps c = radeon(0x7590, true, true);
        c.device_name = "AMD Radeon RX 9060 XT", c.driver_info = "25.11.1", c.vram_bytes = 16ull << 30, c.ram_total_bytes = 24ull << 30;
        c.cpu_threads = 16, c.cpu_perf_cores = 6, c.cpu_eff_cores = 4;
        const std::string gpu = summary_gpu(c, guess_gen(c));
        check("gpu line", gpu.find("RDNA 4") != std::string::npos && gpu.find("1002:7590") != std::string::npos &&
                              gpu.find("discrete") != std::string::npos);
        check("memory line", summary_memory(c).find("VRAM 16384 MB") != std::string::npos);
        check("shader line", summary_shaders(c).find("held to 32 on compute") != std::string::npos);
        check("cpu line", summary_cpu(c) == "CPU 16 threads, 6 performance and 4 efficiency cores");
    }
    if (failures) return 1;
    std::printf("amd_caps_test: all passed\n");
    return 0;
}
