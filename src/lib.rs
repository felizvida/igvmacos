use std::collections::HashMap;
use std::ffi::{c_char, CStr, CString};
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::sync::Mutex;

static ALIGNMENT_PREVIEW_JSON: Mutex<Option<CString>> = Mutex::new(None);

#[derive(Clone, Debug)]
struct Interval {
    contig: String,
    start: u64,
    end: u64,
}

#[derive(Clone, Debug)]
struct PreviewRead {
    start: u64,
    end: u64,
    split: bool,
}

#[derive(Clone, Debug)]
struct PreviewJunction {
    start: u64,
    end: u64,
    count: u32,
}

#[derive(Clone, Debug)]
struct SamPreview {
    read_count: u32,
    split_read_count: u32,
    max_coverage: u32,
    coverage_bins: Vec<u32>,
    reads: Vec<PreviewRead>,
    junctions: Vec<PreviewJunction>,
}

fn json_escape(value: &str) -> String {
    let mut escaped = String::with_capacity(value.len() + 8);
    for character in value.chars() {
        match character {
            '\\' => escaped.push_str("\\\\"),
            '"' => escaped.push_str("\\\""),
            '\n' => escaped.push_str("\\n"),
            '\r' => escaped.push_str("\\r"),
            '\t' => escaped.push_str("\\t"),
            _ => escaped.push(character),
        }
    }
    escaped
}

fn parse_locus_interval(locus: &str) -> Option<Interval> {
    let trimmed = locus.trim();
    let (contig, coordinates) = trimmed.split_once(':')?;
    let (start_text, end_text) = coordinates.split_once('-').unwrap_or((coordinates, coordinates));
    let start = start_text.replace(',', "").parse::<u64>().ok()?;
    let end = end_text.replace(',', "").parse::<u64>().ok()?;
    let (start, end) = if end < start { (end, start) } else { (start, end) };
    Some(Interval {
        contig: contig.trim().to_string(),
        start,
        end,
    })
}

fn strip_chr_prefix(value: &str) -> &str {
    value.strip_prefix("chr")
        .or_else(|| value.strip_prefix("CHR"))
        .unwrap_or(value)
}

fn contigs_match(lhs: &str, rhs: &str) -> bool {
    lhs == rhs || strip_chr_prefix(lhs) == strip_chr_prefix(rhs)
}

fn ranges_overlap(lhs_start: u64, lhs_end: u64, rhs_start: u64, rhs_end: u64) -> bool {
    lhs_start <= rhs_end && rhs_start <= lhs_end
}

fn parse_cigar(cigar: &str, alignment_start: u64) -> Option<(Vec<(u64, u64)>, Vec<(u64, u64)>, bool)> {
    if cigar.is_empty() || cigar == "*" {
        return None;
    }

    let mut blocks = Vec::new();
    let mut junctions = Vec::new();
    let mut split = false;
    let mut digits = String::new();
    let mut ref_pos = alignment_start;

    for character in cigar.chars() {
        if character.is_ascii_digit() {
            digits.push(character);
            continue;
        }

        let length = digits.parse::<u64>().ok()?;
        digits.clear();

        match character {
            'M' | '=' | 'X' => {
                let block_start = ref_pos;
                ref_pos = ref_pos.saturating_add(length);
                let block_end = ref_pos.saturating_sub(1);
                if block_end >= block_start {
                    blocks.push((block_start, block_end));
                }
            }
            'D' => {
                ref_pos = ref_pos.saturating_add(length);
            }
            'N' => {
                split = true;
                let junction_start = ref_pos;
                ref_pos = ref_pos.saturating_add(length);
                let junction_end = ref_pos.saturating_sub(1);
                if junction_end >= junction_start {
                    junctions.push((junction_start, junction_end));
                }
            }
            'I' | 'S' | 'H' | 'P' => {}
            _ => return None,
        }
    }

    if !digits.is_empty() {
        return None;
    }

    if blocks.is_empty() {
        return None;
    }

    Some((blocks, junctions, split))
}

fn build_coverage_bins(coverage: &[u32], desired_bins: usize) -> Vec<u32> {
    if coverage.is_empty() {
        return vec![0];
    }

    let bin_count = desired_bins.max(1).min(coverage.len());
    let mut bins = Vec::with_capacity(bin_count);

    for bin_index in 0..bin_count {
        let start = bin_index * coverage.len() / bin_count;
        let mut end = (bin_index + 1) * coverage.len() / bin_count;
        if end <= start {
            end = (start + 1).min(coverage.len());
        }

        let mut max_value = 0;
        for value in &coverage[start..end] {
            max_value = max_value.max(*value);
        }
        bins.push(max_value);
    }

    bins
}

