# Phase 2 — spratforge redesign and the `sprat forge` entry point

This document records what changed in Phase 2, why, and the exact commands to
build, run and test it. The driving input is `docs/ANALYSIS.md` (Phase 1
audit); the output of this phase is consumed by Phase 3 (RingQueen
integration) through `docs/METADATA_SCHEMA.md`.

Nothing was deleted. The Phase 1 code paths (`generate`, `rig-validate`,
`audit`, the legacy skeleton/renderer/pose model) are untouched and still
build and pass their tests. Phase 2 adds a parallel `forge` subsystem.

---

## 1. What was wrong (Phase 1) and what replaces it

| # | Phase 1 defect | Phase 2 fix |
| --- | --- | --- |
| A | `skeleton.cpp` picked the *highest* pixel cluster per side, so arm joints landed inside the hair (e.g. `(227,125)`). | `forge_rig.cpp` finds the shoulder line as the row of maximum width-gain in the 12–52 % band, then locates hands with a chamfer distance transform restricted to the arm zone **above the waist** and scored by `thickness + 0.20·reach + 0.20·lateral`. Hands now land on the gloves. |
| B | `renderer.cpp::classify_region()` used fixed geometric slabs; the `Gloves` region resolved to **0 px** and was unreachable. | `forge_segment.cpp` assigns every silhouette pixel to the nearest **capsule** derived from the rig (normalised signed distance, so no pixel is ever unassigned). Gloves are explicit degenerate capsules at the hand joints and are non-empty by construction — asserted by `test_forge`. |
| C | Forward-mapped integer scatter tore the sprite apart (+52 % interior holes by jab frame 7); `drawOutline` always early-returned. | `forge_synth.cpp` renders with **backward (inverse) mapping**: for each destination pixel it inverts the per-part affine transform, samples the source nearest-neighbour, and writes only where the part's sample mask and the source alpha agree. No holes, no tearing, alpha stays binary. Parts are drawn with a painter's algorithm over a stable z-order, and each exclusive part mask is dilated by `max(1, round(0.03·body_height))` so joints stay filled when limbs rotate. |
| D | Pose amplitudes were authored in absolute pixels for 32×32 art (≤12 px), giving 3–14 unique frames out of 25 at 400 px. | `forge_motion.cpp` expresses every amplitude as a **fraction of body height** and every rotation in degrees. The same clip data drives a 48 px rig and a 400 px rig proportionally — asserted by `test_forge`. Clips are keyframed with anticipation, a clear active/impact frame, follow-through and per-key easing. |
| F | spratforge was never actually invoked by a developer-facing command. | New `spratforge forge` subcommand plus `sprat-cli/scripts/sprat forge`. |
| — | `generate` rejected anything larger than 256×256, so the 400×400 master could not be processed. | Caps raised to 4096×4096 / 65536 colours in `generation_profiles.{hpp,cpp}` and `profiles/rig/boxer_default.json`. The `forge` path additionally auto-sizes its own canvas and does not use those caps at all. |

Defect **E** (RingQueen playback mismatch) is deliberately out of scope — it is
Phase 3 work.

---

## 2. New code

All new files live under `apps/spratforge/`:

| File | Purpose |
| --- | --- |
| `include/spratforge/forge/forge_rig.hpp`, `src/forge/forge_rig.cpp` | 15-joint rig estimation from a single sprite, JSON (de)serialisation, rig override merging. |
| `include/spratforge/forge/forge_segment.hpp`, `src/forge/forge_segment.cpp` | Capsule-based body-part segmentation (12 parts) and the debug overlay renderer. |
| `include/spratforge/forge/forge_motion.hpp`, `src/forge/forge_motion.cpp` | Normalised pose specification, easing, keyframe sampling, two-bone IK, the nine boxing clips, ground clamping. |
| `include/spratforge/forge/forge_synth.hpp`, `src/forge/forge_synth.cpp` | Per-part affine transforms, shared canvas computation, backward-mapped rendering, nearest upscale, strip sheets. |
| `include/spratforge/forge/forge_pipeline.hpp`, `src/forge/forge_pipeline.cpp` | Orchestration: load → rig → segment → pose all frames → one shared canvas → render → write frames/sheets/rig/overlay/metadata. |
| `tests/test_forge.cpp` | Rig sanity (including the "no joints in the hair" regression), segmentation coverage, glove reachability, override behaviour, normalised motion scaling, frame distinctness. |
| `tests/test_forge_determinism.cpp` | Runs the pipeline twice into separate directories and compares every produced file byte for byte. |
| `tests/test_cli_forge.cpp` | End-to-end CLI run: output layout, metadata fields, attack/loop/hold flags, frame and sheet existence, argument rejection. |
| `docs/METADATA_SCHEMA.md` | Canonical, versioned description of `metadata.json` and `rig.json`. |

### Modified files

* `apps/spratforge/CMakeLists.txt` — five new sources in `spratforge_core`;
  three new tests registered; `test_cli_forge` added to the loop that injects
  `SPRATFORGE_CLI_PATH`.
* `include/spratforge/cli/cli_parser.hpp`, `src/cli/cli_parser.cpp` — new
  `Mode::forge`, `ForgeOptionsCli`, a dedicated `forge` argument parser
  (rejects unknown and duplicate flags), and the updated usage text.
