# `spratforge` release binary — linux-x86_64

| | |
|---|---|
| Binary | `spratforge/bin/linux-x86_64/spratforge` |
| Version | `spratforge 2.1.0` (`spratforge --version`) |
| SHA-256 | `b344faf102e7f492ed9a6e584701b6540d50eefbd06585896db5a79594c0d16e` |
| Checksum file | `SHA256SUMS` (same directory) |
| Built from | `spratforge/apps/spratforge` target `spratforge_cli` |

## How it was built

The binary is produced by the single top-level entry point:

```bash
cd /home/ubuntu/game_dev
JOBS=2 ./build.sh release
```

Verify it:

```bash
cd spratforge/bin/linux-x86_64 && sha256sum -c SHA256SUMS
```

## Exact flags

`./build.sh release` runs exactly this, with `SOURCE_DATE_EPOCH=0`,
`LC_ALL=C` and `TZ=UTC` exported for the whole build:

```bash
REPRO="-ffile-prefix-map=/home/ubuntu/game_dev/spratforge=. \
       -fdebug-prefix-map=/home/ubuntu/game_dev/spratforge=. \
       -Wno-builtin-macro-redefined \
       -D__DATE__=\"redacted\" -D__TIME__=\"redacted\" -D__TIMESTAMP__=\"redacted\""

cmake -S spratforge -B spratforge/build-release \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=OFF \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      -DCMAKE_C_FLAGS="-O2 -DNDEBUG ${REPRO}" \
      -DCMAKE_CXX_FLAGS="-O2 -DNDEBUG ${REPRO}" \
      -DCMAKE_EXE_LINKER_FLAGS="-Wl,--build-id=none"

cmake --build spratforge/build-release --target spratforge_cli -j2

install -m 0755 spratforge/build-release/apps/spratforge/spratforge_cli \
        spratforge/bin/linux-x86_64/spratforge
strip --strip-unneeded --enable-deterministic-archives \
        spratforge/bin/linux-x86_64/spratforge
( cd spratforge/bin/linux-x86_64 && sha256sum spratforge > SHA256SUMS )
```

## Why each reproducibility flag is there

| Flag / variable | Purpose |
|---|---|
| `SOURCE_DATE_EPOCH=0` | Fixes any timestamp a tool would otherwise take from the clock. |
| `LC_ALL=C`, `TZ=UTC` | Removes locale- and timezone-dependent formatting and sort order. |
| `-ffile-prefix-map=<srcdir>=.` | Rewrites absolute source paths baked into the binary (assertions, `__FILE__`) to repo-relative ones, so the output does not depend on where the tree is checked out. |
| `-fdebug-prefix-map=<srcdir>=.` | Same for debug info, for builds that keep it. |
| `-D__DATE__/__TIME__/__TIMESTAMP__="redacted"` | Removes the three macros that would otherwise embed the build clock. `-Wno-builtin-macro-redefined` silences the redefinition warning. |
| `-Wl,--build-id=none` | Drops the linker-generated build-id note, which hashes input file identity. |
| `-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF` | LTO object naming is not deterministic across GCC invocations; pinned off. |
| `strip --strip-unneeded --enable-deterministic-archives` | Removes symbol tables and any residual non-deterministic archive metadata. |
| `-O2 -DNDEBUG` | Pinned optimisation level — not `-O3`/`-march=native`, so the binary is portable across x86-64 hosts and identical regardless of the build machine's CPU. |

## Runtime determinism

Independently of build reproducibility, the binary itself is deterministic:
the same input sprite and the same flags produce byte-identical output,
including `metadata.json`.

Verified for this binary:

```bash
./spratforge/bin/linux-x86_64/spratforge forge \
    --input RingQueen/assets/master_boxer.png --out /tmp/det_a --quiet
./spratforge/bin/linux-x86_64/spratforge forge \
    --input RingQueen/assets/master_boxer.png --out /tmp/det_b --quiet
diff -r /tmp/det_a /tmp/det_b      # no differences
```

## Notes

* The binary is dynamically linked against the system C/C++ runtime
  (`glibc`, `libstdc++`). It is not a static build.
* `spratforge/build-release/` is a throwaway build tree; it is removed and
  recreated by every `./build.sh release`.
