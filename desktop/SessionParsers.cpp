#include "SessionParsers.h"

#include <algorithm>
#include <climits>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>

namespace {

const QStringList kDefaultGenomes = {"GRCh38/hg38", "hg19", "GRCm39/mm39"};
const QStringList kTrackExtensions = {
    ".bam", ".cram", ".sam", ".vcf", ".vcf.gz", ".bcf", ".bed", ".bed.gz", ".gff", ".gff3",
    ".gff.gz", ".gtf", ".gtf.gz", ".bb", ".bigbed", ".bw", ".bigwig", ".wig", ".bedgraph",
    ".tdf", ".seg"
};
const QStringList kIndexExtensions = {".bai", ".crai", ".tbi", ".csi", ".idx"};

QString fileLabelForSource(const QString& source) {
    const QUrl url(source);
    if (url.isValid() && !url.scheme().isEmpty()) {
        const QString path = url.path();
        return QFileInfo(path).fileName().isEmpty() ? source : QFileInfo(path).fileName();
    }

    const QFileInfo fileInfo(source);
    return fileInfo.fileName().isEmpty() ? source : fileInfo.fileName();
}

bool isRemoteSource(const QString& source) {
    const QUrl url(source);
    return url.isValid() && !url.scheme().isEmpty() && !url.isLocalFile();
}

bool isLocalResolvableSource(const QString& source) {
    return !source.trimmed().isEmpty() && !isRemoteSource(source);
}

bool isCompressedTabixTrack(const QString& lowerSource) {
    return lowerSource.endsWith(".vcf.gz") || lowerSource.endsWith(".bed.gz") ||
           lowerSource.endsWith(".gff.gz") || lowerSource.endsWith(".gtf.gz");
}

bool defaultRequiresIndex(const QString& source, const QString& kind) {
    const QString lower = source.toLower();
    if (lower.endsWith(".bam") || lower.endsWith(".cram")) {
        return true;
    }
    if (lower.endsWith(".bcf") || isCompressedTabixTrack(lower)) {
        return true;
    }
    return kind == "alignment" && !lower.endsWith(".sam");
}

QString inferTrackGroupFromSource(const QString& source) {
    const QString lower = source.toLower();
    if (lower.contains("tumor")) {
        return "tumor";
    }
    if (lower.contains("normal")) {
        return "normal";
    }
    if (lower.contains("control")) {
        return "control";
    }
    if (lower.contains("rna")) {
        return "rna";
    }
    if (lower.contains("dna")) {
        return "dna";
    }
    if (lower.contains("cohort")) {
        return "cohort";
    }
    return {};
}

QString inferAlignmentExperimentType(const TrackDescriptor& track) {
    const QString lower = (track.name + " " + track.source + " " + track.group).toLower();
    if (lower.contains("rna") || lower.contains("rnaseq") || lower.contains("rna-seq") ||
        lower.contains("splice") || lower.contains("junction") || lower.contains("transcript") ||
        lower.contains("isoform") || lower.contains("fusion")) {
        return "rna";
    }
    if (lower.contains("ont") || lower.contains("nanopore") || lower.contains("pacbio") ||
        lower.contains("hifi") || lower.contains("longread") || lower.contains("long-read")) {
        return "long-read";
    }
    return "dna";
}

int defaultVisibilityWindowKbForExperiment(const QString& experimentType) {
    if (experimentType == "rna") {
        return 300;
    }
    if (experimentType == "long-read") {
        return 120;
    }
    return 30;
}

void initializeAlignmentSettings(TrackDescriptor* track) {
    if (track == nullptr || track->kind != "alignment" || track->alignmentSettingsInitialized) {
        return;
    }

    if (track->experimentType.isEmpty()) {
        track->experimentType = inferAlignmentExperimentType(*track);
    }
    if (track->visibilityWindowKb <= 0) {
        track->visibilityWindowKb = defaultVisibilityWindowKbForExperiment(track->experimentType);
    }

    track->showCoverageTrack = true;
    track->showAlignmentTrack = true;
    track->showSpliceJunctionTrack = track->experimentType == "rna";
    track->alignmentSettingsInitialized = true;
}

QString inferDefaultIndexSource(const TrackDescriptor& track) {
    if (!track.indexSource.trimmed().isEmpty()) {
        return track.indexSource;
    }

    if (!isLocalResolvableSource(track.source)) {
        return {};
    }

    const QString lower = track.source.toLower();
    if (lower.endsWith(".bam")) {
        return track.source + ".bai";
    }
    if (lower.endsWith(".cram")) {
        return track.source + ".crai";
    }
    if (lower.endsWith(".bcf")) {
        return track.source + ".csi";
    }
    if (isCompressedTabixTrack(lower)) {
        return track.source + ".tbi";
    }
    return {};
}

QString defaultVisibilityForKind(const QString& kind) {
    return (kind == "reference" || kind == "annotation") ? "always" : "zoomed";
}

bool endsWithAny(const QString& value, const QStringList& suffixes) {
    for (const QString& suffix : suffixes) {
        if (value.endsWith(suffix)) {
            return true;
        }
    }
    return false;
}

bool isIndexFilePath(const QString& path) {
    return endsWithAny(path.toLower(), kIndexExtensions);
}

bool isManifestCandidatePath(const QString& path) {
    const QString lower = path.toLower();
    return lower.endsWith(".igvcase.json") || lower.endsWith(".case.json");
}

bool isNativeSessionCandidatePath(const QString& path) {
    const QString lower = path.toLower();
    return lower.endsWith(".igvn.json") || (lower.endsWith(".json") && lower.contains("session"));
}

bool isXmlSessionCandidatePath(const QString& path) {
    return path.toLower().endsWith(".xml");
}

bool isTrackCandidatePath(const QString& path) {
    const QString lower = path.toLower();
    return !isIndexFilePath(lower) && endsWithAny(lower, kTrackExtensions);
}

bool isSampleInfoCandidatePath(const QString& path) {
    const QString lower = path.toLower();
    if (!(lower.endsWith(".tsv") || lower.endsWith(".csv") || lower.endsWith(".txt") || lower.endsWith(".tab"))) {
        return false;
    }
    return lower.contains("sample") || lower.contains("attribute") || lower.contains("metadata");
}

QStringList findFilesRecursively(const QString& rootPath, bool (*predicate)(const QString&)) {
    QStringList matches;
    QDirIterator iterator(rootPath, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = QDir::cleanPath(iterator.next());
        if (predicate(path)) {
            matches.append(path);
        }
    }
    return matches;
}

QString choosePreferredCandidate(const QStringList& candidates, const QString& rootPath) {
    if (candidates.isEmpty()) {
        return {};
    }

    const QDir root(rootPath);
    QString best = candidates.first();
    int bestDepth = INT_MAX;
    int bestLength = INT_MAX;

    for (const QString& candidate : candidates) {
        const QString relative = root.relativeFilePath(candidate);
        const int depth = relative.count('/');
        const int length = relative.size();
        if (depth < bestDepth || (depth == bestDepth && length < bestLength)) {
            best = candidate;
            bestDepth = depth;
            bestLength = length;
        }
    }

    return best;
}

QString normalizeIdentifier(const QString& value) {
    QString normalized;
    normalized.reserve(value.size());
    for (const QChar character : value.toLower()) {
        if (character.isLetterOrNumber()) {
            normalized.append(character);
        }
    }
    return normalized;
}

QString titleCaseLabel(const QString& value) {
    const QStringList parts =
        value.split(QRegularExpression("[\\s_\\-]+"), Qt::SkipEmptyParts);
    QStringList titled;
    for (QString part : parts) {
        if (part.isEmpty()) {
            continue;
        }
        part[0] = part.at(0).toUpper();
        titled.append(part);
    }
    return titled.join(" ");
}

QString markdownEscape(const QString& value) {
    QString escaped = value;
    escaped.replace("\\", "\\\\");
    escaped.replace("|", "\\|");
    escaped.replace("\n", "<br/>");
    return escaped;
}

QString htmlEscape(const QString& value) {
    return value.toHtmlEscaped().replace("\n", "<br/>");
}

QString statusClassName(const QString& status) {
    const QString normalized = status.trimmed().toLower();
    if (normalized == "reviewed") {
        return "reviewed";
    }
    if (normalized == "follow-up") {
        return "follow-up";
    }
    return "pending";
}

int firstMatchingHeaderIndex(const QStringList& headers, const QStringList& candidates) {
    for (int index = 0; index < headers.size(); ++index) {
        const QString normalized = normalizeIdentifier(headers.at(index));
        if (candidates.contains(normalized)) {
            return index;
        }
    }
    return -1;
}

bool looksLikeSessionReference(const QString& value) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    if (trimmed.startsWith(".") || trimmed.startsWith("/") || trimmed.startsWith("\\")) {
        return true;
    }

