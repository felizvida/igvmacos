#include "GenomeCanvas.h"

#include "RustBridge.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRegularExpression>
#include <QVector>

namespace {

struct ParsedInterval {
    QString contig;
    qint64 start = 0;
    qint64 end = 0;
    bool valid = false;
};

struct RoiOverlay {
    RoiDescriptor roi;
    ParsedInterval interval;
    bool positioned = false;
};

struct PreviewReadSpan {
    qint64 start = 0;
    qint64 end = 0;
    bool split = false;
};

struct PreviewJunctionArc {
    qint64 start = 0;
    qint64 end = 0;
    int count = 0;
};

struct ParsedAlignmentPreview {
    bool ok = false;
    QString message;
    int readCount = 0;
    int splitReadCount = 0;
    int maxCoverage = 0;
    QList<int> coverageBins;
    QList<PreviewReadSpan> reads;
    QList<PreviewJunctionArc> junctions;
};

QStringList visibleLoci(const SessionState& state) {
    if (state.multiLocus && !state.loci.isEmpty()) {
        return state.loci;
    }

    if (!state.locus.trimmed().isEmpty()) {
        return {state.locus};
    }

    return {"All"};
}

ParsedInterval parseInterval(const QString& locus) {
    static const QRegularExpression kCoordinatePattern(R"(^\s*([^:\s]+):([0-9,]+)(?:-([0-9,]+))?\s*$)");

    const QRegularExpressionMatch match = kCoordinatePattern.match(locus);
    if (!match.hasMatch()) {
        return {};
    }

    QString startText = match.captured(2);
    QString endText = match.captured(3).isEmpty() ? startText : match.captured(3);
    startText.remove(',');
    endText.remove(',');

    bool startOk = false;
    bool endOk = false;
    qint64 start = startText.toLongLong(&startOk);
    qint64 end = endText.toLongLong(&endOk);
    if (!startOk || !endOk) {
        return {};
    }

    if (end < start) {
        std::swap(start, end);
    }

    ParsedInterval interval;
    interval.contig = match.captured(1);
    interval.start = start;
    interval.end = end;
    interval.valid = true;
    return interval;
}

bool overlaps(const ParsedInterval& lhs, const ParsedInterval& rhs) {
    return lhs.valid && rhs.valid && lhs.contig == rhs.contig && lhs.start <= rhs.end && rhs.start <= lhs.end;
}

qint64 intervalSpanBp(const ParsedInterval& interval) {
    if (!interval.valid) {
        return 0;
    }
    return std::max<qint64>(1, interval.end - interval.start + 1);
}

qreal projectCoordinate(qint64 coordinate, const ParsedInterval& interval, qreal left, qreal width) {
    if (!interval.valid || interval.end <= interval.start) {
        return left;
    }

    const double ratio = static_cast<double>(coordinate - interval.start) /
                         static_cast<double>(interval.end - interval.start);
    return left + std::clamp(ratio, 0.0, 1.0) * width;
}

QList<RoiOverlay> roiOverlaysForPanel(const QString& panelLocus, const QList<RoiDescriptor>& rois) {
    QList<RoiOverlay> overlays;
    const ParsedInterval panelInterval = parseInterval(panelLocus);
    const QString trimmedPanelLocus = panelLocus.trimmed();

    for (const RoiDescriptor& roi : rois) {
        RoiOverlay overlay;
        overlay.roi = roi;

        if (panelInterval.valid) {
            overlay.interval = parseInterval(roi.locus);
            overlay.positioned = overlaps(panelInterval, overlay.interval);
            if (overlay.positioned) {
                overlays.append(overlay);
                continue;
            }
        }

        if (roi.locus.trimmed() == trimmedPanelLocus) {
            overlays.append(overlay);
        }
    }

    return overlays;
}

QString experimentDisplayLabel(const QString& experimentType) {
    if (experimentType == "rna") {
        return "RNA";
    }
    if (experimentType == "dna") {
        return "DNA";
    }
    if (experimentType == "long-read") {
        return "Long-read";
    }
    return experimentType.isEmpty() ? "Track" : experimentType;
}

QString trackModeLabel(const TrackDescriptor& track) {
    if (track.kind != "alignment") {
        return track.visibility;
    }

    QStringList parts;
    parts.append(experimentDisplayLabel(track.experimentType));
    if (track.visibilityWindowKb > 0) {
        parts.append(QString("<%1 kb").arg(track.visibilityWindowKb));
    }
    return parts.join(" | ");
}

QString alignmentPreviewKey(const QString& source, const QString& locus) {
    return source + "|" + locus;
}

QJsonObject requestAlignmentPreview(const QString& source, const QString& locus) {
    const QByteArray sourceUtf8 = source.toUtf8();
    const QByteArray locusUtf8 = locus.toUtf8();
    const char* response = igv_alignment_preview_json(sourceUtf8.constData(), locusUtf8.constData());
    if (response == nullptr) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(response));
    return document.isObject() ? document.object() : QJsonObject();
}

