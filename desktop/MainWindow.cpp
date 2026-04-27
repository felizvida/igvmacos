#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMap>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "GenomeCanvas.h"
#include "RustBridge.h"
#include "SessionParsers.h"

namespace {

QJsonDocument parseRustJson(const char* bytes) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(bytes), &error);
    if (error.error != QJsonParseError::NoError) {
        return {};
    }
    return document;
}

QString parseRustMarkdown(const char* bytes) {
    return QString::fromUtf8(bytes);
}

QString fileLabelForSource(const QString& source) {
    const QUrl url(source);
    if (url.isValid() && !url.scheme().isEmpty()) {
        const QString path = url.path();
        return QFileInfo(path).fileName().isEmpty() ? source : QFileInfo(path).fileName();
    }

    const QFileInfo fileInfo(source);
    return fileInfo.fileName().isEmpty() ? source : fileInfo.fileName();
}

QString sanitizeFileStem(const QString& value) {
    QString sanitized;
    sanitized.reserve(value.size());
    for (const QChar character : value.toLower()) {
        if (character.isLetterOrNumber()) {
            sanitized.append(character);
        } else if (!sanitized.isEmpty() && !sanitized.endsWith('-')) {
            sanitized.append('-');
        }
    }

    while (sanitized.endsWith('-')) {
        sanitized.chop(1);
    }
    if (sanitized.isEmpty()) {
        return "review-item";
    }
    return sanitized.left(48);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("IGV Native Desktop");
    resize(1500, 940);
    setUnifiedTitleAndToolBarOnMac(true);

    loadRustContent();
    buildCentralWidget();
    buildDockWidgets();
    buildMenus();
    buildToolbar();
    syncUiFromSession();

    statusBar()->showMessage("Native macOS prototype ready");
    appendActivity("Loaded prototype session from the Rust core.");
}

void MainWindow::clearWorkspaceContext() {
    workspace_ = WorkspaceState();
    activePresetName_.clear();
}

void MainWindow::syncWorkspaceState() {
    if (workspace_.schema.isEmpty()) {
        return;
    }

    workspace_.session = session_;
    session_parsers::refreshWorkspaceReadiness(&workspace_);
}

void MainWindow::loadRustContent() {
    const QJsonDocument featureDocument = parseRustJson(igv_feature_inventory_json());
    const QJsonDocument roadmapDocument = parseRustJson(igv_roadmap_json());
    const QJsonDocument sessionDocument = parseRustJson(igv_demo_session_json());

    featureCatalog_ = featureDocument.object().value("features").toArray();
    roadmap_ = roadmapDocument.object().value("milestones").toArray();
    sourceLinksMarkdown_ = parseRustMarkdown(igv_source_links_markdown());
    designNotesMarkdown_ = parseRustMarkdown(igv_design_notes_markdown());

    loadSessionFromObject(sessionDocument.object());
}

void MainWindow::loadSessionFromObject(const QJsonObject& object) {
    clearWorkspaceContext();
    session_ = session_parsers::loadNativeSession(object);
}

bool MainWindow::loadSessionFromIgvXml(const QString& fileName, QString* errorMessage) {
    SessionState imported;
    if (!session_parsers::loadIgvXmlSession(fileName, &imported, errorMessage)) {
        return false;
    }

    clearWorkspaceContext();
    session_ = imported;
    return true;
}

bool MainWindow::loadCaseFolderPath(const QString& directoryPath, QString* errorMessage) {
    WorkspaceState workspace;
    if (!session_parsers::loadCaseFolder(directoryPath, &workspace, errorMessage)) {
        return false;
    }

    workspace_ = workspace;
    session_ = workspace_.session;
    return true;
}

bool MainWindow::loadCaseManifestFile(const QString& fileName, QString* errorMessage) {
    WorkspaceState workspace;
    if (!session_parsers::loadCaseManifest(fileName, &workspace, errorMessage)) {
        return false;
    }

    workspace_ = workspace;
    session_ = workspace_.session;
    return true;
}

QJsonObject MainWindow::sessionToObject() const {
    QJsonArray genomes;
    for (const QString& genome : session_.genomes) {
        genomes.append(genome);
    }

    QJsonArray loci;
    for (const QString& locus : session_.loci) {
        loci.append(locus);
    }

    QJsonArray rois;
    for (const RoiDescriptor& roi : session_.rois) {
        QJsonObject roiObject;
        roiObject.insert("locus", roi.locus);
        roiObject.insert("label", roi.label);
        rois.append(roiObject);
    }

    QJsonArray reviewQueue;
    for (const ReviewItem& item : session_.reviewQueue) {
        QJsonObject itemObject;
        itemObject.insert("locus", item.locus);
        itemObject.insert("label", item.label);
        itemObject.insert("status", item.status);
        itemObject.insert("note", item.note);
        reviewQueue.append(itemObject);
    }

    QJsonArray tracks;
    for (const TrackDescriptor& track : session_.tracks) {
        QJsonObject trackObject;
        trackObject.insert("name", track.name);
        trackObject.insert("kind", track.kind);
        trackObject.insert("source", track.source);
        trackObject.insert("visibility", track.visibility);
        trackObject.insert("requires_index", track.requiresIndex);
        if (!track.indexSource.isEmpty()) {
            trackObject.insert("index_source", track.indexSource);
        }
        if (!track.expectedGenome.isEmpty()) {
            trackObject.insert("expected_genome", track.expectedGenome);
        }
        if (!track.group.isEmpty()) {
            trackObject.insert("group", track.group);
        }
        if (!track.sampleId.isEmpty()) {
            trackObject.insert("sample_id", track.sampleId);
        }
        if (track.kind == "alignment") {
            if (!track.experimentType.isEmpty()) {
                trackObject.insert("experiment_type", track.experimentType);
            }
            if (track.visibilityWindowKb > 0) {
                trackObject.insert("visibility_window_kb", track.visibilityWindowKb);
            }
            trackObject.insert("show_coverage", track.showCoverageTrack);
            trackObject.insert("show_alignments", track.showAlignmentTrack);
            trackObject.insert("show_splice_junctions", track.showSpliceJunctionTrack);
        }
        tracks.append(trackObject);
    }

    QJsonObject root;
    root.insert("schema", session_.schema);
    root.insert("genome", session_.genome);
    root.insert("locus", session_.locus);
    root.insert("multi_locus", session_.multiLocus);
    root.insert("genomes", genomes);
    root.insert("loci", loci);
    root.insert("rois", rois);
    root.insert("review_queue", reviewQueue);
    root.insert("tracks", tracks);
    return root;
}

