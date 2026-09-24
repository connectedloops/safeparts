#include "desktop_window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLibraryInfo>

namespace {
void discardQtMessages(QtMsgType, const QMessageLogContext &, const QString &) {}
}

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager);
    const QDir executableDirectory(QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath());
    const QString bundledPlugins = executableDirectory.absoluteFilePath(QStringLiteral("../PlugIns"));
    QCoreApplication::setLibraryPaths({QFileInfo::exists(bundledPlugins)
                                           ? QDir::cleanPath(bundledPlugins)
                                           : QLibraryInfo::path(QLibraryInfo::PluginsPath)});
    qInstallMessageHandler(discardQtMessages);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Safeparts"));
    app.setOrganizationName(QStringLiteral("Safeparts"));
    app.setQuitOnLastWindowClosed(true);
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));

    DesktopWindow window;
    window.show();
    return app.exec();
}