fn resolve_local_alignment_source(source: &str) -> Result<String, String> {
    let trimmed = source.trim();
    if trimmed.is_empty() {
        return Err("Track source is empty.".to_string());
    }
    if trimmed.starts_with("file://") {
        return Ok(trimmed.trim_start_matches("file://").to_string());
    }
    if trimmed.contains("://") {
        return Err("Real alignment previews are currently supported only for local SAM files.".to_string());
    }
    let lower = trimmed.to_ascii_lowercase();
    if lower.ends_with(".sam") {
        return Ok(trimmed.to_string());
    }
    if lower.ends_with(".bam") || lower.ends_with(".cram") {
        return Err("Real alignment previews are currently implemented for local SAM files. BAM and CRAM support are still pending.".to_string());
    }
    Err("Alignment preview expects a local SAM, BAM, or CRAM source.".to_string())
}

fn summarize_sam_alignment(source: &str, locus: &str) -> Result<SamPreview, String> {
    let path = resolve_local_alignment_source(source)?;
    let interval = parse_locus_interval(locus)
        .ok_or_else(|| "Alignment preview requires a coordinate locus such as chr1:100-200.".to_string())?;

    let span = interval
        .end
        .checked_sub(interval.start)
        .and_then(|value| value.checked_add(1))
        .ok_or_else(|| "Requested locus span is too large to preview.".to_string())? as usize;

    let file = File::open(&path)
        .map_err(|error| format!("Could not open alignment source {}: {}", path, error))?;
    let reader = BufReader::new(file);

    let mut coverage_diff = vec![0i32; span + 1];
    let mut read_count = 0u32;
    let mut split_read_count = 0u32;
    let mut preview_reads = Vec::new();
    let mut junction_counts: HashMap<(u64, u64), u32> = HashMap::new();

    for line_result in reader.lines() {
        let line = line_result.map_err(|error| format!("Could not read {}: {}", path, error))?;
        if line.is_empty() || line.starts_with('@') {
            continue;
        }

        let mut fields = line.split('\t');
        let _qname = fields.next();
        let _flag = fields.next();
        let rname = match fields.next() {
            Some(value) => value,
            None => continue,
        };
        if !contigs_match(rname, &interval.contig) {
            continue;
        }

        let pos = match fields.next().and_then(|value| value.parse::<u64>().ok()) {
            Some(value) if value > 0 => value,
            _ => continue,
        };

        let _mapq = fields.next();
        let cigar = match fields.next() {
            Some(value) => value,
            None => continue,
        };

        let (blocks, junctions, split) = match parse_cigar(cigar, pos) {
            Some(value) => value,
            None => continue,
        };

        let read_start = blocks.first().map(|block| block.0).unwrap_or(pos);
        let read_end = blocks.last().map(|block| block.1).unwrap_or(pos);
        if !ranges_overlap(read_start, read_end, interval.start, interval.end) {
            continue;
        }

        read_count = read_count.saturating_add(1);
        if split {
            split_read_count = split_read_count.saturating_add(1);
        }

        if preview_reads.len() < 24 {
            preview_reads.push(PreviewRead {
                start: read_start,
                end: read_end,
                split,
            });
        }

        for (block_start, block_end) in blocks {
            if !ranges_overlap(block_start, block_end, interval.start, interval.end) {
                continue;
            }

            let overlap_start = block_start.max(interval.start);
            let overlap_end = block_end.min(interval.end);
            let start_index = (overlap_start - interval.start) as usize;
            let end_index = (overlap_end - interval.start + 1) as usize;
            coverage_diff[start_index] += 1;
            if end_index < coverage_diff.len() {
                coverage_diff[end_index] -= 1;
            }
        }

        for (junction_start, junction_end) in junctions {
            if ranges_overlap(junction_start, junction_end, interval.start, interval.end) {
                *junction_counts.entry((junction_start, junction_end)).or_insert(0) += 1;
            }
        }
    }

    let mut coverage = Vec::with_capacity(span);
    let mut running = 0i32;
    let mut max_coverage = 0u32;
    for value in coverage_diff.into_iter().take(span) {
        running += value;
        let normalized = running.max(0) as u32;
        max_coverage = max_coverage.max(normalized);
        coverage.push(normalized);
    }

    let mut junctions: Vec<PreviewJunction> = junction_counts
        .into_iter()
        .map(|((start, end), count)| PreviewJunction { start, end, count })
        .collect();
    junctions.sort_by_key(|junction| (junction.start, junction.end));

    Ok(SamPreview {
        read_count,
        split_read_count,
        max_coverage,
        coverage_bins: build_coverage_bins(&coverage, 24),
        reads: preview_reads,
        junctions,
    })
}