void MainWindow::buildCentralWidget() {
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    auto* summaryCard = new QFrame(container);
    summaryCard->setObjectName("summaryCard");
    auto* summaryLayout = new QVBoxLayout(summaryCard);
    summaryLayout->setContentsMargins(18, 14, 18, 14);
    summaryLayout->setSpacing(6);

    auto* titleLabel = new QLabel("Native rewrite workbench", summaryCard);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: 700; color: #16303a;");
    summaryLabel_ = new QLabel(summaryCard);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setStyleSheet("color: #52626c;");
    summaryLayout->addWidget(titleLabel);
    summaryLayout->addWidget(summaryLabel_);

    canvas_ = new GenomeCanvas(container);

    auto* lowerTabs = new QTabWidget(container);
    sourcesBrowser_ = new QTextBrowser(lowerTabs);
    notesBrowser_ = new QTextBrowser(lowerTabs);
    activityLog_ = new QPlainTextEdit(lowerTabs);
    activityLog_->setReadOnly(true);

    sourcesBrowser_->setOpenExternalLinks(true);
    sourcesBrowser_->setMarkdown(sourceLinksMarkdown_);
    notesBrowser_->setMarkdown(designNotesMarkdown_);

    lowerTabs->addTab(activityLog_, "Activity");
    lowerTabs->addTab(sourcesBrowser_, "Sources");
    lowerTabs->addTab(notesBrowser_, "Rewrite Notes");

    layout->addWidget(summaryCard);
    layout->addWidget(canvas_, 1);
    layout->addWidget(lowerTabs, 1);

    setCentralWidget(container);
}

void MainWindow::buildDockWidgets() {
    auto* tracksDock = new QDockWidget("Tracks", this);
    tracksDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    trackTable_ = new QTableWidget(0, 5, tracksDock);
    trackTable_->setHorizontalHeaderLabels({"Track", "Kind", "Group", "Sample", "Source"});
    trackTable_->horizontalHeader()->setStretchLastSection(true);
    trackTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    trackTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    trackTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    trackTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    trackTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    trackTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tracksDock->setWidget(trackTable_);
    addDockWidget(Qt::LeftDockWidgetArea, tracksDock);

    readinessDock_ = new QDockWidget("Readiness", this);
    readinessDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    readinessTable_ = new QTableWidget(0, 3, readinessDock_);
    readinessTable_->setHorizontalHeaderLabels({"Severity", "Check", "Detail"});
    readinessTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    readinessTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    readinessTable_->horizontalHeader()->setStretchLastSection(true);
    readinessTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    readinessTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    readinessDock_->setWidget(readinessTable_);
    addDockWidget(Qt::LeftDockWidgetArea, readinessDock_);
    tabifyDockWidget(tracksDock, readinessDock_);

    sampleInfoDock_ = new QDockWidget("Samples", this);
    sampleInfoDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    sampleInfoTable_ = new QTableWidget(0, 0, sampleInfoDock_);
    sampleInfoTable_->horizontalHeader()->setStretchLastSection(true);
    sampleInfoTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    sampleInfoTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sampleInfoDock_->setWidget(sampleInfoTable_);
    addDockWidget(Qt::LeftDockWidgetArea, sampleInfoDock_);
    tabifyDockWidget(readinessDock_, sampleInfoDock_);

    reviewDock_ = new QDockWidget("Review Queue", this);
    reviewDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    reviewQueueTable_ = new QTableWidget(0, 4, reviewDock_);
    reviewQueueTable_->setHorizontalHeaderLabels({"Locus", "Label", "Status", "Note"});
    reviewQueueTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    reviewQueueTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    reviewQueueTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    reviewQueueTable_->horizontalHeader()->setStretchLastSection(true);
    reviewQueueTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    reviewQueueTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    reviewDock_->setWidget(reviewQueueTable_);
    addDockWidget(Qt::RightDockWidgetArea, reviewDock_);

    featureDock_ = new QDockWidget("Product Map", this);
    featureDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* productTabs = new QTabWidget(featureDock_);
    featureTree_ = new QTreeWidget(productTabs);
    featureTree_->setColumnCount(4);
    featureTree_->setHeaderLabels({"Feature", "Phase", "Status", "Notes"});
    featureTree_->header()->setStretchLastSection(true);
    featureTree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    featureTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    featureTree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    roadmapTable_ = new QTableWidget(0, 3, productTabs);
    roadmapTable_->setHorizontalHeaderLabels({"Phase", "Objective", "Deliverables"});
    roadmapTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    roadmapTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    roadmapTable_->horizontalHeader()->setStretchLastSection(true);
    roadmapTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    roadmapTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    productTabs->addTab(featureTree_, "Features");
    productTabs->addTab(roadmapTable_, "Roadmap");
    featureDock_->setWidget(productTabs);
    addDockWidget(Qt::RightDockWidgetArea, featureDock_);

    connect(featureTree_, &QTreeWidget::itemSelectionChanged, this, [this]() {
        const QList<QTreeWidgetItem*> selected = featureTree_->selectedItems();
        if (!selected.isEmpty()) {
            statusBar()->showMessage(selected.first()->text(3));
        }
    });

    connect(reviewQueueTable_, &QTableWidget::itemDoubleClicked, this, [this]() {
        jumpToSelectedReviewItem();
    });
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("Open Case Folder...", this, &MainWindow::openCaseFolder);
    fileMenu->addAction("Open Case Manifest...", this, &MainWindow::openCaseManifest);
    fileMenu->addSeparator();
    fileMenu->addAction("Load Track(s)...", this, &MainWindow::loadTracksFromFiles, QKeySequence::Open);
    fileMenu->addAction("Load from URL...", this, &MainWindow::loadTrackFromUrl);
    fileMenu->addSeparator();
    fileMenu->addAction("Open Session...", this, &MainWindow::openSession);
    fileMenu->addAction("Save Session...", this, &MainWindow::saveSession, QKeySequence::Save);
    fileMenu->addSeparator();
    fileMenu->addAction("Export Snapshot...", this, &MainWindow::exportSnapshot);
    fileMenu->addAction("Export Review Packet...", this, &MainWindow::exportReviewPacket);
    fileMenu->addAction("Export HTML Review Report...", this, &MainWindow::exportHtmlReviewReport);
    fileMenu->addSeparator();
    fileMenu->addAction("Quit", qApp, &QApplication::quit, QKeySequence::Quit);

    auto* genomeMenu = menuBar()->addMenu("&Genome");
    genomeMenu->addAction("GRCh38/hg38", this, [this]() { changeGenome("GRCh38/hg38"); });
    genomeMenu->addAction("T2T-CHM13v2", this, [this]() { changeGenome("T2T-CHM13v2"); });
    genomeMenu->addAction("GRCm39/mm39", this, [this]() { changeGenome("GRCm39/mm39"); });

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Apply Locus", this, &MainWindow::applyLocusText);
    multiLocusAction_ = viewMenu->addAction("Multi-Locus Preview");
    multiLocusAction_->setCheckable(true);
    connect(multiLocusAction_, &QAction::toggled, this, &MainWindow::toggleMultiLocus);
    viewMenu->addAction("Reveal Feature Inventory", this, &MainWindow::revealFeatureInventory);

    auto* regionsMenu = menuBar()->addMenu("&Regions");
    regionsMenu->addAction("Add ROI from Current Locus", this, &MainWindow::addRegionOfInterest);

    auto* reviewMenu = menuBar()->addMenu("&Review");
    reviewMenu->addAction("Add Current Locus to Queue", this, &MainWindow::addCurrentLocusToReviewQueue);
    reviewMenu->addAction("Previous Item", this, &MainWindow::jumpToPreviousReviewItem);
    reviewMenu->addAction("Next Item", this, &MainWindow::jumpToNextReviewItem);
    reviewMenu->addAction("Next Pending Item", this, &MainWindow::jumpToNextPendingReviewItem);
    reviewMenu->addSeparator();
    autoAdvanceReviewAction_ = reviewMenu->addAction("Auto-Advance On Review");
    autoAdvanceReviewAction_->setCheckable(true);
    autoAdvanceReviewAction_->setChecked(true);
    reviewMenu->addAction("Jump to Selected Item", this, &MainWindow::jumpToSelectedReviewItem);
    reviewMenu->addAction("Mark Reviewed", this, [this]() { markSelectedReviewItem("reviewed"); });
    reviewMenu->addAction("Mark Needs Follow-up", this, [this]() { markSelectedReviewItem("follow-up"); });
    reviewMenu->addAction("Edit Note...", this, &MainWindow::editSelectedReviewNote);

    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("About Native Rewrite", this, &MainWindow::showAboutDialog);
}

