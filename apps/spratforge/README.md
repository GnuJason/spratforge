# spratforge

spratforge is the pixel-art application in the Ringqueen monorepo, informed by spratgen and LibreSprite ideas. It generates individual frames, profile-driven animation sequences, and sprite atlases while keeping the rendering pipeline deterministic.

spratforge consumes the local `apps/spratgen` subproject. It does not vendor or modify spratgen sources.

## Dependencies

spratgen belongs at `apps/spratgen` as a source checkout or submodule. The top-level CMake project configures spratgen before spratforge and normalizes its reusable `spratgen_core`, `spratgen_static`, `spratcore`, or `spratgen` target to `spratgen::spratgen`.

`export.hpp` is not part of spratforge: it must be provided through spratgen's public include directories. The profile and template loaders also use `json/json.hpp`; that header is not part of spratforge either. CMake prefers an installed jsoncpp package, whose exported include directories must contain `json/json.hpp`. When jsoncpp is unavailable, it first uses a `json/json.hpp` path exported by spratgen, then falls back to spratgen's Jsonnet third-party path at `${jsonnet_SOURCE_DIR}/third_party/json/json.hpp`.

During configuration, `scripts/check_spratgen_exports.cmake` verifies that spratgen exports `jsonnet_SOURCE_DIR`, that its target exports an include path containing `export.hpp`, and that the Jsonnet JSON header exists. Configuration stops with a direct diagnostic when one of those contracts is missing. This check is intended for CI as well as local builds.

### Offline Builds

The monorepo is offline-first: populate `apps/spratgen` locally, then configure from the repository root:

```sh
cmake -S . -B build
```

This uses `add_subdirectory` and does not fetch from GitHub. The local spratgen project must export one of `spratgen_core`, `spratgen_static`, `spratcore`, or `spratgen`, set `jsonnet_SOURCE_DIR`, and expose `export.hpp` through the selected target's public include directories.

## Roadmap

1. Establish the CLI, module boundaries, example profile, and build/test skeleton.
2. Load and validate JSON animation profiles.
3. Render deterministic single frames and profile sequences.
4. Apply NES, Game Boy, and dithering palette modes.
5. Pack rendered frames into sprite atlases with metadata.
6. Add deterministic AI motion quantization and golden-frame regression coverage.

## CLI

```sh
spratforge_cli --mode single --input source.png --grid 16x16 --out frame_000.png
spratforge_cli --mode profile --input source.png --grid 16x16 --profile idle_6 --out out_dir
spratforge_cli --mode atlas --atlas 5x5 --out atlas.png
spratforge_cli --mode ai-motion --input source.png --grid 16x16 --motion 2,-1 --out motion.png
```

Rendering modes load an RGBA PNG, apply deterministic nearest-neighbor grid downsampling, optionally quantize to `nes`, `gb`, `strict`, or `dither` palettes, and write PNG output through spratgen.

## Profile-driven generation (Phase 2)

The new commands use the Phase 1 rig renderer with versioned JSON profiles. The
existing `--mode` commands and `--turnkey` retain their previous behavior and
metadata formats.

```sh
spratforge_cli generate --input boxer.png --out generated/boxer
spratforge_cli generate --input boxer.png --out generated/blue --variant blue
spratforge_cli rig-validate --rig generated/boxer/rig.json --out rig-report.json
spratforge_cli audit --input boxer.png
spratforge_cli audit --input generated/boxer --out audit-report.json
```

`generate` requires a nonexistent or empty output directory. It writes a global
atlas, atlas metadata, manifest, original-coordinate rig, aligned anchor,
individual animation frames, and per-animation sheets. The shipped motion set
contains **45 frames** across `block`, `hit`, `idle`, `jab`, `ko`, `specials`, and
`walk`, ordered by animation name. These are editable example motions, not a
claim of production-ready animation for every silhouette.

All three commands print a JSON object with `valid` and `diagnostics` to stdout.
Diagnostics contain `code`, `path`, and `message`. Exit status is `0` for success,
`2` for invalid command arguments, and `5` for validation or execution failure.
Validators optionally write the same report using `--out`, refusing to overwrite
existing files. `--help` displays command syntax. Unknown and duplicate options
are rejected.

