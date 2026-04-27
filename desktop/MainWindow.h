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
    void clearWorkspaceContext();
    void syncWorkspaceState();
    void loadRustContent();
    void loadSessionFromObject(const QJsonObject& object);
    bool loadSessionFromIgvXml(const QString& fileName, QString* errorMessage);
    bool loadCaseFolderPath(const QString& directoryPath, QString* errorMessage);
    bool loadCaseManifestFile(const QString& fileName, QString* errorMessage);
    QJsonObject sessionToObject() const;
    void refreshPresetSelector();
    void syncUiFromSession();
    void refreshSummary();
    void refreshTrackTable();
    void refreshReadinessTable();
    void refreshSampleInfoTable();
    void refreshReviewQueueTable();
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
    void addCurrentLocusToReviewQueue();
    int findReviewRow(int startRow, int step, bool pendingOnly) const;
    void navigateToReviewRow(int row);
    void jumpToPreviousReviewItem();
    void jumpToNextReviewItem();
    void jumpToNextPendingReviewItem();
    void jumpToSelectedReviewItem();
    void markSelectedReviewItem(const QString& status);
    void editSelectedReviewNote();
    void saveSession();
    void applyTrackPreset(const QString& presetName);
    void openCaseFolder();
    void openCaseManifest();
    void openSession();
    void loadTracksFromFiles();
    void loadTrackFromUrl();
    void exportSnapshot();
    void exportReviewPacket();
    void exportHtmlReviewReport();
    void revealFeatureInventory();
    void showAboutDialog();

    SessionState session_;
    WorkspaceState workspace_;
    QJsonArray featureCatalog_;
    QJsonArray roadmap_;
    QString sourceLinksMarkdown_;
    QString designNotesMarkdown_;

    QAction* multiLocusAction_ = nullptr;
    QAction* autoAdvanceReviewAction_ = nullptr;
    QComboBox* genomeCombo_ = nullptr;
    QComboBox* presetCombo_ = nullptr;
    QLineEdit* locusEdit_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    GenomeCanvas* canvas_ = nullptr;
    QTableWidget* trackTable_ = nullptr;
    QTableWidget* readinessTable_ = nullptr;
    QTableWidget* sampleInfoTable_ = nullptr;
    QTableWidget* reviewQueueTable_ = nullptr;
    QTreeWidget* featureTree_ = nullptr;
    QTableWidget* roadmapTable_ = nullptr;
    QTextBrowser* sourcesBrowser_ = nullptr;
    QTextBrowser* notesBrowser_ = nullptr;
    QPlainTextEdit* activityLog_ = nullptr;
    QDockWidget* featureDock_ = nullptr;
    QDockWidget* readinessDock_ = nullptr;
    QDockWidget* sampleInfoDock_ = nullptr;
    QDockWidget* reviewDock_ = nullptr;
    QString activePresetName_;
};
