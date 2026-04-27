# Researcher Workflow Roadmap

90-day strategic plan for turning the current native macOS IGV prototype into a daily-use research workbench.

## Why this roadmap exists

The current prototype already proves the direction:

- Native macOS windowing and navigation shell
- JSON-native sessions
- Best-effort IGV XML import
- Multi-locus preview
- ROI overlays
- PNG snapshot export
- Parser tests and a basic smoke-test path

That is a good base, but it is not yet a researcher's daily home. The next phase should not optimize only for parity with legacy IGV Desktop. It should optimize for the real daily loop:

1. Open a case quickly
2. Get to the right loci quickly
3. Compare samples and tracks with context
4. Record findings while reviewing
5. Export evidence that others can reopen and trust

## North star

Build a native genomic review workbench where a researcher can move from raw case inputs to a reproducible evidence package without switching between IGV, spreadsheets, screenshots, notes, and one-off scripts.

## Product thesis

IGV's core primitives are still correct:

- loci navigation
- sessions
- ROIs and gene lists
- sample attributes
- alignment sorting and grouping
- variant review
- batch scripts
- external control

What is missing for daily work is orchestration around those primitives:

- case-centric entry instead of file-by-file loading
- persistent review state
- cohort-aware comparison controls
- one-click evidence packaging
- automation templates for repeated analysis tasks

## Target users

### 1. Variant reviewer

- Opens BAM, CRAM, VCF, BED, copy-number, and annotation tracks together
- Works through a list of candidate loci
- Needs quick sample comparison and exportable evidence

### 2. Translational researcher

- Reviews cohorts and subsets, not just one locus at a time
- Needs sample attributes, saved filters, and group presets
- Wants reports that can be shared with collaborators

### 3. Computational biologist

- Wants reproducibility, automation, and command-line entry points
- Needs session import/export and machine-readable manifests
- Cares about portability and scripted workflows

## Strategic principles

### Case-first, not file-first

The main object should become a case workspace or manifest, not a pile of manually loaded tracks.

### Review-state is first-class data

ROIs, notes, bookmarks, review status, panel layout, and export history should live with the case.

### Local-first and reproducible

No surprise uploads. Every GUI action that matters should be replayable from a session, manifest, or script.

### Fast path for common tasks

The best workflow should be obvious for:

- "open this case"
- "jump through this review list"
- "compare tumor and normal"
- "export what I found"

### Interop over lock-in

Preserve import/export compatibility with IGV Desktop, and add bridge formats for igv.js and report-style sharing.

## Outcome we want after 90 days

By the end of the first 90 days, a researcher should be able to:

- open a case manifest or IGV XML session
- see a validated readiness summary
- move through a saved queue of loci and ROIs
- compare samples using attributes and presets
- attach notes and review state to loci
- export a reproducible review packet
- rerun the same workflow from CLI or batch templates

## Core workstreams

### A. Foundation and trust

Purpose:
make the application dependable enough that researchers trust it with real work

Repo areas:

- [CMakeLists.txt](/Users/liux17/codex/igv/CMakeLists.txt)
- [src/lib.rs](/Users/liux17/codex/igv/src/lib.rs)
- [desktop/SessionParsers.cpp](/Users/liux17/codex/igv/desktop/SessionParsers.cpp)
- [tests/session_parsers_test.cpp](/Users/liux17/codex/igv/tests/session_parsers_test.cpp)

Key outputs:

- portable build and CI
- stronger parser and session tests
- crash recovery and autosave policy
- stable session and manifest schema versioning

### B. Case workspace

Purpose:
replace ad hoc file loading with a structured researcher workspace

Repo areas:

- [desktop/MainWindow.cpp](/Users/liux17/codex/igv/desktop/MainWindow.cpp)
- new manifest/session model files in `src/` and `desktop/`

Key outputs:

- case manifest loader
- paired file/index discovery
- dataset health checks
- recent cases and autosave recovery

### C. Review workbench

Purpose:
turn the viewer into a review environment instead of a passive browser

Repo areas:

- [desktop/GenomeCanvas.cpp](/Users/liux17/codex/igv/desktop/GenomeCanvas.cpp)
- new ROI navigator, review queue, notes, and comparison panels

Key outputs:

