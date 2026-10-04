# Phase 4A — spratforge / sprat-cli changes

Scope of this phase: finish the outstanding spratforge correctness and ergonomics
work, raise output quality, collapse the triplicated sprite engine down to one
implementation, and ship a reproducible release binary.

No gameplay work was done in `RingQueen` in this phase; the only change there is a
single build-keeping flag (see §8).

---

## 1. Input size handling

**Before.** The forge path assumed the neutral input was effectively the historical
fixture size. Rig landmarks and clip templates were expressed against hard-coded
pixel extents, so a 400x400 master (the real `RingQueen/assets/master_boxer.png`)
was rigged against the wrong reference frame.

**After.** The rig and the pose templates are derived from the *measured* bounds of
the opaque pixels in the input, not from constants. Accepted input range is
**32 x 32 to 2048 x 2048**, square or not; anything outside that range is rejected
with a clear message and a non-zero exit. All joint positions, limb lengths and
clip offsets are now expressed as fractions of the input bounding box and scaled on
load, so a 400x400 input and a 64x96 input rig identically in normalized space.

**Test.** `tests/test_forge_input_sizes.cpp` forges synthetic inputs at several
sizes including a **400 x 400** case and a deliberately non-square case, and asserts
that rigging succeeds, that the rig stays inside the input bounds, and that the
output canvas scales with the input rather than being clamped to a fixed size.

---

## 2. Output directory policy

**Before.** `forge` wrote into whatever directory it was given, silently mixing new
output with whatever was already there.

**After.** New module `core/output_dir.{hpp,cpp}` implements three policies:

| Policy | Flag | Behaviour on a non-empty outdir |
| --- | --- | --- |
| `Refuse` | *(default)* | Abort with a clear message, exit code **5** |
| `Force` | `--force` | Write over the directory, leaving foreign files alone |
| `Clean` | `--clean` | Delete **only** files spratforge previously wrote, then write |

`--clean` is safe because every successful run writes a manifest,
**`.spratforge-manifest.json`**, listing exactly the relative paths it created.
`--clean` reads that manifest and removes only those paths. If the manifest is
absent (a directory that predates this feature, or one spratforge never wrote),
`--clean` **refuses** rather than guessing — the operator must opt in once with
`--force`, after which the manifest exists and `--clean` works from then on.

**Tests.** `tests/test_output_policy.cpp` covers: fresh directory succeeds; non-empty
directory refused by default with exit 5; `--force` overwrites; `--clean` removes the
previously written files; `--clean` leaves a foreign file untouched; `--clean` on a
manifest-less directory refuses.

---

## 3. Knockdown animation

`knockdown` is now generated on **all three** code paths and is distinct from `ko`:

| Path | knockdown | ko |
| --- | --- | --- |
| `forge` (13 clips) | 10 frames @ 12 fps, `hold_last_frame`, non-attack | 14 frames @ 12 fps |
| `generate` (8 motion profiles, 51 frames) | first_frame 21, 6 frames, `loop: false` | 8 frames, `loop: false` |
| `--turnkey` (11 animations) | 25 frames, 17 distinct, 10 fps | 25 frames, 9 distinct, 6 fps |

The rendered sheets differ byte-wise (`knockdown.png != ko.png`), and the metadata
flags differ as described above. spratgen's `PoseModel` already carried a
`"knockdown"` template, so this required only the two profile JSONs
(`profiles/templates/knockdown.json`, `profiles/motion/knockdown.json`) plus the
forge clip definition — no new pose code.

**Tests.** `test_templates` asserts 11 templates and that knockdown exists and is
distinct from ko; `test_cli_forge`, `test_cli_generate`, `test_pipeline` and
`test_ringqueen_export` assert the updated counts and the knockdown/ko distinction.
The `test_ringqueen_export` golden fixtures (`expected_atlas.json`,
`expected_atlas.png`, `expected_manifest.json`) were regenerated with
`--update-fixtures`; the input fixture `neutral_boxer.png` is unchanged, which
confirms the generator itself is still deterministic.

