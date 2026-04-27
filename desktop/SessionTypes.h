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
    bool requiresIndex = false;
    QString indexSource;
    QString expectedGenome;
    QString group;
    QString sampleId;
    QString experimentType;
    int visibilityWindowKb = 0;
    bool showCoverageTrack = false;
    bool showAlignmentTrack = false;
    bool showSpliceJunctionTrack = false;
    bool alignmentSettingsInitialized = false;
};

struct RoiDescriptor {
    QString locus;
    QString label;
};

struct ReviewItem {
    QString locus;
    QString label;
    QString status;
    QString note;
};

struct SessionState {
    QString schema;
    QString genome;
    QString locus;
    QStringList genomes;
    QStringList loci;
    QList<RoiDescriptor> rois;
    QList<ReviewItem> reviewQueue;
    QList<TrackDescriptor> tracks;
    bool multiLocus = false;
};

struct ReadinessIssue {
    QString severity;
    QString check;
    QString detail;
};

struct SampleInfoTable {
    QStringList headers;
    QList<QStringList> rows;
};

struct CohortPreset {
    QString name;
    QString description;
    QStringList groups;
    QStringList sampleIds;
    bool matchedOnly = false;
};

struct WorkspaceState {
    QString schema;
    QString title;
    QString description;
    QString manifestPath;
    QString workspaceRoot;
    QString sessionSource;
    QStringList sampleInfoFiles;
    SampleInfoTable sampleInfo;
    QList<CohortPreset> cohortPresets;
    QList<ReadinessIssue> importIssues;
    QList<ReadinessIssue> readinessIssues;
    SessionState session;
};
