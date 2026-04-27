#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

#include "SessionParsers.h"

namespace {

bool expectTrue(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool expectEqual(const QString& actual, const QString& expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << "\n  expected: " << expected.toStdString()
                  << "\n  actual:   " << actual.toStdString() << '\n';
        return false;
    }
    return true;
}

bool expectEqualInt(int actual, int expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << "\n  expected: " << expected
                  << "\n  actual:   " << actual << '\n';
        return false;
    }
    return true;
}

bool expectStringList(const QStringList& actual, const QStringList& expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << "\n  expected: " << expected.join(" | ").toStdString()
                  << "\n  actual:   " << actual.join(" | ").toStdString() << '\n';
        return false;
    }
    return true;
}

bool expectContains(const QString& actual, const QString& needle, const char* message) {
    if (!actual.contains(needle)) {
        std::cerr << "FAIL: " << message << "\n  missing:  " << needle.toStdString()
                  << "\n  actual:   " << actual.toStdString() << '\n';
        return false;
    }
    return true;
}

bool expectNotContains(const QString& actual, const QString& needle, const char* message) {
    if (actual.contains(needle)) {
        std::cerr << "FAIL: " << message << "\n  unexpected: " << needle.toStdString()
                  << "\n  actual:     " << actual.toStdString() << '\n';
        return false;
    }
    return true;
}

int countIssues(const QList<ReadinessIssue>& issues, const QString& severity, const QString& check) {
    int count = 0;
    for (const ReadinessIssue& issue : issues) {
        if ((severity.isEmpty() || issue.severity == severity) && (check.isEmpty() || issue.check == check)) {
            ++count;
        }
    }
    return count;
}

const TrackDescriptor* findTrackBySuffix(const QList<TrackDescriptor>& tracks, const QString& suffix) {
    for (const TrackDescriptor& track : tracks) {
        if (track.source.endsWith(suffix)) {
            return &track;
        }
    }
    return nullptr;
}

const CohortPreset* findPresetByName(const QList<CohortPreset>& presets, const QString& name) {
    for (const CohortPreset& preset : presets) {
        if (preset.name == name) {
            return &preset;
        }
    }
    return nullptr;
}

const ReviewItem* findReviewItemByLocus(const QList<ReviewItem>& items, const QString& locus) {
    for (const ReviewItem& item : items) {
        if (item.locus == locus) {
            return &item;
        }
    }
    return nullptr;
}

bool writeTextFile(const QString& fileName, const QByteArray& contents) {
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    file.write(contents);
    return true;
}

bool testParseLociText() {
    const QStringList loci = session_parsers::parseLociText(
        "chr1:10-20   chr2:30-40\nKRAS:G12C");
    return expectStringList(loci,
                            QStringList({"chr1:10-20", "chr2:30-40", "KRAS:G12C"}),
                            "parseLociText should split whitespace-delimited loci");
}

bool testLoadNativeSessionDefaults() {
    QJsonObject object;
    object.insert("genome", "hg19");
    object.insert("locus", "chr7:140453136-140453136");

    const SessionState session = session_parsers::loadNativeSession(object);
    return expectTrue(session.tracks.size() == 2, "native sessions without tracks should add builtin reference tracks") &&
           expectEqual(session.tracks.at(0).kind, "reference", "first builtin track should be reference") &&
           expectEqual(session.loci.value(0), "chr7:140453136-140453136", "native session should seed loci from locus text");
}

bool testAlignmentTrackDefaults() {
    QJsonObject object;
    QJsonArray tracks;
    QJsonObject track;
    track.insert("name", "Tumor RNA");
    track.insert("source", "/tmp/tumor-rna.bam");
    tracks.append(track);
    object.insert("tracks", tracks);

    const SessionState session = session_parsers::loadNativeSession(object);
    const TrackDescriptor& alignmentTrack = session.tracks.at(0);

    return expectEqual(alignmentTrack.kind, "alignment", "BAM tracks should load as alignment tracks") &&
           expectEqual(alignmentTrack.experimentType, "rna", "RNA-labeled alignment tracks should infer an RNA experiment type") &&
           expectEqualInt(alignmentTrack.visibilityWindowKb, 300, "RNA alignment tracks should default to a broader visibility window") &&
           expectTrue(alignmentTrack.showCoverageTrack, "alignment tracks should show a coverage lane by default") &&
           expectTrue(alignmentTrack.showAlignmentTrack, "alignment tracks should show read glyphs by default") &&
           expectTrue(alignmentTrack.showSpliceJunctionTrack, "RNA alignment tracks should show splice junctions by default");
}

