Embedding SPA into firmware (overview)

Goal
- Serve the static SPA from flash (PROGMEM) so the web page is part of the firmware binary.
- Allow the UI to edit JSON config files and persist them to LittleFS so changes survive power cycles or reset.
- On first boot (or when configs are missing or corrupted), copy embedded default configs into LittleFS.

Workflow
1. Build your Svelte app (e.g. `npm run build`) so the compiled files are available (dist or public/build).
2. Run the embed tool to generate a C header with PROGMEM arrays:

```bash
node tools/embed_static.js --input ui/public/build --output include/embedded_files.h --prefix / --gzip
```

3. In PlatformIO project, include `include/embedded_files.h` (it will be generated) and call `setupEmbeddedWebServer(server);` from your `setup()` after calling `LittleFS.begin()`.
4. Upload firmware via PlatformIO. The web assets are embedded in the firmware image.

Notes
- The web page is now embedded; editing it requires firmware update. The config JSONs are stored in LittleFS so they can be edited from the UI.
- The embed tool gzips assets if --gzip is passed and keeps compressed data when it's beneficial.
- The web server module falls back to `/index.html` for SPA navigation.

Next steps / optional
- Add file versioning so that when a new firmware contains updated default configs you can either force overwrite or merge.
- Add authentication for config endpoints.
- Optionally store configs as key/value in Preferences (NVS) instead of files.
