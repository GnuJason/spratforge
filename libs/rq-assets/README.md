# rq-assets

RingQueen's versioned sprite contract and renderer-independent runtime loader.

## Contract v1.0

`schema/sprite.schema.json` is a JSON Schema Draft 7 document. Contract v1.0
retains Phase 2's integer `schema_version: 1`; the schema's `$id` identifies the
contract version, not the JSON Schema dialect. Unsupported versions and unknown
properties are rejected. The same schema accepts atlas metadata and manifests;
manifests additionally require all three asset references together. Runtime
loading requires a manifest, not standalone atlas metadata.

The schema defines animation names/ranges, frame indices/rectangles, integer
durations, frame-local pivots, pivot-relative hitboxes, palette colors, and
variants. Animation events use animation-local frame indices. Cross-field
constraints (range coverage, unique animation names, stable pivots, rectangle
placement, event bounds, and PNG consistency) are enforced by the loader.

## Runtime API

Link `rq::assets` and include `<rq/assets/sprite_asset.hpp>`:

```cpp
const auto boxer = rq::assets::SpriteAsset::load("assets/boxer/manifest.json");
const auto& frame = boxer.frame_at("idle", elapsed_ms);
```

`SpriteAsset` owns decoded RGBA pixels and typed animation, frame, palette,
event, and hitbox data. Getters return immutable views. `frame_at` uses exported
per-frame durations, loops looping animations, and clamps one-shots to their
last frame. Unknown animation names throw `std::out_of_range`; invalid assets
throw exceptions before an asset can be returned. Texture upload/drawing belongs
to a future rendering backend; no graphics API or generator is linked at runtime.

The loader validates both JSON documents with the embedded schema and requires
their shared fields to match exactly. It restricts references to regular files
in the same canonical directory, including symlink resolution. It checks image
dimensions before decoding, limits the atlas to 16,777,216 pixels and each axis
to 8192, checks binary alpha, transparent padding, nonempty frames, and exact
visible palette membership. JSON input is limited to 32 MiB and PNG input to
128 MiB. Rig and anchor references must exist; their internal data is checked by
Spratforge's build-time preflight, not interpreted by the runtime loader.

`rq_asset_validate <manifest.json>` exposes the runtime validation path to CI.
Exit status is 0 on success, 1 for invalid assets, and 2 for invalid arguments.

## Build Integration

From the repository root:

```sh
cmake -S . -B /tmp/ringqueen-build
cmake --build /tmp/ringqueen-build --target ringqueen-engine -j 2
cmake --build /tmp/ringqueen-build --target rq_validate_assets
ctest --test-dir /tmp/ringqueen-build --output-on-failure
```

The `spratforge_generate_assets` target invokes `spratforge_cli generate` using
the RingQueen export profile. Outputs live under the binary tree at
`libs/rq-assets/generated/neutral_boxer/`. The default source is the committed
neutral boxer fixture. Set `RQ_NEUTRAL_BOXER` to another neutral PNG and
`RQ_ASSET_PROFILE_DIR` to a compatible profile tree to customize build inputs.
Export-profile filenames are honored. Changes to source, profiles, schema,
generator, validator, or build script regenerate the assets.

Generation uses a staging directory, then runs `rig-validate`, `audit`, and
`rq_asset_validate` before publishing. A validation failure fails the build and
leaves previously published assets intact. Missing generated outputs trigger
regeneration; missing/invalid sources fail. `ringqueen-engine` depends on
`rq_validate_assets`, which reruns preflight on every engine build even when
generation is up to date, so corrupted outputs cannot silently pass a no-op
build. `rq_validate_assets` is also the explicit CI preflight target.

Valijson 1.0.2 (header-only, BSD-2-Clause) provides standards-based validation.
CMake fetches its SHA-256-pinned archive on first configuration; no runtime
downloads occur. Offline builds must prepopulate the source and configure with
`-DFETCHCONTENT_SOURCE_DIR_VALIJSON=/path/to/valijson-1.0.2` (the directory must
contain `include/valijson`). JSON and PNG decoding reuse the repository's bundled
nlohmann and stb headers, without linking the sprite generator. The schema is
embedded at build time, so runtime deployment needs no source-tree/schema path.

## Tests and Goldens

- `test_sprite_schema`: positive atlas/manifest and malformed schema inputs.
- `test_ringqueen_export`: real CLI generation from the neutral boxer, golden
	PNG/JSON comparisons, all seven animation bindings, variable timing, loops,
	events, hitboxes, variants, invalid metadata/PNG, and deterministic reruns.
- `test_ringqueen_build`: no-op builds, corrupted output failure, missing atlas
	regeneration, and rejection of missing/invalid sources without replacing good
	published assets. This test runs serially because it exercises the build tree.
- `test_ringqueen_load`: the engine entry point loads generated assets.

Golden fixtures are under `apps/spratforge/tests/fixtures`. They are frozen
regression baselines, not proof of animation/artistic quality. After intentional
contract/rendering changes, explicitly run `test_ringqueen_export --update-fixtures`
and review both the images and JSON diff. Normal tests never update goldens.