bool testAlignmentTrackSettingsRoundTrip() {
    QJsonObject object;
    QJsonArray tracks;
    QJsonObject track;
    track.insert("name", "Long-read tumor support");
    track.insert("source", "/tmp/tumor-ont.bam");
    track.insert("experiment_type", "long-read");
    track.insert("visibility_window_kb", 180);
    track.insert("show_coverage", false);
    track.insert("show_alignments", true);
    track.insert("show_splice_junctions", false);
    tracks.append(track);
    object.insert("tracks", tracks);

    const SessionState session = session_parsers::loadNativeSession(object);
    const TrackDescriptor& alignmentTrack = session.tracks.at(0);

    return expectEqual(alignmentTrack.experimentType, "long-read", "explicit alignment experiment types should be preserved") &&
           expectEqualInt(alignmentTrack.visibilityWindowKb, 180, "explicit alignment visibility windows should be preserved") &&
           expectTrue(!alignmentTrack.showCoverageTrack, "explicit coverage visibility should be preserved") &&
           expectTrue(alignmentTrack.showAlignmentTrack, "explicit alignment glyph visibility should be preserved") &&
           expectTrue(!alignmentTrack.showSpliceJunctionTrack, "explicit splice-junction visibility should be preserved");
}

bool testLoadNativeSessionSeedsReviewQueue() {
    QJsonObject object;
    object.insert("genome", "GRCh38/hg38");
    object.insert("locus", "chr7:140453136-140453136");
    object.insert("loci", QJsonArray({"chr7:140453130-140453150", "chr12:25245350-25245350"}));

    QJsonArray rois;
    QJsonObject roi;
    roi.insert("locus", "chr7:140453130-140453150");
    roi.insert("label", "EGFR hotspot");
    rois.append(roi);
    object.insert("rois", rois);

    QJsonArray reviewQueue;
    QJsonObject seededItem;
    seededItem.insert("locus", "chr7:140453130-140453150");
    seededItem.insert("status", "follow-up");
    seededItem.insert("note", "Needs manual confirmation");
    reviewQueue.append(seededItem);

    QJsonObject manualItem;
    manualItem.insert("locus", "chr17:7674220-7674220");
    manualItem.insert("label", "TP53 review");
    manualItem.insert("status", "reviewed");
    reviewQueue.append(manualItem);
    object.insert("review_queue", reviewQueue);

    const SessionState session = session_parsers::loadNativeSession(object);
    const ReviewItem* roiItem = findReviewItemByLocus(session.reviewQueue, "chr7:140453130-140453150");
    const ReviewItem* locusItem = findReviewItemByLocus(session.reviewQueue, "chr12:25245350-25245350");
    const ReviewItem* manualReviewItem = findReviewItemByLocus(session.reviewQueue, "chr17:7674220-7674220");

    return expectTrue(session.reviewQueue.size() == 3, "native sessions should merge ROI, locus, and explicit review items") &&
           expectTrue(roiItem != nullptr, "ROI-backed review items should be present") &&
           expectEqual(roiItem->label, "EGFR hotspot", "ROI labels should seed review item labels") &&
           expectEqual(roiItem->status, "follow-up", "explicit review status should be preserved for matching loci") &&
           expectEqual(roiItem->note, "Needs manual confirmation", "explicit review notes should be preserved for matching loci") &&
           expectTrue(locusItem != nullptr, "visible loci should seed pending review items") &&
           expectEqual(locusItem->label, "Review Locus", "locus-seeded review items should get a default label") &&
           expectEqual(locusItem->status, "pending", "locus-seeded review items should default to pending") &&
           expectTrue(manualReviewItem != nullptr, "explicit review-only loci should be retained") &&
           expectEqual(manualReviewItem->label, "TP53 review", "manual review labels should be preserved") &&
           expectEqual(manualReviewItem->status, "reviewed", "manual review status should be preserved");
}

