#!/usr/bin/env bash
# Frame generation shaders, before (the git ref given, default main) and after (the working tree): same output, bit for bit,
# and their time on the Vulkan device the loader picks (VK_ICD_FILENAMES chooses one: a real Radeon with RADV, or lavapipe).
# With RADV_FORCE_FAMILY=navi21|navi31|gfx1201 and RADV's ICD, "stats" prints ACO's ISA statistics with no GPU at all.
# Needs: glslangValidator, a C++17 compiler, the Vulkan loader and headers, Python 3 with numpy.
set -euo pipefail
cd "$(dirname "$0")"
REF=${1:-main}
MODE=${2:-run}
ROOT=$(git rev-parse --show-toplevel)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
c++ -O2 -std=c++17 harness.cpp -lvulkan -o "$W/harness"
python3 -I make_inputs.py "$W/data" >/dev/null
for v in before after; do mkdir -p "$W/$v"; done
for f in fg_flow fg_median fg_scale; do
    git -C "$ROOT" show "$REF:gpu/vk/shaders/$f.comp" > "$W/before/$f.comp"
    cp "$ROOT/gpu/vk/shaders/$f.comp" "$W/after/$f.comp"
    for v in before after; do glslangValidator -V --target-env vulkan1.2 "$W/$v/$f.comp" -o "$W/$v/$f.spv" >/dev/null; done
done
H="$W/harness"; D="$W/data"
one() {   # name, shader, then the harness's arguments with OUT standing for the output file
    local name=$1 sh=$2 key; shift 2
    key=${name// /_}
    for v in before after; do
        local args=("${@//OUT/$W/$v.$key.out}")
        printf '%-28s %-6s ' "$name" "$v"
        if [ "$MODE" = stats ]; then
            "$H" "$W/$v/$sh.spv" stats "${args[@]}" 2>/dev/null | grep -E '^  (VGPRs|LDS size|Subgroups per SIMD|Instructions|Latency|Inverse Throughput)=' | tr -d ' ' | tr '\n' ' '; echo
        else
            "$H" "$W/$v/$sh.spv" run iters=5 "${args[@]}" 2>/dev/null | grep -o 'dispatch_ms_best=[0-9.]*'
        fi
    done
    if [ "$MODE" != stats ]; then cmp -s "$W/before.$key.out" "$W/after.$key.out" && echo "  identical" || echo "  DIFFERENT"; fi
}
for r in "1080p 480 270" "1440p 640 360" "2160p 960 540"; do
    set -- $r
    for R in 8 16; do
        one "flow $1 radius $R" fg_flow spec=$R "in:$2x$3:r16f:n:$D/la_$1.bin" "in:$2x$3:r16f:n:$D/lb_$1.bin" "out:$((($2 + 3) / 4))x$((($3 + 3) / 4)):rgba16f:OUT"
        [ "$MODE" = stats ] && break 2
    done
done
one "median 160x90" fg_median "in:160x90:rgba16f:n:$D/flow.bin" "out:160x90:rgba16f:OUT"
one "scale catmull 720p>1440p" fg_scale "pc=$(cat "$D/pc1.hex")" "in:1280x720:rgba8:l:$D/frame720.bin" "out:2560x1440:rgba8:OUT"
one "scale lanczos 720p>1440p" fg_scale "pc=$(cat "$D/pc2.hex")" "in:1280x720:rgba8:l:$D/frame720.bin" "out:2560x1440:rgba8:OUT"
