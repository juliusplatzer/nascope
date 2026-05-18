#include "math/latlong.h"

#include "math/core.h"

#include <cmath>

namespace math {

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

} // namespace math
