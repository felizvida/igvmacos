# Case Manifest

`igv-native-case/v1` is the first case-first workspace format for the native desktop app.

The goal is to let a researcher open one manifest and immediately land in a usable review state with readiness feedback.

## Supported fields

```json
{
  "schema": "igv-native-case/v1",
  "title": "EGFR Review",
  "description": "Tumor-normal review workspace",
  "session_file": "sessions/review.xml",
  "session": {},
  "genome": "GRCh38/hg38",
  "genomes": ["GRCh38/hg38", "hg19"],
  "locus": "chr7:55086714-55275034",
  "loci": ["chr7:55174772-55174772", "chr7:55242465-55242465"],
  "multi_locus": true,
  "tracks": [],
  "rois": [],
  "review_queue": [],
  "sample_info_files": ["metadata/sample-info.tsv"]
}
```

## Behavior

- `session_file` can reference a native JSON session or an IGV XML session.
- `session` can embed a native JSON session directly in the manifest.
- top-level `tracks` append to the referenced or embedded session.
- top-level `rois` append to the referenced or embedded session.
- top-level `review_queue` entries append to the session review queue and merge by locus with imported ROIs and loci.
- relative paths resolve from the manifest location, except tracks inside a referenced session file, which resolve from that session file.

## Track fields

Each track entry supports:

```json
{
  "name": "Tumor DNA",
  "source": "tracks/tumor.bam",
  "kind": "alignment",
  "visibility": "zoomed",
  "requires_index": true,
  "index_source": "tracks/tumor.bam.bai",
  "expected_genome": "GRCh38/hg38",
  "group": "tumor"
}
```

## Current readiness checks

- missing local track files
- missing local BAM, CRAM, BCF, and tabix-style sidecar indexes
- missing declared sample metadata files
- empty sample metadata tables
- explicit track genome mismatches against the active session genome

## Review queue items

Each review queue entry supports:

```json
{
  "locus": "chr7:55174772-55174772",
  "label": "EGFR L858R",
  "status": "follow-up",
  "note": "Supportive in tumor, check copy-number context."
}
```

Current behavior:

- review items merge by `locus`
- imported ROIs seed queue items with their ROI labels
- imported loci seed pending queue items when no explicit review item exists yet
- explicit `status` and `note` fields are preserved when a manifest or session already defines them
- the desktop app can export a review packet that includes queue status, notes, visible tracks for the active preset, readiness issues, and a PNG snapshot
- the desktop app can auto-advance to the next pending queue item after `reviewed`
- the desktop app can export an HTML review report that captures one snapshot per review item

## Sample matching and cohort presets

When sample metadata is available, the app now tries to match tracks to sample records using sample-like columns such as:

- `sample`
- `sample_id`
- `sample name`
- `id`
- `name`

If a matching group-like column is also present, such as `group`, `cohort`, `sample_type`, or `condition`, the app uses it to enrich track grouping and generate cohort presets.

Current preset types:

- `All Tracks`
- `Matched Samples`
- one preset per detected group
- `Tumor vs Normal` when both groups are present

## Case folder autodiscovery

The desktop app can also open a folder directly without a manifest.

Current behavior:

- recursively prefers an existing `*.igvcase.json` or `*.case.json` manifest
- otherwise prefers a native session JSON, then an IGV XML session
- autodiscovers supported track files in the folder tree
- auto-links common sidecar indexes like `.bai`, `.crai`, `.tbi`, and `.csi`
- autodiscovers sample metadata files whose names include `sample`, `attribute`, or `metadata`
- parses those sample metadata files into the native sample table
- builds cohort presets from matched sample metadata and inferred track groups
- seeds the review queue from imported loci and ROIs

## Current scope limits

- readiness is local-file oriented and does not verify remote URLs
- sample info parsing is basic and expects plain tabular text, not quoted or nested formats
- autodiscovery does not yet infer cohort relationships beyond simple path/name heuristics
- manifests do not yet include autosave or recent-case history
