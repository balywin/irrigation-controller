Embedding SPA into firmware (overview)

Goal
- Serve the static SPA from flash (PROGMEM) so the web page is part of the firmware binary.
- Allow the UI to edit JSON config files and persist them to LittleFS so changes survive power cycles or reset.
- On first boot (or when configs are missing or corrupted), copy embedded default configs into LittleFS.

Workflow
Building the firmware with PlatformIO handles everything automatically via two pre-build scripts
configured in `platformio.ini`:

1. `tools/embed_configs.py` — embeds `data/config/samples/` into `include/embedded_configs.h`
   using URL prefix `/config/`.
2. `tools/embed_ui.py` — runs `npm run build` in `ui/`, then embeds the fresh `ui/dist/` output
   into `include/embedded_files.h` using URL prefix `/` and gzip compression.
   Internally calls `tools/embed_static.js` with `--input ui/dist --output include/embedded_files.h
   --prefix / --gzip`.

To build and flash:
```bash
pio run -e denky32 -t upload
```

No manual embed step is required. Both headers are regenerated on every `pio run`.

To iterate on UI only (no firmware flash needed):
```bash
cd ui && npm run build
```
Then run `pio run` to embed and flash.

Notes
- The web page is embedded in the firmware image; editing it requires a firmware update.
  Config JSONs are stored in LittleFS so they can be edited from the UI without reflashing.
- `include/embedded_files.h` and `include/embedded_configs.h` are generated files.
  Do not edit them by hand — regenerate via `pio run`.
- `data/` is not the Svelte build output directory. Do not copy `ui/dist` into `data/`.
- The web server module falls back to `/index.html` for SPA navigation.
- The embed tool gzips assets when it reduces size.

Next steps / optional
- Add file versioning so that when a new firmware contains updated default configs you can either force overwrite or merge.
- Add authentication for config endpoints.
- Optionally store configs as key/value in Preferences (NVS) instead of files.
