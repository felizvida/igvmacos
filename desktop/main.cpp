#include <QApplication>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("IGV Native Desktop");
    app.setOrganizationName("OpenAI Prototype");
    app.setStyleSheet(
        "QMainWindow { background: #f3efe7; }"
        "QToolBar { background: #f3efe7; border: none; spacing: 6px; }"
        "QDockWidget::title { background: #d9e4e3; padding: 6px 10px; font-weight: 600; }"
        "#summaryCard { background: rgba(255, 255, 255, 0.85); border-radius: 18px; }"
        "QTabBar::tab { padding: 8px 14px; }"
        "QHeaderView::section { background: #e7eeeb; padding: 6px; border: none; }");

    MainWindow window;
    window.show();
    return app.exec();
}
