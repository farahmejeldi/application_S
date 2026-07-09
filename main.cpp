
#include "mainwindow.h"
#include <QApplication>
#include <QFont>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("Système d'Inspection Industrielle");
    app.setOrganizationName("Inspection QC");

    QFont defaultFont("Segoe UI", 10);
    app.setFont(defaultFont);

    MainWindow window;
    window.show();

    return app.exec();
}