---

## 4. `pose_renderer` double transform — root cause and fix

**Location.** `apps/spratforge/src/motion/pose_renderer.cpp`, the per-layer loop in
`render_pose` (ANALYSIS.md issue 23).

**Root cause.** Each layer was transformed **twice**:

1. A backward, inverse-mapped affine pass applied **only `layer.linear`** (rotation,
   scale, shear) and wrote the layer *unshifted* into a scratch
   `spratgen::Image` / `Silhouette`.
2. A skeleton was then rebuilt **from that already-warped silhouette**, every joint
   offset by `layer.shift`, and handed to `spratgen::PixelRenderer::renderFrame(...)`,
   whose `drawBody` / `drawOutline` **forward-scatter** each pixel through
   `transform_pixel()` — a second transform pass applying the translation.

Why that is a defect regardless of the pixels it happens to produce:

- A forward scatter can **tear** (destination pixels no backward map lands on) and
  **clips silently** via `putPixel`.
- `drawBody` substitutes `makeBoxerPalette()` colours for masked pixels whose scratch
  colour has alpha 0 — i.e. it can **invent colours that are not in the input**.
- It rebuilt a skeleton and allocated two canvas-sized buffers **per layer, per frame**.

**Honest verification result.** Because `shifted()` offsets all six joints uniformly,
`transform_pixel`'s per-region delta collapses to the *same* integer translation for
every region, so the second pass degenerates to an exact translation on the inputs we
can actually reach. A digest harness compared old vs new output across 8 default clips
x all frames on both the 32x48 fixture and the 400x400 master: **byte-identical, zero
differences**. `validate_rig` ("Region union must equal source silhouette") makes the
palette-substitution branch unreachable.

So this fix is **behaviour-preserving on reachable inputs**. It is worth doing because
it removes a latent forward-scatter / clipping / colour-invention hazard and because it
is **3.8x faster**: the master pose battery went from **7.909 s to 2.094 s**.
`PixelRenderer::applyPose` is now a no-op.

**New implementation.** A `layer_index_of` lambda plus a single `layer_mask` buffer;
one backward pass over the **shifted** destination rect
(`unit * (x - shift.x) - matrix.tx`); a forward gap-fill via `destination(layer,x,y)`;
composites straight into `result.frame`. `copy_pixel` bounds-checks and skips
transparent source pixels.

**Regression test.** `tests/test_pose_renderer.cpp` asserts (a) *rigid-translation
invariance* — pixel count, relative positions and colours are preserved across four
offsets — and (b) *no invented colour* — every output colour exists in the input, over
angles 0/15/45/90 crossed with stretch 100/150/200.

---

## 5. CLI ergonomics

`spratforge` is now subcommand-driven, with a `sprat forge ...` front end in
`sprat-cli`. Full reference: [`docs/CLI.md`](CLI.md).

- Subcommands: `forge`, `generate`, `list-animations`, `help`, `version`.
- `--version` prints `spratforge 2.1.0`.
- Per-subcommand `--help`, each with a worked **Examples:** block.
- `--list-animations` prints the available clip names for the active profile.
- `--only <a,b,c>` renders a subset; an unknown name is a hard error (exit 5) listing
  the valid names rather than silently rendering nothing.
- `--seed <n>` pins any stochastic component (output is deterministic regardless).
- `--quiet` / `--verbose` control logging; `--quiet` still prints errors.
- Sensible defaults for `--out`, fps and scale, so a bare
  `spratforge forge --input x.png --out dir` works.
- Clear error text and **non-zero exits**: `2` for usage/argument errors, `5` for
  runtime refusals (non-empty outdir, unknown animation, bad input size).

Observed exit codes: fresh run `0`, refuse `5`, `--clean` `0`, `--force` `0`,
unknown `--only` `5`, missing `--input` `2`.

