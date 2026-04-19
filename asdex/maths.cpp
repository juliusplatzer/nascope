#include "maths.h"

#include <algorithm>
#include <cmath>

namespace asdex {

QTransform lonLatToNm(const QPointF& anchor) {
    const double cosLat = std::cos(anchor.y() * M_PI / 180.0);
    // One degree of latitude ≈ 60 NM; a degree of longitude shrinks with cos(lat).
    QTransform t;
    t.scale(60.0 * cosLat, 60.0);
    t.translate(-anchor.x(), -anchor.y());
    return t;
}

QTransform nmToScreen(const QPointF& centerNm, double halfRangeNm, const QSize& view) {
    if (view.isEmpty() || halfRangeNm <= 0.0) return {};

    const double availW   = view.width()  * (1.0 - 2.0 * kViewportMargin);
    const double availH   = view.height() * (1.0 - 2.0 * kViewportMargin);
    const double radiusPx = 0.5 * std::min(availW, availH);
    const double pxPerNm  = radiusPx / halfRangeNm;

    QTransform t;
    t.translate(view.width() * 0.5, view.height() * 0.5);
    t.scale(pxPerNm, -pxPerNm);  // north-up
    t.translate(-centerNm.x(), -centerNm.y());
    return t;
}

} // namespace asdex
