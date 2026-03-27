#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
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
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMap>
#include <QPlainTextEdit>
#include <QRegularExpression>
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
    session_ = {};
    session_.schema = object.value("schema").toString("igv-native-session/v1");
    session_.genome = object.value("genome").toString("GRCh38/hg38");
    session_.locus = object.value("locus").toString("All");
    session_.multiLocus = object.value("multi_locus").toBool(false);

    for (const QJsonValue& genomeValue : object.value("genomes").toArray()) {
        const QString genome = genomeValue.toString();
        if (!genome.isEmpty()) {
            session_.genomes.append(genome);
        }
    }
    if (session_.genomes.isEmpty()) {
        session_.genomes = QStringList({"GRCh38/hg38", "hg19", "GRCm39/mm39"});
    }

    for (const QJsonValue& locusValue : object.value("loci").toArray()) {
        const QString locus = locusValue.toString();
        if (!locus.isEmpty()) {
            session_.loci.append(locus);
        }
    }
    if (session_.loci.isEmpty() && !session_.locus.isEmpty()) {
        session_.loci = QStringList({session_.locus});
    }

    for (const QJsonValue& roiValue : object.value("rois").toArray()) {
        const QJsonObject roiObject = roiValue.toObject();
        RoiDescriptor roi;
        roi.locus = roiObject.value("locus").toString();
        roi.label = roiObject.value("label").toString();
        if (!roi.locus.isEmpty()) {
            session_.rois.append(roi);
        }
    }

    for (const QJsonValue& trackValue : object.value("tracks").toArray()) {
        const QJsonObject trackObject = trackValue.toObject();
        TrackDescriptor track;
        track.name = trackObject.value("name").toString("Track");
        track.kind = trackObject.value("kind").toString("track");
        track.source = trackObject.value("source").toString();
        track.visibility = trackObject.value("visibility").toString("always");
        track.color = colorForKind(track.kind);
        session_.tracks.append(track);
    }

    if (session_.tracks.isEmpty()) {
        loadTrackDescriptor("builtin://reference", true);
        loadTrackDescriptor("builtin://genes", true);
    }
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

    QJsonArray tracks;
    for (const TrackDescriptor& track : session_.tracks) {
        QJsonObject trackObject;
        trackObject.insert("name", track.name);
        trackObject.insert("kind", track.kind);
        trackObject.insert("source", track.source);
        trackObject.insert("visibility", track.visibility);
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
    trackTable_ = new QTableWidget(0, 3, tracksDock);
    trackTable_->setHorizontalHeaderLabels({"Track", "Kind", "Source"});
    trackTable_->horizontalHeader()->setStretchLastSection(true);
    trackTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    trackTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    trackTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    trackTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tracksDock->setWidget(trackTable_);
    addDockWidget(Qt::LeftDockWidgetArea, tracksDock);

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
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("Load Track(s)...", this, &MainWindow::loadTracksFromFiles, QKeySequence::Open);
    fileMenu->addAction("Load from URL...", this, &MainWindow::loadTrackFromUrl);
    fileMenu->addSeparator();
    fileMenu->addAction("Open Session...", this, &MainWindow::openSession);
    fileMenu->addAction("Save Session...", this, &MainWindow::saveSession, QKeySequence::Save);
    fileMenu->addSeparator();
    fileMenu->addAction("Export Snapshot...", this, &MainWindow::exportSnapshot);
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
    toolbar->addWidget(new QLabel("Locus", toolbar));
    locusEdit_ = new QLineEdit(toolbar);
    locusEdit_->setMinimumWidth(320);
    toolbar->addWidget(locusEdit_);

    auto* applyAction = toolbar->addAction("Go");
    auto* snapshotAction = toolbar->addAction("Snapshot");
    toolbar->addAction(multiLocusAction_);

    connect(genomeCombo_, &QComboBox::currentTextChanged, this, &MainWindow::changeGenome);
    connect(locusEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyLocusText);
    connect(applyAction, &QAction::triggered, this, &MainWindow::applyLocusText);
    connect(snapshotAction, &QAction::triggered, this, &MainWindow::exportSnapshot);
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

    refreshTrackTable();
    refreshFeatureTree();
    refreshRoadmapTable();
    refreshSummary();
}

