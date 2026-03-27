use std::ffi::c_char;

const FEATURE_INVENTORY_JSON: &str = concat!(
    r#"
{
  "features": [
    {
      "category": "Navigation",
      "name": "Whole-genome, chromosome, locus, gene, and mutation search",
      "phase": "MVP",
      "status": "scaffolded",
      "notes": "Support All, chr:start-end, gene symbols, mutation shortcuts such as KRAS:G12C, and multi-locus inputs."
    },
    {
      "category": "Reference",
      "name": "Reference genome switching and hosted genome metadata",
      "phase": "MVP",
      "status": "planned",
      "notes": "Load curated genomes first, then add local FASTA, 2bit, and GenBank ingest with index awareness."
    },
    {
      "category": "Tracks",
      "name": "Track loading from files, URLs, and hosted catalogs",
      "phase": "MVP",
      "status": "scaffolded",
      "notes": "Native shell already supports local files and URLs. Hosted catalogs and cloud auth are follow-on work."
    },
    {
      "category": "Sessions",
      "name": "Session persistence and reload",
      "phase": "MVP",
      "status": "scaffolded",
      "notes": "Prototype saves and opens a native JSON session schema. IGV XML import belongs in the next phase."
    },
    {
      "category": "Rendering",
      "name": "Track ordering, visibility windows, layout modes, and autoscale",
      "phase": "Phase 2",
      "status": "planned",
      "notes": "Replace the preview canvas with dedicated annotation, quantitative, and coverage renderers."
    },
    {
      "category": "Export",
      "name": "PNG and SVG snapshot export",
      "phase": "MVP",
      "status": "partially implemented",
      "notes": "PNG snapshot export is live in the prototype. SVG export should arrive with the richer scene graph."
    },
    {
      "category": "ROI",
      "name": "Regions of interest, region navigator, and gene-list workflows",
      "phase": "MVP",
      "status": "scaffolded",
      "notes": "Prototype supports ROI overlays. Region collections and gene-list tooling are next."
    },
    {
      "category": "Alignments",
      "name": "BAM, SAM, and CRAM review with coverage, pairing, coloring, sorting, and grouping",
      "phase": "Phase 3",
      "status": "planned",
      "notes": "Add read pileups, paired-end layout, base coloring, splice junctions, and downsampling controls."
    },
    {
      "category": "RNA-seq",
      "name": "Split-read and splice-junction visualization",
      "phase": "Phase 3",
      "status": "planned",
      "notes": "Native canvas should expose junction arcs, sashimi-like summaries, and RNA-specific alignment toggles."
    },
    {
      "category": "Variants",
      "name": "VCF-driven variant review and genotype-aware display",
      "phase": "Phase 3",
      "status": "planned",
      "notes": "Track engine should support variant density, site detail popovers, and review-centric filtering."
    },
    {
      "category": "Automation",
      "name": "Batch scripts, reproducible snapshots, and external control",
      "phase": "Phase 4",
      "status": "planned",
      "notes": "Expose a native command layer for scriptable navigation, loading, and snapshot generation."
    },
    {
      "category": "Preferences",
      "name": "Global preferences and per-track display controls",
      "phase": "Phase 2",
      "status": "planned",
      "notes": "Move Java preferences into a native settings layer backed by JSON and macOS defaults integration."
    }
  ]
}
"#,
    "\0"
);

const ROADMAP_JSON: &str = concat!(
    r#"
{
  "milestones": [
    {
      "phase": "MVP",
      "objective": "Native macOS workbench",
      "deliverables": "Menus, toolbar, file and URL track loading, session JSON, loci parsing, ROI overlays, and PNG snapshots."
    },
    {
      "phase": "Phase 2",
      "objective": "Genome and track rendering parity",
      "deliverables": "Reference sequence, annotation layout, quantitative plots, autoscale, visibility windows, and richer preferences."
    },
    {
      "phase": "Phase 3",
      "objective": "Deep review workflows",
      "deliverables": "Alignment pileups, paired-end review, splice junctions, coverage tracks, and VCF-centric variant review."
    },
    {
      "phase": "Phase 4",
      "objective": "Automation and ecosystem fit",
      "deliverables": "Batch commands, session importers, hosted-track catalogs, cloud authentication, and reproducible export pipelines."
    }
  ]
}
"#,
    "\0"
);

