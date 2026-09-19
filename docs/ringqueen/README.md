# Ringqueen Documentation

## Phase 3 Implementation Report

Phase 3 connects Spratforge's existing `generate` output to a shared RingQueen
sprite schema, independent runtime loader, and validating asset build. Phase 1
rig/renderer internals and Phase 2 implementation modules remain unchanged;
Spratforge changes are limited to test registration, new end-to-end fixtures and
tests, and integration documentation. Legacy CLI modes and metadata are unchanged.

### Files Added

- [Sprite schema](../../libs/rq-assets/schema/sprite.schema.json): Draft 7 JSON Schema, contract v1.0.
- [Runtime API](../../libs/rq-assets/include/rq/assets/sprite_asset.hpp): immutable sprite data and timed animation lookup.
- [Runtime loader](../../libs/rq-assets/src/sprite_asset.cpp): schema, metadata, reference, and PNG validation plus typed binding.
- [Embedded schema template](../../libs/rq-assets/src/sprite_schema.hpp.in): removes source-path dependencies at runtime.
- [Runtime validator CLI](../../libs/rq-assets/src/validate_main.cpp): consumer-side validation for builds and CI.
- [Asset target wiring](../../libs/rq-assets/cmake/assets.cmake): generation dependencies and always-run validation.
- [Generation script](../../libs/rq-assets/cmake/generate_assets.cmake): staging, preflight, and publication.
- [Engine entry point](../../apps/ringqueen-engine/src/main.cpp): loads generated assets and binds idle playback.
- [Schema tests](../../libs/rq-assets/tests/test_sprite_schema.cpp).
- [Build enforcement tests](../../libs/rq-assets/tests/test_asset_build.cmake).
- [End-to-end tests](../../apps/spratforge/tests/test_ringqueen_export.cpp).
- [Neutral boxer PNG](../../apps/spratforge/tests/fixtures/neutral_boxer.png).
- [Expected atlas PNG](../../apps/spratforge/tests/fixtures/expected_atlas.png).
- [Expected atlas metadata](../../apps/spratforge/tests/fixtures/expected_atlas.json).
- [Expected manifest](../../apps/spratforge/tests/fixtures/expected_manifest.json).

### Files Modified

- [Root CMake](../../CMakeLists.txt): configure the consumer before the engine and include asset targets.
- [rq-assets CMake](../../libs/rq-assets/CMakeLists.txt): runtime library, pinned validator dependency, validator executable, and schema test.
- [Engine CMake](../../apps/ringqueen-engine/CMakeLists.txt): executable, mandatory validation dependency, and runtime smoke test.
- [Spratforge CMake](../../apps/spratforge/CMakeLists.txt): end-to-end test registration only.
- [rq-assets README](../../libs/rq-assets/README.md): contract, API, builds, dependency, and tests.
- [Engine README](../../apps/ringqueen-engine/README.md): current runtime scope and invocation.
- [Spratforge README](../../apps/spratforge/README.md): Phase 3 integration and offline dependency notes.
- [This report](README.md).

### Tests and Verification

Four new CTest tests were added:

| Test | Coverage |
| --- | --- |
| `test_sprite_schema` | Valid atlas/manifest; missing/unknown fields, unsupported version, wrong types, unsafe references, durations, colors, hitboxes, malformed JSON. |
| `test_ringqueen_export` | Real CLI neutral PNG to 7 animations/45 frames to runtime load; golden output; named animations, timing boundaries, loop/clamp behavior, events, hitboxes, variants; invalid ranges/pivots/palette/PNG/references. |
| `test_ringqueen_build` | Incremental builds do not regenerate; corrupt manifests fail engine builds; missing atlases regenerate identically; invalid/missing sources fail without replacing valid output. |
| `test_ringqueen_load` | The engine entry point loads build-generated assets and resolves idle. |

- Phase 3 tests: **4/4 passed**.
- Full Debug suite: **19/19 passed**.
- Full ASAN + UBSAN suite: **19/19 passed**, including instrumented build-time
	generation and CLI subprocesses. Options: `detect_leaks=1:halt_on_error=1` and
	`UBSAN_OPTIONS=halt_on_error=1`.
- Sanitizer compilation used `-fsanitize=address,undefined -fno-omit-frame-pointer`.
- Offline Valijson source override was exercised by the sanitizer build.
- No new editor diagnostics or whitespace errors were reported.

### Behavioral Changes

`ringqueen-engine` is now a buildable headless consumer and depends on generated,
validated assets. `spratforge_generate_assets` creates outputs under the binary
tree's `libs/rq-assets/generated/neutral_boxer`. `rq_validate_assets` runs
Spratforge rig validation/audit and consumer schema/runtime validation on every
engine build, including no-op builds, and is available as the CI preflight target.
Generation stages output and publishes only after validation passes. Missing
generated files are regenerated; invalid or missing inputs fail the build.

Contract v1.0 preserves `schema_version: 1` and all existing Phase 2 output fields.
Runtime validation rejects unsupported versions and incompatible metadata. The
runtime exposes RGBA pixels and typed frames/animations without linking
Spratforge or spratgen rendering libraries. Runtime deployment accepts an explicit
manifest path; schemas are embedded, and metadata uses relative asset references.

### Determinism

The end-to-end test compares every generated file byte-for-byte across two real
CLI invocations. The atlas PNG and both metadata documents also match committed
goldens. The build test deletes the generated atlas, rebuilds, and checks atlas
and manifest SHA-256 equality. The same golden comparisons pass under sanitizers.

### Plan Details and Limits

- Required schema, consumer, build target/dependency, golden fixtures, and tests
	are implemented. Small supporting headers, CMake scripts, and a validator CLI
	were added to make the specified integration testable and deployable.
- JSON Schema validation uses Valijson 1.0.2, with its archive SHA-256 pinned.
	This adds a first-configure download unless an offline source directory is
	supplied. No network access is required at runtime.
- The former engine placeholder becomes a headless asset-binding entry point,
	not a gameplay or graphics implementation. GPU upload/windowing remain outside
	Phase 3. Golden fixtures establish regression behavior, not artistic quality.
- Rig/anchor internals remain the responsibility of build-time Spratforge
	preflight; the runtime checks their references but does not interpret rigs.
- CI validation is provided as a CMake target and CTest coverage; no hosted CI
	workflow was invented because none exists in this repository.

Implementation is complete and stops at Phase 3. No commit, push, or merge was
performed for this phase; approval is required before merging.