void MainWindow::buildToolbar() {
    auto* toolbar = addToolBar("Primary");
    toolbar->setMovable(false);

    toolbar->addWidget(new QLabel("Genome", toolbar));
    genomeCombo_ = new QComboBox(toolbar);
    genomeCombo_->setMinimumContentsLength(14);
    toolbar->addWidget(genomeCombo_);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Preset", toolbar));
    presetCombo_ = new QComboBox(toolbar);
    presetCombo_->setMinimumContentsLength(14);
    presetCombo_->setEnabled(false);
    toolbar->addWidget(presetCombo_);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Locus", toolbar));
    locusEdit_ = new QLineEdit(toolbar);
    locusEdit_->setMinimumWidth(320);
    toolbar->addWidget(locusEdit_);

    auto* applyAction = toolbar->addAction("Go");
    auto* snapshotAction = toolbar->addAction("Snapshot");
    toolbar->addAction(multiLocusAction_);

    connect(genomeCombo_, &QComboBox::currentTextChanged, this, &MainWindow::changeGenome);
    connect(presetCombo_, &QComboBox::currentTextChanged, this, &MainWindow::applyTrackPreset);
    connect(locusEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyLocusText);
    connect(applyAction, &QAction::triggered, this, &MainWindow::applyLocusText);
    connect(snapshotAction, &QAction::triggered, this, &MainWindow::exportSnapshot);
}

void MainWindow::refreshPresetSelector() {
    if (presetCombo_ == nullptr) {
        return;
    }

    presetCombo_->blockSignals(true);
    presetCombo_->clear();

    if (workspace_.cohortPresets.isEmpty()) {
        presetCombo_->addItem("All Tracks");
        presetCombo_->setEnabled(false);
        activePresetName_.clear();
        presetCombo_->blockSignals(false);
        return;
    }

    for (const CohortPreset& preset : workspace_.cohortPresets) {
        presetCombo_->addItem(preset.name);
    }

    if (activePresetName_.isEmpty()) {
        activePresetName_ = workspace_.cohortPresets.first().name;
    }

    int activeIndex = presetCombo_->findText(activePresetName_);
    if (activeIndex < 0) {
        activePresetName_ = workspace_.cohortPresets.first().name;
        activeIndex = 0;
    }
    presetCombo_->setCurrentIndex(activeIndex);
    presetCombo_->setEnabled(presetCombo_->count() > 1);
    presetCombo_->blockSignals(false);
}

