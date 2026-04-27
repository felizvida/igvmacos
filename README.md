# IGV Native Desktop

This workspace contains a source-backed feature inventory for IGV Desktop plus a native macOS prototype built with a Rust core and a Qt Widgets shell.

## What is here

- [docs/igv-desktop-feature-inventory.md](/Users/liux17/codex/igv/docs/igv-desktop-feature-inventory.md) summarizes the real IGV Desktop feature surface from official documentation and maps it into a native rewrite plan.
- [docs/igv-parity-backlog.md](/Users/liux17/codex/igv/docs/igv-parity-backlog.md) ranks the remaining IGV Desktop parity gaps into `P0`, `P1`, and `P2` implementation tracks.
- A Rust static library exposes the feature catalog, roadmap, notes, and a starter session schema.
- A Qt macOS desktop app provides a native shell with session JSON import/export, local file and URL track loading, locus parsing, ROI overlays, and snapshot export.

## Current prototype scope

- Native macOS window, menus, toolbar, docks, and canvas.
- Genome selection and locus entry.
- Multi-locus preview mode.
- Track inventory with simple kind inference from file extensions.
- Alignment-track visibility windows with real local `SAM` preview summaries for coverage, overlapping reads, and splice junctions.
- Region-of-interest overlays.
- Snapshot export as PNG.
- Session save/open using a native JSON schema.
- Best-effort import of IGV XML sessions with genome, loci, resource-backed tracks, and regions.
- Case manifest loading with a readiness panel for missing files, missing indexes, genome mismatches, and sample metadata gaps.
- Case folder opening with autodiscovery for sessions, tracks, sidecar indexes, and sample metadata files.
- Sample metadata table parsing for TSV, CSV, TXT, and TAB files with `sample`, `attribute`, or `metadata` naming.
- Sample-to-track matching with cohort presets like `Matched Samples`, `Tumor`, `Normal`, and `Tumor vs Normal`.
- Review queue workflow with locus/ROI seeding, status tracking, notes, and jump-to-locus navigation.
- Review packet export as Markdown plus a paired PNG snapshot for case handoff.
- HTML review report export with auto-captured per-locus snapshots for the whole queue.

## Build

```bash
cmake -S /Users/liux17/codex/igv -B /Users/liux17/codex/igv/build
cmake --build /Users/liux17/codex/igv/build -j4
```

The build produces `igv_native_desktop.app` in the build directory.

If Qt is not on a standard CMake path, either make `qmake` available on `PATH` or configure with an explicit prefix such as `-DCMAKE_PREFIX_PATH=/path/to/Qt/lib/cmake`.

## Case-first workflow

The app now supports a native case manifest format for researcher-facing workspaces. A manifest can:

- point at a referenced native session or IGV XML session
- add case-specific tracks and ROIs
- preseed a review queue with per-locus status and notes
- declare sample metadata files
- surface readiness issues before review starts

The app can also open a case folder directly. In that mode it will:

- prefer a case manifest if one already exists in the folder tree
- otherwise autodiscover a native session or IGV XML session
- scan the folder for supported track files and sidecar indexes
- parse sample metadata into a native table view
- generate cohort presets from matched sample metadata and inferred track groups
- seed a native review queue from imported loci and ROIs for daily review work
- export a review packet that captures the active preset, visible tracks, readiness issues, queue status, notes, and a snapshot
- auto-advance to the next pending locus when a reviewer marks the current item as reviewed
- export an HTML review report that captures the overview plus one snapshot per queued review item

See [docs/case-manifest.md](/Users/liux17/codex/igv/docs/case-manifest.md) for the schema and [docs/example.case.json](/Users/liux17/codex/igv/docs/example.case.json) for a concrete example.

## Rewrite shape

- Qt owns the macOS desktop shell, menus, file dialogs, and drawing surface.
- Rust owns the domain model, feature inventory, roadmap, session schema, and future genomics backends.
- The current canvas is a native preview surface. It is intentionally ready to be replaced by tiled reference, annotation, quantitative, alignment, and variant renderers in later phases.
