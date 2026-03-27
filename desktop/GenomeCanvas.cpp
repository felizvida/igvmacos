#include "GenomeCanvas.h"

#include <algorithm>

#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace {

QStringList visibleLoci(const SessionState& state) {
    if (state.multiLocus && !state.loci.isEmpty()) {
        return state.loci;
    }

    if (!state.locus.trimmed().isEmpty()) {
        return {state.locus};
    }

    return {"All"};
}

void drawTrackGlyphs(QPainter& painter, const QRectF& lane, const TrackDescriptor& track) {
    painter.save();
    painter.setClipRect(lane.adjusted(4.0, 3.0, -4.0, -3.0));

    if (track.kind == "alignment") {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 170));
        const qreal readWidth = lane.width() / 10.0;
        for (int i = 0; i < 7; ++i) {
            const qreal x = lane.left() + 10.0 + (i * readWidth * 0.95);
            const QRectF readRect(x, lane.center().y() - 5.0, readWidth * 0.62, 10.0);
            painter.drawRoundedRect(readRect, 3.0, 3.0);
        }
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

}  // namespace

GenomeCanvas::GenomeCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(780, 360);
}

void GenomeCanvas::setSessionState(const SessionState& state) {
    state_ = state;
    update();
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
    const int panelCount = std::max(1, loci.size());
    const qreal panelGap = 12.0;
    const qreal totalGap = panelGap * (panelCount - 1);
    const qreal panelWidth = (frame.width() - 36.0 - totalGap) / panelCount;
    const qreal panelTop = frame.top() + 72.0;
    const qreal panelHeight = frame.height() - 92.0;

    for (int panelIndex = 0; panelIndex < panelCount; ++panelIndex) {
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
        painter.drawText(panelRect.adjusted(18.0, 16.0, -18.0, 0.0), loci.at(panelIndex));

        painter.setPen(QPen(QColor("#6a7d85"), 1.0));
        const qreal rulerY = panelRect.top() + 62.0;
        painter.drawLine(QPointF(panelRect.left() + 16.0, rulerY), QPointF(panelRect.right() - 16.0, rulerY));
        for (int tick = 0; tick <= 8; ++tick) {
            const qreal tickX = panelRect.left() + 16.0 + tick * ((panelRect.width() - 32.0) / 8.0);
            const qreal tickHeight = tick % 2 == 0 ? 9.0 : 5.0;
            painter.drawLine(QPointF(tickX, rulerY - tickHeight / 2.0), QPointF(tickX, rulerY + tickHeight / 2.0));
        }

        if (!state_.rois.isEmpty()) {
            const qreal roiX = panelRect.left() + panelRect.width() * 0.35;
            const QRectF roiRect(roiX, panelRect.top() + 74.0, std::max(24.0, panelRect.width() * 0.12), panelRect.height() - 90.0);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(180, 66, 42, 42));
            painter.drawRoundedRect(roiRect, 8.0, 8.0);

            painter.setPen(QColor("#8c3f2d"));
            painter.drawText(roiRect.adjusted(6.0, 8.0, -6.0, -8.0), Qt::AlignTop | Qt::TextWordWrap, state_.rois.first().label);
        }

        const int trackCount = std::max(1, state_.tracks.size());
        const qreal laneHeight = std::clamp((panelRect.height() - 108.0) / trackCount - 6.0, 26.0, 48.0);
        qreal laneTop = panelRect.top() + 82.0;

        for (int trackIndex = 0; trackIndex < state_.tracks.size(); ++trackIndex) {
            const TrackDescriptor& track = state_.tracks.at(trackIndex);
            const QRectF lane(panelRect.left() + 14.0, laneTop, panelRect.width() - 28.0, laneHeight);

            painter.setPen(Qt::NoPen);
            painter.setBrush(track.color);
            painter.drawRoundedRect(lane, 11.0, 11.0);

            drawTrackGlyphs(painter, lane, track);

            painter.setPen(QColor("#10262f"));
            painter.drawText(lane.adjusted(12.0, 6.0, -110.0, -6.0),
                             Qt::AlignVCenter | Qt::TextSingleLine,
                             track.name);

            painter.setPen(QColor("#eef2f2"));
            painter.drawText(lane.adjusted(10.0, 6.0, -12.0, -6.0),
                             Qt::AlignRight | Qt::AlignVCenter,
                             track.visibility);

            laneTop += laneHeight + 8.0;
        }
    }
}