ParsedAlignmentPreview parseAlignmentPreview(const QJsonObject& object) {
    ParsedAlignmentPreview preview;
    preview.ok = object.value("ok").toBool(false);
    preview.message = object.value("message").toString().trimmed();
    preview.readCount = object.value("read_count").toInt();
    preview.splitReadCount = object.value("split_read_count").toInt();
    preview.maxCoverage = object.value("max_coverage").toInt();

    for (const QJsonValue& value : object.value("coverage_bins").toArray()) {
        preview.coverageBins.append(value.toInt());
    }

    for (const QJsonValue& value : object.value("reads").toArray()) {
        const QJsonObject readObject = value.toObject();
        PreviewReadSpan read;
        read.start = readObject.value("start").toVariant().toLongLong();
        read.end = readObject.value("end").toVariant().toLongLong();
        read.split = readObject.value("split").toBool(false);
        if (read.end >= read.start) {
            preview.reads.append(read);
        }
    }

    for (const QJsonValue& value : object.value("junctions").toArray()) {
        const QJsonObject junctionObject = value.toObject();
        PreviewJunctionArc junction;
        junction.start = junctionObject.value("start").toVariant().toLongLong();
        junction.end = junctionObject.value("end").toVariant().toLongLong();
        junction.count = junctionObject.value("count").toInt();
        if (junction.end >= junction.start) {
            preview.junctions.append(junction);
        }
    }

    return preview;
}

void drawLaneMessage(QPainter& painter, const QRectF& lane, const QString& message) {
    painter.setPen(QColor(255, 255, 255, 210));
    painter.drawText(lane.adjusted(12.0, 10.0, -12.0, -8.0),
                     Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                     message);
}

void drawSyntheticAlignmentTrackGlyphs(QPainter& painter, const QRectF& lane) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 180));
    const qreal readWidth = lane.width() / 10.0;
    for (int i = 0; i < 7; ++i) {
        const qreal x = lane.left() + 10.0 + (i * readWidth * 0.95);
        const QRectF readRect(x, lane.center().y() - 5.0, readWidth * 0.62, 10.0);
        painter.drawRoundedRect(readRect, 3.0, 3.0);
    }
}

