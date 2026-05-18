#include "radar/tools.h"

#include <algorithm>

namespace radar {

QTransform nmToScreen(const QPointF& centerNm, double halfRangeNm, const QSize& view) {
    if (view.isEmpty() || halfRangeNm <= 0.0) return {};

    const double availW = view.width() * (1.0 - 2.0 * kViewportMargin);
    const double availH = view.height() * (1.0 - 2.0 * kViewportMargin);
    const double radiusPx = 0.5 * std::min(availW, availH);
    const double pxPerNm = radiusPx / halfRangeNm;

    QTransform transform;
    transform.translate(view.width() * 0.5, view.height() * 0.5);
    transform.scale(pxPerNm, -pxPerNm);
    transform.translate(-centerNm.x(), -centerNm.y());
    return transform;
}

} // namespace radar
