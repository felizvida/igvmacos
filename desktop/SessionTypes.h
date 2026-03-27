#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

struct TrackDescriptor {
    QString name;
    QString kind;
    QString source;
    QString visibility;
    QColor color;
};

struct RoiDescriptor {
    QString locus;
    QString label;
};

struct SessionState {
    QString schema;
    QString genome;
    QString locus;
    QStringList genomes;
    QStringList loci;
    QList<RoiDescriptor> rois;
    QList<TrackDescriptor> tracks;
    bool multiLocus = false;
};