---

## 6. Output quality

Measured over the full default clip set, **before** (`/tmp/p4_before`, 73 frames) vs
**after** (`/tmp/p4_after`, 105 frames):

| Metric | Before | After |
| --- | --- | --- |
| Isolated speckles (total) | 3 | **0** |
| Pinholes (transparent px with >= 7 opaque neighbours) | 85 (1.164 / frame) | **5 (0.048 / frame, -96%)** |
| Thin gaps (transparent px with >= 5 opaque neighbours) per frame | 77.04 | **61.48 (-20%)** |
| Connected components per frame | 1.00 | 1.00 |
| Alpha levels present | {0, 255} | **{0, 255}** (no halos / fringing) |
| Distinct colours | 1920 | 1920, **0 foreign pixels** vs the 2145-colour master |

Visual check at 3x zoom (contact sheets below): before, `jab_003` and `knockdown_005`
showed severe horizontal striping / tearing on hair and limbs and a fragmented foot,
and `walk_002` had a notched leg and speckled shorts; after, those areas are continuous
and solid. A faint leg seam remains in some walk/knockdown frames — see §10.

**Attribution.** These visible improvements come from the forge **supersampling +
`clean_frame`** work in `src/forge/forge_synth.cpp`. That is a *different* code path
from `src/motion/pose_renderer.cpp`; the double-transform fix in §4 did not change
rendered pixels.

Contact sheets, inspected directly:

- `docs/phase4_before_contact_sheet.png` (4006 x 1930)
- `docs/phase4_after_contact_sheet.png` (4006 x 2770)

---

## 7. Directional walks

The clip set grew from 9 to 13. Walking is now directional:
**`walk_left`, `walk_right`, `walk_up`, `walk_down`**.

`walk` is **kept as a backward-compatible alias** so existing consumers keep working.
Its motion is byte-for-byte the original curve (mean alpha delta vs frame 0:
1.76% before, 1.75% after — the 0.01% is the render-quality change, not a motion
change). In metadata it carries `alias_of: "walk_down"` plus `category` and
`direction`, so a consumer can detect and skip the alias.

The five walks are genuinely distinct: pairwise byte differences range from
**9.6% to 15.8%**. Motion amplitudes: `walk_down` 3.24%, `walk_left` 3.11%,
`walk_right` 3.01%, `walk_up` 2.61%.

---

## 8. Cleanup and unification

**74 paths** removed or archived — 73 tracked files staged for deletion with `git rm`
(21 in spratforge, 52 in sprat-cli) and 1 untracked file moved to `_archive/`.
Per-path justification is in [`../../docs/CLEANUP_LOG.md`](../../docs/CLEANUP_LOG.md).

Summary:

- **spratforge (21):** `apps/spratgen/src_export/` and `apps/spratgen/include_export/`
  (verified byte-identical duplicates of `src/` and `include/`, zero references
  anywhere), the dead `apps/spratgen/spratgen_core/CMakeLists.txt`, and two stale
  tracked files under `build/`.
- **sprat-cli (52):** the entire duplicated engine — `src/spratgen/`,
  `include/spratgen/`, `spratforge_export/`, `tools/spratforge/spratforge_main.cpp`,
  `tests/spratforge_test.cpp`, `third_party/libresprite_core/`. Removing these emptied
  `sprat-cli/include/` and `sprat-cli/third_party/` completely; nothing else referenced
  them. The `spratgen_core`, `libresprite_core`, `spratgen` and `spratforge` targets
  were removed from `CMakeLists.txt` and the `spratforge_test` target from
  `tests/CMakeLists.txt`.
- **Archived (1):** `_archive/sprat-cli/build-local/spratgen` — stale binary from a
  removed target. Untracked files are **moved**, never deleted.