void drawAlignmentTrackGlyphs(QPainter& painter,
                              const QRectF& lane,
                              const TrackDescriptor& track,
                              const ParsedInterval& panelInterval,
                              const QJsonObject* previewObject) {
    if (!panelInterval.valid) {
        drawLaneMessage(painter,
                        lane,
                        QString("Enter a coordinate locus to inspect %1 alignments")
                            .arg(experimentDisplayLabel(track.experimentType).toLower()));
        return;
    }

    const qint64 visibilityWindowBp = static_cast<qint64>(track.visibilityWindowKb) * 1000;
    if (visibilityWindowBp > 0 && intervalSpanBp(panelInterval) > visibilityWindowBp) {
        drawLaneMessage(painter,
                        lane,
                        QString("Zoom below %1 kb to load %2 alignments")
                            .arg(track.visibilityWindowKb)
                            .arg(experimentDisplayLabel(track.experimentType).toLower()));
        return;
    }

    const QRectF content = lane.adjusted(10.0, 6.0, -10.0, -6.0);
    const qreal contentWidth = content.width();
    const qreal contentHeight = content.height();
    ParsedAlignmentPreview preview;
    const bool hasPreview = previewObject != nullptr && !previewObject->isEmpty();
    if (hasPreview) {
        preview = parseAlignmentPreview(*previewObject);
    }

    if (hasPreview && !preview.ok) {
        drawLaneMessage(painter,
                        lane,
                        preview.message.isEmpty() ? "Real alignment preview is unavailable for this source."
                                                  : preview.message);
        return;
    }

    const bool useRealPreview = hasPreview && preview.ok;

    if (track.showSpliceJunctionTrack) {
        painter.setPen(QPen(QColor(255, 255, 255, 165), 1.3));
        const qreal baseY = content.top() + std::max(5.0, contentHeight * 0.26);
        painter.drawLine(QPointF(content.left() + 6.0, baseY), QPointF(content.right() - 6.0, baseY));
        if (useRealPreview && !preview.junctions.isEmpty()) {
            for (const PreviewJunctionArc& junction : preview.junctions) {
                const qreal startX = projectCoordinate(junction.start, panelInterval, content.left(), contentWidth);
                const qreal endX = projectCoordinate(junction.end, panelInterval, content.left(), contentWidth);
                QPen arcPen(QColor(255, 255, 255, 175), std::min(3.0, 1.0 + junction.count * 0.35));
                painter.setPen(arcPen);
                QPainterPath arc;
                arc.moveTo(startX, baseY);
                arc.quadTo((startX + endX) / 2.0,
                           content.top() - std::min(4.0, contentHeight * 0.12),
                           endX,
                           baseY);
                painter.drawPath(arc);
            }
        } else {
            for (int arcIndex = 0; arcIndex < 3; ++arcIndex) {
                const qreal startX = content.left() + 18.0 + arcIndex * (contentWidth / 4.2);
                const qreal endX = std::min(content.right() - 8.0, startX + contentWidth / 6.0);
                QPainterPath arc;
                arc.moveTo(startX, baseY);
                arc.quadTo((startX + endX) / 2.0,
                           content.top() - std::min(4.0, contentHeight * 0.12),
                           endX,
                           baseY);
                painter.drawPath(arc);
            }
        }
    }

    if (track.showAlignmentTrack) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 180));
        if (useRealPreview && !preview.reads.isEmpty()) {
            const int maxRows = track.showCoverageTrack ? 2 : 3;
            const qreal readHeight = std::clamp(contentHeight * 0.18, 5.0, 8.0);
            const qreal rowGap = std::max(3.0, contentHeight * 0.08);
            QVector<qint64> rowEnds;

            for (int readIndex = 0; readIndex < preview.reads.size(); ++readIndex) {
                const PreviewReadSpan& read = preview.reads.at(readIndex);
                int assignedRow = -1;
                for (int row = 0; row < rowEnds.size(); ++row) {
                    if (read.start > rowEnds.at(row) + 4) {
                        assignedRow = row;
                        rowEnds[row] = read.end;
                        break;
                    }
                }
                if (assignedRow < 0) {
                    if (rowEnds.size() < maxRows) {
                        rowEnds.append(read.end);
                        assignedRow = rowEnds.size() - 1;
                    } else {
                        assignedRow = readIndex % maxRows;
                        rowEnds[assignedRow] = std::max(rowEnds.at(assignedRow), read.end);
                    }
                }

                const qreal y = content.top() + contentHeight * 0.36 + assignedRow * (readHeight + rowGap);
                const qreal readLeft = projectCoordinate(read.start, panelInterval, content.left(), contentWidth);
                const qreal readRight = projectCoordinate(read.end, panelInterval, content.left(), contentWidth);
                const QRectF readRect(std::min(readLeft, readRight),
                                      y,
                                      std::max(14.0, std::abs(readRight - readLeft)),
                                      readHeight);
                painter.setBrush(read.split ? QColor(255, 244, 225, 210) : QColor(255, 255, 255, 180));
                painter.drawRoundedRect(readRect, 2.5, 2.5);
            }
        } else {
            drawSyntheticAlignmentTrackGlyphs(painter, lane);
        }
    }

    if (track.showCoverageTrack) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 145));
        const qreal baseline = content.bottom() - 2.0;
        if (useRealPreview && !preview.coverageBins.isEmpty() && preview.maxCoverage > 0) {
            const int coverageBinCount = std::max(8, static_cast<int>(preview.coverageBins.size()) + 2);
            const qreal barWidth = std::max(3.0, contentWidth / coverageBinCount);
            for (int barIndex = 0; barIndex < preview.coverageBins.size(); ++barIndex) {
                const qreal normalized =
                    static_cast<qreal>(preview.coverageBins.at(barIndex)) / static_cast<qreal>(preview.maxCoverage);
                const qreal barHeight = std::max(2.0, normalized * contentHeight * 0.32);
                const qreal x = content.left() + 6.0 + barIndex * (barWidth * 1.08);
                if (x + barWidth > content.right() - 4.0) {
                    break;
                }
                painter.drawRoundedRect(QRectF(x, baseline - barHeight, barWidth, barHeight), 1.8, 1.8);
            }
        } else {
            const qreal barWidth = std::max(4.0, contentWidth / 18.0);
            for (int barIndex = 0; barIndex < 12; ++barIndex) {
                const qreal phase = static_cast<qreal>((barIndex % 6) + 1) / 6.0;
                const qreal barHeight = std::max(4.0, contentHeight * (0.16 + phase * 0.22));
                const qreal x = content.left() + 6.0 + barIndex * (barWidth * 1.08);
                if (x + barWidth > content.right() - 4.0) {
                    break;
                }
                painter.drawRoundedRect(QRectF(x, baseline - barHeight, barWidth, barHeight), 1.8, 1.8);
            }
        }
    }

    if (useRealPreview) {
        painter.setPen(QColor(255, 255, 255, 210));
        const QString metrics = preview.splitReadCount > 0
                                    ? QString("%1 reads | %2 split")
                                          .arg(preview.readCount)
                                          .arg(preview.splitReadCount)
                                    : QString("%1 reads").arg(preview.readCount);
        painter.drawText(content.adjusted(0.0, 2.0, -2.0, 0.0),
                         Qt::AlignRight | Qt::AlignTop | Qt::TextSingleLine,
                         metrics);
        if (preview.readCount == 0) {
            drawLaneMessage(painter, lane, "No overlapping reads in the local SAM preview");
        }
    }
}