bool testLoadIgvXmlSessionImportsTracksAndRois() {
    QTemporaryDir tempDir;
    if (!expectTrue(tempDir.isValid(), "temporary directory should be created")) {
        return false;
    }

    QDir dir(tempDir.path());
    dir.mkpath("data");
    const QString sessionPath = dir.filePath("example.xml");
    const QByteArray xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Session genome="hg19" locus="chr1:100-200 chr2:300-400">
  <Resources>
    <Resource path="data/sample.bam" name="Tumor DNA"/>
    <Resource path="https://example.org/variants.vcf.gz"/>
  </Resources>
  <Panel name="DataPanel">
    <Track id="data/sample.bam" name="Tumor DNA"/>
    <Track id="https://example.org/variants.vcf.gz" name="Variants"/>
  </Panel>
  <Regions>
    <Region chromosome="chr1" start="110" end="120" description="Focus ROI"/>
  </Regions>
</Session>)";
    if (!expectTrue(writeTextFile(sessionPath, xml), "XML session fixture should be written")) {
        return false;
    }

    SessionState session;
    QString errorMessage;
    if (!expectTrue(session_parsers::loadIgvXmlSession(sessionPath, &session, &errorMessage),
                    "IGV XML session should import successfully")) {
        std::cerr << "  parser error: " << errorMessage.toStdString() << '\n';
        return false;
    }

    return expectEqual(session.schema, "igv-xml-session/v1", "XML import should tag the imported schema") &&
           expectEqual(session.genome, "hg19", "XML import should preserve session genome") &&
           expectTrue(session.multiLocus, "multi-locus session should be detected from locus text") &&
           expectStringList(session.loci,
                            QStringList({"chr1:100-200", "chr2:300-400"}),
                            "XML import should split session loci") &&
           expectTrue(session.tracks.size() == 2, "XML import should capture explicit tracks") &&
           expectEqual(session.tracks.at(0).source,
                       QDir::cleanPath(dir.filePath("data/sample.bam")),
                       "relative resource paths should resolve against the XML session") &&
           expectEqual(session.tracks.at(0).kind, "alignment", "BAM resources should map to alignment tracks") &&
           expectEqual(session.tracks.at(1).kind, "variant", "VCF resources should map to variant tracks") &&
           expectTrue(session.rois.size() == 1, "XML import should capture ROI regions") &&
           expectEqual(session.rois.at(0).locus, "chr1:110-120", "ROI coordinates should be normalized into locus text") &&
           expectEqual(session.rois.at(0).label, "Focus ROI", "ROI descriptions should become labels");
}

bool testLoadIgvXmlSessionFallsBackToResources() {
    QTemporaryDir tempDir;
    if (!expectTrue(tempDir.isValid(), "temporary directory should be created")) {
        return false;
    }

    QDir dir(tempDir.path());
    const QString sessionPath = dir.filePath("resources-only.xml");
    const QByteArray xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Session genome="T2T-CHM13v2" locus="chr8:1-1000">
  <Resources>
    <Resource path="copy-number.seg" name="Copy Number"/>
  </Resources>
</Session>)";
    if (!expectTrue(writeTextFile(sessionPath, xml), "XML session fixture should be written")) {
        return false;
    }

    SessionState session;
    QString errorMessage;
    if (!expectTrue(session_parsers::loadIgvXmlSession(sessionPath, &session, &errorMessage),
                    "resource-only XML session should import successfully")) {
        std::cerr << "  parser error: " << errorMessage.toStdString() << '\n';
        return false;
    }

    return expectTrue(session.tracks.size() == 1, "resource-only session should still produce a track list") &&
           expectEqual(session.tracks.at(0).kind, "quantitative", "SEG resources should map to quantitative tracks") &&
           expectEqual(session.tracks.at(0).source,
                       QDir::cleanPath(dir.filePath("copy-number.seg")),
                       "resource-only import should resolve relative resource paths");
}