    if (trimmed.contains('/') || trimmed.contains('\\')) {
        return true;
    }

    const QString lower = trimmed.toLower();
    static const QStringList kKnownSuffixes = {
        ".bam", ".cram", ".sam", ".vcf", ".vcf.gz", ".bcf", ".bed", ".bed.gz", ".gff", ".gff3",
        ".gtf", ".gtf.gz", ".bb", ".bigbed", ".bw", ".bigwig", ".wig", ".bedgraph", ".tdf",
        ".seg", ".fa", ".fasta", ".2bit", ".genome", ".xml", ".json", ".gz", ".txt", ".tsv", ".tab"
    };

    for (const QString& suffix : kKnownSuffixes) {
        if (lower.endsWith(suffix)) {
            return true;
        }
    }

    const QUrl url(trimmed);
    return url.isValid() && !url.scheme().isEmpty();
}

QString resolveSessionReference(const QString& value, const QFileInfo& sessionFileInfo) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QUrl url(trimmed);
    if (url.isValid() && !url.scheme().isEmpty()) {
        if (url.isLocalFile()) {
            return QDir::cleanPath(url.toLocalFile());
        }
        return url.toString();
    }

    if (!looksLikeSessionReference(trimmed)) {
        return trimmed;
    }

    const QFileInfo fileInfo(trimmed);
    if (fileInfo.isAbsolute()) {
        return QDir::cleanPath(fileInfo.filePath());
    }

    return QDir::cleanPath(sessionFileInfo.dir().absoluteFilePath(trimmed));
}

QString normalizedBooleanAttribute(const QString& value) {
    return value.trimmed().toLower();
}

QString buildRoiLocus(const QXmlStreamAttributes& attributes) {
    QString chrom = attributes.value("chromosome").toString().trimmed();
    if (chrom.isEmpty()) {
        chrom = attributes.value("chr").toString().trimmed();
    }

    const QString locus = attributes.value("locus").toString().trimmed();
    if (!locus.isEmpty()) {
        return locus;
    }

    if (chrom.isEmpty()) {
        return {};
    }

    bool startOk = false;
    bool endOk = false;
    const qlonglong start = attributes.value("start").toString().remove(',').toLongLong(&startOk);
    qlonglong end = attributes.value("end").toString().remove(',').toLongLong(&endOk);
    if (!endOk) {
        end = attributes.value("stop").toString().remove(',').toLongLong(&endOk);
    }

    if (!startOk || !endOk) {
        return chrom;
    }

    if (end < start) {
        return QString("%1:%2-%3").arg(chrom).arg(end).arg(start);
    }
    return QString("%1:%2-%3").arg(chrom).arg(start).arg(end);
}

QString buildTrackKey(const TrackDescriptor& track) {
    const QString sourceKey = track.source.isEmpty() ? QStringLiteral("<none>") : track.source;
    return sourceKey + "|" + track.name;
}

void finalizeTrackDescriptor(TrackDescriptor* track) {
    if (track == nullptr) {
        return;
    }

    if (track->kind.isEmpty()) {
        track->kind = session_parsers::inferTrackKind(track->source);
    }
    if (track->name.isEmpty()) {
        track->name = fileLabelForSource(track->source);
    }
    if (track->name.isEmpty()) {
        track->name = "Track";
    }
    if (track->visibility.isEmpty()) {
        track->visibility = defaultVisibilityForKind(track->kind);
    }
    if (!track->color.isValid()) {
        track->color = session_parsers::colorForTrackKind(track->kind);
    }
    if (track->group.isEmpty()) {
        track->group = inferTrackGroupFromSource(track->source);
    }
    initializeAlignmentSettings(track);
    if (track->requiresIndex && track->indexSource.isEmpty()) {
        const QString inferredIndex = inferDefaultIndexSource(*track);
        if (!inferredIndex.isEmpty() && QFileInfo::exists(inferredIndex)) {
            track->indexSource = inferredIndex;
        }
    }
}

void appendUniqueTrack(QList<TrackDescriptor>* target, QSet<QString>* seen, const TrackDescriptor& track) {
    if (target == nullptr || seen == nullptr) {
        return;
    }

    const QString key = buildTrackKey(track);
    if (seen->contains(key)) {
        return;
    }

    seen->insert(key);
    target->append(track);
}

