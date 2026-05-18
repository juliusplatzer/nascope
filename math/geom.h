#ifndef MATH_GEOM_H_
#define MATH_GEOM_H_

#include <QPointF>

namespace math {

bool lineSegmentsIntersect(const QPointF& a,
                           const QPointF& b,
                           const QPointF& c,
                           const QPointF& d);

} // namespace math

#endif  // MATH_GEOM_H_
