// (relink-amd 0.7.9.7) The shader compile profile: every shader translation and every pipeline the driver compiles, from the
// game's first frame to its last, timed and written beside relink_pipelines.bin as
//   relink_shader_profile.txt   a summary to read: per start phase and in play, how many, how long (total, median, p95, slowest),
//                               how much of it ran in parallel, the slowest items, and the in-play compiles long enough to stutter
//   relink_shader_profile.csv   every item, one line each (seconds since the start, phase, kind, program key, ms, ok, thread)
//   relink_frame_profile.csv    in play, every 120 frames: fps, the slowest frames, GPU time, the replay's CPU time, waits for
//                               shaders, draws, new pipelines and programs (the summary says which held each window: GPU or CPU)
// written at the end of the start, then again in play at most every 30 s, and when the game closes.
// RX_VK_COMPILE_PROFILE=0: off (nothing timed, nothing written).
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef min
#undef max
#else
extern char** environ;
#endif

namespace cprof {

enum Kind : uint8_t { Translate, Graphics, Compute, NKinds };
inline const char* const kind_names[NKinds] = {"translation", "graphics pipeline", "compute pipeline"};

struct Item {
    float t_s;      // (seconds since the game started)
    float ms;
    uint64_t key;   // (the program's key; 0 when the call site does not know it)
    uint8_t kind, phase, ok;
    uint16_t thread;
};
struct Phase {
    std::string name;
    double t0 = 0, t1 = -1;   // (t1 < 0: still running)
    unsigned threads = 0;     // (the workers the phase was given: start_threads)
    uint64_t ram_mb = 0;
};

/// (in play) 120 presented frames at a time, from vk_replay.cpp's log_frame_stats: how fast, and what held each frame
struct Window {
    float t_s, fps, worst_ms, p99_ms, gpu_ms, replay_ms, wait_ms, draws;
    uint32_t new_pipelines, new_programs;
};

inline std::mutex mu;
inline std::vector<Item> items;
inline std::vector<Window> windows;
inline std::vector<Phase> phases{{"before the start phases", 0, -1, 0, 0}};
inline std::map<std::thread::id, uint16_t> thread_ids;
inline std::atomic<uint8_t> cur_phase{0};
inline const std::chrono::steady_clock::time_point origin = std::chrono::steady_clock::now();
inline thread_local uint64_t cur_key = 0;   // (set by a call site that knows the program, read by the pipeline guards)
inline std::string out_dir;                  // (where the files go: set once the notes' folder is known)
inline std::atomic<double> last_write{0};
inline std::atomic<size_t> written_items{0};
inline std::atomic<bool> writing{false};

inline bool enabled() {
    static const bool on = [] {
        const char* e = getenv("RX_VK_COMPILE_PROFILE");
        return !(e && atoi(e) == 0);
    }();
    return on;
}
inline double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - origin).count(); }

/// A new phase from now on (the previous one ends here). "in play" is the last one.
inline void phase(const char* name, unsigned threads = 0, uint64_t ram_mb = 0) {
    if (!enabled()) return;
    std::lock_guard lk(mu);
    const double t = now_s();
    if (phases.back().t1 < 0) phases.back().t1 = t;
    phases.push_back({name, t, -1, threads, ram_mb});
    cur_phase.store((uint8_t)std::min<size_t>(phases.size() - 1, 255));
}
/// The workers a running phase was given (start_threads tells it once it has counted them)
inline void phase_threads(unsigned threads) {
    if (!enabled()) return;
    std::lock_guard lk(mu);
    phases.back().threads = std::max(phases.back().threads, threads);
}

inline void write(const char* why);

/// (in play) written again 30 s after the last time, on a thread of its own, so a compile or a frame never waits on the disk
inline void write_soon() {
    const double t = now_s();
    if (t - last_write.load() > 30 && !writing.exchange(true)) {
        last_write.store(t);
        std::thread([] {
            write("in play");
            writing.store(false);
        }).detach();
    }
}

