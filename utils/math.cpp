#include "utils/math.h"

#include <algorithm>
#include <cmath>

namespace utils {
namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

QTransform lonLatToNm(const QPointF& anchorLonLat) {
    const double cosLat = std::cos(anchorLonLat.y() * kPi / 180.0);

    QTransform transform;
    transform.scale(60.0 * cosLat, 60.0);
    transform.translate(-anchorLonLat.x(), -anchorLonLat.y());
    return transform;
}

QTransform lonLatToFeet(const QPointF& anchorLonLat) {
    const double cosLat = std::cos(anchorLonLat.y() * kPi / 180.0);

    QTransform transform;
    transform.scale(60.0 * cosLat * kFeetPerNm, 60.0 * kFeetPerNm);
    transform.translate(-anchorLonLat.x(), -anchorLonLat.y());
    return transform;
}

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

} // namespace utils