void drawTrackGlyphs(QPainter& painter,
                     const QRectF& lane,
                     const TrackDescriptor& track,
                     const ParsedInterval& panelInterval,
                     const QJsonObject* previewObject) {
    painter.save();
    painter.setClipRect(lane.adjusted(4.0, 3.0, -4.0, -3.0));

    if (track.kind == "alignment") {
        drawAlignmentTrackGlyphs(painter, lane, track, panelInterval, previewObject);
    } else if (track.kind == "splice") {
        painter.setPen(QPen(QColor(255, 255, 255, 180), 1.7));
        const qreal baseY = lane.center().y();
        painter.drawLine(QPointF(lane.left() + 8.0, baseY), QPointF(lane.right() - 8.0, baseY));
        for (int i = 0; i < 3; ++i) {
            const qreal startX = lane.left() + 20.0 + (i * lane.width() / 4.0);
            const qreal endX = startX + lane.width() / 6.0;
            QPainterPath arc;
            arc.moveTo(startX, baseY);
            arc.quadTo((startX + endX) / 2.0, lane.top() - 4.0, endX, baseY);
            painter.drawPath(arc);
        }
    } else if (track.kind == "variant") {
        painter.setPen(QPen(QColor(255, 255, 255, 190), 1.5));
        const qreal baseline = lane.bottom() - 4.0;
        painter.drawLine(QPointF(lane.left() + 8.0, baseline), QPointF(lane.right() - 8.0, baseline));
        for (int i = 0; i < 6; ++i) {
            const qreal x = lane.left() + 18.0 + (i * lane.width() / 7.0);
            painter.drawLine(QPointF(x, baseline), QPointF(x, lane.top() + 7.0 + (i % 2) * 4.0));
        }
    } else if (track.kind == "quantitative") {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 180));
        const qreal barWidth = lane.width() / 14.0;
        for (int i = 0; i < 10; ++i) {
            const qreal barHeight = 6.0 + (i % 5) * 3.0;
            const qreal x = lane.left() + 10.0 + (i * barWidth * 1.1);
            const QRectF barRect(x, lane.bottom() - 5.0 - barHeight, barWidth * 0.65, barHeight);
            painter.drawRoundedRect(barRect, 2.0, 2.0);
        }
    } else if (track.kind == "annotation" || track.kind == "reference") {
        painter.setPen(QPen(QColor(255, 255, 255, 180), 1.5));
        const qreal midY = lane.center().y();
        painter.drawLine(QPointF(lane.left() + 8.0, midY), QPointF(lane.right() - 8.0, midY));
        painter.setBrush(QColor(255, 255, 255, 180));
        painter.setPen(Qt::NoPen);
        for (int i = 0; i < 4; ++i) {
            const qreal x = lane.left() + 20.0 + (i * lane.width() / 5.0);
            const QRectF exonRect(x, midY - 6.0, lane.width() / 10.0, 12.0);
            painter.drawRoundedRect(exonRect, 3.0, 3.0);
        }
    } else {
        painter.setPen(QPen(QColor(255, 255, 255, 150), 1.0));
        painter.drawLine(QPointF(lane.left() + 8.0, lane.center().y()),
                         QPointF(lane.right() - 8.0, lane.center().y()));
    }

    painter.restore();
}