bool testLoadCaseManifestBuildsWorkspaceReadiness() {
    QTemporaryDir tempDir;
    if (!expectTrue(tempDir.isValid(), "temporary directory should be created")) {
        return false;
    }

    QDir dir(tempDir.path());
    dir.mkpath("tracks");
    if (!expectTrue(writeTextFile(dir.filePath("tracks/tumor.bam"), "bam"), "tumor BAM fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("tracks/normal.bam"), "bam"), "normal BAM fixture should be written")) {
        return false;
    }

    const QString manifestPath = dir.filePath("review.case.json");
    const QByteArray manifest = R"({
  "schema": "igv-native-case/v1",
  "title": "EGFR Review",
  "description": "Tumor-normal review workspace",
  "session": {
    "genome": "hg19",
    "locus": "chr7:140453136-140453136",
    "tracks": [
      { "name": "Tumor DNA", "source": "tracks/tumor.bam", "group": "tumor" },
      {
        "name": "Remote Variants",
        "source": "https://example.org/variants.vcf.gz",
        "expected_genome": "GRCh38/hg38"
      }
    ]
  },
  "tracks": [
    {
      "name": "Normal DNA",
      "source": "tracks/normal.bam",
      "group": "normal",
      "index_source": "tracks/normal.bam.bai"
    }
  ],
  "sample_info_files": ["sample-info.tsv"],
  "rois": [
    { "locus": "chr7:140453130-140453150", "label": "EGFR hotspot" }
  ]
})";
    if (!expectTrue(writeTextFile(manifestPath, manifest), "case manifest fixture should be written")) {
        return false;
    }

    WorkspaceState workspace;
    QString errorMessage;
    if (!expectTrue(session_parsers::loadCaseManifest(manifestPath, &workspace, &errorMessage),
                    "case manifest should load successfully")) {
        std::cerr << "  parser error: " << errorMessage.toStdString() << '\n';
        return false;
    }

    return expectEqual(workspace.schema, "igv-native-case/v1", "case manifest should preserve schema") &&
           expectEqual(workspace.title, "EGFR Review", "case manifest should preserve title") &&
           expectTrue(workspace.session.tracks.size() == 3, "case manifest should merge embedded and top-level tracks") &&
           expectEqual(workspace.session.tracks.at(0).source,
                       QDir::cleanPath(dir.filePath("tracks/tumor.bam")),
                       "embedded session tracks should resolve relative to the manifest") &&
           expectEqual(workspace.sampleInfoFiles.value(0),
                       QDir::cleanPath(dir.filePath("sample-info.tsv")),
                       "sample info files should resolve relative to the manifest") &&
           expectTrue(workspace.session.rois.size() == 1, "case manifest should append ROI records") &&
           expectTrue(countIssues(workspace.readinessIssues, "error", "Index file missing") == 2,
                      "case manifest readiness should detect missing indexes") &&
           expectTrue(countIssues(workspace.readinessIssues, "warning", "Genome mismatch") == 1,
                      "case manifest readiness should flag genome mismatches") &&
           expectTrue(countIssues(workspace.readinessIssues, "warning", "Sample info missing") == 1,
                      "case manifest readiness should flag missing sample metadata");
}

bool testLoadCaseManifestImportsReferencedSessionFile() {
    QTemporaryDir tempDir;
    if (!expectTrue(tempDir.isValid(), "temporary directory should be created")) {
        return false;
    }

    QDir dir(tempDir.path());
    dir.mkpath("nested/data");
    if (!expectTrue(writeTextFile(dir.filePath("nested/data/tumor.bam"), "bam"), "referenced BAM fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("nested/data/tumor.bam.bai"), "bai"), "referenced BAM index fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("nested/sample-info.tsv"), "sample\tgroup\nS1\ttumor\n"),
                    "sample info fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("copy-number.seg"), "seg"), "extra track fixture should be written")) {
        return false;
    }

    const QString sessionPath = dir.filePath("nested/session.json");
    const QByteArray session = R"({
  "schema": "igv-native-session/v1",
  "genome": "T2T-CHM13v2",
  "locus": "chr8:1-1000",
  "tracks": [
    { "name": "Tumor DNA", "source": "data/tumor.bam" }
  ]
})";
    if (!expectTrue(writeTextFile(sessionPath, session), "referenced session fixture should be written")) {
        return false;
    }

    const QString manifestPath = dir.filePath("with-session.case.json");
    const QByteArray manifest = R"({
  "schema": "igv-native-case/v1",
  "title": "Referenced Session",
  "session_file": "nested/session.json",
  "tracks": [
    { "name": "Copy Number", "source": "copy-number.seg" }
  ],
  "sample_info_files": ["nested/sample-info.tsv"]
})";
    if (!expectTrue(writeTextFile(manifestPath, manifest), "manifest fixture should be written")) {
        return false;
    }

    WorkspaceState workspace;
    QString errorMessage;
    if (!expectTrue(session_parsers::loadCaseManifest(manifestPath, &workspace, &errorMessage),
                    "manifest with referenced session should load successfully")) {
        std::cerr << "  parser error: " << errorMessage.toStdString() << '\n';
        return false;
    }

    return expectEqual(workspace.sessionSource,
                       QDir::cleanPath(dir.filePath("nested/session.json")),
                       "workspace should record the referenced session path") &&
           expectEqual(workspace.session.genome, "T2T-CHM13v2", "referenced session should set the genome") &&
           expectEqual(workspace.session.tracks.at(0).source,
                       QDir::cleanPath(dir.filePath("nested/data/tumor.bam")),
                       "referenced session tracks should resolve relative to the session file") &&
           expectTrue(workspace.session.tracks.size() == 2, "manifest tracks should append to referenced session tracks") &&
           expectTrue(workspace.sampleInfo.headers.size() >= 3, "sample info headers should be parsed into the workspace") &&
           expectEqual(workspace.sampleInfo.headers.value(0), "File", "sample info tables should include a source file column") &&
           expectTrue(workspace.sampleInfo.rows.size() == 1, "sample info rows should be parsed") &&
           expectEqual(workspace.sampleInfo.rows.at(0).value(1), "S1", "sample info values should preserve sample ids") &&
           expectTrue(workspace.readinessIssues.isEmpty(), "fully populated manifest should not raise readiness issues");
}

