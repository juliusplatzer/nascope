#ifndef RADAR_TOOLS_H_
#define RADAR_TOOLS_H_

#include <QPointF>
#include <QSize>
#include <QTransform>

namespace radar {

inline constexpr double kViewportMargin = 0.04;

QTransform nmToScreen(const QPointF& centerNm, double halfRangeNm, const QSize& view);

} // namespace radar

#endif  // RADAR_TOOLS_H_