inline void frame_window(const Window& w) {
    if (!enabled()) return;
    {
        std::lock_guard lk(mu);
        if (windows.size() < 200000) windows.push_back(w);
    }
    write_soon();
}

inline void record(Kind kind, uint64_t key, double ms, bool ok) {
    if (!enabled()) return;
    const double t = now_s();
    bool in_play = false;
    {
        std::lock_guard lk(mu);
        if (items.size() >= 2000000) return;   // (64 MB: past it the profile has long said what it can)
        auto [it, fresh] = thread_ids.try_emplace(std::this_thread::get_id(), (uint16_t)thread_ids.size());
        items.push_back({(float)t, (float)ms, key, kind, cur_phase.load(), (uint8_t)ok, it->second});
        in_play = phases[cur_phase.load()].name == "in play";
    }
    if (in_play) write_soon();
}

/// Times a scope and records it (kind, key) when it ends
struct Timer {
    Kind kind;
    uint64_t key;
    bool ok = true;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    Timer(Kind k, uint64_t key_) : kind(k), key(key_) {}
    ~Timer() {
        if (enabled()) record(kind, key, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), ok);
    }
};

inline double pct(std::vector<float>& v, double p) {   // (v sorted)
    if (v.empty()) return 0;
    return v[std::min(v.size() - 1, (size_t)(p * (v.size() - 1) + 0.5))];
}

inline void env_lines(FILE* f) {
    std::vector<std::string> env;
#ifdef _WIN32
    if (char* block = GetEnvironmentStringsA()) {
        for (const char* s = block; *s; s += strlen(s) + 1) env.emplace_back(s);
        FreeEnvironmentStringsA(block);
    }
#else
    for (char** e = environ; e && *e; e++) env.emplace_back(*e);
#endif
    std::sort(env.begin(), env.end());
    bool any = false;
    for (const auto& s : env)
        if (s.rfind("RX_", 0) == 0) fprintf(f, "  %s\n", s.c_str()), any = true;
    if (!any) fprintf(f, "  (none: every option at its default)\n");
}