void MainWindow::syncUiFromSession() {
    if (genomeCombo_ != nullptr) {
        genomeCombo_->blockSignals(true);
        genomeCombo_->clear();
        genomeCombo_->addItems(session_.genomes);
        genomeCombo_->setCurrentText(session_.genome);
        genomeCombo_->blockSignals(false);
    }

    if (locusEdit_ != nullptr) {
        const QString locusText = session_.multiLocus ? session_.loci.join(" ") : session_.locus;
        locusEdit_->setText(locusText);
    }

    if (multiLocusAction_ != nullptr) {
        multiLocusAction_->blockSignals(true);
        multiLocusAction_->setChecked(session_.multiLocus);
        multiLocusAction_->blockSignals(false);
    }

    if (canvas_ != nullptr) {
        canvas_->setSessionState(session_);
    }

    refreshPresetSelector();
    refreshTrackTable();
    refreshReadinessTable();
    refreshSampleInfoTable();
    refreshReviewQueueTable();
    refreshFeatureTree();
    refreshRoadmapTable();
    refreshSummary();
}

void MainWindow::refreshSummary() {
    if (summaryLabel_ == nullptr) {
        return;
    }

    const QString lociSummary = session_.multiLocus ? session_.loci.join(" | ") : session_.locus;
    int errorCount = 0;
    int warningCount = 0;
    for (const ReadinessIssue& issue : workspace_.readinessIssues) {
        if (issue.severity == "error") {
            ++errorCount;
        } else if (issue.severity == "warning") {
            ++warningCount;
        }
    }

    QString workspaceSummary = "Ad hoc session";
    if (!workspace_.title.isEmpty()) {
        workspaceSummary = workspace_.title;
    }

    QString readinessSummary = "No workspace loaded";
    if (!workspace_.schema.isEmpty()) {
        readinessSummary = QString("%1 error(s), %2 warning(s)").arg(errorCount).arg(warningCount);
    }

    int matchedTracks = 0;
    for (const TrackDescriptor& track : session_.tracks) {
        if (!track.sampleId.isEmpty()) {
            ++matchedTracks;
        }
    }
    int reviewedCount = 0;
    for (const ReviewItem& item : session_.reviewQueue) {
        if (item.status == "reviewed") {
            ++reviewedCount;
        }
    }

    const QString presetSummary = activePresetName_.isEmpty() ? "All Tracks" : activePresetName_;
    const int sampleCount = workspace_.sampleInfo.rows.size();
    summaryLabel_->setText(
        QString("Workspace: %1\nGenome: %2\nLocus: %3\nTracks: %4  |  Regions of interest: %5  |  Sample rows: %6  |  Matched tracks: %7\nReview queue: %8 item(s), %9 reviewed\nSession schema: %10  |  Readiness: %11  |  Preset: %12")
            .arg(workspaceSummary)
            .arg(session_.genome)
            .arg(lociSummary)
            .arg(session_.tracks.size())
            .arg(session_.rois.size())
            .arg(sampleCount)
            .arg(matchedTracks)
            .arg(session_.reviewQueue.size())
            .arg(reviewedCount)
            .arg(session_.schema)
            .arg(readinessSummary)
            .arg(presetSummary));
}

void MainWindow::refreshTrackTable() {
    if (trackTable_ == nullptr) {
        return;
    }

    WorkspaceState visibleWorkspace = workspace_;
    visibleWorkspace.session = session_;
    QList<TrackDescriptor> visibleTracks = session_parsers::tracksForPreset(visibleWorkspace, activePresetName_);

    trackTable_->setRowCount(visibleTracks.size());
    for (int row = 0; row < visibleTracks.size(); ++row) {
        const TrackDescriptor& track = visibleTracks.at(row);
        auto* nameItem = new QTableWidgetItem(track.name);
        nameItem->setBackground(track.color);
        auto* kindItem = new QTableWidgetItem(track.kind);
        auto* groupItem = new QTableWidgetItem(track.group.isEmpty() ? "-" : track.group);
        auto* sampleItem = new QTableWidgetItem(track.sampleId.isEmpty() ? "-" : track.sampleId);
        auto* sourceItem = new QTableWidgetItem(track.source);
        trackTable_->setItem(row, 0, nameItem);
        trackTable_->setItem(row, 1, kindItem);
        trackTable_->setItem(row, 2, groupItem);
        trackTable_->setItem(row, 3, sampleItem);
        trackTable_->setItem(row, 4, sourceItem);
    }
}

void MainWindow::refreshReadinessTable() {
    if (readinessTable_ == nullptr) {
        return;
    }

    if (workspace_.schema.isEmpty()) {
        readinessTable_->setRowCount(1);
        auto* severityItem = new QTableWidgetItem("info");
        severityItem->setBackground(QColor("#d9e5eb"));
        readinessTable_->setItem(0, 0, severityItem);
        readinessTable_->setItem(0, 1, new QTableWidgetItem("Workspace not loaded"));
        readinessTable_->setItem(0, 2, new QTableWidgetItem("Open a case folder or manifest to validate tracks, indexes, and sample metadata."));
        return;
    }

    if (workspace_.readinessIssues.isEmpty()) {
        readinessTable_->setRowCount(1);
        auto* severityItem = new QTableWidgetItem("ok");
        severityItem->setBackground(QColor("#d8ead5"));
        readinessTable_->setItem(0, 0, severityItem);
        readinessTable_->setItem(0, 1, new QTableWidgetItem("Workspace ready"));
        readinessTable_->setItem(0, 2, new QTableWidgetItem("No local readiness issues were detected for this workspace."));
        return;
    }

    readinessTable_->setRowCount(workspace_.readinessIssues.size());
    for (int row = 0; row < workspace_.readinessIssues.size(); ++row) {
        const ReadinessIssue& issue = workspace_.readinessIssues.at(row);
        auto* severityItem = new QTableWidgetItem(issue.severity);
        if (issue.severity == "error") {
            severityItem->setBackground(QColor("#f5d0d0"));
        } else if (issue.severity == "warning") {
            severityItem->setBackground(QColor("#f6e6c8"));
        } else {
            severityItem->setBackground(QColor("#d9e5eb"));
        }
        readinessTable_->setItem(row, 0, severityItem);
        readinessTable_->setItem(row, 1, new QTableWidgetItem(issue.check));
        readinessTable_->setItem(row, 2, new QTableWidgetItem(issue.detail));
    }
}

