# fg_bench

Checks the frame generation and upscale compute shaders (`gpu/vk/shaders/fg_flow`, `fg_median`, `fg_scale`) against a git
ref, on synthetic inputs (no game data):

- `./run.sh [ref] run`: both versions run on the Vulkan device the loader picks; their outputs must be **identical**, and
  the best of 5 dispatch times is printed (GPU timestamps). On a real Radeon this is the number to look at.
- `./run.sh [ref] stats`: the driver's statistics for each pipeline (`VK_KHR_pipeline_executable_properties`).

Without a GPU, Mesa gives both: lavapipe runs the shaders on the CPU (correctness; times are only a CPU's), and RADV with
`RADV_FORCE_FAMILY` compiles them with ACO, AMD's open compiler, for a chosen chip with no hardware at all:

```
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json tools/fg_bench/run.sh main run
RADV_FORCE_FAMILY=navi21 VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json tools/fg_bench/run.sh main stats   # RDNA 2
RADV_FORCE_FAMILY=navi31 ...   # RDNA 3      RADV_FORCE_FAMILY=gfx1201 ...   # RDNA 4
```

ACO's statistics are not those of AMD's Windows driver, which has its own compiler; its latency figures are static
estimates. Times on the card itself are what count.

Needs `glslangValidator`, a C++17 compiler, the Vulkan loader and headers, Python 3 with numpy.