bool trackMatchesPreset(const TrackDescriptor& track, const CohortPreset& preset) {
    if (preset.matchedOnly && track.sampleId.trimmed().isEmpty()) {
        return false;
    }
    if (!preset.groups.isEmpty() && !preset.groups.contains(track.group, Qt::CaseInsensitive)) {
        return false;
    }
    if (!preset.sampleIds.isEmpty() && !preset.sampleIds.contains(track.sampleId, Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

QString effectivePresetName(const QString& presetName) {
    return presetName.trimmed().isEmpty() ? "All Tracks" : presetName.trimmed();
}

QString readinessSummaryMarkdown(const QList<ReadinessIssue>& issues) {
    if (issues.isEmpty()) {
        return "- No local readiness issues detected.\n";
    }

    QString summary;
    for (const ReadinessIssue& issue : issues) {
        summary += QString("- `%1` %2: %3\n")
                       .arg(markdownEscape(issue.severity),
                            markdownEscape(issue.check),
                            markdownEscape(issue.detail));
    }
    return summary;
}

void appendPresetIfUseful(QList<CohortPreset>* presets,
                          const CohortPreset& preset,
                          const QList<TrackDescriptor>& tracks) {
    if (presets == nullptr) {
        return;
    }
    for (const CohortPreset& existing : *presets) {
        if (existing.name == preset.name) {
            return;
        }
    }

    int matchCount = 0;
    for (const TrackDescriptor& track : tracks) {
        if (trackMatchesPreset(track, preset)) {
            ++matchCount;
        }
    }
    if (matchCount > 0) {
        presets->append(preset);
    }
}

TrackDescriptor makeTrack(const QString& source,
                          const QString& name,
                          const QString& kind,
                          const QString& visibility) {
    TrackDescriptor track;
    track.source = source;
    track.name = name;
    track.kind = kind;
    track.visibility = visibility;
    track.color = session_parsers::colorForTrackKind(kind);
    track.requiresIndex = defaultRequiresIndex(source, kind);
    return track;
}

QStringList parseGenomeList(const QJsonArray& genomes) {
    QStringList values;
    for (const QJsonValue& genomeValue : genomes) {
        const QString genome = genomeValue.toString().trimmed();
        if (!genome.isEmpty() && !values.contains(genome)) {
            values.append(genome);
        }
    }
    return values;
}

QList<RoiDescriptor> parseRois(const QJsonArray& rois) {
    QList<RoiDescriptor> values;
    for (const QJsonValue& roiValue : rois) {
        const QJsonObject roiObject = roiValue.toObject();
        RoiDescriptor roi;
        roi.locus = roiObject.value("locus").toString().trimmed();
        roi.label = roiObject.value("label").toString().trimmed();
        if (!roi.locus.isEmpty()) {
            values.append(roi);
        }
    }
    return values;
}

QList<ReviewItem> parseReviewQueue(const QJsonArray& items) {
    QList<ReviewItem> values;
    for (const QJsonValue& itemValue : items) {
        const QJsonObject itemObject = itemValue.toObject();
        ReviewItem item;
        item.locus = itemObject.value("locus").toString().trimmed();
        item.label = itemObject.value("label").toString().trimmed();
        item.status = itemObject.value("status").toString("pending").trimmed();
        item.note = itemObject.value("note").toString().trimmed();
        if (!item.locus.isEmpty()) {
            values.append(item);
        }
    }
    return values;
}

QList<TrackDescriptor> parseTracks(const QJsonArray& tracks, const QFileInfo* baseFileInfo) {
    QList<TrackDescriptor> values;
    for (const QJsonValue& trackValue : tracks) {
        const QJsonObject trackObject = trackValue.toObject();

        TrackDescriptor track;
        const QString rawSource = trackObject.value("source").toString().trimmed();
        track.source = baseFileInfo == nullptr ? rawSource : resolveSessionReference(rawSource, *baseFileInfo);
        track.kind = trackObject.value("kind").toString().trimmed();
        if (track.kind.isEmpty()) {
            track.kind = session_parsers::inferTrackKind(track.source);
        }

        track.name = trackObject.value("name").toString().trimmed();
        if (track.name.isEmpty()) {
            track.name = fileLabelForSource(track.source);
        }
        if (track.name.isEmpty()) {
            track.name = "Track";
        }

        track.visibility = trackObject.value("visibility").toString().trimmed();
        if (track.visibility.isEmpty()) {
            track.visibility = defaultVisibilityForKind(track.kind);
        }

        const QString rawIndexSource =
            trackObject.value("index_source").toString(trackObject.value("index").toString()).trimmed();
        track.indexSource = baseFileInfo == nullptr ? rawIndexSource : resolveSessionReference(rawIndexSource, *baseFileInfo);
        track.expectedGenome = trackObject.value("expected_genome").toString().trimmed();
        track.group = trackObject.value("group").toString().trimmed();
        track.sampleId = trackObject.value("sample_id").toString().trimmed();
        track.requiresIndex = trackObject.contains("requires_index")
                                  ? trackObject.value("requires_index").toBool()
                                  : defaultRequiresIndex(track.source, track.kind);

        if (track.kind == "alignment") {
            const bool hasExperimentType = trackObject.contains("experiment_type");
            const bool hasVisibilityWindowKb = trackObject.contains("visibility_window_kb");
            const bool hasShowCoverage =
                trackObject.contains("show_coverage") || trackObject.contains("show_coverage_track");
            const bool hasShowAlignments =
                trackObject.contains("show_alignments") || trackObject.contains("show_alignment_track");
            const bool hasShowSplice =
                trackObject.contains("show_splice_junctions") || trackObject.contains("show_splice_junction_track");

            if (hasExperimentType) {
                track.experimentType = trackObject.value("experiment_type").toString().trimmed().toLower();
            }
            if (track.experimentType.isEmpty()) {
                track.experimentType = inferAlignmentExperimentType(track);
            }

            if (hasVisibilityWindowKb) {
                track.visibilityWindowKb = std::max(0, trackObject.value("visibility_window_kb").toInt());
            }
            if (track.visibilityWindowKb <= 0) {
                track.visibilityWindowKb = defaultVisibilityWindowKbForExperiment(track.experimentType);
            }

            track.showCoverageTrack =
                hasShowCoverage ? trackObject.value("show_coverage").toBool(trackObject.value("show_coverage_track").toBool())
                                : true;
            track.showAlignmentTrack =
                hasShowAlignments ? trackObject.value("show_alignments").toBool(trackObject.value("show_alignment_track").toBool())
                                  : true;
            track.showSpliceJunctionTrack = hasShowSplice
                                                ? trackObject.value("show_splice_junctions")
                                                      .toBool(trackObject.value("show_splice_junction_track").toBool())
                                                : track.experimentType == "rna";
            track.alignmentSettingsInitialized = true;
        }

        track.color = session_parsers::colorForTrackKind(track.kind);
        finalizeTrackDescriptor(&track);

        values.append(track);
    }
    return values;
}

QChar inferSampleDelimiter(const QString& headerLine, const QString& fileName) {
    if (headerLine.contains('\t')) {
        return '\t';
    }
    if (headerLine.contains(',')) {
        return ',';
    }
    if (headerLine.contains(';')) {
        return ';';
    }
    return QFileInfo(fileName).suffix().toLower() == "csv" ? ',' : '\t';
}

QStringList splitSampleLine(const QString& line, const QChar delimiter) {
    QStringList values = line.split(delimiter, Qt::KeepEmptyParts);
    for (QString& value : values) {
        value = value.trimmed();
    }
    return values;
}

SampleInfoTable parseSampleInfoFiles(const QStringList& fileNames, QList<ReadinessIssue>* importIssues) {
    SampleInfoTable table;
    if (fileNames.isEmpty()) {
        return table;
    }

    table.headers = QStringList({"File"});

    for (const QString& fileName : fileNames) {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        const QString text = QString::fromUtf8(file.readAll());
        file.close();

        QStringList lines;
        for (const QString& rawLine : text.split(QRegularExpression("\r?\n"), Qt::KeepEmptyParts)) {
            const QString trimmed = rawLine.trimmed();
            if (!trimmed.isEmpty() && !trimmed.startsWith('#')) {
                lines.append(rawLine);
            }
        }

        if (lines.isEmpty()) {
            if (importIssues != nullptr) {
                importIssues->append(
                    {"warning", "Sample info empty", QString("%1 did not contain any tabular rows.").arg(QFileInfo(fileName).fileName())});
            }
            continue;
        }

        const QChar delimiter = inferSampleDelimiter(lines.first(), fileName);
        QStringList headers = splitSampleLine(lines.first(), delimiter);
        for (int index = 0; index < headers.size(); ++index) {
            if (headers.at(index).isEmpty()) {
                headers[index] = QString("Column %1").arg(index + 1);
            }
            if (!table.headers.contains(headers.at(index))) {
                table.headers.append(headers.at(index));
                for (QStringList& row : table.rows) {
                    row.append(QString());
                }
            }
        }

        int parsedRows = 0;
        for (int lineIndex = 1; lineIndex < lines.size(); ++lineIndex) {
            QStringList values = splitSampleLine(lines.at(lineIndex), delimiter);
            while (values.size() < headers.size()) {
                values.append(QString());
            }

            bool hasContent = false;
            for (const QString& value : values) {
                if (!value.trimmed().isEmpty()) {
                    hasContent = true;
                    break;
                }
            }
            if (!hasContent) {
                continue;
            }

            QStringList row;
            for (int column = 0; column < table.headers.size(); ++column) {
                row.append(QString());
            }
            row[0] = QFileInfo(fileName).fileName();
            for (int column = 0; column < headers.size(); ++column) {
                const int tableColumn = table.headers.indexOf(headers.at(column));
                if (tableColumn >= 0 && column < values.size()) {
                    row[tableColumn] = values.at(column);
                }
            }
            table.rows.append(row);
            ++parsedRows;
        }

        if (parsedRows == 0 && importIssues != nullptr) {
            importIssues->append({"warning",
                                  "Sample info empty",
                                  QString("%1 only contained a header row and no sample records.")
                                      .arg(QFileInfo(fileName).fileName())});
        }
    }

    return table;
}

void annotateTracksFromSampleInfo(WorkspaceState* workspace) {
    if (workspace == nullptr) {
        return;
    }

    const SampleInfoTable& table = workspace->sampleInfo;
    const int sampleColumn =
        firstMatchingHeaderIndex(table.headers, {"sample", "sampleid", "samplename", "id", "name"});
    const int groupColumn =
        firstMatchingHeaderIndex(table.headers, {"group", "cohort", "sampletype", "type", "condition", "status"});

    for (TrackDescriptor& track : workspace->session.tracks) {
        track.sampleId.clear();
    }

    if (sampleColumn < 0 || table.rows.isEmpty()) {
        return;
    }

    for (TrackDescriptor& track : workspace->session.tracks) {
        const QString haystack = normalizeIdentifier(track.name + " " + track.source);
        int bestScore = -1;
        QString bestSampleId;
        QString bestGroup;

        for (const QStringList& row : table.rows) {
            if (sampleColumn >= row.size()) {
                continue;
            }

            const QString sampleId = row.at(sampleColumn).trimmed();
            const QString normalizedSample = normalizeIdentifier(sampleId);
            if (normalizedSample.size() < 3) {
                continue;
            }
            if (!haystack.contains(normalizedSample)) {
                continue;
            }

            const int score = normalizedSample.size();
            if (score > bestScore) {
                bestScore = score;
                bestSampleId = sampleId;
                if (groupColumn >= 0 && groupColumn < row.size()) {
                    bestGroup = row.at(groupColumn).trimmed();
                } else {
                    bestGroup.clear();
                }
            }
        }

        if (bestScore >= 0) {
            track.sampleId = bestSampleId;
            if (!bestGroup.isEmpty()) {
                track.group = bestGroup;
            }
        }
    }
}

QList<CohortPreset> buildCohortPresets(const WorkspaceState& workspace) {
    QList<CohortPreset> presets;

    CohortPreset allTracks;
    allTracks.name = "All Tracks";
    allTracks.description = "Show every track in the current workspace.";
    presets.append(allTracks);

    QSet<QString> uniqueGroups;
    bool hasMatchedTracks = false;
    for (const TrackDescriptor& track : workspace.session.tracks) {
        const QString normalizedGroup = track.group.trimmed().toLower();
        if (!normalizedGroup.isEmpty()) {
            uniqueGroups.insert(normalizedGroup);
        }
        if (!track.sampleId.trimmed().isEmpty()) {
            hasMatchedTracks = true;
        }
    }

    if (hasMatchedTracks) {
        CohortPreset matchedSamples;
        matchedSamples.name = "Matched Samples";
        matchedSamples.description = "Tracks that match parsed sample metadata.";
        matchedSamples.matchedOnly = true;
        appendPresetIfUseful(&presets, matchedSamples, workspace.session.tracks);
    }

    const QList<QString> groups = uniqueGroups.values();
    for (const QString& group : groups) {
        CohortPreset preset;
        preset.name = titleCaseLabel(group);
        preset.description = QString("Tracks grouped as %1.").arg(group);
        preset.groups = QStringList({group});
        appendPresetIfUseful(&presets, preset, workspace.session.tracks);
    }

    if (uniqueGroups.contains("tumor") && uniqueGroups.contains("normal")) {
        CohortPreset paired;
        paired.name = "Tumor vs Normal";
        paired.description = "Matched tumor and normal cohort tracks.";
        paired.groups = QStringList({"tumor", "normal"});
        appendPresetIfUseful(&presets, paired, workspace.session.tracks);
    }

    if (uniqueGroups.contains("control") && !uniqueGroups.contains("tumor")) {
        CohortPreset control;
        control.name = "Controls";
        control.description = "Tracks grouped as controls.";
        control.groups = QStringList({"control"});
        appendPresetIfUseful(&presets, control, workspace.session.tracks);
    }

    return presets;
}

void refreshReviewQueue(SessionState* session) {
    if (session == nullptr) {
        return;
    }

    QMap<QString, ReviewItem> existingByLocus;
    for (const ReviewItem& item : session->reviewQueue) {
        if (!item.locus.trimmed().isEmpty()) {
            existingByLocus.insert(item.locus, item);
        }
    }

    QList<ReviewItem> mergedQueue;
    QSet<QString> seenLoci;

    auto appendOrMerge = [&existingByLocus, &mergedQueue, &seenLoci](const QString& locus,
                                                                     const QString& label,
                                                                     const QString& defaultStatus) {
        const QString trimmedLocus = locus.trimmed();
        if (trimmedLocus.isEmpty() || trimmedLocus.compare("All", Qt::CaseInsensitive) == 0 ||
            seenLoci.contains(trimmedLocus)) {
            return;
        }

        ReviewItem item = existingByLocus.value(trimmedLocus);
        item.locus = trimmedLocus;
        if (item.label.isEmpty()) {
            item.label = label;
        }
        if (item.status.isEmpty()) {
            item.status = defaultStatus;
        }
        seenLoci.insert(trimmedLocus);
        mergedQueue.append(item);
    };

    for (const RoiDescriptor& roi : session->rois) {
        appendOrMerge(roi.locus, roi.label.isEmpty() ? "ROI" : roi.label, "pending");
    }

    for (const QString& locus : session->loci) {
        appendOrMerge(locus, "Review Locus", "pending");
    }

    for (const ReviewItem& item : session->reviewQueue) {
        appendOrMerge(item.locus, item.label, item.status.isEmpty() ? "pending" : item.status);
    }

    session->reviewQueue = mergedQueue;
}

SessionState loadNativeSessionObject(const QJsonObject& object, const QFileInfo* baseFileInfo) {
    SessionState session;
    session.schema = object.value("schema").toString("igv-native-session/v1");
    session.genome = object.value("genome").toString("GRCh38/hg38");
    session.locus = object.value("locus").toString("All");
    session.multiLocus = object.value("multi_locus").toBool(false);

    session.genomes = parseGenomeList(object.value("genomes").toArray());
    if (session.genomes.isEmpty()) {
        session.genomes = kDefaultGenomes;
    } else if (!session.genomes.contains(session.genome)) {
        session.genomes.prepend(session.genome);
    }

    session.loci = session_parsers::parseLociText(object.value("loci_text").toString());
    for (const QJsonValue& locusValue : object.value("loci").toArray()) {
        const QString locus = locusValue.toString().trimmed();
        if (!locus.isEmpty()) {
            session.loci.append(locus);
        }
    }
    if (session.loci.isEmpty() && !session.locus.isEmpty()) {
        session.loci = QStringList({session.locus});
    }
    if (session.loci.size() > 1 && !object.contains("multi_locus")) {
        session.multiLocus = true;
    }
    if (!session.loci.isEmpty()) {
        session.locus = session.multiLocus ? session.loci.join(" ") : session.loci.first();
    }

    session.rois = parseRois(object.value("rois").toArray());
    session.reviewQueue = parseReviewQueue(object.value("review_queue").toArray());
    session.tracks = parseTracks(object.value("tracks").toArray(), baseFileInfo);

    if (session.tracks.isEmpty()) {
        session.tracks.append(makeTrack("builtin://reference", "Reference Sequence", "reference", "always"));
        session.tracks.append(makeTrack("builtin://genes", "RefSeq Genes", "annotation", "always"));
    }

    refreshReviewQueue(&session);

    return session;
}

bool loadNativeSessionFile(const QString& fileName, SessionState* out, QString* errorMessage) {
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = "Could not read the selected native session file.";
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = "The selected file is not a valid native session.";
        }
        return false;
    }

    const QFileInfo fileInfo(fileName);
    if (out != nullptr) {
        *out = loadNativeSessionObject(document.object(), &fileInfo);
    }
    return true;
}

TrackDescriptor makeDiscoveredTrack(const QString& path) {
    return session_parsers::describeTrackSource(QDir::cleanPath(path), fileLabelForSource(path));
}

QList<TrackDescriptor> discoverTracks(const QString& rootPath) {
    QList<TrackDescriptor> tracks;
    QSet<QString> seen;
    const QStringList files = findFilesRecursively(rootPath, &isTrackCandidatePath);
    for (const QString& fileName : files) {
        TrackDescriptor track = makeDiscoveredTrack(fileName);
        appendUniqueTrack(&tracks, &seen, track);
    }
    return tracks;
}

QList<ReadinessIssue> discoveryIssuesForCandidates(const QStringList& candidates,
                                                   const QString& chosen,
                                                   const QString& checkLabel) {
    QList<ReadinessIssue> issues;
    if (candidates.size() > 1) {
        issues.append({"warning",
                       checkLabel,
                       QString("Multiple candidates were found. Using %1 and ignoring %2 others.")
                           .arg(QDir::toNativeSeparators(chosen))
                           .arg(candidates.size() - 1)});
    }
    return issues;
}

QList<ReadinessIssue> buildReadinessIssues(const WorkspaceState& workspace) {
    QList<ReadinessIssue> issues = workspace.importIssues;
    const SessionState& session = workspace.session;

    if (session.genome.trimmed().isEmpty()) {
        issues.append({"warning", "Genome not set", "This workspace does not declare a target genome build."});
    }

    if (session.tracks.isEmpty()) {
        issues.append({"warning", "No tracks loaded", "The workspace opened without any data tracks."});
    }

    for (const TrackDescriptor& track : session.tracks) {
        const QString trackName = track.name.isEmpty() ? "Track" : track.name;

        if (track.source.trimmed().isEmpty()) {
            issues.append({"error", "Track source missing", QString("%1 does not declare a source path or URL.").arg(trackName)});
            continue;
        }

        if (isLocalResolvableSource(track.source) && !QFileInfo::exists(track.source)) {
            issues.append({"error",
                           "Track file missing",
                           QString("%1 could not be found at %2.").arg(trackName, QDir::toNativeSeparators(track.source))});
        }

        if (!track.expectedGenome.trimmed().isEmpty() && !session.genome.trimmed().isEmpty() &&
            track.expectedGenome != session.genome) {
            issues.append({"warning",
                           "Genome mismatch",
                           QString("%1 expects %2 but the workspace is using %3.")
                               .arg(trackName, track.expectedGenome, session.genome)});
        }

        const QString inferredIndex = inferDefaultIndexSource(track);
        if (!track.indexSource.trimmed().isEmpty() && isLocalResolvableSource(track.indexSource) &&
            !QFileInfo::exists(track.indexSource)) {
            issues.append({"error",
                           "Index file missing",
                           QString("%1 expects an index at %2.")
                               .arg(trackName, QDir::toNativeSeparators(track.indexSource))});
        } else if (track.requiresIndex && !inferredIndex.isEmpty() && !QFileInfo::exists(inferredIndex)) {
            issues.append({"error",
                           "Index file missing",
                           QString("%1 requires an index sidecar and none was found at %2.")
                               .arg(trackName, QDir::toNativeSeparators(inferredIndex))});
        }
    }

    for (const QString& sampleInfoFile : workspace.sampleInfoFiles) {
        if (sampleInfoFile.trimmed().isEmpty()) {
            continue;
        }
        if (isLocalResolvableSource(sampleInfoFile) && !QFileInfo::exists(sampleInfoFile)) {
            issues.append({"warning",
                           "Sample info missing",
                           QString("Sample metadata file %1 could not be found.")
                               .arg(QDir::toNativeSeparators(sampleInfoFile))});
        }
    }

    if (!workspace.sampleInfoFiles.isEmpty() && workspace.sampleInfo.rows.isEmpty()) {
        bool hasExistingFiles = false;
        for (const QString& sampleInfoFile : workspace.sampleInfoFiles) {
            if (QFileInfo::exists(sampleInfoFile)) {
                hasExistingFiles = true;
                break;
            }
        }
        if (hasExistingFiles) {
            issues.append({"warning",
                           "Sample info empty",
                           "Sample metadata files were found, but no sample records were parsed yet."});
        }
    }

    return issues;
}

}  // namespace

