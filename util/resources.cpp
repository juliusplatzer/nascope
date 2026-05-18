#include "util/resources.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace util {
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
    const QString candidate = QDir(findProjectRoot()).filePath(relativePath);
    const QFileInfo info(candidate);
    if ((wantDir && info.isDir()) || (!wantDir && info.isFile())) {
        return info.canonicalFilePath().isEmpty() ? candidate : info.canonicalFilePath();
    }
    return candidate;
}

} // namespace

QString findProjectRoot() {
    static const QString root = [] {
        const auto isProjectRoot = [](const QString& path) {
            const QDir dir(path);
            return QFileInfo(dir.filePath(QStringLiteral("resources/videomaps"))).isDir()
                || QFileInfo(dir.filePath(QStringLiteral("asdex/assets"))).isDir();
        };

        for (const QString& candidate : candidateRoots()) {
            if (isProjectRoot(candidate)) {
                return candidate;
            }
        }

        return QDir::currentPath();
    }();

    return root;
}

QString findProjectRelativeFile(const QString& relativePath) {
    return findProjectRelativePath(relativePath, false);
}

QString findProjectRelativeDir(const QString& relativePath) {
    return findProjectRelativePath(relativePath, true);
}

} // namespace util