bool testLoadCaseManifestSeedsReviewQueue() {
    QTemporaryDir tempDir;
    if (!expectTrue(tempDir.isValid(), "temporary directory should be created")) {
        return false;
    }

    QDir dir(tempDir.path());
    const QString manifestPath = dir.filePath("review-queue.case.json");
    const QByteArray manifest = R"({
  "schema": "igv-native-case/v1",
  "title": "Review Queue",
  "loci": ["chr7:140453130-140453150", "chr12:25245350-25245350"],
  "multi_locus": true,
  "rois": [
    { "locus": "chr7:140453130-140453150", "label": "EGFR hotspot" }
  ],
  "review_queue": [
    {
      "locus": "chr12:25245350-25245350",
      "label": "KRAS review",
      "status": "reviewed",
      "note": "Confirmed in tumor sample"
    }
  ]
})";
    if (!expectTrue(writeTextFile(manifestPath, manifest), "review queue manifest fixture should be written")) {
        return false;
    }

    WorkspaceState workspace;
    QString errorMessage;
    if (!expectTrue(session_parsers::loadCaseManifest(manifestPath, &workspace, &errorMessage),
                    "manifest with review queue should load successfully")) {
        std::cerr << "  parser error: " << errorMessage.toStdString() << '\n';
        return false;
    }

    const ReviewItem* roiItem = findReviewItemByLocus(workspace.session.reviewQueue, "chr7:140453130-140453150");
    const ReviewItem* manualItem = findReviewItemByLocus(workspace.session.reviewQueue, "chr12:25245350-25245350");

    return expectTrue(workspace.session.reviewQueue.size() == 2, "manifests should keep a deduplicated review queue") &&
           expectTrue(roiItem != nullptr, "manifest ROIs should seed review queue items") &&
           expectEqual(roiItem->label, "EGFR hotspot", "manifest ROI labels should carry into the review queue") &&
           expectEqual(roiItem->status, "pending", "ROI-seeded manifest review items should default to pending") &&
           expectTrue(manualItem != nullptr, "explicit manifest review items should be retained") &&
           expectEqual(manualItem->label, "KRAS review", "explicit manifest review labels should be preserved") &&
           expectEqual(manualItem->status, "reviewed", "explicit manifest review status should be preserved") &&
           expectEqual(manualItem->note, "Confirmed in tumor sample", "explicit manifest review notes should be preserved");
}

