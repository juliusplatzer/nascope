#pragma once

#include <QString>
#include <QStringList>

namespace utils {

QString findProjectRoot();
QString findProjectRelativeFile(const QString& relativePath);
QString findProjectRelativeDir(const QString& relativePath);
QString videomapPath(const QString& icao);
QStringList availableAirports();

} // namespace utils
