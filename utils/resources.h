#ifndef UTILS_RESOURCES_H_
#define UTILS_RESOURCES_H_

#include <QString>
#include <QStringList>

namespace utils {

QString findProjectRoot();
QString findProjectRelativeFile(const QString& relativePath);
QString findProjectRelativeDir(const QString& relativePath);
QString videomapPath(const QString& icao);
QStringList availableAirports();

} // namespace utils

#endif  // UTILS_RESOURCES_H_
