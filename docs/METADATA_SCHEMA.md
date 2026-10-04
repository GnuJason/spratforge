# spratforge character metadata schema

**Schema version: `2`, minor `1`** — emitted by `spratforge forge` as `<out>/metadata.json`.
This document is the canonical contract consumed by downstream integrations
(RingQueen playback, Phase 3). The file is UTF-8 JSON, indented with two
spaces, terminated by a single newline. Object keys are serialised in
alphabetical order, which makes the file byte-stable across runs.

## Output directory layout

```
<out>/
  metadata.json                      # this document
  rig.json                           # estimated (or overridden) rig, see below
  rig_debug.png                      # joint + segmentation debug overlay
  frames/<animation>/<animation>_NNN.png   # one PNG per frame, zero-padded to 3
  sheets/<animation>.png             # horizontal strip sheet, 1 row
  .spratforge-manifest.json          # list of the files this run wrote (see below)
```

Every frame PNG in a character has **the same width and height** and the same
**anchor**, so a consumer can blit frames from any animation without
re-aligning.

`forge` additionally writes `.spratforge-manifest.json`, a sorted list of every
artefact the run produced. It exists so `--clean` can delete **only** files that
spratforge itself wrote and never touch anything else in the directory. It is
not part of the character contract; consumers should ignore it.

A **non-empty output directory is refused by default** (exit code 5). Pass
`--clean` to remove the artefacts recorded in a previous run's manifest, or
`--force` to overwrite in place without deleting anything.

## Top level

