#ifndef UTIL_RESOURCES_H_
#define UTIL_RESOURCES_H_

#include <QString>

namespace util {

QString findProjectRoot();
QString findProjectRelativeFile(const QString& relativePath);
QString findProjectRelativeDir(const QString& relativePath);

} // namespace util

#endif  // UTIL_RESOURCES_H_
