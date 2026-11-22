# ProjectReLinkAMD: what this fork changes, and its options

The fork of pixl27/ProjectReLink for AMD Radeon cards (RDNA 2 to RDNA 4). Every change can be turned back with a line in
`relink_env.txt` beside the game, or reverted as its own commit.

## Frame generation shaders (0.7.9.3)

`gpu/vk/shaders/fg_flow.comp`, `fg_median.comp`, `fg_scale.comp`: the same pictures, bit for bit, for less GPU work (shared memory
for the block search, a min/max network for the median, separable filter weights). `tools/fg_bench` checks and times them.

## The start's shader preparation (0.7.9.3)

| Option | Default | What it does |
|---|---|---|
| `RX_VK_PREPARE_MB_PER_THREAD` | `768` | The start's workers (shader translation, the driver cache warm-up, the pipelines made before the game) are as many as the free RAM holds at this many MB each, from 2 up to every CPU thread but one. `0`: every CPU thread but one, whatever the RAM (as upstream). |
| `RX_VK_PREPARE_THREADS` | unset | That many workers, whatever the RAM (as upstream). |
| `RX_VK_WARM_PROBE_MS` | unset | The probe that decides whether the driver's cache lost the pipelines calls a pipeline cold over this many ms (upstream: always 12, measured on NVIDIA). Unset: 4 times the median measured on this PC right after its last whole warm-up (kept in `relink_pipelines_warm.txt`, second line `probe_ms`), and 12 when none was measured yet. `0`: no probe. |

The log says what each start did:

- `vk: start phase <name>: <MB> MB of RAM free` before each phase and `vk: start phase <name>: <s> s, <MB> MB of RAM free after it`
  after it, then `vk: start phases: <s> s in all`.
- `vk: <phase>: <n> threads (<cpus> CPU threads, <MB> MB of RAM free, <MB> MB a thread ...)`.
- `vk: the driver's cache measured warm: ... median <ms>` at the end of a whole warm-up, and `vk: the driver's cache probed: ...
  (<which limit>): warm` or `... warmed again` at the starts after it.
- `vk: AMD: drawing stages ..., mesh/task pipelines ..., fast sync ..., RAM ... MB free at the start`: one line, on AMD GPUs only.

To go back to upstream's start entirely: `RX_VK_PREPARE_MB_PER_THREAD=0` and `RX_VK_WARM_PROBE_MS=12`.

## The download: RelinkAMD.exe (0.7.9.5)

The one exe a release offers is `RelinkAMD.exe` (upstream: `ReLink-Setup.exe`). It is the same setup; inside the installed folder
its copy keeps the name `ReLink-Setup.exe`, which the builder's Repair runs. The launcher's updater looks for `RelinkAMD.exe` in
the latest release (`launcher/Updater.cs`, `SetupAsset`), so a fork release must carry it under this name.

## Brazilian Portuguese (0.7.9.6)

Everything the player reads is in Brazilian Portuguese when they choose it:

- The launcher: Game page, "Launcher language", now English, Français or Português (Brasil) (`launcher/Texts.cs`, each `L(fr, en, pt)`).
  Without a choice yet it follows Windows: Portuguese on a Windows in Portuguese. `RELINK_LANG=pt` for one run.
- The Insert menu, the start-up screen and the game's own message boxes follow the game's language (`language.txt`, which the
  launcher's "Game language" writes; `pt-BR` gives Portuguese). `RELINK_MENU_LANG=pt` or `en` overrides it for one run. The
  menu's English stays in the code; `gpu/vk/menu_lang.h` looks each drawn text up in `gpu/vk/menu_pt.inc` and draws the
  Portuguese, with the wide GDI calls so the accents show whatever the system's code page. A text missing from the table stays
  English: add a line `{"English as in the code", "Português"},` there.
- The setup (`RelinkAMD.exe`, `launcher/setup/setup.c`) on a Windows in Portuguese, and `recomplauncher.json`'s labels.

Not translated: the log (`relink_log.txt`), the build tools' own error lines, and the game's text itself (the game has its own
Portuguese: choose `pt-BR` as the game language).

## What to report

`relink_log.txt` from the game folder, with the card, the driver version and the `relink_env.txt` used.

## Releases from main (0.7.9.5)

A push to main whose `launcher/ReLink.csproj` `<Version>` has no tag `v<Version>` yet is a release: `windows.yml` builds it and
makes the release and its tag at that commit, with `RelinkAMD.exe` attached. A new version only needs its number raised in the
csproj; a tag pushed by hand (`v*`, lowercase) still works as upstream's.