* `src/main.cpp` — `run_forge_command()`, dispatched before the legacy modes.
* `include/spratforge/profiles/generation_profiles.hpp`,
  `src/profiles/generation_profiles.cpp`,
  `profiles/rig/boxer_default.json` — the 256 px / 256 colour caps raised.
* `sprat-cli/scripts/sprat` — new developer entry point (additive; no existing
  sprat-cli target, script or test was modified).

---

## 3. Design notes

**Rig convention.** `back` is the screen-left side, `front` the screen-right
side. The master sprite is front-facing, so punches are rendered as
foreshortening — the glove travels toward the frame centre while its scale
grows — rather than as a side-view arm extension.

**Canvas and anchor.** The pipeline runs in two passes. Pass 1 poses every
frame of every clip and unions the covered area; pass 2 renders into one
canvas sized to that union plus `--margin`. Every frame of every animation
therefore shares the same dimensions and the same ground-contact anchor, so a
consumer never has to re-align between clips.

**Ground clamping.** `knockdown` and `ko` set `Clip::ground_clamp`. After
posing, `clamp_to_ground()` translates the skeleton up if any joint (expanded
by its part radius) would fall below the rest ground line. Without it the
rotated lying body sank ~84 px below the floor; with it the final frames rest
exactly on the anchor line, and the shared canvas shrank from 269×267 to
269×204.

**Determinism.** No RNG, no clock, no filesystem iteration order dependency.
Z-ordering uses `std::stable_sort`, JSON is serialised by nlohmann in
alphabetical key order with `dump(2)`, and the written-file list is sorted.
`test_forge_determinism` enforces this.

---

## 4. Build, run and test

### Build

```bash
cd /home/ubuntu/game_dev/spratforge
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-local -j"$(nproc)"
```

### Test

```bash
ctest --test-dir build-local --output-on-failure -j"$(nproc)"
```

22 tests: the 19 pre-existing ones plus `test_forge`,
`test_forge_determinism` and `test_cli_forge`.

To run only the Phase 2 tests:

```bash
ctest --test-dir build-local --output-on-failure -R 'forge'
```

### Generate a character — spratforge directly

```bash
/home/ubuntu/game_dev/spratforge/build-local/apps/spratforge/spratforge_cli forge \
  --input /home/ubuntu/game_dev/RingQueen/assets/master_boxer.png \
  --out /home/ubuntu/game_dev/_forge_out/boxer \
  --character boxer
```

### Generate a character — via sprat-cli

```bash
/home/ubuntu/game_dev/sprat-cli/scripts/sprat forge \
  --input /home/ubuntu/game_dev/RingQueen/assets/master_boxer.png \
  --out /home/ubuntu/game_dev/_forge_out/boxer \
  --character boxer
```

`scripts/sprat` locates `spratforge_cli` (checking `SPRATFORGE_CLI`, then
`SPRATFORGE_ROOT`, then `../spratforge/<build dir>`), builds it once if it is
missing, and `exec`s it. Any other subcommand is delegated to the existing
sprat-cli tools, so `sprat pack`, `sprat spratpack`, `sprat spratgen` and
friends keep working.

### Options

| Flag | Default | Meaning |
| --- | --- | --- |
| `--input <png>` | required | Single neutral sprite. |
| `--out <dir>` | required | Output directory; created if missing, updated in place. |
| `--character <name>` | `boxer` | Written to `metadata.json`. |
| `--rig-override <json>` | – | Rig corrections; see `METADATA_SCHEMA.md`. |
| `--fps <1-240>` | per clip | Overrides the frame rate of every clip. |
| `--scale <1-16>` | `1` | Integer nearest-neighbour upscale of every frame. |
| `--alpha-threshold <1-255>` | `16` | Alpha value at or above which a pixel counts as opaque. |
| `--margin <0-256>` | `2` | Extra pixels around the unioned canvas. |
| `--no-sheets` | off | Skip the strip sheets. |
| `--no-debug` | off | Skip `rig_debug.png`. |

Exit codes: `0` success, `2` bad arguments, `5` pipeline failure (a JSON
diagnostics object is printed on stdout).

### Small-sprite check

```bash
build-local/apps/spratforge/spratforge_cli forge \
  --input apps/spratforge/tests/fixtures/neutral_boxer.png \
  --out /home/ubuntu/game_dev/_forge_out/neutral_small \
  --character neutral_small --scale 4
```

The 32×48 fixture produces the same nine clips and the same metadata shape,
confirming the normalised motion model scales both ways.

---

## 5. Verification performed

* Full suite green: 22/22 `ctest` tests.
* Pipeline run on the 400×400 master: 73 frames across 9 animations, frame
  size 269×204, anchor `(175, 191)`.
* Pipeline run on the 32×48 fixture at `--scale 4`: 73 frames, frame size
  260×220, anchor `(180, 192)`.
* Contact sheets and the rig debug overlay were rendered and visually
  inspected; joints sit on the gloves, shoulders and feet (not in the hair),
  the silhouette stays intact through every punch, and the knockdown/ko final
  frames rest on the ground line.
* Determinism verified by `test_forge_determinism` (byte-identical files over
  two consecutive runs).

Artefacts:

* `docs/spratforge_output_contact_sheet.png` (master, all 9 animations)
* `docs/spratforge_small_sprite_contact_sheet.png` (32×48 fixture)
* `docs/spratforge_rig_debug_overlay.png` (= `_forge_out/boxer/rig_debug.png`)
