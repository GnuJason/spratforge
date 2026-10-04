# `spratforge_cli` — command reference

`spratforge 2.1.0`

```
spratforge_cli <subcommand> [options]
```

| Subcommand | Purpose |
|---|---|
| `forge` | One neutral sprite → a full, game-ready animation set. **Use this.** |
| `generate` | Profile-driven pipeline (legacy, still supported). |
| `rig-validate` | Validate a rig JSON against a rig profile. |
| `audit` | Audit a sprite or a generated output directory. |
| `legacy` | Single / profile / atlas / ai-motion render modes, and `--turnkey`. |

Global options, accepted anywhere in the argument list:

| Flag | Effect |
|---|---|
| `--help`, `-h`, `help` | Overview. `<subcommand> --help` prints that subcommand's options **with examples**. |
| `--version`, `-V`, `version` | Print `spratforge <version>` and exit 0. |

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Success. |
| `2` | Bad arguments — unknown flag, missing value, missing required option, mutually exclusive flags. The message always ends with `(see: spratforge_cli <cmd> --help)`. |
| `3`, `4` | Legacy render-mode errors. |
| `5` | The run itself failed (bad input, refused output directory, unknown animation name, …). |

Every failure prints a human-readable message on stderr **and** a machine-readable
JSON report on stdout (`{"valid": false, "diagnostics": [...]}`), unless `--quiet`
is given.

---

## `forge`

```
spratforge_cli forge --input <sprite.png> --out <dir> [options]
```

Estimates a rig from a single neutral sprite, segments the body, and synthesises
the full animation set plus `metadata.json`. See `docs/METADATA_SCHEMA.md` for
the output contract.

### Input and output

| Flag | Default | Description |
|---|---|---|
| `--input <png>` | *required* | Neutral sprite. RGBA PNG, **32–2048 px on each axis**, square or not. |
| `--out <dir>` | *required* | Output directory. Created if missing. |
| `--character <name>` | `boxer` | Character id recorded in `metadata.json`. |
| `--rig-override <json>` | – | Hand-corrected rig to use instead of the estimate. |

### Output directory policy

A **non-empty** output directory is refused by default (exit 5), so a stray
`--out` can never silently wipe a directory.

| Flag | Behaviour |
|---|---|
| *(none)* | Refuse if the directory contains anything. |
| `--clean` | Delete **only** the files listed in the previous run's `.spratforge-manifest.json`, then write. Anything spratforge did not write is left alone; if the directory holds unknown files and no manifest, the run still refuses. |
| `--force` | Write over the directory in place, deleting nothing. Use this once to adopt a directory that predates the manifest. |

### Animation selection

| Flag | Description |
|---|---|
| `--list-animations` | Print every animation name the forge can emit, one per line, and exit 0. Does not need `--input`/`--out`. |
| `--only <a,b,c>` | Forge only these animations. Order is normalised to the canonical order, so `--only jab,idle` and `--only idle,jab` produce the same output. An unknown name fails with exit 5, names the offending clip and lists the available ones. |

The 13 clips are `idle`, `walk`, `walk_down`, `walk_up`, `walk_left`,
`walk_right`, `block`, `jab`, `hook`, `uppercut`, `hit`, `knockdown`, `ko`.

### Render quality

| Flag | Default | Description |
|---|---|---|
| `--supersample <1-8>` | `3` | Sub-samples per axis when rasterising a posed part. `1` reproduces the pre-Phase-4A renderer. |
| `--soft-edges` | off | Average in premultiplied-alpha space instead of taking a modal-colour vote. Produces anti-aliased edges and intermediate alpha; **off** keeps alpha binary and the output strictly inside the source palette. |
| `--no-cleanup` | off | Skip the pinhole-fill and despeckle passes. |
| `--scale <n>` | `1` | Integer nearest-neighbour upscale of every frame. |
| `--alpha-threshold <n>` | – | Alpha cutoff used when extracting the source silhouette. |
| `--margin <px>` | – | Extra transparent margin around the shared canvas. |

### Timing and output artefacts

| Flag | Description |
|---|---|
| `--fps <n>` | Override the frame rate of every clip. |
| `--no-sheets` | Do not write `sheets/<anim>.png`. |
| `--no-debug` | Do not write `rig_debug.png`. |
| `--seed <n>` | Deterministic seed, recorded in `metadata.json` under `render.seed`. |

### Reporting

| Flag | Description |
|---|---|
| `--quiet` | Suppress the JSON report on stdout. Exit code still signals success. |
| `--verbose` | Add `written_files` (every artefact produced) to the JSON report. |

`--quiet` and `--verbose` are mutually exclusive (exit 2).

### Examples

```bash
# Full set from a neutral sprite, replacing a previous run's artefacts
spratforge_cli forge --input master_boxer.png --out assets/boxer --clean

# What can this build emit?
spratforge_cli forge --list-animations

# Just the two clips you are iterating on, at 4x supersampling
spratforge_cli forge --input master_boxer.png --out /tmp/try \
    --only jab,hook --supersample 4 --clean

# Anti-aliased edges, 2x upscale, no debug overlay, machine-readable file list
spratforge_cli forge --input master_boxer.png --out /tmp/aa \
    --soft-edges --scale 2 --no-debug --verbose --clean
```

---

## `generate`

```
spratforge_cli generate --input <sprite.png> --out <dir> [--variant <name>]
                        [--motion-dir <dir>] [--force|--clean]
```

The profile-driven Phase-2 pipeline. Reads motion profiles from
`profiles/motion/` (override with `--motion-dir`) and writes `manifest.json`,
`atlas.json`, `atlas.png` and per-animation directories. It honours the same
output-directory policy as `forge`.

```bash
spratforge_cli generate --input neutral.png --out out/
spratforge_cli generate --input neutral.png --out out/ --variant blue --clean
```

## `rig-validate`

```bash
spratforge_cli rig-validate --rig rig.json --profile profiles/rig/boxer_default.json
```

## `audit`

```bash
spratforge_cli audit --input master_boxer.png     # audit a source sprite
spratforge_cli audit --path assets/boxer          # audit a generated directory
```

## `legacy`

```bash
spratforge_cli --turnkey master_boxer.png --out out/
spratforge_cli --mode atlas --input sheet.png --out atlas.png --atlas 4x4 --padding 2
```

Kept for existing scripts. Prefer `forge` for new work.

---

## Driving it from `sprat-cli`

`sprat-cli/scripts/sprat` is the developer front end. `sprat forge ...` locates
`spratforge_cli` (building it if necessary) and forwards every argument to it,
so everything documented above works identically:

```bash
sprat forge --input master_boxer.png --out assets/boxer --clean
```
