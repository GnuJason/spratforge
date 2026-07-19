# spratforge

spratforge is a standalone pixel-art engine informed by spratgen and LibreSprite ideas. It will generate individual frames, profile-driven animation sequences, and sprite atlases while keeping the rendering pipeline deterministic.

spratgen is an external dependency (our fork of sprat-cli) resolved at configure time by CMake FetchContent. spratforge does not vendor or modify spratgen sources.

## Dependency

spratforge requires spratgen and fetches it automatically from Git during CMake configure.

The repository URL and revision are configurable cache variables:

```sh
cmake -S . -B build \
	-DSPRATGEN_GIT_REPOSITORY=https://github.com/GnuJason/sprat-cli.git \
	-DSPRATGEN_GIT_TAG=main
```

The default configuration uses the spratgen fork at `https://github.com/GnuJason/sprat-cli.git`, pinned to `main`. spratforge normalizes spratgen's reusable static `spratgen_core` target to `spratgen::spratgen`; the upstream `spratgen` target is a command-line executable.

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

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

If you need to override the default spratgen fetch source, pass `SPRATGEN_GIT_REPOSITORY` and `SPRATGEN_GIT_TAG` as shown above.

## License

spratforge is licensed under GPL-3.0-or-later. Confirm compatibility when updating the referenced spratgen fork revision.