namespace session_parsers {

TrackDescriptor describeTrackSource(const QString& source, const QString& name) {
    TrackDescriptor track;
    track.source = source.trimmed();
    track.kind = inferTrackKind(track.source);
    track.name = name.trimmed();
    track.visibility = defaultVisibilityForKind(track.kind);
    track.color = colorForTrackKind(track.kind);
    track.requiresIndex = defaultRequiresIndex(track.source, track.kind);
    finalizeTrackDescriptor(&track);
    return track;
}

SessionState loadNativeSession(const QJsonObject& object) {
    return loadNativeSessionObject(object, nullptr);
}

bool loadIgvXmlSession(const QString& fileName, SessionState* out, QString* errorMessage) {
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = "Could not read the selected IGV XML session.";
        }
        return false;
    }

    QXmlStreamReader xml(&file);
    const QFileInfo sessionFileInfo(fileName);

    SessionState imported;
    imported.schema = "igv-xml-session/v1";
    imported.genome = "GRCh38/hg38";
    imported.locus = "All";
    imported.genomes = kDefaultGenomes;

    QList<TrackDescriptor> resources;
    QList<TrackDescriptor> explicitTracks;
    QMap<QString, TrackDescriptor> resourcesByKey;
    QSet<QString> resourceKeys;
    QSet<QString> explicitTrackKeys;
    bool sawSessionRoot = false;

    auto registerTrack = [](QList<TrackDescriptor>* target, QSet<QString>* seen, const TrackDescriptor& track) {
        const QString key = buildTrackKey(track);
        if (seen->contains(key)) {
            return;
        }
        seen->insert(key);
        target->append(track);
    };

    auto storeResourceLookup = [&resourcesByKey](const QString& key, const TrackDescriptor& track) {
        if (!key.trimmed().isEmpty() && !resourcesByKey.contains(key)) {
            resourcesByKey.insert(key, track);
        }
    };

    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }

        const QString elementName = xml.name().toString();
        const QXmlStreamAttributes attributes = xml.attributes();

        if (elementName == "Session" || elementName == "Global") {
            sawSessionRoot = true;

            const QString genome = resolveSessionReference(attributes.value("genome").toString(), sessionFileInfo);
            if (!genome.isEmpty()) {
                imported.genome = genome;
            }

            const QString locus = attributes.value("locus").toString().trimmed();
            if (!locus.isEmpty()) {
                imported.locus = locus;
            }
            continue;
        }

        if (elementName == "Resource") {
            QString source = attributes.value("path").toString().trimmed();
            if (source.isEmpty()) {
                source = attributes.value("url").toString().trimmed();
            }
            source = resolveSessionReference(source, sessionFileInfo);
            if (source.isEmpty()) {
                continue;
            }

            QString name = attributes.value("name").toString().trimmed();
            if (name.isEmpty()) {
                name = attributes.value("label").toString().trimmed();
            }
            if (name.isEmpty()) {
                name = fileLabelForSource(source);
            }

            TrackDescriptor track = makeTrack(source, name, inferTrackKind(source), "always");
            finalizeTrackDescriptor(&track);
            registerTrack(&resources, &resourceKeys, track);
            storeResourceLookup(attributes.value("path").toString().trimmed(), track);
            storeResourceLookup(source, track);
            storeResourceLookup(track.name, track);
            continue;
        }

        if (elementName == "Region") {
            RoiDescriptor roi;
            roi.locus = buildRoiLocus(attributes);
            roi.label = attributes.value("description").toString().trimmed();
            if (roi.label.isEmpty()) {
                roi.label = attributes.value("label").toString().trimmed();
            }
            if (roi.label.isEmpty()) {
                roi.label = attributes.value("name").toString().trimmed();
            }
            if (!roi.locus.isEmpty()) {
                imported.rois.append(roi);
            }
            continue;
        }

        if (elementName == "Track") {
            const QString id = attributes.value("id").toString().trimmed();
            const QString rawName = attributes.value("name").toString().trimmed();
            const QString resolvedId = resolveSessionReference(id, sessionFileInfo);
            const QString visibilityAttr = normalizedBooleanAttribute(attributes.value("visible").toString());

            TrackDescriptor track = resourcesByKey.value(resolvedId);
            if (track.source.isEmpty()) {
                track = resourcesByKey.value(id);
            }
            if (track.source.isEmpty() && !rawName.isEmpty()) {
                track = resourcesByKey.value(rawName);
            }

            QString source = track.source;
            if (source.isEmpty()) {
                source = resolvedId;
            }
            if (source.isEmpty()) {
                source = rawName;
            }
            if (source.isEmpty()) {
                continue;
            }

            track.source = source;
            if (!rawName.isEmpty()) {
                track.name = rawName;
            }
            if (track.name.isEmpty()) {
                track.name = fileLabelForSource(source);
            }

            QString kind = inferTrackKind(source);
            if (kind == "track") {
                const QString metadata =
                    (attributes.value("clazz").toString() + " " + attributes.value("renderer").toString() + " " +
                     attributes.value("windowFunction").toString() + " " + rawName)
                        .toLower();

                if (metadata.contains("alignment")) {
                    kind = "alignment";
                } else if (metadata.contains("splice") || metadata.contains("junction")) {
                    kind = "splice";
                } else if (metadata.contains("variant") || metadata.contains("mutation")) {
                    kind = "variant";
                } else if (metadata.contains("gene") || metadata.contains("annotation")) {
                    kind = "annotation";
                } else if (metadata.contains("coverage") || metadata.contains("wig") || metadata.contains("heatmap")) {
                    kind = "quantitative";
                }
            }

            track.kind = kind;
            track.visibility =
                visibilityAttr == "false" ? "hidden" : (track.visibility.isEmpty() ? "always" : track.visibility);
            track.color = colorForTrackKind(track.kind);
            track.requiresIndex = defaultRequiresIndex(track.source, track.kind);
            finalizeTrackDescriptor(&track);

            registerTrack(&explicitTracks, &explicitTrackKeys, track);
        }
    }

    file.close();

    if (xml.hasError()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("IGV XML parse error: %1").arg(xml.errorString());
        }
        return false;
    }

    if (!sawSessionRoot) {
        if (errorMessage != nullptr) {
            *errorMessage = "The selected XML file does not look like an IGV session.";
        }
        return false;
    }

    if (!imported.genomes.contains(imported.genome)) {
        imported.genomes.prepend(imported.genome);
    }

    imported.loci = parseLociText(imported.locus);
    if (imported.loci.isEmpty()) {
        imported.loci = QStringList({imported.locus});
    }
    imported.multiLocus = imported.loci.size() > 1;

    imported.tracks = explicitTracks.isEmpty() ? resources : explicitTracks;
    if (imported.tracks.isEmpty()) {
        imported.tracks.append(makeTrack(fileName, "Imported IGV Session", "track", "always"));
    }

    if (out != nullptr) {
        *out = imported;
    }
    return true;
}