bool testReviewPacketMarkdownUsesPresetAndQueue() {
    WorkspaceState workspace;
    workspace.schema = "igv-native-case/v1";
    workspace.title = "EGFR Review";
    workspace.session.schema = "igv-native-session/v1";
    workspace.session.genome = "GRCh38/hg38";
    workspace.session.locus = "chr7:55174772-55174772";
    workspace.session.loci = QStringList({"chr7:55174772-55174772", "chr7:55242465-55242465"});

    TrackDescriptor tumorTrack;
    tumorTrack.name = "Tumor DNA";
    tumorTrack.kind = "alignment";
    tumorTrack.source = "/tmp/tracks/tumor.bam";
    tumorTrack.group = "tumor";
    tumorTrack.sampleId = "Tumor-1";

    TrackDescriptor normalTrack;
    normalTrack.name = "Normal DNA";
    normalTrack.kind = "alignment";
    normalTrack.source = "/tmp/tracks/normal.bam";
    normalTrack.group = "normal";
    normalTrack.sampleId = "Normal-1";

    TrackDescriptor cohortTrack;
    cohortTrack.name = "Copy Number";
    cohortTrack.kind = "quantitative";
    cohortTrack.source = "/tmp/tracks/copy-number.seg";
    cohortTrack.group = "cohort";

    workspace.session.tracks = {tumorTrack, normalTrack, cohortTrack};

    CohortPreset allTracks;
    allTracks.name = "All Tracks";
    allTracks.description = "All tracks.";

    CohortPreset paired;
    paired.name = "Tumor vs Normal";
    paired.description = "Matched tumor and normal tracks.";
    paired.groups = QStringList({"tumor", "normal"});

    workspace.cohortPresets = {allTracks, paired};

    ReviewItem reviewedItem;
    reviewedItem.locus = "chr7:55174772-55174772";
    reviewedItem.label = "EGFR L858R";
    reviewedItem.status = "reviewed";
    reviewedItem.note = "Confirmed in tumor";

    ReviewItem pendingItem;
    pendingItem.locus = "chr7:55242465-55242465";
    pendingItem.label = "EGFR amplification";
    pendingItem.status = "follow-up";
    pendingItem.note = "Check copy-number support";

    workspace.session.reviewQueue = {reviewedItem, pendingItem};
    workspace.session.rois = {RoiDescriptor{"chr7:55174760-55174790", "EGFR hotspot"}};
    workspace.readinessIssues = {ReadinessIssue{"warning", "Genome mismatch", "Remote track expects hg19."}};

    const QString markdown = session_parsers::buildReviewPacketMarkdown(workspace, "Tumor vs Normal");
    const QList<TrackDescriptor> pairedTracks = session_parsers::tracksForPreset(workspace, "Tumor vs Normal");

    return expectTrue(pairedTracks.size() == 2, "preset filtering should keep tumor and normal tracks") &&
           expectContains(markdown, "# EGFR Review", "review packet should include the workspace title") &&
           expectContains(markdown, "Preset: `Tumor vs Normal`", "review packet should record the active preset") &&
           expectContains(markdown, "Review queue: 2 total, 1 reviewed, 1 follow-up, 0 pending",
                          "review packet should summarize review item status counts") &&
           expectContains(markdown, "Tumor DNA", "review packet should include tumor tracks for the selected preset") &&
           expectContains(markdown, "Normal DNA", "review packet should include normal tracks for the selected preset") &&
           expectNotContains(markdown, "Copy Number", "review packet should omit tracks outside the active preset") &&
           expectContains(markdown, "Confirmed in tumor", "review packet should include review notes") &&
           expectContains(markdown, "Genome mismatch", "review packet should include readiness issues");
}