const DESIGN_NOTES_MARKDOWN: &str = concat!(
    r#"
# Native Rewrite Notes

- Qt Widgets provides the native macOS shell, menus, dialogs, docks, and rendering surface.
- Rust owns stable product concepts: genomes, loci, sessions, tracks, feature catalogs, and future back-end parsers.
- The current canvas is a deliberate stepping stone. It proves the interaction model while leaving room for high-performance renderers later.
- Keep sessions JSON-first in the native app, then add an importer for IGV XML sessions once the track model is stable.
- Treat alignment and variant review as separate engines with shared viewport, scale, and annotation services.
- Favor a tiled render pipeline so the same viewport can back whole-genome, quantitative, annotation, and read-level zoom states.
"#,
    "\0"
);

const SOURCE_LINKS_MARKDOWN: &str = concat!(
    r#"
# Official Sources

- [IGV Desktop: Loading and removing tracks](https://igv.org/doc/desktop/UserGuide/loading_data/)
- [IGV Desktop: Navigating the view](https://igv.org/doc/desktop/UserGuide/navigation/)
- [IGV Desktop: Saving images](https://igv.org/doc/desktop/UserGuide/saving_images/)
- [IGV Desktop: Data track file formats](https://igv.org/doc/desktop/FileFormats/DataTracks/)
- [IGV Desktop: Paired-end alignments](https://igv.org/doc/desktop/UserGuide/tracks/alignments/paired_end_alignments/index.html)
- [IGV Desktop: RNA-seq data](https://igv.org/doc/desktop/UserGuide/tracks/alignments/rna_seq/)
- [IGV snapshot build notes](https://igv.org/doc/desktop/DownloadSnapshot/)
"#,
    "\0"
);

const DEMO_SESSION_JSON: &str = concat!(
    r#"
{
  "schema": "igv-native-session/v1",
  "genome": "GRCh38/hg38",
  "locus": "chr17:43,044,294-43,125,482",
  "multi_locus": false,
  "loci": [
    "chr17:43,044,294-43,125,482"
  ],
  "rois": [
    {
      "locus": "chr17:43,070,000-43,075,000",
      "label": "BRCA1 ROI"
    }
  ],
  "tracks": [
    {
      "name": "Reference Sequence",
      "kind": "reference",
      "source": "builtin://reference",
      "visibility": "always"
    },
    {
      "name": "RefSeq Genes",
      "kind": "annotation",
      "source": "builtin://genes",
      "visibility": "always"
    },
    {
      "name": "Tumor DNA BAM",
      "kind": "alignment",
      "source": "demo://tumor.bam",
      "visibility": "zoomed"
    },
    {
      "name": "RNA Junctions",
      "kind": "splice",
      "source": "demo://rna.bam",
      "visibility": "zoomed"
    },
    {
      "name": "Somatic Variants",
      "kind": "variant",
      "source": "demo://variants.vcf.gz",
      "visibility": "zoomed"
    },
    {
      "name": "Copy Number",
      "kind": "quantitative",
      "source": "demo://copy-number.seg",
      "visibility": "chromosome"
    }
  ],
  "genomes": [
    "GRCh38/hg38",
    "T2T-CHM13v2",
    "GRCm39/mm39",
    "hg19"
  ]
}
"#,
    "\0"
);

#[no_mangle]
pub extern "C" fn igv_feature_inventory_json() -> *const c_char {
    FEATURE_INVENTORY_JSON.as_ptr().cast()
}

#[no_mangle]
pub extern "C" fn igv_roadmap_json() -> *const c_char {
    ROADMAP_JSON.as_ptr().cast()
}

#[no_mangle]
pub extern "C" fn igv_design_notes_markdown() -> *const c_char {
    DESIGN_NOTES_MARKDOWN.as_ptr().cast()
}

#[no_mangle]
pub extern "C" fn igv_source_links_markdown() -> *const c_char {
    SOURCE_LINKS_MARKDOWN.as_ptr().cast()
}

#[no_mangle]
pub extern "C" fn igv_demo_session_json() -> *const c_char {
    DEMO_SESSION_JSON.as_ptr().cast()
}
