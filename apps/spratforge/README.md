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