bool testReviewPacketHtmlIncludesSnapshots() {
    WorkspaceState workspace;
    workspace.schema = "igv-native-case/v1";
    workspace.title = "Daily Review";
    workspace.session.genome = "GRCh38/hg38";
    workspace.session.locus = "chr7:55174772-55174772";

    TrackDescriptor tumorTrack;
    tumorTrack.name = "Tumor DNA";
    tumorTrack.kind = "alignment";
    tumorTrack.source = "/tmp/tracks/tumor.bam";
    tumorTrack.group = "tumor";
    tumorTrack.sampleId = "Tumor-1";

    TrackDescriptor copyTrack;
    copyTrack.name = "Copy Number";
    copyTrack.kind = "quantitative";
    copyTrack.source = "/tmp/tracks/copy-number.seg";
    copyTrack.group = "cohort";

    workspace.session.tracks = {tumorTrack, copyTrack};

    CohortPreset allTracks;
    allTracks.name = "All Tracks";

    CohortPreset tumorPreset;
    tumorPreset.name = "Tumor";
    tumorPreset.groups = QStringList({"tumor"});

    workspace.cohortPresets = {allTracks, tumorPreset};

    ReviewItem reviewItem;
    reviewItem.locus = "chr7:55174772-55174772";
    reviewItem.label = "EGFR L858R";
    reviewItem.status = "reviewed";
    reviewItem.note = "Tumor-only support.";
    workspace.session.reviewQueue = {reviewItem};

    QMap<QString, QString> snapshots;
    snapshots.insert(reviewItem.locus, "report_assets/review-01-egfr.png");

    const QString html = session_parsers::buildReviewPacketHtml(
        workspace,
        "Tumor",
        "report_assets/overview.png",
        snapshots);

    return expectContains(html, "<title>Daily Review</title>", "HTML report should include a title") &&
           expectContains(html, "report_assets/overview.png", "HTML report should include the overview snapshot") &&
           expectContains(html, "report_assets/review-01-egfr.png", "HTML report should include per-item snapshots") &&
           expectContains(html, "Tumor DNA", "HTML report should include visible preset tracks") &&
           expectNotContains(html, "Copy Number", "HTML report should omit tracks filtered by the preset") &&
           expectContains(html, "Tumor-only support.", "HTML report should include review notes");
}

bool testLoadCaseFolderAutoDiscoversTracksAndSampleInfo() {
    QTemporaryDir tempDir;
    if (!expectTrue(tempDir.isValid(), "temporary directory should be created")) {
        return false;
    }

    QDir dir(tempDir.path());
    dir.mkpath("tracks");
    dir.mkpath("metadata");

    if (!expectTrue(writeTextFile(dir.filePath("tracks/tumor.bam"), "bam"), "tumor BAM fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("tracks/tumor.bam.bai"), "bai"), "tumor BAI fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("tracks/normal.bam"), "bam"), "normal BAM fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("tracks/normal.bam.bai"), "bai"), "normal BAI fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("tracks/copy-number.seg"), "seg"), "SEG fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("metadata/sample-info.tsv"), "sample\tgroup\nTumor-1\ttumor\nNormal-1\tnormal\n"),
                    "sample metadata fixture should be written")) {
        return false;
    }

    WorkspaceState workspace;
    QString errorMessage;
    if (!expectTrue(session_parsers::loadCaseFolder(dir.path(), &workspace, &errorMessage),
                    "case folder should load successfully")) {
        std::cerr << "  parser error: " << errorMessage.toStdString() << '\n';
        return false;
    }

    QString tumorIndexSource;
    for (const TrackDescriptor& track : workspace.session.tracks) {
        if (track.source.endsWith("tracks/tumor.bam")) {
            tumorIndexSource = track.indexSource;
            break;
        }
    }

    return expectEqual(workspace.schema, "igv-native-case-folder/v1", "case folder should tag the workspace schema") &&
           expectTrue(workspace.sessionSource.isEmpty(), "autodiscovered folders without sessions should not report a session source") &&
           expectTrue(workspace.session.tracks.size() == 3, "case folders should discover supported track files") &&
           expectEqual(tumorIndexSource,
                       QDir::cleanPath(dir.filePath("tracks/tumor.bam.bai")),
                       "case folders should auto-link BAM sidecar indexes") &&
           expectEqual(workspace.sampleInfo.headers.value(0), "File", "sample info parsing should add a file column") &&
           expectTrue(workspace.sampleInfo.rows.size() == 2, "case folders should parse sample metadata rows") &&
           expectEqual(workspace.sampleInfo.rows.at(0).value(1), "Tumor-1", "parsed sample metadata should preserve the first sample") &&
           expectTrue(workspace.readinessIssues.isEmpty(), "well-formed case folders should not raise readiness issues");
}