void drawRoiBadges(QPainter& painter, const QRectF& panelRect, const QList<RoiOverlay>& overlays) {
    qreal badgeLeft = panelRect.left() + 16.0;
    qreal badgeTop = panelRect.top() + 68.0;

    for (const RoiOverlay& overlay : overlays) {
        const QString label = overlay.roi.label.isEmpty() ? overlay.roi.locus : overlay.roi.label;
        const QRectF badgeRect(badgeLeft, badgeTop, std::min(panelRect.width() - 32.0, 126.0), 16.0);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(140, 63, 45, 170));
        painter.drawRoundedRect(badgeRect, 8.0, 8.0);

        painter.setPen(QColor("#fff8f2"));
        painter.drawText(badgeRect.adjusted(8.0, 0.0, -8.0, 0.0),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                         label);

        badgeTop += 18.0;
        if (badgeTop + 16.0 > panelRect.bottom() - 12.0) {
            break;
        }
    }
}

}  // namespace

GenomeCanvas::GenomeCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(780, 360);
}

void GenomeCanvas::setSessionState(const SessionState& state) {
    state_ = state;
    refreshAlignmentPreviews();
    update();
}

void GenomeCanvas::refreshAlignmentPreviews() {
    alignmentPreviews_.clear();

    const QStringList loci = visibleLoci(state_);
    for (const QString& locus : loci) {
        const ParsedInterval panelInterval = parseInterval(locus);
        if (!panelInterval.valid) {
            continue;
        }

        for (const TrackDescriptor& track : state_.tracks) {
            if (track.kind != "alignment" || track.source.trimmed().isEmpty()) {
                continue;
            }

            const qint64 visibilityWindowBp = static_cast<qint64>(track.visibilityWindowKb) * 1000;
            if (visibilityWindowBp > 0 && intervalSpanBp(panelInterval) > visibilityWindowBp) {
                continue;
            }

            alignmentPreviews_.insert(alignmentPreviewKey(track.source, locus),
                                      requestAlignmentPreview(track.source, locus));
        }
    }
}