bool loadCaseManifest(const QString& fileName, WorkspaceState* out, QString* errorMessage) {
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = "Could not read the selected case manifest.";
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = "The selected file is not a valid JSON case manifest.";
        }
        return false;
    }

    const QJsonObject object = document.object();
    const QFileInfo manifestFileInfo(fileName);

    WorkspaceState workspace;
    workspace.schema = object.value("schema").toString("igv-native-case/v1");
    workspace.title = object.value("title").toString(manifestFileInfo.completeBaseName());
    workspace.description = object.value("description").toString().trimmed();
    workspace.manifestPath = manifestFileInfo.absoluteFilePath();
    workspace.workspaceRoot = manifestFileInfo.dir().absolutePath();

    SessionState session = loadNativeSessionObject(QJsonObject(), &manifestFileInfo);
    session.schema = "igv-native-workspace-session/v1";

    const QString sessionFile = object.value("session_file").toString().trimmed();
    if (!sessionFile.isEmpty()) {
        const QString resolvedSessionFile = resolveSessionReference(sessionFile, manifestFileInfo);
        workspace.sessionSource = resolvedSessionFile;

        if (resolvedSessionFile.endsWith(".xml", Qt::CaseInsensitive)) {
            if (!loadIgvXmlSession(resolvedSessionFile, &session, errorMessage)) {
                return false;
            }
        } else if (!loadNativeSessionFile(resolvedSessionFile, &session, errorMessage)) {
            return false;
        }
    } else if (object.value("session").isObject()) {
        session = loadNativeSessionObject(object.value("session").toObject(), &manifestFileInfo);
        workspace.sessionSource = "embedded session";
    }

    if (object.contains("genome")) {
        session.genome = object.value("genome").toString(session.genome);
    }

    const QStringList manifestGenomes = parseGenomeList(object.value("genomes").toArray());
    for (const QString& genome : manifestGenomes) {
        if (!session.genomes.contains(genome)) {
            session.genomes.append(genome);
        }
    }
    if (!session.genomes.contains(session.genome)) {
        session.genomes.prepend(session.genome);
    }

    const QString topLevelLocus = object.value("locus").toString().trimmed();
    if (!topLevelLocus.isEmpty()) {
        session.locus = topLevelLocus;
    }

    const QStringList topLevelLoci = parseLociText(object.value("loci_text").toString());
    if (!topLevelLoci.isEmpty()) {
        session.loci = topLevelLoci;
    }
    for (const QJsonValue& locusValue : object.value("loci").toArray()) {
        const QString locus = locusValue.toString().trimmed();
        if (!locus.isEmpty() && !session.loci.contains(locus)) {
            session.loci.append(locus);
        }
    }
    if (session.loci.isEmpty() && !session.locus.isEmpty()) {
        session.loci = QStringList({session.locus});
    }
    session.multiLocus = object.contains("multi_locus") ? object.value("multi_locus").toBool() : session.loci.size() > 1;
    if (!session.loci.isEmpty()) {
        session.locus = session.multiLocus ? session.loci.join(" ") : session.loci.first();
    }

    const QList<RoiDescriptor> extraRois = parseRois(object.value("rois").toArray());
    for (const RoiDescriptor& roi : extraRois) {
        session.rois.append(roi);
    }

    const QList<ReviewItem> extraReviewItems = parseReviewQueue(object.value("review_queue").toArray());
    for (const ReviewItem& item : extraReviewItems) {
        session.reviewQueue.append(item);
    }

    QSet<QString> seenTracks;
    for (const TrackDescriptor& track : session.tracks) {
        seenTracks.insert(buildTrackKey(track));
    }

    const QList<TrackDescriptor> extraTracks = parseTracks(object.value("tracks").toArray(), &manifestFileInfo);
    for (const TrackDescriptor& track : extraTracks) {
        appendUniqueTrack(&session.tracks, &seenTracks, track);
    }

    for (const QJsonValue& sampleInfoValue : object.value("sample_info_files").toArray()) {
        const QString resolved = resolveSessionReference(sampleInfoValue.toString(), manifestFileInfo);
        if (!resolved.isEmpty()) {
            workspace.sampleInfoFiles.append(resolved);
        }
    }

    workspace.session = session;
    workspace.sampleInfo = parseSampleInfoFiles(workspace.sampleInfoFiles, &workspace.importIssues);
    refreshWorkspaceReadiness(&workspace);

    if (out != nullptr) {
        *out = workspace;
    }
    return true;
}

