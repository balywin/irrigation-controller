# Project Guidelines

When working on this project, always consult the following files for context:

- **IMPLEMENTATION_PLAN.md** - Contains the implementation plan and roadmap
- **README.md** - Project overview and documentation
- **README_EMBED_UI.md** - Embedded UI specific documentation

## UI changes

**Each time you change the Svelte UI (`ui/src/**`), you MUST also update `preview.html` to reflect the same change.** `preview.html` is a standalone, dependency-free mock of the production UI used to review layout/behavior without a build step. The two must stay visually and behaviorally in sync. Apply the equivalent change to `preview.html` in the same response — do not defer.
