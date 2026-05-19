#ifndef MATH_LATLONG_H_
#define MATH_LATLONG_H_

#include <QPointF>
#include <QTransform>

namespace math {

QTransform lonLatToNm(const QPointF& anchorLonLat);
QTransform lonLatToFeet(const QPointF& anchorLonLat);

class LocalMeterProjector {
public:
    LocalMeterProjector() = default;
    LocalMeterProjector(double lon0, double lat0);

    QPointF map(double lon, double lat) const;
    QPointF map(const QPointF& lonLat) const;

    double originLon() const { return lon0_; }
    double originLat() const { return lat0_; }

private:
    double lon0_ = 0.0;
    double lat0_ = 0.0;
    double cosLat0_ = 1.0;
};

} // namespace math

#endif  // MATH_LATLONG_H_