bool testSampleMatchingBuildsCohortPresets() {
    QTemporaryDir tempDir;
    if (!expectTrue(tempDir.isValid(), "temporary directory should be created")) {
        return false;
    }

    QDir dir(tempDir.path());
    dir.mkpath("tracks");
    dir.mkpath("metadata");

    if (!expectTrue(writeTextFile(dir.filePath("tracks/tumor-1.bam"), "bam"), "tumor BAM fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("tracks/tumor-1.bam.bai"), "bai"), "tumor BAI fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("tracks/normal-1.bam"), "bam"), "normal BAM fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("tracks/normal-1.bam.bai"), "bai"), "normal BAI fixture should be written")) {
        return false;
    }
    if (!expectTrue(writeTextFile(dir.filePath("metadata/sample-info.tsv"), "sample\tgroup\nTumor-1\tTumor\nNormal-1\tNormal\n"),
                    "sample metadata fixture should be written")) {
        return false;
    }

    WorkspaceState workspace;
    QString errorMessage;
    if (!expectTrue(session_parsers::loadCaseFolder(dir.path(), &workspace, &errorMessage),
                    "case folder with matchable sample ids should load successfully")) {
        std::cerr << "  parser error: " << errorMessage.toStdString() << '\n';
        return false;
    }

    const TrackDescriptor* tumorTrack = findTrackBySuffix(workspace.session.tracks, "tracks/tumor-1.bam");
    const TrackDescriptor* normalTrack = findTrackBySuffix(workspace.session.tracks, "tracks/normal-1.bam");
    const CohortPreset* allTracks = findPresetByName(workspace.cohortPresets, "All Tracks");
    const CohortPreset* matchedSamples = findPresetByName(workspace.cohortPresets, "Matched Samples");
    const CohortPreset* tumorPreset = findPresetByName(workspace.cohortPresets, "Tumor");
    const CohortPreset* normalPreset = findPresetByName(workspace.cohortPresets, "Normal");
    const CohortPreset* pairedPreset = findPresetByName(workspace.cohortPresets, "Tumor vs Normal");

    return expectTrue(tumorTrack != nullptr, "tumor track should be found by suffix") &&
           expectTrue(normalTrack != nullptr, "normal track should be found by suffix") &&
           expectEqual(tumorTrack->sampleId, "Tumor-1", "tumor track should match the sample metadata id") &&
           expectEqual(normalTrack->sampleId, "Normal-1", "normal track should match the sample metadata id") &&
           expectEqual(tumorTrack->group, "Tumor", "tumor track should inherit the sample group") &&
           expectEqual(normalTrack->group, "Normal", "normal track should inherit the sample group") &&
           expectTrue(allTracks != nullptr, "all tracks preset should exist") &&
           expectTrue(matchedSamples != nullptr, "matched samples preset should exist") &&
           expectTrue(tumorPreset != nullptr, "tumor preset should exist") &&
           expectTrue(normalPreset != nullptr, "normal preset should exist") &&
           expectTrue(pairedPreset != nullptr, "paired tumor-vs-normal preset should exist") &&
           expectTrue(matchedSamples->matchedOnly, "matched samples preset should filter to matched tracks") &&
           expectStringList(pairedPreset->groups, QStringList({"tumor", "normal"}), "tumor-vs-normal preset should track both groups");
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const struct {
        const char* name;
        bool (*fn)();
    } tests[] = {
        {"parse loci text", &testParseLociText},
        {"native session defaults", &testLoadNativeSessionDefaults},
        {"alignment track defaults", &testAlignmentTrackDefaults},
        {"alignment track settings round-trip", &testAlignmentTrackSettingsRoundTrip},
        {"native session seeds review queue", &testLoadNativeSessionSeedsReviewQueue},
        {"xml session imports tracks and rois", &testLoadIgvXmlSessionImportsTracksAndRois},
        {"xml session falls back to resources", &testLoadIgvXmlSessionFallsBackToResources},
        {"case manifest builds workspace readiness", &testLoadCaseManifestBuildsWorkspaceReadiness},
        {"case manifest imports referenced session file", &testLoadCaseManifestImportsReferencedSessionFile},
        {"case manifest seeds review queue", &testLoadCaseManifestSeedsReviewQueue},
        {"review packet markdown uses preset and queue", &testReviewPacketMarkdownUsesPresetAndQueue},
        {"review packet html includes snapshots", &testReviewPacketHtmlIncludesSnapshots},
        {"case folder autodiscovers tracks and sample info", &testLoadCaseFolderAutoDiscoversTracksAndSampleInfo},
        {"sample matching builds cohort presets", &testSampleMatchingBuildsCohortPresets},
    };

    int failures = 0;
    for (const auto& test : tests) {
        const bool passed = test.fn();
        std::cout << (passed ? "PASS" : "FAIL") << ": " << test.name << '\n';
        if (!passed) {
            ++failures;
        }
    }

    return failures == 0 ? 0 : 1;
}