`apps/spratgen/{src,include,stb,third_party}` are **load-bearing** and were kept:
`apps/spratforge/CMakeLists.txt` needs the `spratgen_core` target, `jsonnet_SOURCE_DIR`
(for `json/json.hpp`) and the `stb` include dir, enforced by
`scripts/check_spratgen_exports.cmake`.

**Result:** spratforge is the single sprite engine; sprat-cli is the developer
interface (`sprat forge ...`, which locates or builds the spratforge CLI and delegates).

**Single build entry point:** `/home/ubuntu/game_dev/build.sh` with commands
`build`, `test`, `release`, `clean` (default: build + test both projects).

**One build-keeping change in RingQueen:** `tools/forge_sprites.sh` now passes
`--clean` so repeated `npm run sprites` works under the new outdir policy. Because
`RingQueen/assets/generated/boxer` predated the manifest, it was adopted once with
`--force`; it now carries `.spratforge-manifest.json` and `--clean` works from here on.

---

## 9. Release binary

| | |
| --- | --- |
| Path | `spratforge/bin/linux-x86_64/spratforge` |
| Version | `spratforge 2.1.0` |
| Size | 1 370 552 bytes |
| SHA-256 | `b344faf102e7f492ed9a6e584701b6540d50eefbd06585896db5a79594c0d16e` |
| Checksums | `spratforge/bin/linux-x86_64/SHA256SUMS` |
| Build documentation | `spratforge/bin/linux-x86_64/BUILD.md` |

Built by `./build.sh release`, which wipes `spratforge/build-release`, configures
`Release`, builds `spratforge_cli`, installs and strips the binary into the tracked
path, writes `SHA256SUMS`, and prints `--version`.

Reproducibility measures (all documented with rationale in `BUILD.md`):
`SOURCE_DATE_EPOCH=0`, `LC_ALL=C`, `TZ=UTC`, `-ffile-prefix-map` and
`-fdebug-prefix-map` to strip absolute paths, and `__DATE__` / `__TIME__` /
`__TIMESTAMP__` redefined to `"redacted"`.

The binary, `SHA256SUMS` and `BUILD.md` are **staged** (`git add -f`) and not
committed, per the constraints of this phase.

---

## 10. Verification performed

| Check | Result |
| --- | --- |
| spratforge clean rebuild + `ctest` | **24 / 24 pass** (13.23 s) |
| sprat-cli clean rebuild + `ctest` (`-DBUILD_TESTING=ON`) | **18 / 18 pass** |
| Determinism: forge the master twice, `diff -r` incl. `metadata.json` | **identical, no differences** (using the staged release binary) |
| RingQueen `npm run sprites` | **ok** — 105 frames, atlas 4081 x 714, 13 animations |
| RingQueen `npx tsc --noEmit` | **ok**, no errors |
| RingQueen `npm run build` | **ok**, vite build succeeded |

### Open items

- A faint leg seam remains in some `walk*` / `knockdown` frames. Cosmetic residual,
  not a tear; left as-is.
- Build reproducibility is flag-pinned, documented and runtime-deterministic, but it
  has **not** been proven by a second independent build producing an identical hash.
- `RingQueen/tools/gen_boxer_sprites.sh` is legacy (not referenced by `package.json`)
  and can no longer find a `spratgen` binary now that the duplicate target is gone.
  Recorded in the cleanup log; left for the RingQueen pass.
- `sprat-cli/build_debug/`, `build_release/` and `build_test/` are committed build
  trees left in place — out of scope here, and `build_test` is the target of a
  RingQueen symlink.

---

## 11. Metadata compatibility

`schema_version` stays **2**; all additions are additive and a `schema_minor: 1` field
was introduced to signal them. New fields: top-level `render`; per-animation
`category`, `direction`, `alias_of`, `ground_clamp`. The clip list grew from 9 to 13.
RingQueen's `tools/build_boxer_atlas.mjs` consumes the new metadata unchanged — see
the verification table in §10. Details in [`METADATA_SCHEMA.md`](METADATA_SCHEMA.md).