void MainWindow::refreshSampleInfoTable() {
    if (sampleInfoTable_ == nullptr) {
        return;
    }

    if (workspace_.sampleInfo.headers.isEmpty()) {
        sampleInfoTable_->setColumnCount(2);
        sampleInfoTable_->setHorizontalHeaderLabels({"State", "Detail"});
        sampleInfoTable_->setRowCount(1);
        sampleInfoTable_->setItem(0, 0, new QTableWidgetItem("No sample metadata"));
        sampleInfoTable_->setItem(0, 1, new QTableWidgetItem("Open a case folder or manifest with sample info files to populate this view."));
        return;
    }

    sampleInfoTable_->setColumnCount(workspace_.sampleInfo.headers.size());
    sampleInfoTable_->setHorizontalHeaderLabels(workspace_.sampleInfo.headers);
    sampleInfoTable_->setRowCount(workspace_.sampleInfo.rows.size());
    for (int row = 0; row < workspace_.sampleInfo.rows.size(); ++row) {
        const QStringList& values = workspace_.sampleInfo.rows.at(row);
        for (int column = 0; column < workspace_.sampleInfo.headers.size(); ++column) {
            const QString value = column < values.size() ? values.at(column) : QString();
            sampleInfoTable_->setItem(row, column, new QTableWidgetItem(value));
        }
    }
    sampleInfoTable_->resizeColumnsToContents();
}

void MainWindow::refreshReviewQueueTable() {
    if (reviewQueueTable_ == nullptr) {
        return;
    }

    if (session_.reviewQueue.isEmpty()) {
        reviewQueueTable_->setRowCount(1);
        reviewQueueTable_->setItem(0, 0, new QTableWidgetItem("No review items"));
        reviewQueueTable_->setItem(0, 1, new QTableWidgetItem("Use Review -> Add Current Locus to Queue or add ROIs to seed the queue."));
        reviewQueueTable_->setItem(0, 2, new QTableWidgetItem("-"));
        reviewQueueTable_->setItem(0, 3, new QTableWidgetItem("-"));
        return;
    }

    reviewQueueTable_->setRowCount(session_.reviewQueue.size());
    for (int row = 0; row < session_.reviewQueue.size(); ++row) {
        const ReviewItem& item = session_.reviewQueue.at(row);
        auto* locusItem = new QTableWidgetItem(item.locus);
        auto* labelItem = new QTableWidgetItem(item.label.isEmpty() ? "Review Item" : item.label);
        auto* statusItem = new QTableWidgetItem(item.status.isEmpty() ? "pending" : item.status);
        auto* noteItem = new QTableWidgetItem(item.note.isEmpty() ? "-" : item.note);
        if (item.status == "reviewed") {
            statusItem->setBackground(QColor("#d8ead5"));
        } else if (item.status == "follow-up") {
            statusItem->setBackground(QColor("#f6e6c8"));
        }
        reviewQueueTable_->setItem(row, 0, locusItem);
        reviewQueueTable_->setItem(row, 1, labelItem);
        reviewQueueTable_->setItem(row, 2, statusItem);
        reviewQueueTable_->setItem(row, 3, noteItem);
    }
}

void MainWindow::refreshFeatureTree() {
    if (featureTree_ == nullptr) {
        return;
    }

    featureTree_->clear();

    QMap<QString, QTreeWidgetItem*> categories;
    for (const QJsonValue& value : featureCatalog_) {
        const QJsonObject feature = value.toObject();
        const QString category = feature.value("category").toString("Feature");
        if (!categories.contains(category)) {
            auto* categoryItem = new QTreeWidgetItem(featureTree_);
            categoryItem->setText(0, category);
            categoryItem->setFirstColumnSpanned(true);
            categories.insert(category, categoryItem);
        }

        auto* item = new QTreeWidgetItem(categories.value(category));
        item->setText(0, feature.value("name").toString());
        item->setText(1, feature.value("phase").toString());
        item->setText(2, feature.value("status").toString());
        item->setText(3, feature.value("notes").toString());
    }

    featureTree_->expandAll();
}

void MainWindow::refreshRoadmapTable() {
    if (roadmapTable_ == nullptr) {
        return;
    }

    roadmapTable_->setRowCount(roadmap_.size());
    for (int row = 0; row < roadmap_.size(); ++row) {
        const QJsonObject milestone = roadmap_.at(row).toObject();
        roadmapTable_->setItem(row, 0, new QTableWidgetItem(milestone.value("phase").toString()));
        roadmapTable_->setItem(row, 1, new QTableWidgetItem(milestone.value("objective").toString()));
        roadmapTable_->setItem(row, 2, new QTableWidgetItem(milestone.value("deliverables").toString()));
    }
}

void MainWindow::appendActivity(const QString& message) {
    if (activityLog_ == nullptr) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    activityLog_->appendPlainText(QString("[%1] %2").arg(timestamp, message));
}

void MainWindow::loadTrackDescriptor(const QString& source, bool fromUrl) {
    QString name = fileLabelForSource(source);
    if (name == source || name.isEmpty()) {
        name = fromUrl ? "Remote Track" : "Local Track";
    }

    TrackDescriptor track = session_parsers::describeTrackSource(source, name);
    if (!track.color.isValid()) {
        track.color = colorForKind(track.kind);
    }
    if (track.name == source || track.name.isEmpty()) {
        track.name = name;
    }
    session_.tracks.append(track);

    syncWorkspaceState();
    syncUiFromSession();
    appendActivity(QString("Loaded %1 track from %2").arg(track.kind, source));
    statusBar()->showMessage(QString("Loaded %1").arg(track.name), 2500);
}

QString MainWindow::inferTrackKind(const QString& source) const {
    return session_parsers::inferTrackKind(source);
}

QColor MainWindow::colorForKind(const QString& kind) const {
    return session_parsers::colorForTrackKind(kind);
}

QStringList MainWindow::parseLoci(const QString& text) const {
    return session_parsers::parseLociText(text);
}

