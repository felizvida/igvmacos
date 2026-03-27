# IGV Native Desktop

This workspace contains a source-backed feature inventory for IGV Desktop plus a native macOS prototype built with a Rust core and a Qt Widgets shell.

## What is here

- [docs/igv-desktop-feature-inventory.md](/Users/liux17/codex/igv/docs/igv-desktop-feature-inventory.md) summarizes the real IGV Desktop feature surface from official documentation and maps it into a native rewrite plan.
- A Rust static library exposes the feature catalog, roadmap, notes, and a starter session schema.
- A Qt macOS desktop app provides a native shell with session JSON import/export, local file and URL track loading, locus parsing, ROI overlays, and snapshot export.

## Current prototype scope

- Native macOS window, menus, toolbar, docks, and canvas.
- Genome selection and locus entry.
- Multi-locus preview mode.
- Track inventory with simple kind inference from file extensions.
- Region-of-interest overlays.
- Snapshot export as PNG.
- Session save/open using a native JSON schema.

## Build

```bash
cmake -S /Users/liux17/codex/igv -B /Users/liux17/codex/igv/build -DCMAKE_PREFIX_PATH=/Users/liux17/miniforge/lib/cmake
cmake --build /Users/liux17/codex/igv/build -j4
```

The build produces `igv_native_desktop.app` in the build directory.

## Rewrite shape

- Qt owns the macOS desktop shell, menus, file dialogs, and drawing surface.
- Rust owns the domain model, feature inventory, roadmap, session schema, and future genomics backends.
- The current canvas is a native preview surface. It is intentionally ready to be replaced by tiled reference, annotation, quantitative, alignment, and variant renderers in later phases.
