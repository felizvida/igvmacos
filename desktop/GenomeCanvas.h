#pragma once

#include <QHash>
#include <QJsonObject>
#include <QWidget>

#include "SessionTypes.h"

class GenomeCanvas : public QWidget {
public:
    explicit GenomeCanvas(QWidget* parent = nullptr);

    void setSessionState(const SessionState& state);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void refreshAlignmentPreviews();

    SessionState state_;
    QHash<QString, QJsonObject> alignmentPreviews_;
};