bool loadCaseFolder(const QString& directoryPath, WorkspaceState* out, QString* errorMessage) {
    const QFileInfo rootInfo(directoryPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = "The selected path is not a readable case folder.";
        }
        return false;
    }

    const QString rootPath = QDir::cleanPath(rootInfo.absoluteFilePath());
    const QStringList manifestCandidates = findFilesRecursively(rootPath, &isManifestCandidatePath);
    if (!manifestCandidates.isEmpty()) {
        const QString chosenManifest = choosePreferredCandidate(manifestCandidates, rootPath);
        WorkspaceState manifestWorkspace;
        if (!loadCaseManifest(chosenManifest, &manifestWorkspace, errorMessage)) {
            return false;
        }

        manifestWorkspace.importIssues.append(
            discoveryIssuesForCandidates(manifestCandidates, chosenManifest, "Multiple manifests"));
        if (out != nullptr) {
            *out = manifestWorkspace;
        }
        return true;
    }

    WorkspaceState workspace;
    workspace.schema = "igv-native-case-folder/v1";
    workspace.title = rootInfo.fileName().isEmpty() ? rootPath : rootInfo.fileName();
    workspace.description = "Autodiscovered from a case folder";
    workspace.workspaceRoot = rootPath;

    SessionState session = loadNativeSessionObject(QJsonObject(), nullptr);
    session.schema = "igv-native-workspace-session/v1";

    const QStringList nativeSessionCandidates = findFilesRecursively(rootPath, &isNativeSessionCandidatePath);
    const QStringList xmlSessionCandidates = findFilesRecursively(rootPath, &isXmlSessionCandidatePath);

    QString chosenSessionFile;
    if (!nativeSessionCandidates.isEmpty()) {
        chosenSessionFile = choosePreferredCandidate(nativeSessionCandidates, rootPath);
        workspace.importIssues.append(
            discoveryIssuesForCandidates(nativeSessionCandidates, chosenSessionFile, "Multiple native sessions"));
        if (!loadNativeSessionFile(chosenSessionFile, &session, errorMessage)) {
            return false;
        }
        workspace.sessionSource = chosenSessionFile;
    } else if (!xmlSessionCandidates.isEmpty()) {
        chosenSessionFile = choosePreferredCandidate(xmlSessionCandidates, rootPath);
        workspace.importIssues.append(
            discoveryIssuesForCandidates(xmlSessionCandidates, chosenSessionFile, "Multiple XML sessions"));
        if (!loadIgvXmlSession(chosenSessionFile, &session, errorMessage)) {
            return false;
        }
        workspace.sessionSource = chosenSessionFile;
    }

    QSet<QString> seenTracks;
    for (const TrackDescriptor& track : session.tracks) {
        seenTracks.insert(buildTrackKey(track));
    }

    const QList<TrackDescriptor> discoveredTracks = discoverTracks(rootPath);
    if (workspace.sessionSource.isEmpty()) {
        session.tracks.clear();
        seenTracks.clear();
    }
    for (const TrackDescriptor& track : discoveredTracks) {
        appendUniqueTrack(&session.tracks, &seenTracks, track);
    }

    const QStringList discoveredSampleInfo = findFilesRecursively(rootPath, &isSampleInfoCandidatePath);
    for (const QString& sampleInfoFile : discoveredSampleInfo) {
        if (!workspace.sampleInfoFiles.contains(sampleInfoFile)) {
            workspace.sampleInfoFiles.append(sampleInfoFile);
        }
    }

    workspace.session = session;
    workspace.sampleInfo = parseSampleInfoFiles(workspace.sampleInfoFiles, &workspace.importIssues);

    if (workspace.sessionSource.isEmpty() && workspace.session.tracks.isEmpty() && workspace.sampleInfoFiles.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "No supported sessions, tracks, or sample metadata files were found in the selected folder.";
        }
        return false;
    }

    refreshWorkspaceReadiness(&workspace);
    if (out != nullptr) {
        *out = workspace;
    }
    return true;
}

