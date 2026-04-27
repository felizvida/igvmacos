# IGV Desktop Parity Backlog

Ranked backlog collected on April 27, 2026 by comparing the current native prototype against the official IGV Desktop documentation.

## How to read this

- `P0` means the native app is not yet a credible IGV replacement without it.
- `P1` means important researcher workflow or compatibility gaps remain.
- `P2` means advanced parity, power-user features, or polish.

The current native app already covers:

- native macOS shell
- case manifests and case-folder opening
- JSON-native sessions and best-effort IGV XML import
- sample-info parsing and cohort presets
- review queue, notes, and report export

This backlog only lists the major remaining gaps.

## P0

### 1. Native alignment engine

Why it matters:
IGV’s core value still depends on real BAM, CRAM, and SAM review, including coverage, splice junctions, visibility windows, and experiment-type behavior.

Missing today:

- no alignment pileup renderer
- no coverage track
- no splice-junction track
- no experiment type handling for RNA / long-read / short-read
- no visibility window behavior or zoom-to-load gating

Likely repo areas:

- [desktop/GenomeCanvas.cpp](/Users/liux17/codex/igv/desktop/GenomeCanvas.cpp)
- [src/lib.rs](/Users/liux17/codex/igv/src/lib.rs)
- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- new Rust alignment/tile/cache modules under `src/`

Exit criteria:

- BAM and CRAM open natively
- alignments load only within visibility windows
- coverage track is rendered
- RNA data shows splice junction track
- experiment type changes defaults and available options

