#ifndef UTILS_MATH_H_
#define UTILS_MATH_H_

#include <QPointF>
#include <QSize>
#include <QTransform>

namespace utils {

inline constexpr double kViewportMargin = 0.04;
inline constexpr double kFeetPerNm = 6076.12;

QTransform lonLatToNm(const QPointF& anchorLonLat);
QTransform lonLatToFeet(const QPointF& anchorLonLat);
QTransform nmToScreen(const QPointF& centerNm, double halfRangeNm, const QSize& view);
bool lineSegmentsIntersect(const QPointF& a,
                           const QPointF& b,
                           const QPointF& c,
                           const QPointF& d);

} // namespace utils

#endif  // UTILS_MATH_H_