void MainWindow::applyLocusText() {
    if (locusEdit_ == nullptr) {
        return;
    }

    const QString text = locusEdit_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    QStringList loci = parseLoci(text);
    if (loci.isEmpty()) {
        loci = QStringList({text});
    }

    session_.loci = loci;
    if (loci.size() > 1 && multiLocusAction_ != nullptr && !multiLocusAction_->isChecked()) {
        multiLocusAction_->setChecked(true);
    }

    session_.multiLocus = multiLocusAction_ != nullptr && multiLocusAction_->isChecked() && loci.size() > 1;
    session_.locus = session_.multiLocus ? loci.join(" ") : loci.first();

    syncWorkspaceState();
    syncUiFromSession();
    appendActivity(QString("Applied locus input: %1").arg(text));
    statusBar()->showMessage("Viewport updated", 2500);
}

void MainWindow::toggleMultiLocus(bool enabled) {
    session_.multiLocus = enabled && session_.loci.size() > 1;
    if (!session_.loci.isEmpty()) {
        session_.locus = session_.multiLocus ? session_.loci.join(" ") : session_.loci.first();
    }

    syncWorkspaceState();
    syncUiFromSession();
    appendActivity(session_.multiLocus ? "Enabled multi-locus preview." : "Returned to single-locus preview.");
}

void MainWindow::changeGenome(const QString& genome) {
    if (genome.isEmpty() || genome == session_.genome) {
        return;
    }

    if (!session_.genomes.contains(genome)) {
        session_.genomes.append(genome);
    }
    session_.genome = genome;

    syncWorkspaceState();
    syncUiFromSession();
    appendActivity(QString("Switched genome to %1").arg(genome));
    statusBar()->showMessage(QString("Genome %1").arg(genome), 2500);
}

void MainWindow::addRegionOfInterest() {
    const QString locus = session_.multiLocus && !session_.loci.isEmpty() ? session_.loci.first() : session_.locus;
    if (locus.trimmed().isEmpty()) {
        QMessageBox::information(this, "Add ROI", "Enter a locus first.");
        return;
    }

    RoiDescriptor roi;
    roi.locus = locus;
    roi.label = QString("ROI %1").arg(session_.rois.size() + 1);
    session_.rois.append(roi);

    syncWorkspaceState();
    syncUiFromSession();
    appendActivity(QString("Added ROI at %1").arg(locus));
    statusBar()->showMessage("Region of interest added", 2500);
}

void MainWindow::addCurrentLocusToReviewQueue() {
    const QString locus = session_.multiLocus && !session_.loci.isEmpty() ? session_.loci.first() : session_.locus;
    if (locus.trimmed().isEmpty()) {
        QMessageBox::information(this, "Review Queue", "Enter a locus first.");
        return;
    }

    for (const ReviewItem& item : session_.reviewQueue) {
        if (item.locus == locus) {
            statusBar()->showMessage("Locus already exists in review queue", 2500);
            return;
        }
    }

    ReviewItem item;
    item.locus = locus;
    item.label = "Manual Review";
    item.status = "pending";
    session_.reviewQueue.append(item);

    syncWorkspaceState();
    syncUiFromSession();
    if (reviewQueueTable_ != nullptr) {
        reviewQueueTable_->setCurrentCell(session_.reviewQueue.size() - 1, 0);
    }
    appendActivity(QString("Added %1 to the review queue").arg(locus));
    statusBar()->showMessage("Review queue updated", 2500);
}

int MainWindow::findReviewRow(int startRow, int step, bool pendingOnly) const {
    if (session_.reviewQueue.isEmpty()) {
        return -1;
    }

    const int count = session_.reviewQueue.size();
    const int direction = step < 0 ? -1 : 1;
    for (int offset = 1; offset <= count; ++offset) {
        int row = startRow + (direction * offset);
        while (row < 0) {
            row += count;
        }
        row %= count;

        if (!pendingOnly) {
            return row;
        }

        const QString status = session_.reviewQueue.at(row).status.trimmed().toLower();
        if (status != "reviewed") {
            return row;
        }
    }

    return -1;
}

void MainWindow::navigateToReviewRow(int row) {
    if (row < 0 || row >= session_.reviewQueue.size()) {
        return;
    }

    if (reviewQueueTable_ != nullptr) {
        reviewQueueTable_->setCurrentCell(row, 0);
    }

    const ReviewItem& item = session_.reviewQueue.at(row);
    if (item.locus.isEmpty()) {
        return;
    }

    session_.multiLocus = false;
    session_.loci = QStringList({item.locus});
    session_.locus = item.locus;

    syncWorkspaceState();
    syncUiFromSession();
    if (reviewQueueTable_ != nullptr) {
        reviewQueueTable_->setCurrentCell(row, 0);
    }
    appendActivity(QString("Jumped to review item %1").arg(item.locus));
    statusBar()->showMessage(QString("Navigated to %1").arg(item.locus), 2500);
}

void MainWindow::jumpToPreviousReviewItem() {
    if (reviewQueueTable_ == nullptr || session_.reviewQueue.isEmpty()) {
        return;
    }

    const int currentRow = reviewQueueTable_->currentRow();
    const int row = findReviewRow(currentRow >= 0 ? currentRow : 0, -1, false);
    navigateToReviewRow(row);
}

void MainWindow::jumpToNextReviewItem() {
    if (reviewQueueTable_ == nullptr || session_.reviewQueue.isEmpty()) {
        return;
    }

    const int currentRow = reviewQueueTable_->currentRow();
    const int row = findReviewRow(currentRow >= 0 ? currentRow : -1, 1, false);
    navigateToReviewRow(row);
}

void MainWindow::jumpToNextPendingReviewItem() {
    if (reviewQueueTable_ == nullptr || session_.reviewQueue.isEmpty()) {
        return;
    }

    const int currentRow = reviewQueueTable_->currentRow();
    const int row = findReviewRow(currentRow >= 0 ? currentRow : -1, 1, true);
    if (row >= 0) {
        navigateToReviewRow(row);
        return;
    }

    statusBar()->showMessage("No pending review items remain", 2500);
}

