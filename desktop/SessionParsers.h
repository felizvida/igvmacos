#pragma once

#include <QColor>
#include <QMap>
#include <QJsonObject>
#include <QStringList>

#include "SessionTypes.h"

namespace session_parsers {

SessionState loadNativeSession(const QJsonObject& object);
bool loadIgvXmlSession(const QString& fileName, SessionState* out, QString* errorMessage);
bool loadCaseManifest(const QString& fileName, WorkspaceState* out, QString* errorMessage);
bool loadCaseFolder(const QString& directoryPath, WorkspaceState* out, QString* errorMessage);
void refreshWorkspaceReadiness(WorkspaceState* workspace);
QList<TrackDescriptor> tracksForPreset(const WorkspaceState& workspace, const QString& presetName);
QString buildReviewPacketMarkdown(const WorkspaceState& workspace, const QString& presetName);
QString buildReviewPacketHtml(const WorkspaceState& workspace,
                              const QString& presetName,
                              const QString& overviewSnapshotFile,
                              const QMap<QString, QString>& reviewSnapshotFiles);
QStringList parseLociText(const QString& text);
TrackDescriptor describeTrackSource(const QString& source, const QString& name = QString());
QString inferTrackKind(const QString& source);
QColor colorForTrackKind(const QString& kind);

}  // namespace session_parsers
