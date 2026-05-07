#pragma once

#include <QString>
#include <QStringList>

namespace renderer::asdex {

QString findProjectRelativeFile(const QString& relativePath);
QString findProjectRelativeDir(const QString& relativePath);
QString videomapPath(const QString& icao);
QStringList availableAirports();

} // namespace renderer::asdex
