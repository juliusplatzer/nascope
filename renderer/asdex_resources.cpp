#include "renderer/asdex_resources.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace renderer::asdex {
namespace {

QStringList candidateRoots() {
    QStringList roots;
    const auto add = [&roots](const QString& path) {
        if (path.isEmpty()) return;
        const QString canonical = QDir(path).canonicalPath();
        const QString normalized = canonical.isEmpty() ? QDir(path).absolutePath() : canonical;
        if (!roots.contains(normalized)) roots << normalized;
    };

    add(QDir::currentPath());

    const QDir appDir(QCoreApplication::applicationDirPath());
    add(appDir.absolutePath());
    add(appDir.filePath(QStringLiteral("..")));
    add(appDir.filePath(QStringLiteral("../..")));
    add(appDir.filePath(QStringLiteral("../../..")));

    return roots;
}

QString findProjectRelativePath(const QString& relativePath, bool wantDir) {
    for (const QString& root : candidateRoots()) {
        const QString candidate = QDir(root).filePath(relativePath);
        const QFileInfo info(candidate);
        if ((wantDir && info.isDir()) || (!wantDir && info.isFile()))
            return info.canonicalFilePath().isEmpty() ? candidate : info.canonicalFilePath();
    }
    return relativePath;
}

} // namespace

QString findProjectRelativeFile(const QString& relativePath) {
    return findProjectRelativePath(relativePath, false);
}

QString findProjectRelativeDir(const QString& relativePath) {
    return findProjectRelativePath(relativePath, true);
}

QString videomapPath(const QString& icao) {
    return findProjectRelativeFile(
        QStringLiteral("resources/videomaps/asdex/%1.geojson.gz").arg(icao.toUpper()));
}

QStringList availableAirports() {
    const QDir dir(findProjectRelativeDir(QStringLiteral("resources/videomaps/asdex")));
    QStringList icaos;
    for (const QString& name : dir.entryList(QStringList{QStringLiteral("*.geojson.gz")},
                                             QDir::Files,
                                             QDir::Name)) {
        icaos << name.left(name.indexOf(QLatin1Char('.')));
    }
    return icaos;
}

} // namespace renderer::asdex