void refreshWorkspaceReadiness(WorkspaceState* workspace) {
    if (workspace == nullptr) {
        return;
    }
    refreshReviewQueue(&workspace->session);
    annotateTracksFromSampleInfo(workspace);
    workspace->cohortPresets = buildCohortPresets(*workspace);
    workspace->readinessIssues = buildReadinessIssues(*workspace);
}

QList<TrackDescriptor> tracksForPreset(const WorkspaceState& workspace, const QString& presetName) {
    const QString resolvedPresetName = effectivePresetName(presetName);
    if (workspace.cohortPresets.isEmpty() || resolvedPresetName == "All Tracks") {
        return workspace.session.tracks;
    }

    for (const CohortPreset& preset : workspace.cohortPresets) {
        if (preset.name != resolvedPresetName) {
            continue;
        }

        QList<TrackDescriptor> visibleTracks;
        for (const TrackDescriptor& track : workspace.session.tracks) {
            if (trackMatchesPreset(track, preset)) {
                visibleTracks.append(track);
            }
        }
        return visibleTracks;
    }

    return workspace.session.tracks;
}

QString buildReviewPacketMarkdown(const WorkspaceState& workspace, const QString& presetName) {
    const QString resolvedPresetName = effectivePresetName(presetName);
    const QList<TrackDescriptor> visibleTracks = tracksForPreset(workspace, resolvedPresetName);
    const QString title = workspace.title.trimmed().isEmpty() ? "Ad hoc review packet" : workspace.title.trimmed();

    int reviewedCount = 0;
    int followUpCount = 0;
    int pendingCount = 0;
    for (const ReviewItem& item : workspace.session.reviewQueue) {
        const QString status = item.status.trimmed().toLower();
        if (status == "reviewed") {
            ++reviewedCount;
        } else if (status == "follow-up") {
            ++followUpCount;
        } else {
            ++pendingCount;
        }
    }

    QString markdown;
    markdown += QString("# %1\n\n").arg(markdownEscape(title));
    markdown += "## Summary\n\n";
    markdown += QString("- Workspace schema: `%1`\n").arg(markdownEscape(workspace.schema));
    markdown += QString("- Genome: `%1`\n").arg(markdownEscape(workspace.session.genome));
    markdown += QString("- Preset: `%1`\n").arg(markdownEscape(resolvedPresetName));
    markdown += QString("- Visible tracks: %1\n").arg(visibleTracks.size());
    markdown += QString("- Review queue: %1 total, %2 reviewed, %3 follow-up, %4 pending\n")
                    .arg(workspace.session.reviewQueue.size())
                    .arg(reviewedCount)
                    .arg(followUpCount)
                    .arg(pendingCount);
    if (!workspace.session.locus.trimmed().isEmpty()) {
        markdown += QString("- Current locus: `%1`\n").arg(markdownEscape(workspace.session.locus));
    }

    markdown += "\n## Readiness\n\n";
    markdown += readinessSummaryMarkdown(workspace.readinessIssues);

    markdown += "\n## Visible Tracks\n\n";
    if (visibleTracks.isEmpty()) {
        markdown += "No tracks are visible for the selected preset.\n";
    } else {
        markdown += "| Name | Kind | Group | Sample | Source |\n";
        markdown += "| --- | --- | --- | --- | --- |\n";
        for (const TrackDescriptor& track : visibleTracks) {
            markdown += QString("| %1 | %2 | %3 | %4 | %5 |\n")
                            .arg(markdownEscape(track.name),
                                 markdownEscape(track.kind),
                                 markdownEscape(track.group.isEmpty() ? "-" : track.group),
                                 markdownEscape(track.sampleId.isEmpty() ? "-" : track.sampleId),
                                 markdownEscape(track.source));
        }
    }

    markdown += "\n## Review Queue\n\n";
    if (workspace.session.reviewQueue.isEmpty()) {
        markdown += "No review items are currently queued.\n";
    } else {
        markdown += "| Locus | Label | Status | Note |\n";
        markdown += "| --- | --- | --- | --- |\n";
        for (const ReviewItem& item : workspace.session.reviewQueue) {
            markdown += QString("| %1 | %2 | %3 | %4 |\n")
                            .arg(markdownEscape(item.locus),
                                 markdownEscape(item.label.isEmpty() ? "Review Item" : item.label),
                                 markdownEscape(item.status.isEmpty() ? "pending" : item.status),
                                 markdownEscape(item.note.isEmpty() ? "-" : item.note));
        }
    }

    if (!workspace.session.rois.isEmpty()) {
        markdown += "\n## Regions Of Interest\n\n";
        markdown += "| Locus | Label |\n";
        markdown += "| --- | --- |\n";
        for (const RoiDescriptor& roi : workspace.session.rois) {
            markdown += QString("| %1 | %2 |\n")
                            .arg(markdownEscape(roi.locus),
                                 markdownEscape(roi.label.isEmpty() ? "ROI" : roi.label));
        }
    }

    return markdown;
}