Sources:
[Alignments basics](https://igv.org/doc/desktop/UserGuide/tracks/alignments/viewing_alignments_basics/)
[Paired-end alignments](https://igv.org/doc/desktop/UserGuide/tracks/alignments/paired_end_alignments/)
[RNA-seq data](https://igv.org/doc/desktop/UserGuide/tracks/alignments/rna_seq/)

### 2. Variant review surface

Why it matters:
Researchers use IGV to inspect genotype matrices, not just load a `.vcf.gz` file.

Missing today:

- no genotype-row rendering
- no sample sorting by genotype, depth, or quality
- no attribute-driven grouping for variant samples
- no feature-level variant review UI

Likely repo areas:

- [desktop/GenomeCanvas.cpp](/Users/liux17/codex/igv/desktop/GenomeCanvas.cpp)
- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- [desktop/SessionParsers.cpp](/Users/liux17/codex/igv/desktop/SessionParsers.cpp)
- new Rust VCF parsing and view-model code under `src/`

Exit criteria:

- VCF calls render at locus level
- genotype rows render when samples are present
- sample rows can sort by genotype, sample name, depth, and quality
- sample attributes can group variant rows

Sources:
[Variants (VCF)](https://igv.org/doc/desktop/UserGuide/tracks/vcf/)
[Sample attributes](https://igv.org/doc/desktop/UserGuide/sample_attributes/)

### 3. Quantitative track rendering and controls

Why it matters:
Copy-number, expression, methylation, coverage, and bigWig-style workflows depend on quantitative behavior, not just file detection.

Missing today:

- no real chart rendering
- no graph-type switching
- no autoscale or group autoscale
- no heatmap scale or log scale
- no windowing function support

Likely repo areas:

- [desktop/GenomeCanvas.cpp](/Users/liux17/codex/igv/desktop/GenomeCanvas.cpp)
- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- [src/lib.rs](/Users/liux17/codex/igv/src/lib.rs)

Exit criteria:

- quantitative tracks render as heatmap, bar, points, and line
- data range and autoscale controls work
- group autoscale works across selected tracks
- heatmap and log-scale controls persist in session state

Sources:
[Quantitative data](https://igv.org/doc/desktop/UserGuide/tracks/quantitative_data/)
[Combining tracks](https://igv.org/doc/desktop/UserGuide/tools/combine_tracks/)

### 4. Reference genome loading beyond a fixed menu

Why it matters:
IGV is a genome viewer first. Hosted genomes, custom genomes, GenArk hubs, and annotation bundles are core product surface.

Missing today:

- no hosted-genome browser
- no UCSC GenArk / track-hub flow
- no custom FASTA / 2bit / genome JSON / GenBank loading
- no automatic reference-sequence track behavior

Likely repo areas:

- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- [desktop/SessionParsers.cpp](/Users/liux17/codex/igv/desktop/SessionParsers.cpp)
- [src/lib.rs](/Users/liux17/codex/igv/src/lib.rs)

Exit criteria:

- custom genome assets load from file and URL
- hosted genome selection works
- GenArk-backed genomes and track selection work
- switching genomes clears or rebinds session state correctly

Sources:
[Reference genome](https://igv.org/doc/desktop/UserGuide/reference_genome/)

### 5. Automation parity: batch, port, and command line

Why it matters:
IGV is often embedded in reproducible pipelines through batch scripts, startup flags, and port commands.

Missing today:

- no batch command runner
- no port listener for external control
- no native CLI startup flags for genome, locus, tracks, batch file, index files, or headers

Likely repo areas:

- [src/lib.rs](/Users/liux17/codex/igv/src/lib.rs)
- [desktop/main.cpp](/Users/liux17/codex/igv/desktop/main.cpp)
- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- new automation / command parser modules under `src/`

Exit criteria:

- native app can execute an IGV-like batch file
- running app can accept port commands
- command-line launch supports genome, locus, tracks, indexes, headers, and batch

Sources:
[Batch scripts](https://igv.org/doc/desktop/UserGuide/tools/batch/)
[External control](https://igv.org/doc/desktop/UserGuide/advanced/external_control/)
[IGV from the command line](https://igv.org/doc/desktop/UserGuide/advanced/command_line/)

## P1

### 1. Full ROI navigator and ROI actions

Missing today:

- no dedicated Region Navigator
- no ROI import/export workflow
- no ROI-specific actions like copy sequence, BLAT sequence, scatter plot, delete, or navigator multi-select view

Likely repo areas:

- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- [desktop/GenomeCanvas.cpp](/Users/liux17/codex/igv/desktop/GenomeCanvas.cpp)
- [desktop/SessionParsers.cpp](/Users/liux17/codex/igv/desktop/SessionParsers.cpp)

Sources:
[Regions](https://igv.org/doc/desktop/UserGuide/regions/)

### 2. Complete sample-info compatibility

Missing today:

- no `#sampleMapping` support
- no `#colors` support
- no attribute color rules or heatmaps
- no full attribute-panel behavior for show/hide, selection, and filtering

Likely repo areas:

- [desktop/SessionParsers.cpp](/Users/liux17/codex/igv/desktop/SessionParsers.cpp)
- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- [tests/session_parsers_test.cpp](/Users/liux17/codex/igv/tests/session_parsers_test.cpp)

Sources:
[Sample Information](https://igv.org/doc/desktop/FileFormats/SampleInfo/)
[Sample attributes](https://igv.org/doc/desktop/UserGuide/sample_attributes/)

### 3. Hosted tracks and remote-track ergonomics

Missing today:

- no hosted-track catalog browser
- no explicit URL + index dialog parity
- no remote auth/header handling
- no data-server URL support

Likely repo areas:

- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- [desktop/SessionParsers.cpp](/Users/liux17/codex/igv/desktop/SessionParsers.cpp)
- [src/lib.rs](/Users/liux17/codex/igv/src/lib.rs)

Sources:
[Loading and removing tracks](https://igv.org/doc/desktop/UserGuide/loading_data/)
[IGV from the command line](https://igv.org/doc/desktop/UserGuide/advanced/command_line/)

### 4. Export parity

Missing today:

- no SVG export
- no per-panel export parity
- no batch-driven export
- no session-plus-assets bundle export

Likely repo areas:

- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- [desktop/GenomeCanvas.cpp](/Users/liux17/codex/igv/desktop/GenomeCanvas.cpp)
- [src/lib.rs](/Users/liux17/codex/igv/src/lib.rs)

Sources:
[Saving images](https://igv.org/doc/desktop/UserGuide/saving_images/)
[Batch scripts](https://igv.org/doc/desktop/UserGuide/tools/batch/)

### 5. BLAT integration

Missing today:

- no BLAT tool entry
- no BLAT from reads, features, or ROIs
- no BLAT result tracks or result table

Likely repo areas:

- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- [desktop/GenomeCanvas.cpp](/Users/liux17/codex/igv/desktop/GenomeCanvas.cpp)
- [src/lib.rs](/Users/liux17/codex/igv/src/lib.rs)

Sources:
[BLAT](https://igv.org/doc/desktop/UserGuide/tools/blat/)

## P2

### 1. Track operations and power-user menus

Missing today:

- track rename, remove, multi-select operations
- track height and display-mode controls
- richer per-track context menus by data type

Likely repo areas:

- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- [desktop/GenomeCanvas.cpp](/Users/liux17/codex/igv/desktop/GenomeCanvas.cpp)

Sources:
[Loading and removing tracks](https://igv.org/doc/desktop/UserGuide/loading_data/)
[Quantitative data](https://igv.org/doc/desktop/UserGuide/tracks/quantitative_data/)

### 2. Quantitative overlays and arithmetic tools

Missing today:

- overlay tracks
- separate overlaid tracks
- combine tracks with add/subtract/multiply/divide

Likely repo areas:

- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- [desktop/GenomeCanvas.cpp](/Users/liux17/codex/igv/desktop/GenomeCanvas.cpp)
- [src/lib.rs](/Users/liux17/codex/igv/src/lib.rs)

Sources:
[Quantitative data](https://igv.org/doc/desktop/UserGuide/tracks/quantitative_data/)
[Combining tracks](https://igv.org/doc/desktop/UserGuide/tools/combine_tracks/)

### 3. Preferences parity

Missing today:

- no real preferences surface for alignment, RNA, long-read, genome server, BLAT URL, or port settings
- no persistent app-level power-user configuration

Likely repo areas:

- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- [src/lib.rs](/Users/liux17/codex/igv/src/lib.rs)

Sources:
[Alignments basics](https://igv.org/doc/desktop/UserGuide/tracks/alignments/viewing_alignments_basics/)
[Reference genome](https://igv.org/doc/desktop/UserGuide/reference_genome/)
[External control](https://igv.org/doc/desktop/UserGuide/advanced/external_control/)
[BLAT](https://igv.org/doc/desktop/UserGuide/tools/blat/)

## Recommended build order

1. `P0.1` Native alignment engine
2. `P0.2` Variant review surface
3. `P0.3` Quantitative track rendering and controls
4. `P0.4` Reference genome loading beyond a fixed menu
5. `P1.2` Complete sample-info compatibility
6. `P0.5` Automation parity
7. `P1.1` Full ROI navigator and ROI actions
8. `P1.4` Export parity
9. `P1.5` BLAT integration

## Why this order

- Alignment, variant, and quantitative rendering close the biggest trust gap between the native shell and real IGV usage.
- Reference-genome loading is a hard dependency for many serious datasets.
- Sample-info compatibility is the shortest path from “viewer” to “researcher daily tool.”
- Automation should come after core renderers exist, so the scripted surface exposes real work rather than placeholders.