fn build_alignment_preview_json_string(source: &str, locus: &str) -> String {
    match summarize_sam_alignment(source, locus) {
        Ok(preview) => {
            let coverage_bins = preview
                .coverage_bins
                .iter()
                .map(|value| value.to_string())
                .collect::<Vec<_>>()
                .join(",");
            let reads = preview
                .reads
                .iter()
                .map(|read| {
                    format!(
                        "{{\"start\":{},\"end\":{},\"split\":{}}}",
                        read.start,
                        read.end,
                        if read.split { "true" } else { "false" }
                    )
                })
                .collect::<Vec<_>>()
                .join(",");
            let junctions = preview
                .junctions
                .iter()
                .map(|junction| {
                    format!(
                        "{{\"start\":{},\"end\":{},\"count\":{}}}",
                        junction.start, junction.end, junction.count
                    )
                })
                .collect::<Vec<_>>()
                .join(",");

            format!(
                concat!(
                    "{{",
                    "\"ok\":true,",
                    "\"source\":\"{}\",",
                    "\"locus\":\"{}\",",
                    "\"message\":\"Loaded {} overlapping SAM read(s).\",",
                    "\"read_count\":{},",
                    "\"split_read_count\":{},",
                    "\"max_coverage\":{},",
                    "\"coverage_bins\":[{}],",
                    "\"reads\":[{}],",
                    "\"junctions\":[{}]",
                    "}}"
                ),
                json_escape(source),
                json_escape(locus),
                preview.read_count,
                preview.read_count,
                preview.split_read_count,
                preview.max_coverage,
                coverage_bins,
                reads,
                junctions
            )
        }
        Err(message) => format!(
            "{{\"ok\":false,\"source\":\"{}\",\"locus\":\"{}\",\"message\":\"{}\"}}",
            json_escape(source),
            json_escape(locus),
            json_escape(&message)
        ),
    }
}

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

#[no_mangle]
pub extern "C" fn igv_alignment_preview_json(
    source: *const c_char,
    locus: *const c_char,
) -> *const c_char {
    let source = unsafe { source.as_ref() }
        .and_then(|_| unsafe { CStr::from_ptr(source).to_str().ok() })
        .unwrap_or_default();
    let locus = unsafe { locus.as_ref() }
        .and_then(|_| unsafe { CStr::from_ptr(locus).to_str().ok() })
        .unwrap_or_default();

    let response = build_alignment_preview_json_string(source, locus);
    let c_string = CString::new(response)
        .unwrap_or_else(|_| CString::new("{\"ok\":false,\"message\":\"Could not encode alignment preview.\"}").unwrap());

    let mut buffer = ALIGNMENT_PREVIEW_JSON.lock().unwrap();
    *buffer = Some(c_string);
    buffer
        .as_ref()
        .map(|value| value.as_ptr())
        .unwrap_or(std::ptr::null())
}

#[cfg(test)]
mod tests {
    use super::build_alignment_preview_json_string;
    use std::env;
    use std::fs;
    use std::process;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn unique_temp_path(file_name: &str) -> std::path::PathBuf {
        let timestamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        env::temp_dir().join(format!(
            "igv-native-core-{}-{}-{}",
            process::id(),
            timestamp,
            file_name
        ))
    }

    #[test]
    fn sam_alignment_preview_counts_reads_and_junctions() {
        let sam_path = unique_temp_path("preview.sam");
        let sam = concat!(
            "@HD\tVN:1.6\tSO:coordinate\n",
            "@SQ\tSN:chr1\tLN:1000\n",
            "read1\t0\tchr1\t100\t60\t10M\t*\t0\t0\tAAAAAAAAAA\t*\n",
            "read2\t0\tchr1\t108\t60\t6M4N6M\t*\t0\t0\tCCCCCCCCCCCC\t*\n",
            "read3\t0\tchr2\t100\t60\t10M\t*\t0\t0\tTTTTTTTTTT\t*\n"
        );
        fs::write(&sam_path, sam).unwrap();

        let json = build_alignment_preview_json_string(
            sam_path.to_string_lossy().as_ref(),
            "chr1:100-130",
        );

        fs::remove_file(&sam_path).ok();

        assert!(json.contains("\"ok\":true"));
        assert!(json.contains("\"read_count\":2"));
        assert!(json.contains("\"split_read_count\":1"));
        assert!(json.contains("\"max_coverage\":2"));
        assert!(json.contains("\"junctions\":[{\"start\":114,\"end\":117,\"count\":1}]"));
    }

    #[test]
    fn bam_alignment_preview_reports_pending_support() {
        let json = build_alignment_preview_json_string("/tmp/example.bam", "chr1:100-200");
        assert!(json.contains("\"ok\":false"));
        assert!(json.contains("BAM and CRAM support are still pending"));
    }
}
