#include "math/geom.h"

namespace math {
namespace {

constexpr double kGeometryEpsilon = 1e-9;

double orient(const QPointF& a, const QPointF& b, const QPointF& c) {
    return (b.x() - a.x()) * (c.y() - a.y())
         - (b.y() - a.y()) * (c.x() - a.x());
}

} // namespace

bool lineSegmentsIntersect(const QPointF& a,
                           const QPointF& b,
                           const QPointF& c,
                           const QPointF& d) {
    const double o1 = orient(a, b, c);
    const double o2 = orient(a, b, d);
    const double o3 = orient(c, d, a);
    const double o4 = orient(c, d, b);

    return ((o1 > kGeometryEpsilon && o2 < -kGeometryEpsilon)
            || (o1 < -kGeometryEpsilon && o2 > kGeometryEpsilon))
        && ((o3 > kGeometryEpsilon && o4 < -kGeometryEpsilon)
            || (o3 < -kGeometryEpsilon && o4 > kGeometryEpsilon));
}

} // namespace math
