# ringqueen-engine

Phase 3 supplies a headless runtime asset-binding entry point. It loads the
generated boxer through `rq::assets` and selects the initial idle frame; gameplay,
windowing, and GPU texture upload are not implemented.

```sh
cmake --build /tmp/ringqueen-build --target ringqueen-engine -j 2
/tmp/ringqueen-build/apps/ringqueen-engine/ringqueen-engine
/tmp/ringqueen-build/apps/ringqueen-engine/ringqueen-engine /path/to/manifest.json
```

The engine build depends on generation and validation of its configured assets.
No-op builds still validate them. The optional runtime argument overrides the
build-tree manifest path for deployment. Invalid assets return exit status 1.

See [rq-assets](../../libs/rq-assets/README.md) for schema, loader, build-input,
offline dependency, and CI details.