### Profile selection

`generate` accepts `--rig-profile`, `--motion-dir`, `--palette-profile`,
`--export-profile`, `--variant`, `--rig`, and `--rig-override`. Paths select JSON
files except `--motion-dir`, which loads all `.json` motion files in a directory.
Defaults come from this application's `profiles/` directory. An explicit `--rig`
loads a saved rig instead of inferring one. Profile overrides are applied first,
then `--rig-override`; unlike legacy turnkey, generation does not auto-load
source-adjacent sidecars.

`rig-validate` accepts `--rig-profile` and `--rig-override`. `audit` accepts
`--rig-profile` and `--palette-profile`; `--rig` is available for source-image
audits, and `--export-profile` for output-directory audits. Supply the same custom
profiles used during generation when auditing its output.

Each profile requires integer `profile_version: 1`; unknown fields, invalid
types, unsafe export filenames, and unsupported versions are rejected.

| Profile | Default | Contract |
| --- | --- | --- |
| Rig | `profiles/rig/boxer_default.json` | Source dimension/color limits, transparency requirement, minimum joint confidence (0..1000), required nonempty regions, and rig overrides. |
| Motion | `profiles/motion/*.json` | Name, frame count (1..1000), FPS (1..1000), loop flag, integer keyframes, optional durations, events, and hitboxes. |
| Palette | `profiles/palette/boxer_default.json` | RGB source constraints, immutable color locks, region roles, ramps, variants, and animation styles. |
| Export | `profiles/export/ringqueen.json` | Version 1 global grid, column count, padding, PNG/JSON filenames, and frame/sheet output toggles. |

The sample `profiles/rig/boxer_override.json` is a raw rig override for
`--rig-override`, not a versioned rig profile. Rig override structure remains the
Phase 1 structure (`joints`, `bones`, `regions`, `pivot`).

Motion keyframes use integer times from `0` to `1000`, including both endpoints.
Transforms name joints and optionally set `dx`, `dy`, `rotation`, `scale_x`, and
`scale_y`. Rotations use 15-degree increments; scales are integer percentages
from 50 to 200. Interpolation supports `linear`, `ease_in`, `ease_out`,
`ease_in_out`, and `smoothstep`. `durations_ms`, when present, supplies one
positive integer duration per frame and overrides FPS-derived timing. Otherwise,
durations use consecutive differences of `floor(frame * 1000 / fps)`.
Events use animation-local zero-based `frame` and `name`. Optional authored
hitboxes use `frame`, `x`, `y`, `w`, `h`, and `kind`.

Palette colors are RGB triples. Empty `allowed_colors` preserves unrestricted
source RGB; a nonempty list constrains visible source colors. `roles` maps role
names to rig regions, `ramps` defines base colors, and `variants` maps variant
names to replacement role ramps of equal length. Alternatively, `variants_file`
loads a sibling versioned variants file. A selected variant maps each region's
colors through its nearest base-ramp color. Locked source colors remain unchanged
through variants and styles. `animation_styles` can enable `desaturate` or set
an RGB `flash` for a whole animation; flash takes precedence. Output auditing
allows source colors plus declared variant/style colors. The default variant is
`default`; the supplied `blue` variant uses blue gloves and green trunks.

### Versioned output metadata

The Phase 2 atlas and manifest use `schema_version: 1`, `format: "ringqueen"`,
`image`, `size`, grid dimensions/padding, `variant`, and a palette name plus the
actual visible RGB set. Each frame has an index, atlas rectangle, frame-local
`pivot`, and authoritative `duration_ms`, with optional `hitboxes`. Each animation
has `name`, `first_frame`, `frame_count`, `fps`, `loop`, and local-frame `events`.
Ranges are contiguous and cover all frames; the end index is exclusive
(`first_frame + frame_count`). All frames share one canvas size and pivot.
Hitboxes are authored coordinates relative to that pivot, as declared by
`hitbox_space: "pivot_relative"`; they are not inferred from pixels or transformed
automatically. The manifest also references the atlas JSON, rig, and anchor.

