#pragma once

#include <QPointF>
#include <QSize>
#include <QTransform>

namespace utils {

inline constexpr double kViewportMargin = 0.04;
inline constexpr double kFeetPerNm = 6076.12;

QTransform lonLatToNm(const QPointF& anchorLonLat);
QTransform lonLatToFeet(const QPointF& anchorLonLat);
QTransform nmToScreen(const QPointF& centerNm, double halfRangeNm, const QSize& view);

} // namespace utils
