#pragma once

#include <QJsonArray>
#include <QMainWindow>

#include "SessionTypes.h"

class QAction;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;
class QTextBrowser;
class QTreeWidget;

class GenomeCanvas;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildMenus();
    void buildToolbar();
    void buildCentralWidget();
    void buildDockWidgets();
    void loadRustContent();
    void loadSessionFromObject(const QJsonObject& object);
    QJsonObject sessionToObject() const;
    void syncUiFromSession();
    void refreshSummary();
    void refreshTrackTable();
    void refreshFeatureTree();
    void refreshRoadmapTable();
    void appendActivity(const QString& message);
    void loadTrackDescriptor(const QString& source, bool fromUrl);
    QString inferTrackKind(const QString& source) const;
    QColor colorForKind(const QString& kind) const;
    QStringList parseLoci(const QString& text) const;
    void applyLocusText();
    void toggleMultiLocus(bool enabled);
    void changeGenome(const QString& genome);
    void addRegionOfInterest();
    void saveSession();
    void openSession();
    void loadTracksFromFiles();
    void loadTrackFromUrl();
    void exportSnapshot();
    void revealFeatureInventory();
    void showAboutDialog();

    SessionState session_;
    QJsonArray featureCatalog_;
    QJsonArray roadmap_;
    QString sourceLinksMarkdown_;
    QString designNotesMarkdown_;

    QAction* multiLocusAction_ = nullptr;
    QComboBox* genomeCombo_ = nullptr;
    QLineEdit* locusEdit_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    GenomeCanvas* canvas_ = nullptr;
    QTableWidget* trackTable_ = nullptr;
    QTreeWidget* featureTree_ = nullptr;
    QTableWidget* roadmapTable_ = nullptr;
    QTextBrowser* sourcesBrowser_ = nullptr;
    QTextBrowser* notesBrowser_ = nullptr;
    QPlainTextEdit* activityLog_ = nullptr;
    QDockWidget* featureDock_ = nullptr;
};
