# Project Guidelines

When working on this project, always consult the following files for context:

- **IMPLEMENTATION_PLAN.md** - Contains the implementation plan and roadmap
- **README.md** - Project overview and documentation
- **README_EMBED_UI.md** - Embedded UI specific documentation

## Current workflow

- Firmware is built with PlatformIO using the `denky32` environment in `platformio.ini`.
- PlatformIO pre-build scripts generate embedded headers:
  - `tools/embed_configs.py` embeds `data/config/samples/` into `include/embedded_configs.h`.
  - `tools/embed_ui.py` runs `npm run build` in `ui/`, then embeds the fresh Svelte output from `ui/dist/` into `include/embedded_files.h`.
- The `data/` directory is for LittleFS/config files and config samples. Do not describe it as the Svelte build output directory.
- Runtime config files live on LittleFS under `/config/*.json`; the working source copies are under `data/config/`, and canonical sample schemas live under `data/config/samples/`.
- Do not edit files under `data/config/samples/` unless the user explicitly asks. They are schema references and firmware default source material.
- `AGENTS.md` is a symlink to this file, so update `CLAUDE.md` for project-agent instructions.

## Area IDs

Area IDs (e.g. `grass`, `drip`, `filling`) must be **all lowercase** — no uppercase letters. The UI capitalizes them for display only via `capitalize()` in `ui/src/lib/utils.js`.

## UI changes

**Each time you change the Svelte UI (`ui/src/**`), you MUST also update `preview.html` to reflect the same change.** `preview.html` is a standalone, dependency-free mock of the production UI used to review layout/behavior without a build step. The two must stay visually and behaviorally in sync. Apply the equivalent change to `preview.html` in the same response — do not defer.