void MainWindow::jumpToSelectedReviewItem() {
    if (reviewQueueTable_ == nullptr) {
        return;
    }

    const int row = reviewQueueTable_->currentRow();
    if (row < 0 || row >= session_.reviewQueue.size()) {
        return;
    }

    navigateToReviewRow(row);
}

void MainWindow::markSelectedReviewItem(const QString& status) {
    if (reviewQueueTable_ == nullptr) {
        return;
    }

    const int row = reviewQueueTable_->currentRow();
    if (row < 0 || row >= session_.reviewQueue.size()) {
        return;
    }

    session_.reviewQueue[row].status = status;
    syncWorkspaceState();
    syncUiFromSession();
    appendActivity(QString("Marked %1 as %2").arg(session_.reviewQueue.at(row).locus, status));

    if (reviewQueueTable_ != nullptr && row < session_.reviewQueue.size()) {
        reviewQueueTable_->setCurrentCell(row, 0);
    }

    if (status.trimmed().compare("reviewed", Qt::CaseInsensitive) == 0 &&
        autoAdvanceReviewAction_ != nullptr && autoAdvanceReviewAction_->isChecked()) {
        const int nextPendingRow = findReviewRow(row, 1, true);
        if (nextPendingRow >= 0) {
            navigateToReviewRow(nextPendingRow);
            return;
        }

        statusBar()->showMessage("Review queue complete", 2500);
        return;
    }

    statusBar()->showMessage(QString("Review item marked %1").arg(status), 2500);
}

void MainWindow::editSelectedReviewNote() {
    if (reviewQueueTable_ == nullptr) {
        return;
    }

    const int row = reviewQueueTable_->currentRow();
    if (row < 0 || row >= session_.reviewQueue.size()) {
        return;
    }

    bool accepted = false;
    const QString note = QInputDialog::getMultiLineText(
        this,
        "Edit Review Note",
        "Review note:",
        session_.reviewQueue.at(row).note,
        &accepted);

    if (!accepted) {
        return;
    }

    session_.reviewQueue[row].note = note.trimmed();
    syncWorkspaceState();
    syncUiFromSession();
    appendActivity(QString("Updated review note for %1").arg(session_.reviewQueue.at(row).locus));
    statusBar()->showMessage("Review note saved", 2500);
}

void MainWindow::saveSession() {
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Save Native Session",
        QString(),
        "IGV Native Session (*.igvn.json *.json)");

    if (fileName.isEmpty()) {
        return;
    }
    if (!fileName.endsWith(".json")) {
        fileName += ".json";
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Save Session", "Could not write the selected session file.");
        return;
    }

    file.write(QJsonDocument(sessionToObject()).toJson(QJsonDocument::Indented));
    file.close();

    appendActivity(QString("Saved session to %1").arg(fileName));
    statusBar()->showMessage("Session saved", 2500);
}

void MainWindow::applyTrackPreset(const QString& presetName) {
    activePresetName_ = presetName;
    refreshTrackTable();
    refreshSummary();
    if (!presetName.isEmpty()) {
        statusBar()->showMessage(QString("Applied preset %1").arg(presetName), 2500);
    }
}

void MainWindow::openCaseFolder() {
    const QString directoryPath = QFileDialog::getExistingDirectory(this, "Open Case Folder");
    if (directoryPath.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!loadCaseFolderPath(directoryPath, &errorMessage)) {
        QMessageBox::warning(this, "Open Case Folder", errorMessage);
        return;
    }

    syncUiFromSession();
    appendActivity(QString("Opened case folder from %1").arg(directoryPath));
    statusBar()->showMessage("Case folder loaded", 2500);
    if (readinessDock_ != nullptr) {
        readinessDock_->show();
        readinessDock_->raise();
    }
    if (sampleInfoDock_ != nullptr && !workspace_.sampleInfo.headers.isEmpty()) {
        sampleInfoDock_->show();
    }
}

void MainWindow::openCaseManifest() {
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        "Open Case Manifest",
        QString(),
        "IGV Case Manifest (*.igvcase.json *.case.json *.json)");

    if (fileName.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!loadCaseManifestFile(fileName, &errorMessage)) {
        QMessageBox::warning(this, "Open Case Manifest", errorMessage);
        return;
    }

    syncUiFromSession();
    appendActivity(QString("Opened case manifest from %1").arg(fileName));
    statusBar()->showMessage("Case manifest loaded", 2500);
    if (readinessDock_ != nullptr) {
        readinessDock_->show();
        readinessDock_->raise();
    }
    if (sampleInfoDock_ != nullptr && !workspace_.sampleInfo.headers.isEmpty()) {
        sampleInfoDock_->show();
    }
}

void MainWindow::openSession() {
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        "Open Session",
        QString(),
        "IGV Native Session (*.igvn.json *.json);;IGV XML Session (*.xml)");

    if (fileName.isEmpty()) {
        return;
    }

    if (fileName.endsWith(".xml", Qt::CaseInsensitive)) {
        QString errorMessage;
        if (!loadSessionFromIgvXml(fileName, &errorMessage)) {
            QMessageBox::warning(this, "Open Session", errorMessage);
            return;
        }

        syncUiFromSession();
        appendActivity(QString("Imported IGV XML session from %1").arg(fileName));
        statusBar()->showMessage("IGV XML session imported", 2500);
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Open Session", "Could not read the selected session file.");
        return;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, "Open Session", "The selected file is not a valid native session.");
        return;
    }

    loadSessionFromObject(document.object());
    syncUiFromSession();
    appendActivity(QString("Opened session from %1").arg(fileName));
    statusBar()->showMessage("Session opened", 2500);
}

void MainWindow::loadTracksFromFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Load Tracks",
        QString(),
        "All Files (*.*)");

    for (const QString& file : files) {
        loadTrackDescriptor(file, false);
    }
}

void MainWindow::loadTrackFromUrl() {
    bool accepted = false;
    const QString url = QInputDialog::getText(
        this,
        "Load Track from URL",
        "Track URL:",
        QLineEdit::Normal,
        "https://example.org/data/sample.bam",
        &accepted);

    if (!accepted || url.trimmed().isEmpty()) {
        return;
    }

    loadTrackDescriptor(url.trimmed(), true);
}