Source audits check dimensions, binary alpha, visible content, transparency, and
palette constraints, optionally comparing a supplied rig. Output audits check
the global PNG and metadata, ranges, timing, pivots, palette, empty padding,
manifest references, rig validity, and aligned anchor silhouette. They do not
re-render animations or verify optional sheets/individual-frame files. Generation
is bounded to 10,000 frames and 16,777,216 aggregate aligned frame pixels; the
global atlas has the same pixel budget and an 8192-pixel dimension limit.
I/O failures can leave partial output; use a fresh directory for retries.

This is the exporter-side contract only. The shared RingQueen schema, runtime
consumer, and asset-build integration belong to Phase 3 and are not implemented
here. No Phase 3 compatibility claim is made by the `ringqueen` format label.

## AI motion

AI motion mode applies a pixel-aligned translation described by `--motion x,y`, where both components are signed integers:

```sh
spratforge_cli --mode ai-motion --input source.png --grid 16x16 --motion 2,-1 --out motion.png
```

Motion components are clamped independently to `-8` through `8`, with no floating-point arithmetic or randomness. Multi-frame motion sequences retain frame zero and apply the clamped vector multiplied by the frame index, clamping every resulting displacement to the same range. Pixels translated outside the frame become transparent, so identical input and arguments always produce identical output.

## Animation profiles

Profile files live in `profiles/` and are loaded by name with `--profile`; for example, `--profile idle_6` loads `profiles/idle_6.json`.

```json
{
	"name": "idle_6",
	"frames": 6,
	"interpolation": "ease_in_out",
	"motion": "idle",
	"palette": "nes"
}
```

`frames` must be at least one. Interpolation accepts `none`, `linear`, or `ease_in_out`; profile interpolation uses fixed integer arithmetic between the first and final frame. Motion accepts `none`, `subtle`, `idle`, `walk`, or `jab` and applies deterministic pixel translations to the generated frames. `palette` is optional and, when set, overrides the CLI `--palette` value for that profile.

`profiles/pipeline_default.json` is an optional reference configuration for the turnkey pipeline's conventional template directory, source palette, and atlas padding. The current pipeline intentionally does not load this file: its runtime defaults are defined by `PipelineConfig` and `SPRATFORGE_TEMPLATE_DIR`, keeping the `--turnkey` path free of profile-file I/O. It is retained as a documented preset for tooling or future explicit configuration support.

## Atlas packing

Atlas mode renders a profile's frames, packs them left-to-right then top-to-bottom, and writes both the requested PNG and an `atlas.json` file beside it.

```sh
spratforge_cli --mode atlas --input source.png --grid 16x16 --profile idle_6 \
	--atlas 3x2 --padding 1 --out output/atlas.png
```

`--atlas` takes positive `columnsxrows` dimensions. The grid must have at least as many slots as the profile frame count; otherwise atlas generation fails. `--padding` is an optional non-negative pixel count (default `0`) placed between adjacent frames and left transparent.

`output/atlas.json` records the resolved layout:

```json
{
	"frames": [
		{ "index": 0, "x": 0, "y": 0, "w": 16, "h": 16 }
	],
	"columns": 3,
	"rows": 2,
	"padding": 1
}
```

## Palettes

Palette files live in `palettes/` and use the GIMP `.gpl` format. `nes` and `gb` quantize RGB values to their respective fixed palettes, while `strict` preserves source RGB unchanged. `dither` applies a deterministic 4x4 Bayer brightness threshold before NES quantization. Profile and AI-motion frame batches enforce a shared nearest palette color at each visible pixel position to avoid palette flicker.

```sh
spratforge_cli --mode single --input source.png --grid 16x16 --palette gb --out frame.png
spratforge_cli --mode profile --input source.png --profile idle_6 --dither --out out_dir
```

## Build and test

Run these commands from the Ringqueen repository root:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The equivalent helper scripts are `scripts/build.sh` and `scripts/test.sh`.

## License

spratforge is licensed under GPL-3.0-or-later. Confirm compatibility when updating the referenced spratgen fork revision.