#include "math/latlong.h"

#include "math/core.h"

#include <cmath>

namespace math {

QTransform lonLatToNm(const QPointF& anchorLonLat) {
    const double cosLat = std::cos(degreesToRadians(anchorLonLat.y()));

    QTransform transform;
    transform.scale(60.0 * cosLat, 60.0);
    transform.translate(-anchorLonLat.x(), -anchorLonLat.y());
    return transform;
}

QTransform lonLatToFeet(const QPointF& anchorLonLat) {
    const double cosLat = std::cos(degreesToRadians(anchorLonLat.y()));

    QTransform transform;
    transform.scale(60.0 * cosLat * kFeetPerNm, 60.0 * kFeetPerNm);
    transform.translate(-anchorLonLat.x(), -anchorLonLat.y());
    return transform;
}

LocalMeterProjector::LocalMeterProjector(double lon0, double lat0)
    : lon0_(lon0),
      lat0_(lat0),
      cosLat0_(std::cos(degreesToRadians(lat0))) {}

QPointF LocalMeterProjector::map(double lon, double lat) const {
    return QPointF((lon - lon0_) * kDegreesToRadians * kEarthRadiusMeters * cosLat0_,
                   (lat - lat0_) * kDegreesToRadians * kEarthRadiusMeters);
}

QPointF LocalMeterProjector::map(const QPointF& lonLat) const {
    return map(lonLat.x(), lonLat.y());
}

} // namespace math
