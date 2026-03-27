#pragma once

#include <QWidget>

#include "SessionTypes.h"

class GenomeCanvas : public QWidget {
public:
    explicit GenomeCanvas(QWidget* parent = nullptr);

    void setSessionState(const SessionState& state);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    SessionState state_;
};