void MainWindow::refreshSummary() {
    if (summaryLabel_ == nullptr) {
        return;
    }

    const QString lociSummary = session_.multiLocus ? session_.loci.join(" | ") : session_.locus;
    summaryLabel_->setText(
        QString("Genome: %1\nLocus: %2\nTracks: %3  |  Regions of interest: %4  |  Session schema: %5")
            .arg(session_.genome)
            .arg(lociSummary)
            .arg(session_.tracks.size())
            .arg(session_.rois.size())
            .arg(session_.schema));
}

void MainWindow::refreshTrackTable() {
    if (trackTable_ == nullptr) {
        return;
    }

    trackTable_->setRowCount(session_.tracks.size());
    for (int row = 0; row < session_.tracks.size(); ++row) {
        const TrackDescriptor& track = session_.tracks.at(row);
        auto* nameItem = new QTableWidgetItem(track.name);
        nameItem->setBackground(track.color);
        auto* kindItem = new QTableWidgetItem(track.kind);
        auto* sourceItem = new QTableWidgetItem(track.source);
        trackTable_->setItem(row, 0, nameItem);
        trackTable_->setItem(row, 1, kindItem);
        trackTable_->setItem(row, 2, sourceItem);
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
    TrackDescriptor track;
    track.kind = inferTrackKind(source);
    track.name = fileLabelForSource(source);
    if (track.name == source || track.name.isEmpty()) {
        track.name = fromUrl ? "Remote Track" : "Local Track";
    }
    track.source = source;
    track.visibility = (track.kind == "reference" || track.kind == "annotation") ? "always" : "zoomed";
    track.color = colorForKind(track.kind);
    session_.tracks.append(track);

    syncUiFromSession();
    appendActivity(QString("Loaded %1 track from %2").arg(track.kind, source));
    statusBar()->showMessage(QString("Loaded %1").arg(track.name), 2500);
}

QString MainWindow::inferTrackKind(const QString& source) const {
    const QString lower = source.toLower();

    if (lower.endsWith(".bam") || lower.endsWith(".cram") || lower.endsWith(".sam")) {
        return "alignment";
    }
    if (lower.endsWith(".vcf") || lower.endsWith(".vcf.gz") || lower.endsWith(".bcf")) {
        return "variant";
    }
    if (lower.endsWith(".bw") || lower.endsWith(".bigwig") || lower.endsWith(".wig") ||
        lower.endsWith(".bedgraph") || lower.endsWith(".tdf") || lower.endsWith(".seg")) {
        return "quantitative";
    }
    if (lower.endsWith(".bed") || lower.endsWith(".gff") || lower.endsWith(".gff3") ||
        lower.endsWith(".gtf") || lower.endsWith(".bb") || lower.endsWith(".bigbed")) {
        return "annotation";
    }
    if (lower.endsWith(".fa") || lower.endsWith(".fasta") || lower.endsWith(".2bit") || lower.endsWith(".genome")) {
        return "reference";
    }
    if (lower.contains("rna") || lower.contains("junction")) {
        return "splice";
    }
    return "track";
}

QColor MainWindow::colorForKind(const QString& kind) const {
    if (kind == "reference") {
        return QColor("#5f6b7a");
    }
    if (kind == "annotation") {
        return QColor("#247a78");
    }
    if (kind == "alignment") {
        return QColor("#c96e39");
    }
    if (kind == "splice") {
        return QColor("#2c6d8a");
    }
    if (kind == "variant") {
        return QColor("#9f3f4f");
    }
    if (kind == "quantitative") {
        return QColor("#73824a");
    }
    return QColor("#6a6e73");
}

QStringList MainWindow::parseLoci(const QString& text) const {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    return trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
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

    syncUiFromSession();
    appendActivity(QString("Applied locus input: %1").arg(text));
    statusBar()->showMessage("Viewport updated", 2500);
}

void MainWindow::toggleMultiLocus(bool enabled) {
    session_.multiLocus = enabled && session_.loci.size() > 1;
    if (!session_.loci.isEmpty()) {
        session_.locus = session_.multiLocus ? session_.loci.join(" ") : session_.loci.first();
    }

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

    syncUiFromSession();
    appendActivity(QString("Added ROI at %1").arg(locus));
    statusBar()->showMessage("Region of interest added", 2500);
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
        QMessageBox::information(
            this,
            "Open Session",
            "IGV XML session import is planned but not implemented in this prototype yet.");
        appendActivity(QString("Deferred XML session import for %1").arg(fileName));
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