void GenomeCanvas::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QLinearGradient background(rect().topLeft(), rect().bottomRight());
    background.setColorAt(0.0, QColor("#f8f3ea"));
    background.setColorAt(0.5, QColor("#eef3f1"));
    background.setColorAt(1.0, QColor("#e4edf1"));
    painter.fillRect(rect(), background);

    const QRectF frame = rect().adjusted(18.0, 18.0, -18.0, -18.0);
    painter.setPen(QPen(QColor(48, 69, 80, 28), 1.0));
    painter.setBrush(QColor(255, 255, 255, 215));
    painter.drawRoundedRect(frame, 24.0, 24.0);

    QFont titleFont = painter.font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor("#19303b"));
    painter.drawText(frame.adjusted(18.0, 16.0, -18.0, 0.0), "IGV Native Viewport");

    QFont bodyFont = painter.font();
    bodyFont.setPointSize(10);
    bodyFont.setBold(false);
    painter.setFont(bodyFont);
    painter.setPen(QColor("#5b6a73"));
    painter.drawText(frame.adjusted(18.0, 40.0, -18.0, 0.0),
                     QString("Genome %1  |  Tracks %2  |  ROI %3")
                         .arg(state_.genome.isEmpty() ? "Unspecified" : state_.genome)
                         .arg(state_.tracks.size())
                         .arg(state_.rois.size()));

    const QStringList loci = visibleLoci(state_);
    const int panelCount = std::max(1, static_cast<int>(loci.size()));
    const qreal panelGap = 12.0;
    const qreal totalGap = panelGap * (panelCount - 1);
    const qreal panelWidth = (frame.width() - 36.0 - totalGap) / panelCount;
    const qreal panelTop = frame.top() + 72.0;
    const qreal panelHeight = frame.height() - 92.0;

    for (int panelIndex = 0; panelIndex < panelCount; ++panelIndex) {
        const QString& panelLocus = loci.at(panelIndex);
        const ParsedInterval panelInterval = parseInterval(panelLocus);
        const QRectF panelRect(frame.left() + 18.0 + panelIndex * (panelWidth + panelGap),
                               panelTop,
                               panelWidth,
                               panelHeight);

        painter.setPen(QPen(QColor(27, 67, 86, 35), 1.0));
        painter.setBrush(QColor(250, 250, 248, 210));
        painter.drawRoundedRect(panelRect, 18.0, 18.0);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#19303b"));
        painter.drawRoundedRect(panelRect.adjusted(10.0, 10.0, -10.0, -panelRect.height() + 42.0), 12.0, 12.0);

        QFont locusFont = painter.font();
        locusFont.setBold(true);
        locusFont.setPointSize(10);
        painter.setFont(locusFont);
        painter.setPen(QColor("#f6efe5"));
        painter.drawText(panelRect.adjusted(18.0, 16.0, -18.0, 0.0), panelLocus);

        painter.setPen(QPen(QColor("#6a7d85"), 1.0));
        const qreal rulerY = panelRect.top() + 62.0;
        painter.drawLine(QPointF(panelRect.left() + 16.0, rulerY), QPointF(panelRect.right() - 16.0, rulerY));
        for (int tick = 0; tick <= 8; ++tick) {
            const qreal tickX = panelRect.left() + 16.0 + tick * ((panelRect.width() - 32.0) / 8.0);
            const qreal tickHeight = tick % 2 == 0 ? 9.0 : 5.0;
            painter.drawLine(QPointF(tickX, rulerY - tickHeight / 2.0), QPointF(tickX, rulerY + tickHeight / 2.0));
        }

        const QList<RoiOverlay> panelRois = roiOverlaysForPanel(panelLocus, state_.rois);
        if (!panelRois.isEmpty()) {
            const qreal overlayLeft = panelRect.left() + 16.0;
            const qreal overlayWidth = panelRect.width() - 32.0;
            const qreal overlayTop = panelRect.top() + 74.0;
            const qreal overlayHeight = panelRect.height() - 90.0;

            // Only draw positionally when both the panel and ROI map to genomic coordinates.
            for (const RoiOverlay& overlay : panelRois) {
                if (!overlay.positioned || !panelInterval.valid) {
                    continue;
                }

                const qreal roiLeft = projectCoordinate(overlay.interval.start, panelInterval, overlayLeft, overlayWidth);
                const qreal roiRight = projectCoordinate(overlay.interval.end, panelInterval, overlayLeft, overlayWidth);
                const QRectF roiRect(std::min(roiLeft, roiRight),
                                     overlayTop,
                                     std::max(24.0, std::abs(roiRight - roiLeft)),
                                     overlayHeight);

                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(180, 66, 42, 42));
                painter.drawRoundedRect(roiRect, 8.0, 8.0);

                const QString label = overlay.roi.label.isEmpty() ? overlay.roi.locus : overlay.roi.label;
                painter.setPen(QColor("#8c3f2d"));
                painter.drawText(roiRect.adjusted(6.0, 8.0, -6.0, -8.0),
                                 Qt::AlignTop | Qt::TextWordWrap,
                                 label);
            }

            if (!panelInterval.valid) {
                drawRoiBadges(painter, panelRect, panelRois);
            }
        }

        const int trackCount = std::max(1, static_cast<int>(state_.tracks.size()));
        const qreal laneHeight = std::clamp((panelRect.height() - 108.0) / trackCount - 6.0, 26.0, 48.0);
        qreal laneTop = panelRect.top() + 82.0;

        for (int trackIndex = 0; trackIndex < state_.tracks.size(); ++trackIndex) {
            const TrackDescriptor& track = state_.tracks.at(trackIndex);
            const QRectF lane(panelRect.left() + 14.0, laneTop, panelRect.width() - 28.0, laneHeight);
            const QString previewKey = alignmentPreviewKey(track.source, panelLocus);
            QJsonObject previewObject = alignmentPreviews_.value(previewKey);
            const QJsonObject* preview = previewObject.isEmpty() ? nullptr : &previewObject;

            painter.setPen(Qt::NoPen);
            painter.setBrush(track.color);
            painter.drawRoundedRect(lane, 11.0, 11.0);

            drawTrackGlyphs(painter, lane, track, panelInterval, preview);

            painter.setPen(QColor("#10262f"));
            painter.drawText(lane.adjusted(12.0, 6.0, -110.0, -6.0),
                             Qt::AlignVCenter | Qt::TextSingleLine,
                             track.name);

            painter.setPen(QColor("#eef2f2"));
            painter.drawText(lane.adjusted(10.0, 6.0, -12.0, -6.0),
                             Qt::AlignRight | Qt::AlignVCenter,
                             trackModeLabel(track));

            laneTop += laneHeight + 8.0;
        }
    }
}