- ROI navigator
- multi-locus review queue
- sample/group presets
- locus-level notes and statuses

### D. Evidence and export

Purpose:
let users leave the app with a shareable, reproducible result

Repo areas:

- `src/` export/report code
- `desktop/` export dialogs and status UI
- `docs/` examples and templates

Key outputs:

- review packet export
- HTML report export
- richer snapshot flows
- session-plus-assets bundle export

### E. Automation and ecosystem

Purpose:
support repeated workflows and downstream integration

Repo areas:

- `src/` automation engine
- `desktop/` tools and command panels

Key outputs:

- batch templates
- CLI entry points
- external control API
- igv.js-compatible export targets where practical

## 90-day roadmap

## Phase 0: Weeks 1-2

### Theme

Trust, contracts, and instrumentation

### Goals

- Make build, startup, sessions, and import paths reliable
- Define what a "case workspace" will be
- Add enough tests to protect future iteration

### Deliverables

- CI for build plus CTest execution
- Session parser contract tests expanded beyond the current XML cases
- Session schema version policy and migration notes
- Draft case manifest schema
- Autosave and crash-recovery design
- Performance logging for startup, session load, and snapshot export

### Specific backlog

- Add GitHub Actions for `cmake --build` and `ctest`
- Add fixtures for real-ish IGV XML sessions with nested tracks and edge cases
- Add tests for invalid/missing indexes and malformed paths
- Define `case.json` or `workspace.json` schema
- Add startup diagnostics panel for environment and file readiness

### Exit criteria

- Clean build on a second machine or CI
- XML and native session parsing covered by tests
- Clear schema and ownership for sessions versus case manifests

## Phase 1: Weeks 3-6

### Theme

Case-first workspace

### Goals

- Replace "load a file" as the main workflow with "open a case"
- Reduce setup time and setup errors for common review sessions

### Deliverables

- Case manifest open/create flow
- Auto-discovery of companion files like `.bai`, `.crai`, `.tbi`
- Genome mismatch and missing-index warnings
- Recent cases and autosave restore UI
- Structured side panel for tracks, samples, ROIs, and readiness

### Specific backlog

- Add manifest parser in Rust core
- Add "Open Case Folder" and "Open Manifest" actions
- Implement readiness checks:
  - missing index
  - unresolved relative paths
  - duplicate sample names
  - genome mismatch
- Add local case cache and recent history

### Researcher value

This phase removes one of the biggest sources of friction: manual setup and silent misconfiguration.

### Exit criteria

- A user can open one folder or manifest and land in a usable review state
- The app surfaces what's broken before the user wastes time navigating

## Phase 2: Weeks 7-10

### Theme

Review workbench

### Goals

- Make the app useful for daily review sessions, not just display
- Turn loci, ROIs, and sample attributes into active workflow tools

### Deliverables

- ROI navigator with import/export and list management
- Review queue for loci and gene lists
- Sample info panel and cohort filters
- Saved sample grouping and coloring presets
- Notes and statuses on loci:
  - unreviewed
  - interesting
  - needs follow-up
  - exported

### Specific backlog

- Build ROI navigator modeled on IGV's region workflow, but with richer state
- Support BED/GMT/GRP review lists as queue inputs
- Add review annotations stored in native session/manifest
- Add cohort controls using sample information files
- Make multi-locus view act as a queue and comparison surface

### Researcher value

This phase upgrades the app from "viewer" to "review cockpit."

### Exit criteria

- A reviewer can work through a 20-locus list without external notes
- Sample grouping/filtering is fast and saved with the workspace

## Phase 3: Weeks 11-13

### Theme

Evidence packaging and automation

### Goals

- Help users leave the app with something reproducible and shareable
- Make repeated workflows scriptable

### Deliverables

- Review packet export:
  - session
  - loci list
  - ROI set
  - notes
  - snapshots
- HTML report export for collaborator sharing
- CLI entry points for:
  - open case
  - load session
  - export snapshots
  - export report
- External command listener or scripting bridge

### Specific backlog

- Add a report model in Rust core
- Implement export bundling in a deterministic directory layout
- Add snapshot templates and batch actions
- Define minimal command surface modeled after IGV batch and port commands
- Add "record actions to script" groundwork

### Researcher value

