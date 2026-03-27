# IGV Desktop Feature Inventory

Collected on March 27, 2026 to guide a native macOS rewrite.

## Product pillars

### 1. Navigation and locus control

- IGV Desktop supports whole-genome, chromosome, and base-level navigation.
- Users can jump by genomic coordinates, gene symbols, non-indexed feature identifiers, and mutation shortcuts such as `KRAS:G12C` and `KRAS:123A>T`.
- Multiple loci can be opened side-by-side in the same session.

Source:
[Navigating the view](https://igv.org/doc/desktop/UserGuide/navigation/)

### 2. Reference genomes and genome switching

- The desktop app can switch among curated reference genomes.
- IGV documentation also describes loading custom genome assets, which is essential for a native rewrite because the reference genome acts as the coordinate system for every track.

Source:
[IGV Desktop docs](https://igv.org/doc/desktop/)

### 3. Track loading and remote data access

- Tracks can be loaded from local files, URLs, and hosted track catalogs.
- Indexed formats rely on byte-range HTTP support when loaded remotely.
- Sample information files are part of the normal load path, not a side feature.

Source:
[Loading and removing tracks](https://igv.org/doc/desktop/UserGuide/loading_data/)

### 4. Sessions and persistence

- IGV sessions are a first-class workflow for saving a loaded genome, tracks, and view state.
- A native rewrite should keep this idea intact, but a JSON-native session schema is easier to evolve than carrying Java XML forward forever.

Source:
[IGV Desktop User Guide](https://igv.org/doc/desktop/UserGuide/)

### 5. Image export and reproducibility

- Desktop IGV exports PNG and SVG images.
- Batch workflows can automate snapshot production, which means export is both an interactive and scripted feature.

Source:
[Saving images](https://igv.org/doc/desktop/UserGuide/saving_images/)

### 6. Core file types

- Official docs highlight tracks such as GWAS, IGV tabular tracks, copy-number style quantitative tracks, and many standard genomics formats.
- A native rewrite should treat file-type support as a back-end concern, not a UI afterthought.

Source:
[Data track file formats](https://igv.org/doc/desktop/FileFormats/DataTracks/)

### 7. Alignment review

- Paired-end review includes pair linking, mate highlighting, and joint read detail inspection.
- RNA-seq review adds split-read rendering and splice-junction concepts on top of the general alignment model.

Sources:
[Paired-end alignments](https://igv.org/doc/desktop/UserGuide/tracks/alignments/paired_end_alignments/index.html)
[RNA-seq data](https://igv.org/doc/desktop/UserGuide/tracks/alignments/rna_seq/)

### 8. Regions of interest and workflow overlays

- IGV’s workflow is not just raw tracks; researchers frequently mark review regions and move among named loci.
- ROI overlays, region navigation, and gene-list style workflows should stay in the native app because they anchor collaborative review.

Source:
[IGV Desktop User Guide](https://igv.org/doc/desktop/UserGuide/)

### 9. Automation and external control

- Snapshot export is explicitly scriptable in batch flows.
- That strongly suggests the native rewrite should expose a command layer for scripted loading, navigation, and export.

Source:
[Saving images](https://igv.org/doc/desktop/UserGuide/saving_images/)

## Native rewrite recommendation

### MVP

- Native macOS shell with standard menus, toolbar, dialogs, dockable panels, and session persistence.
- Genome selection plus locus parsing for `All`, coordinates, gene symbols, and space-separated multi-locus entries.
- Local file loading, URL loading, ROI overlays, and PNG snapshots.
- JSON-native sessions with a future importer for IGV XML.

### Phase 2

- Reference sequence rendering.
- Annotation and quantitative track layout.
- Track height, autoscale, visibility windows, and preferences.
- Hosted genome and hosted track catalogs.

### Phase 3

- BAM, CRAM, and SAM review.
- Coverage and pileup rendering.
- Paired-end, splice-junction, and RNA-specific controls.
- VCF and mutation review.

### Phase 4

- Batch commands.
- External control API.
- Remote auth and cloud-backed datasets.
- Higher-fidelity export, including SVG.

## Why Rust + Qt fits

- Qt gives us a real native macOS desktop shell immediately: menus, file dialogs, docking, graphics, printing, and app bundles.
- Rust is a strong fit for the session model, genomics parsers, tile caches, and rendering back-ends where correctness and performance matter.
- A clean FFI seam lets the UI iterate quickly without locking the data engine to C++.
