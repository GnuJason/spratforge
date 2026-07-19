# spratforge

spratforge is a standalone pixel-art engine informed by spratgen and LibreSprite ideas. It will generate individual frames, profile-driven animation sequences, and sprite atlases while keeping the rendering pipeline deterministic.

spratgen is an external dependency (our fork of sprat-cli) resolved at configure time by CMake FetchContent. spratforge does not vendor or modify spratgen sources.

## Dependency

spratforge requires spratgen and fetches it automatically from Git during CMake configure.

The repository URL and revision are configurable cache variables:

```sh
cmake -S . -B build \
	-DSPRATGEN_GIT_REPOSITORY=https://github.com/your-org/spratgen.git \
	-DSPRATGEN_GIT_TAG=main
```

Set these values to your spratgen fork URL and desired branch/tag/commit.

## Roadmap

1. Establish the CLI, module boundaries, example profile, and build/test skeleton.
2. Load and validate JSON animation profiles.
3. Render deterministic single frames and profile sequences.
4. Apply NES, Game Boy, and dithering palette modes.
5. Pack rendered frames into sprite atlases with metadata.
6. Add deterministic AI motion quantization and golden-frame regression coverage.

## CLI

```sh
spratforge_cli --mode single --out frame_000.png
spratforge_cli --mode profile --profile idle_6 --out out_dir
spratforge_cli --mode atlas --atlas 5x5 --out atlas.png
```

Phase 1 validates arguments and dispatches to module stubs. It does not write rendered image files yet.

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

If you need to override the default spratgen fetch source, pass `SPRATGEN_GIT_REPOSITORY` and `SPRATGEN_GIT_TAG` as shown above.

## License

spratforge is licensed under GPL-3.0-or-later. Confirm compatibility when updating the referenced spratgen fork revision.