This phase shortens the path from review to collaboration and from GUI exploration to reproducible reruns.

### Exit criteria

- A collaborator can reopen the exported packet and understand the review state
- Common export tasks can run without manual clicking through the full UI

## Milestones

### Milestone 1: Foundation Alpha

Target:
end of week 2

Definition:

- tests and CI are real
- sessions are reliable
- manifest schema exists

### Milestone 2: Case Workspace Beta

Target:
end of week 6

Definition:

- case-first open flow is live
- readiness checks catch common failures
- recent/autosaved workspaces are usable

### Milestone 3: Review Workbench Beta

Target:
end of week 10

Definition:

- ROI navigator
- review queue
- sample attribute controls
- note/status persistence

### Milestone 4: Researcher Pilot Release

Target:
end of week 13

Definition:

- exportable review packet
- automation entry points
- pilot users can perform real review work with it

## Success metrics

### Product metrics

- Time from launch to first interpretable locus: under 2 minutes for a prepared case
- Time from case open to first exportable evidence package: under 10 minutes
- Number of manual setup steps before review starts: cut by at least 50 percent from current prototype flow
- Percentage of session opens that surface problems before review starts: over 90 percent for missing-index/genome mismatch cases

### Workflow metrics

- Reviewing 20 loci requires no external note-taking for normal usage
- A user can reopen yesterday's work without reconstructing state
- Shared export packages reopen deterministically on another machine

### Quality metrics

- Build and test green in CI
- Parser coverage for representative native and IGV XML session cases
- GUI smoke path for launch, open session, export snapshot, and open case

## What not to do in the first 90 days

- Do not attempt full igvtools replacement immediately
- Do not build broad cloud-auth support before case manifests and local workflow are solid
- Do not over-invest in rendering perfection before the review workflow exists
- Do not turn note-taking into a generic ELN
- Do not add AI summarization before the evidence model is structured and reproducible

## Recommended first execution slice

If there is only one place to start, start here:

1. Stabilize foundation and CI
2. Define and implement the case manifest
3. Add readiness checks and recent/autosave recovery

Reason:

everything else depends on researchers trusting that opening a case is fast, correct, and repeatable.

## Suggested repo map for ownership

### Rust core

- `src/`

Own:

- manifest schema
- session schema
- validation
- export/report model
- automation engine

### Native desktop shell

- `desktop/`

Own:

- windows, panels, toolbar, dialogs
- review queue UI
- ROI navigator UI
- sample attribute controls
- export flows

### Tests

- `tests/`

Own:

- parser contracts
- manifest validation
- export bundle checks
- smoke harnesses

### Documentation

- `docs/`

Own:

- roadmap
- schema docs
- pilot workflow guides
- sample manifests and example datasets

## Immediate next sprint recommendation

### Sprint goal

Make case opening feel trustworthy.

### Sprint scope

- Add CI
- Add manifest schema draft and examples
- Implement "Open Case Manifest"
- Add readiness summary panel
- Add tests for:
  - relative path resolution
  - missing indexes
  - genome mismatch detection
  - malformed XML and malformed manifest errors

### Done means

- a user opens one manifest
- the app tells them what is ready or broken
- they can begin review without manual reconstruction

## Sources informing this strategy

- [IGV Desktop overview](https://igv.org/doc/desktop)
- [Navigating the view](https://igv.org/doc/desktop/UserGuide/navigation/)
- [Regions and gene lists](https://igv.org/doc/desktop/UserGuide/regions/)
- [Sessions and autosave](https://igv.org/doc/desktop/UserGuide/sessions/)
- [Alignments basics](https://igv.org/doc/desktop/UserGuide/tracks/alignments/viewing_alignments_basics/)
- [VCF sample review](https://igv.org/doc/desktop/UserGuide/tracks/vcf/)
- [Batch scripts](https://igv.org/doc/desktop/UserGuide/tools/batch/)
- [External control](https://igv.org/doc/desktop/UserGuide/advanced/external_control/)
- [Sample information](https://igv.org/doc/desktop/FileFormats/SampleInfo/)
- [IGV-Web overview](https://igv.org/doc/webapp/)
- [igv.js Browser API](https://igv.org/doc/igvjs/Browser-API/)
- [igv-reports repository](https://github.com/igvteam/igv-reports)