/// Both files, from what is recorded so far. why: "end of the start", "in play", "exit"
inline void write(const char* why) {
    if (!enabled()) return;
    std::vector<Item> its;
    std::vector<Window> wins;
    std::vector<Phase> phs;
    size_t nthreads;
    std::string dir;
    {
        std::lock_guard lk(mu);
        its = items;
        wins = windows;
        phs = phases;
        nthreads = thread_ids.size();
        dir = out_dir;
    }
    if (dir.empty()) return;
    const double t_now = now_s();
    for (auto& p : phs)
        if (p.t1 < 0) p.t1 = t_now;

    const std::string txt = dir + "relink_shader_profile.txt", csv = dir + "relink_shader_profile.csv";
    if (FILE* f = fopen((csv + ".tmp").c_str(), "wb")) {
        fprintf(f, "t_s,phase,kind,key,ms,ok,thread\n");
        for (const Item& i : its)
            fprintf(f, "%.3f,\"%s\",%s,%016llx,%.3f,%d,%u\n", i.t_s, phs[i.phase].name.c_str(),
                    i.kind == Translate ? "translate" : i.kind == Graphics ? "graphics" : "compute", (unsigned long long)i.key, i.ms,
                    (int)i.ok, (unsigned)i.thread);
        fclose(f);
        remove(csv.c_str());
        rename((csv + ".tmp").c_str(), csv.c_str());
    }
    FILE* f = fopen((txt + ".tmp").c_str(), "wb");
    if (!f) return;
    fprintf(f, "ProjectReLinkAMD shader compile profile (written: %s, %.1f s after the game started)\n\n", why, t_now);
    fprintf(f, "PC: %u CPU threads", std::thread::hardware_concurrency());
#ifdef _WIN32
    MEMORYSTATUSEX mem{sizeof mem};
    if (GlobalMemoryStatusEx(&mem))
        fprintf(f, ", %llu MB of RAM (%llu MB free now)", (unsigned long long)(mem.ullTotalPhys >> 20),
                (unsigned long long)(mem.ullAvailPhys >> 20));
#endif
    fprintf(f, "\nThe GPU, its driver and the start's choices are in relink_log.txt (\"vk: device\", \"vk: AMD:\", \"vk: start phase\").\n");
    fprintf(f, "Options set (relink_env.txt and the environment):\n");
    env_lines(f);
    fprintf(f, "\n%zu items timed on %zu threads. A compile under 1 ms almost always came from a cache (the driver's or ReLink's).\n",
            its.size(), nthreads);

    // per phase and kind
    for (size_t ph = 0; ph < phs.size(); ph++) {
        const Phase& P = phs[ph];
        const double wall = std::max(1e-6, P.t1 - P.t0);
        std::vector<const Item*> in;
        for (const Item& i : its)
            if (i.phase == ph) in.push_back(&i);
        fprintf(f, "\n== %s: %.1f s (from %.1f s to %.1f s)", P.name.c_str(), wall, P.t0, P.t1);
        if (P.threads) fprintf(f, ", %u worker threads", P.threads);
        if (P.ram_mb) fprintf(f, ", %llu MB of RAM free at its start", (unsigned long long)P.ram_mb);
        fprintf(f, "\n");
        if (in.empty()) {
            fprintf(f, "   nothing compiled\n");
            continue;
        }
        double busy = 0;
        for (int k = 0; k < NKinds; k++) {
            std::vector<float> ms;
            size_t fails = 0, quick = 0;
            double sum = 0;
            for (const Item* i : in)
                if (i->kind == k) {
                    ms.push_back(i->ms), sum += i->ms;
                    fails += !i->ok, quick += i->ms < 1;
                }
            if (ms.empty()) continue;
            busy += sum;
            std::sort(ms.begin(), ms.end());
            fprintf(f, "   %-18s %7zu, %8.1f s of work; median %.1f ms, p95 %.1f ms, slowest %.0f ms; under 1 ms: %zu (%.0f%%)%s",
                    kind_names[k], ms.size(), sum / 1000, pct(ms, .5), pct(ms, .95), ms.back(), quick, 100.0 * quick / ms.size(),
                    fails ? "" : "\n");
            if (fails) fprintf(f, "; failed: %zu\n", fails);
        }
        // (how full the workers were: the work's time over the phase's time and its threads. Low means the phase waited on
        // something else, the disk, a lock or one long item, more than on the compiles themselves)
        const unsigned th = P.threads ? P.threads : 1;
        if (P.name != "in play" && wall >= 1)
            fprintf(f, "   parallelism: %.1f threads busy on average (%.0f%% of %u)\n", busy / 1000 / wall,
                100.0 * busy / 1000 / wall / th, th);
        std::vector<const Item*> slow = in;
        std::sort(slow.begin(), slow.end(), [](const Item* a, const Item* b) { return a->ms > b->ms; });
        const bool play = P.name == "in play";
        size_t shown = 0;
        fprintf(f, "   slowest:\n");
        for (const Item* i : slow) {
            if (shown == (play ? 40u : 15u)) break;
            fprintf(f, "     %8.1f ms  %-18s %016llx  at %.1f s%s\n", i->ms, kind_names[i->kind], (unsigned long long)i->key, i->t_s,
                    i->ok ? "" : "  FAILED");
            shown++;
        }
        if (play) {
            // (a compile on the game's way longer than a frame is a stutter; at 60 fps one frame is 16.7 ms)
            size_t over16 = 0, over50 = 0, over200 = 0;
            for (const Item* i : in) over16 += i->ms > 16.7, over50 += i->ms > 50, over200 += i->ms > 200;
            fprintf(f, "   in play, compiles longer than a frame at 60 fps (16.7 ms): %zu; over 50 ms: %zu; over 200 ms: %zu\n", over16,
                    over50, over200);
            // (per minute of play: where the compiles fall)
            std::map<int, std::pair<size_t, double>> per_min;
            for (const Item* i : in) {
                auto& m = per_min[(int)((i->t_s - P.t0) / 60)];
                m.first++, m.second += i->ms;
            }
            fprintf(f, "   per minute of play (minute: compiles, ms):");
            for (auto& [m, v] : per_min) fprintf(f, " %d: %zu, %.0f;", m, v.first, v.second);
            fprintf(f, "\n");
        }
    }
    // (in play: the frames, 120 at a time. A window whose GPU time is most of its frame time waits on the GPU (resolution, FSR,
    // effects); one whose GPU time is well under it waits on the CPU side (the replay, compiles, the game's own thread))
    if (!wins.empty()) {
        std::vector<float> fps;
        for (const Window& w : wins) fps.push_back(w.fps);
        std::sort(fps.begin(), fps.end());
        size_t gpu_bound = 0, cpu_bound = 0;
        for (const Window& w : wins) {
            const double frame = 1000.0 / std::max(1.0f, w.fps);
            (w.gpu_ms > 0.85 * frame ? gpu_bound : cpu_bound)++;
        }
        fprintf(f, "\n== in play: frames (%zu windows of 120)\n", wins.size());
        fprintf(f, "   fps: median %.1f, lowest window %.1f, highest %.1f\n", pct(fps, .5), fps.front(), fps.back());
        fprintf(f, "   windows held by the GPU (its time over 85%% of the frame): %zu; by the CPU side: %zu\n", gpu_bound, cpu_bound);
        std::vector<const Window*> worst;
        for (const Window& w : wins) worst.push_back(&w);
        std::sort(worst.begin(), worst.end(), [](const Window* a, const Window* b) { return a->fps < b->fps; });
        fprintf(f, "   slowest windows (at: fps, 99%% of frames under, worst frame; GPU, replay CPU and wait for shaders ms a frame; "
                   "draws a frame; new pipelines, programs):\n");
        for (size_t i = 0; i < worst.size() && i < 25; i++) {
            const Window& w = *worst[i];
            const double frame = 1000.0 / std::max(1.0f, w.fps);
            fprintf(f, "     at %7.1f s: %5.1f fps, p99 %5.1f ms, worst %6.1f ms; GPU %5.1f, replay %5.1f, waits %5.1f; %5.0f draws; "
                       "%u, %u  -> %s\n", w.t_s, w.fps, w.p99_ms, w.worst_ms, w.gpu_ms, w.replay_ms, w.wait_ms, w.draws,
                    w.new_pipelines, w.new_programs, w.gpu_ms > 0.85 * frame ? "GPU" : w.new_pipelines || w.new_programs ? "compiles" : "CPU");
        }
        const std::string fcsv = dir + "relink_frame_profile.csv";
        if (FILE* c = fopen((fcsv + ".tmp").c_str(), "wb")) {
            fprintf(c, "t_s,fps,p99_ms,worst_ms,gpu_ms,replay_ms,wait_ms,draws,new_pipelines,new_programs\n");
            for (const Window& w : wins)
                fprintf(c, "%.1f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.0f,%u,%u\n", w.t_s, w.fps, w.p99_ms, w.worst_ms, w.gpu_ms, w.replay_ms,
                        w.wait_ms, w.draws, w.new_pipelines, w.new_programs);
            fclose(c);
            remove(fcsv.c_str());
            rename((fcsv + ".tmp").c_str(), fcsv.c_str());
        }
    }
    fprintf(f, "\nWhat to look at: a phase whose parallelism is far under its threads waits on something other than the compiles; a "
               "large share under 1 ms means the caches held; many slow graphics pipelines in play mean the warm-up missed them "
               "(relink_pipelines_warm.txt); repeated keys among the slowest are worth a report.\n");
    fclose(f);
    remove(txt.c_str());
    rename((txt + ".tmp").c_str(), txt.c_str());
    written_items.store(its.size());
}

}  // namespace cprof
