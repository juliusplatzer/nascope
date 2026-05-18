#ifndef MATH_LATLONG_H_
#define MATH_LATLONG_H_

#include <QPointF>
#include <QTransform>

namespace math {

QTransform lonLatToNm(const QPointF& anchorLonLat);
QTransform lonLatToFeet(const QPointF& anchorLonLat);

} // namespace math

#endif  // MATH_LATLONG_H_