void MainWindow::exportSnapshot() {
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Export Snapshot",
        QString(),
        "PNG Image (*.png)");

    if (fileName.isEmpty()) {
        return;
    }
    if (!fileName.endsWith(".png")) {
        fileName += ".png";
    }

    if (!canvas_->grab().save(fileName, "PNG")) {
        QMessageBox::warning(this, "Export Snapshot", "Could not save the PNG snapshot.");
        return;
    }

    appendActivity(QString("Exported PNG snapshot to %1").arg(fileName));
    statusBar()->showMessage("Snapshot exported", 2500);
}

void MainWindow::exportReviewPacket() {
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Export Review Packet",
        QString(),
        "Markdown Document (*.md)");

    if (fileName.isEmpty()) {
        return;
    }
    if (!fileName.endsWith(".md")) {
        fileName += ".md";
    }

    WorkspaceState exportWorkspace = workspace_;
    exportWorkspace.session = session_;
    if (exportWorkspace.schema.isEmpty()) {
        exportWorkspace.schema = "igv-native-ad-hoc-workspace/v1";
        exportWorkspace.title = "Ad hoc review packet";
    }
    session_parsers::refreshWorkspaceReadiness(&exportWorkspace);

    QFileInfo fileInfo(fileName);
    const QString snapshotFileName = fileInfo.completeBaseName() + ".png";
    const QString snapshotPath = fileInfo.dir().filePath(snapshotFileName);

    QString markdown = session_parsers::buildReviewPacketMarkdown(exportWorkspace, activePresetName_);
    if (canvas_ != nullptr && canvas_->grab().save(snapshotPath, "PNG")) {
        markdown += QString("\n## Snapshot\n\n![Viewport snapshot](%1)\n").arg(snapshotFileName);
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, "Export Review Packet", "Could not write the review packet markdown.");
        return;
    }

    file.write(markdown.toUtf8());
    file.close();

    appendActivity(QString("Exported review packet to %1").arg(fileName));
    statusBar()->showMessage("Review packet exported", 2500);
}

void MainWindow::exportHtmlReviewReport() {
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Export HTML Review Report",
        QString(),
        "HTML Document (*.html)");

    if (fileName.isEmpty()) {
        return;
    }
    if (!fileName.endsWith(".html")) {
        fileName += ".html";
    }

    WorkspaceState exportWorkspace = workspace_;
    exportWorkspace.session = session_;
    if (exportWorkspace.schema.isEmpty()) {
        exportWorkspace.schema = "igv-native-ad-hoc-workspace/v1";
        exportWorkspace.title = "Ad hoc review report";
    }
    session_parsers::refreshWorkspaceReadiness(&exportWorkspace);

    const QFileInfo reportFileInfo(fileName);
    const QString assetDirectoryName = reportFileInfo.completeBaseName() + "_assets";
    const QString assetDirectoryPath = reportFileInfo.dir().filePath(assetDirectoryName);
    QDir assetDirectory(assetDirectoryPath);
    if (!assetDirectory.exists() && !reportFileInfo.dir().mkpath(assetDirectoryName)) {
        QMessageBox::warning(this, "Export HTML Review Report", "Could not create the report asset directory.");
        return;
    }

    const QString overviewFileName = "overview.png";
    const QString overviewPath = assetDirectory.filePath(overviewFileName);
    if (canvas_ == nullptr || !canvas_->grab().save(overviewPath, "PNG")) {
        QMessageBox::warning(this, "Export HTML Review Report", "Could not capture the overview snapshot.");
        return;
    }

    const SessionState originalSession = session_;
    const int originalRow = reviewQueueTable_ != nullptr ? reviewQueueTable_->currentRow() : -1;

    QMap<QString, QString> reviewSnapshotFiles;
    for (int row = 0; row < session_.reviewQueue.size(); ++row) {
        const ReviewItem& item = session_.reviewQueue.at(row);
        const QString snapshotBaseName =
            QString("review-%1-%2.png").arg(row + 1, 2, 10, QChar('0')).arg(sanitizeFileStem(item.label.isEmpty() ? item.locus : item.label));
        const QString absoluteSnapshotPath = assetDirectory.filePath(snapshotBaseName);

        session_.multiLocus = false;
        session_.loci = QStringList({item.locus});
        session_.locus = item.locus;
        syncWorkspaceState();
        syncUiFromSession();
        if (reviewQueueTable_ != nullptr) {
            reviewQueueTable_->setCurrentCell(row, 0);
        }
        qApp->processEvents();

        if (canvas_ != nullptr && canvas_->grab().save(absoluteSnapshotPath, "PNG")) {
            reviewSnapshotFiles.insert(item.locus, assetDirectoryName + "/" + snapshotBaseName);
        }
    }

    session_ = originalSession;
    syncWorkspaceState();
    syncUiFromSession();
    if (reviewQueueTable_ != nullptr && originalRow >= 0 && originalRow < session_.reviewQueue.size()) {
        reviewQueueTable_->setCurrentCell(originalRow, 0);
    }
    qApp->processEvents();

    const QString html = session_parsers::buildReviewPacketHtml(
        exportWorkspace,
        activePresetName_,
        assetDirectoryName + "/" + overviewFileName,
        reviewSnapshotFiles);

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, "Export HTML Review Report", "Could not write the HTML review report.");
        return;
    }

    file.write(html.toUtf8());
    file.close();

    appendActivity(QString("Exported HTML review report to %1").arg(fileName));
    statusBar()->showMessage("HTML review report exported", 2500);
}

void MainWindow::revealFeatureInventory() {
    if (featureDock_ != nullptr) {
        featureDock_->show();
        featureDock_->raise();
    }
}

void MainWindow::showAboutDialog() {
    QMessageBox::information(
        this,
        "About IGV Native Desktop",
        "This prototype turns the Java-era IGV Desktop feature map into a native macOS workbench.\n\n"
        "Qt handles the app shell and Rust owns the product catalog, session model, and future data engines.");
}