| Key | Type | Description |
| --- | --- | --- |
| `schema_version` | int | `2`. Bump on any breaking change. |
| `schema_minor` | int | `1`. Bumped for **additive** changes only. A consumer that understands minor `0` can read minor `1` unchanged. |
| `format` | string | Always `"spratforge-character"`. |
| `generator` | object | `{ "tool", "subcommand", "pipeline" }` — provenance strings. |
| `character` | string | Character identifier passed via `--character`. |
| `source` | object | See [Source](#source). |
| `scale` | int | Integer nearest-neighbour upscale applied to every frame (`--scale`). |
| `frame` | object | `{ "width", "height" }` in pixels, after `scale`. |
| `anchor` | object | See [Anchor](#anchor). |
| `layout` | object | See [Layout](#layout). |
| `animations` | array | Ordered list of [Animation](#animation) objects. |
| `render` | object | Renderer settings used for this run — see [Render](#render). |
| `totals` | object | `{ "animation_count", "frame_count" }`. |

### Source

```json
"source": {
  "path": "RingQueen/assets/master_boxer.png",
  "width": 400,
  "height": 400,
  "alpha_threshold": 16,
  "body_bounds": { "min_x": 198, "min_y": 120, "max_x": 309, "max_y": 285,
                   "width": 112, "height": 166 },
  "rig_override": ""
}
```

`body_bounds` is the opaque bounding box of the input sprite in source pixels;
all pose amplitudes are expressed as fractions of `body_bounds.height`.
`rig_override` is the path supplied via `--rig-override`, or `""`.

### Anchor

```json
"anchor": { "x": 175, "y": 191, "space": "frame_local", "semantics": "ground_contact" }
```

* `space` is `"frame_local"`: the coordinate is measured from the top-left of
  the frame, in post-`scale` pixels.
* `semantics` is `"ground_contact"`: the anchor is the point the character
  stands on. To place a character at world position `(wx, wy)` where `wy` is
  the floor line, draw the frame at `(wx - anchor.x, wy - anchor.y)`.
* The anchor is identical for every frame of every animation. It is repeated
  per frame for convenience.

### Layout

```json
"layout": {
  "frame_path_pattern": "frames/{animation}/{animation}_{index:03d}.png",
  "sheet_path_pattern": "sheets/{animation}.png",
  "sheet_layout": "horizontal_strip",
  "rig": "rig.json",
  "rig_debug": "rig_debug.png"
}
```

All paths are POSIX-style and relative to the directory containing
`metadata.json`.

### Animation

```json
{
  "name": "jab",
  "frame_count": 7,
  "fps": 18,
  "frame_duration_ms": 56,
  "duration_ms": 392,
  "loop": false,
  "hold_last_frame": false,
  "is_attack": true,
  "active_from": 3,
  "active_to": 4,
  "impact_frame": 3,
  "sheet": { "file": "sheets/jab.png", "width": 1883, "height": 204,
             "columns": 7, "rows": 1 },
  "frames": [ /* Frame objects, ordered */ ]
}
```

| Key | Type | Description |
| --- | --- | --- |
| `name` | string | Animation id. Unique within the character. |
| `frame_count` | int | Number of frames; equals `frames.length` and `sheet.columns`. |
| `fps` | int | Playback rate. Overridable for every clip with `--fps`. |
| `frame_duration_ms` | int | `round(1000 / fps)`. Uniform across the clip. |
| `duration_ms` | int | `frame_duration_ms * frame_count`. |
| `loop` | bool | `true` → wrap to frame 0 after the last frame. |
| `hold_last_frame` | bool | `true` → freeze on the last frame when the clip ends. Mutually exclusive with `loop`. |
| `is_attack` | bool | `true` when the clip has an active hit window. |
| `active_from` / `active_to` | int | Inclusive frame range during which the attack can connect. `-1` when `is_attack` is `false`. |
| `impact_frame` | int | The single frame on which the hit should register (within `[active_from, active_to]`). `-1` when `is_attack` is `false`. |
| `category` | string | *(minor 1)* Coarse gameplay class: `idle`, `locomotion`, `defense`, `attack`, `reaction` or `down`. |
| `direction` | string | *(minor 1)* Facing for directional clips: `down`, `up`, `left`, `right`. Empty string for non-directional clips. |
| `alias_of` | string | *(minor 1)* When non-empty, this clip is kept only for backward compatibility and names the clip it mirrors. `walk` has `alias_of: "walk_down"`. |
| `ground_clamp` | bool | *(minor 1)* The posed skeleton was translated so nothing sinks below the anchor ground line. |
| `sheet` | object | Strip sheet descriptor. `rows` is always `1`. |

### Render

*(added in schema minor 1)*

```json
"render": {
  "supersample": 3,
  "coverage_threshold": 0.5,
  "soft_edges": false,
  "fill_pinholes": true,
  "despeckle": true,
  "seed": 0
}
```

| Key | Type | Description |
| --- | --- | --- |
| `supersample` | int | Sub-samples per axis used when rasterising a posed part (`--supersample`, 1–8, default 3). `1` reproduces the pre-Phase-4A renderer. |
| `coverage_threshold` | float | Fraction of sub-samples that must be opaque before the destination pixel is written. `0.5` keeps the silhouette the same weight as the source. |
| `soft_edges` | bool | `false` (default) resolves each pixel by a modal-colour vote, which keeps output strictly inside the source palette and keeps alpha binary. `true` averages in premultiplied space, producing anti-aliased edges and intermediate alpha (`--soft-edges`). |
| `fill_pinholes` | bool | Fill single transparent pixels that have ≥6 opaque neighbours, using the modal neighbour colour. |
| `despeckle` | bool | Drop opaque pixels that have ≤1 opaque neighbour. |
| `seed` | int | Deterministic seed recorded for reproducibility (`--seed`). The renderer is fully deterministic; the seed is recorded so a run can be reproduced exactly from metadata alone. |

Both cleanup passes operate on a snapshot of the frame, so the result does not
depend on scan order and stays byte-stable.

### Frame

```json
{
  "index": 3,
  "file": "frames/jab/jab_003.png",
  "duration_ms": 56,
  "active": true,
  "impact": true,
  "anchor": { "x": 175, "y": 191 },
  "sheet_rect": { "x": 807, "y": 0, "width": 269, "height": 204 }
}
```

| Key | Type | Description |
| --- | --- | --- |
| `index` | int | 0-based position in the clip; matches the filename suffix. |
| `file` | string | Path relative to `metadata.json`. |
| `duration_ms` | int | Display time for this frame. |
| `active` | bool | Frame lies inside `[active_from, active_to]`. |
| `impact` | bool | Frame equals `impact_frame`. At most one per clip. |
| `anchor` | object | Same value as the top-level anchor. |
| `sheet_rect` | object | Source rectangle inside the animation's strip sheet. |

## Animation set

`spratforge forge` always emits these **thirteen** clips, in this order:

| Name | Category | Direction | Frames | fps | Loop | Hold last | Attack | Impact |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `idle` | idle | – | 8 | 10 | yes | no | no | – |
| `walk` | locomotion | down | 8 | 12 | yes | no | no | – |
| `walk_down` | locomotion | down | 8 | 12 | yes | no | no | – |
| `walk_up` | locomotion | up | 8 | 12 | yes | no | no | – |
| `walk_left` | locomotion | left | 8 | 12 | yes | no | no | – |
| `walk_right` | locomotion | right | 8 | 12 | yes | no | no | – |
| `block` | defense | – | 4 | 14 | no | yes | no | – |
| `jab` | attack | – | 7 | 18 | no | no | yes | 3 |
| `hook` | attack | – | 8 | 16 | no | no | yes | 4 |
| `uppercut` | attack | – | 9 | 16 | no | no | yes | 4 |
| `hit` | reaction | – | 5 | 14 | no | no | no | – |
| `knockdown` | down | – | 10 | 12 | no | yes | no | – |
| `ko` | down | – | 14 | 12 | no | yes | no | – |

### Directional walks and the `walk` alias

`walk_down`, `walk_up`, `walk_left` and `walk_right` were added in schema minor
1. Each is a full four-phase cycle (contact → down → passing → up, twice) with a
stride, vertical lift and spine sway chosen for that facing, so the four clips
are visibly distinct from one another.

`walk` is **unchanged** — it keeps the exact motion it had before Phase 4A so
existing consumers that hard-code `"walk"` keep the frames they already ship.
It is marked `alias_of: "walk_down"` to tell new consumers which directional
clip it corresponds to. Prefer the explicit `walk_<direction>` names in new code.

`knockdown` is a distinct animation from `ko`: fewer frames, a different fps and
a different pose sequence (the fighter drops to the canvas and can rise), where
`ko` is the terminal count-out. Both are non-looping and hold their last frame.
Any consumer that previously treated `knockdown` as a synonym for `ko` will now
get different pixels for each.

`knockdown` and `ko` are ground-clamped: the posed skeleton is translated so
nothing sinks below the anchor's ground line, and the final frame shows the
character lying on the canvas.

## `rig.json`

Written alongside `metadata.json` so a character's rig can be inspected,
hand-corrected and fed back in via `--rig-override`.

```json
{
  "format": "spratforge-rig",
  "schema_version": 1,
  "source_size": { "width": 400, "height": 400 },
  "body_bounds": { "x": 198, "y": 120, "width": 112, "height": 166 },
  "pivot": [245.5, 285.0],
  "joints": {
    "pelvis": [246.5, 237.0], "neck": [246.85, 165.0], "head_top": [247.2, 128.1],
    "shoulder_back": [229.02, 168.0], "elbow_back": [221.1, 179.0], "hand_back": [219.0, 190.0],
    "shoulder_front": [263.98, 168.0], "elbow_front": [277.9, 177.5], "hand_front": [285.0, 187.0],
    "hip_back": [230.0, 237.0], "knee_back": [217.5, 261.0], "foot_back": [210.5, 285.0],
    "hip_front": [263.0, 237.0], "knee_front": [274.5, 261.0], "foot_front": [280.5, 285.0]
  },
  "parts": [ { "name": "torso", "from": "pelvis", "to": "neck",
               "radius_from": 33.66, "radius_to": 20.1, "z_order": 30 } ]
}
```

All coordinates are **source-image pixels** (not frame-local). `back` is the
screen-left side, `front` the screen-right side.

### Rig override file

A rig override is a JSON object accepting the keys `joints`, `pivot` and
`radii`; the informational keys produced by `rig.json` (`schema_version`,
`format`, `source_size`, `body_bounds`, `parts`) are tolerated and ignored.
Any other key is a hard error, so typos cannot silently do nothing.

```json
{
  "joints": { "hand_front": [292, 186], "hand_back": [214, 188] },
  "pivot": [245, 285],
  "radii": { "front_glove": { "from": 12, "to": 12 } }
}
```

Only the listed joints are replaced; everything else keeps its estimated
value.

## Compatibility notes

* Schema `2` replaces the ad-hoc Phase 1 manifest. A consumer should reject a
  file whose `schema_version` it does not recognise.
* Additive changes (new optional keys) will not bump `schema_version`; they bump
  `schema_minor` instead. Removals or semantic changes bump `schema_version`.
* Phase 4A is additive only: every key that existed at minor 0 is still present,
  with the same type and the same meaning, so a consumer written against minor 0
  (including RingQueen, which hard-requires `schema_version === 2`) reads a
  minor-1 file without modification. The new keys are `schema_minor`, `render`,
  and the per-animation `category`, `direction`, `alias_of` and `ground_clamp`.
* The animation list grew from 9 to 13 clips. Consumers that iterate
  `animations` pick the new clips up automatically; consumers that look clips up
  by name are unaffected.
* The file is deterministic: the same input sprite and the same options
  produce byte-identical `metadata.json`, frames and sheets.