QString buildReviewPacketHtml(const WorkspaceState& workspace,
                              const QString& presetName,
                              const QString& overviewSnapshotFile,
                              const QMap<QString, QString>& reviewSnapshotFiles) {
    const QString resolvedPresetName = effectivePresetName(presetName);
    const QList<TrackDescriptor> visibleTracks = tracksForPreset(workspace, resolvedPresetName);
    const QString title = workspace.title.trimmed().isEmpty() ? "Ad hoc review report" : workspace.title.trimmed();

    int reviewedCount = 0;
    int followUpCount = 0;
    int pendingCount = 0;
    for (const ReviewItem& item : workspace.session.reviewQueue) {
        const QString status = item.status.trimmed().toLower();
        if (status == "reviewed") {
            ++reviewedCount;
        } else if (status == "follow-up") {
            ++followUpCount;
        } else {
            ++pendingCount;
        }
    }

    QString html;
    html += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\" />\n";
    html += QString("<title>%1</title>\n").arg(htmlEscape(title));
    html +=
        "<style>"
        "body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif;margin:0;background:#f4f1ea;color:#1f2b33;}"
        ".page{max-width:1100px;margin:0 auto;padding:32px 28px 60px;}"
        ".hero{background:linear-gradient(135deg,#133c55,#2c6e63);color:#f8faf7;padding:28px;border-radius:22px;box-shadow:0 18px 40px rgba(19,60,85,.18);}"
        ".hero h1{margin:0 0 10px;font-size:34px;line-height:1.1;}"
        ".hero p{margin:6px 0 0;color:#dceae4;}"
        ".summary-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px;margin:24px 0;}"
        ".metric{background:#fffdf9;border:1px solid #e3ddd1;border-radius:18px;padding:16px 18px;box-shadow:0 8px 24px rgba(31,43,51,.06);}"
        ".metric .label{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:#6b7a83;margin-bottom:8px;}"
        ".metric .value{font-size:22px;font-weight:700;color:#143545;}"
        ".section{margin-top:28px;background:#fffdf9;border:1px solid #e3ddd1;border-radius:20px;padding:22px 24px;box-shadow:0 8px 24px rgba(31,43,51,.05);}"
        ".section h2{margin:0 0 16px;font-size:20px;color:#16303a;}"
        ".section ul{margin:0;padding-left:20px;}"
        "table{width:100%;border-collapse:collapse;font-size:14px;}"
        "th,td{text-align:left;padding:10px 12px;border-bottom:1px solid #ece5d8;vertical-align:top;}"
        "th{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:#6b7a83;}"
        ".snapshot{margin-top:16px;}"
        ".snapshot img{width:100%;max-width:100%;border-radius:16px;border:1px solid #ddd3c2;display:block;background:#f4f1ea;}"
        ".review-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px;}"
        ".review-card{border:1px solid #ece5d8;border-radius:18px;padding:18px;background:#fff;}"
        ".review-card h3{margin:0 0 8px;font-size:18px;color:#143545;}"
        ".meta{font-size:13px;color:#5d6b72;margin-bottom:10px;}"
        ".status{display:inline-block;padding:4px 10px;border-radius:999px;font-size:12px;font-weight:700;text-transform:uppercase;letter-spacing:.06em;}"
        ".status.reviewed{background:#d8ead5;color:#235239;}"
        ".status.follow-up{background:#f6e6c8;color:#7a5715;}"
        ".status.pending{background:#d9e5eb;color:#38505c;}"
        ".note{margin:10px 0 0;font-size:14px;line-height:1.5;}"
        "code{background:#f1ede5;border-radius:6px;padding:1px 6px;}"
        "</style>\n</head>\n<body>\n";

    html += "<div class=\"page\">\n";
    html += "<section class=\"hero\">\n";
    html += QString("<h1>%1</h1>\n").arg(htmlEscape(title));
    html += QString("<p>Genome <code>%1</code> with preset <code>%2</code>.</p>\n")
                .arg(htmlEscape(workspace.session.genome), htmlEscape(resolvedPresetName));
    if (!workspace.session.locus.trimmed().isEmpty()) {
        html += QString("<p>Current locus: <code>%1</code></p>\n").arg(htmlEscape(workspace.session.locus));
    }
    html += "</section>\n";

    html += "<section class=\"summary-grid\">\n";
    html += QString("<div class=\"metric\"><div class=\"label\">Visible Tracks</div><div class=\"value\">%1</div></div>\n")
                .arg(visibleTracks.size());
    html += QString("<div class=\"metric\"><div class=\"label\">Review Items</div><div class=\"value\">%1</div></div>\n")
                .arg(workspace.session.reviewQueue.size());
    html += QString("<div class=\"metric\"><div class=\"label\">Reviewed</div><div class=\"value\">%1</div></div>\n")
                .arg(reviewedCount);
    html += QString("<div class=\"metric\"><div class=\"label\">Follow-up</div><div class=\"value\">%1</div></div>\n")
                .arg(followUpCount);
    html += QString("<div class=\"metric\"><div class=\"label\">Pending</div><div class=\"value\">%1</div></div>\n")
                .arg(pendingCount);
    html += "</section>\n";

    html += "<section class=\"section\">\n<h2>Readiness</h2>\n";
    if (workspace.readinessIssues.isEmpty()) {
        html += "<p>No local readiness issues detected.</p>\n";
    } else {
        html += "<ul>\n";
        for (const ReadinessIssue& issue : workspace.readinessIssues) {
            html += QString("<li><strong>%1</strong> %2: %3</li>\n")
                        .arg(htmlEscape(issue.severity),
                             htmlEscape(issue.check),
                             htmlEscape(issue.detail));
        }
        html += "</ul>\n";
    }
    html += "</section>\n";

    if (!overviewSnapshotFile.trimmed().isEmpty()) {
        html += "<section class=\"section\">\n<h2>Overview Snapshot</h2>\n";
        html += QString("<div class=\"snapshot\"><img src=\"%1\" alt=\"Overview snapshot\" /></div>\n")
                    .arg(htmlEscape(overviewSnapshotFile));
        html += "</section>\n";
    }

    html += "<section class=\"section\">\n<h2>Visible Tracks</h2>\n";
    if (visibleTracks.isEmpty()) {
        html += "<p>No tracks are visible for the selected preset.</p>\n";
    } else {
        html += "<table><thead><tr><th>Name</th><th>Kind</th><th>Group</th><th>Sample</th><th>Source</th></tr></thead><tbody>\n";
        for (const TrackDescriptor& track : visibleTracks) {
            html += QString("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td><code>%5</code></td></tr>\n")
                        .arg(htmlEscape(track.name),
                             htmlEscape(track.kind),
                             htmlEscape(track.group.isEmpty() ? "-" : track.group),
                             htmlEscape(track.sampleId.isEmpty() ? "-" : track.sampleId),
                             htmlEscape(track.source));
        }
        html += "</tbody></table>\n";
    }
    html += "</section>\n";

    html += "<section class=\"section\">\n<h2>Review Queue</h2>\n";
    if (workspace.session.reviewQueue.isEmpty()) {
        html += "<p>No review items are currently queued.</p>\n";
    } else {
        html += "<div class=\"review-grid\">\n";
        for (const ReviewItem& item : workspace.session.reviewQueue) {
            const QString status = item.status.isEmpty() ? "pending" : item.status;
            html += "<article class=\"review-card\">\n";
            html += QString("<h3>%1</h3>\n").arg(htmlEscape(item.label.isEmpty() ? "Review Item" : item.label));
            html += QString("<div class=\"meta\"><code>%1</code></div>\n").arg(htmlEscape(item.locus));
            html += QString("<span class=\"status %1\">%2</span>\n")
                        .arg(statusClassName(status), htmlEscape(status));
            if (!item.note.trimmed().isEmpty()) {
                html += QString("<p class=\"note\">%1</p>\n").arg(htmlEscape(item.note));
            }
            if (reviewSnapshotFiles.contains(item.locus)) {
                html += QString("<div class=\"snapshot\"><img src=\"%1\" alt=\"%2 snapshot\" /></div>\n")
                            .arg(htmlEscape(reviewSnapshotFiles.value(item.locus)),
                                 htmlEscape(item.label.isEmpty() ? item.locus : item.label));
            }
            html += "</article>\n";
        }
        html += "</div>\n";
    }
    html += "</section>\n";

    if (!workspace.session.rois.isEmpty()) {
        html += "<section class=\"section\">\n<h2>Regions Of Interest</h2>\n";
        html += "<table><thead><tr><th>Locus</th><th>Label</th></tr></thead><tbody>\n";
        for (const RoiDescriptor& roi : workspace.session.rois) {
            html += QString("<tr><td><code>%1</code></td><td>%2</td></tr>\n")
                        .arg(htmlEscape(roi.locus),
                             htmlEscape(roi.label.isEmpty() ? "ROI" : roi.label));
        }
        html += "</tbody></table>\n</section>\n";
    }

    html += "</div>\n</body>\n</html>\n";
    return html;
}

QStringList parseLociText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    return trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
}

QString inferTrackKind(const QString& source) {
    const QString lower = source.toLower();

    if (lower.endsWith(".bam") || lower.endsWith(".cram") || lower.endsWith(".sam")) {
        return "alignment";
    }
    if (lower.endsWith(".vcf") || lower.endsWith(".vcf.gz") || lower.endsWith(".bcf")) {
        return "variant";
    }
    if (lower.endsWith(".bw") || lower.endsWith(".bigwig") || lower.endsWith(".wig") || lower.endsWith(".bedgraph") ||
        lower.endsWith(".tdf") || lower.endsWith(".seg")) {
        return "quantitative";
    }
    if (lower.endsWith(".bed") || lower.endsWith(".bed.gz") || lower.endsWith(".gff") || lower.endsWith(".gff3") ||
        lower.endsWith(".gff.gz") || lower.endsWith(".gtf") || lower.endsWith(".gtf.gz") ||
        lower.endsWith(".bb") || lower.endsWith(".bigbed")) {
        return "annotation";
    }
    if (lower.endsWith(".fa") || lower.endsWith(".fasta") || lower.endsWith(".2bit") || lower.endsWith(".genome")) {
        return "reference";
    }
    if (lower.contains("rna") || lower.contains("junction")) {
        return "splice";
    }
    return "track";
}

QColor colorForTrackKind(const QString& kind) {
    if (kind == "reference") {
        return QColor("#5f6b7a");
    }
    if (kind == "annotation") {
        return QColor("#247a78");
    }
    if (kind == "alignment") {
        return QColor("#c96e39");
    }
    if (kind == "splice") {
        return QColor("#2c6d8a");
    }
    if (kind == "variant") {
        return QColor("#9f3f4f");
    }
    if (kind == "quantitative") {
        return QColor("#73824a");
    }
    return QColor("#6a6e73");
}

}  // namespace session_parsers
