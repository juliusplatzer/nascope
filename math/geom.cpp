#include "math/geom.h"

#include "math/core.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace math {
namespace {

constexpr double kGeometryEpsilon = 1e-9;

double orient(const QPointF& a, const QPointF& b, const QPointF& c) {
    return (b.x() - a.x()) * (c.y() - a.y())
         - (b.y() - a.y()) * (c.x() - a.x());
}

} // namespace

double dot(const QPointF& a, const QPointF& b) {
    return a.x() * b.x() + a.y() * b.y();
}

double cross(const QPointF& a, const QPointF& b) {
    return a.x() * b.y() - a.y() * b.x();
}

double lengthSquared(const QPointF& v) {
    return dot(v, v);
}

double length(const QPointF& v) {
    return std::hypot(v.x(), v.y());
}

double distanceSquared(const QPointF& a, const QPointF& b) {
    return lengthSquared(b - a);
}

double distance(const QPointF& a, const QPointF& b) {
    return length(b - a);
}

bool pointsNear(const QPointF& a, const QPointF& b, double tolerance) {
    return distanceSquared(a, b) <= tolerance * tolerance;
}

QPointF rotateRadians(const QPointF& point, double radians) {
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return QPointF(point.x() * c - point.y() * s,
                   point.x() * s + point.y() * c);
}

QPointF rotate(const QPointF& point, double degrees) {
    return rotateRadians(point, degreesToRadians(degrees));
}

QPointF rotateScaleTranslate(const QPointF& point,
                             const QPointF& translate,
                             double rotationDegrees,
                             double scale) {
    const QPointF rotated = rotate(point * scale, rotationDegrees);
    return translate + rotated;
}

bool pointOnSegment(const QPointF& point,
                    const QPointF& segmentA,
                    const QPointF& segmentB,
                    double epsilon) {
    const QPointF ab = segmentB - segmentA;
    const QPointF ap = point - segmentA;
    const double abLen2 = lengthSquared(ab);

    if (abLen2 <= epsilon * epsilon) {
        return distanceSquared(point, segmentA) <= epsilon * epsilon;
    }

    if (std::abs(cross(ab, ap)) > epsilon) return false;

    const double projection = dot(ap, ab);
    if (projection < -epsilon) return false;
    if (projection > abLen2 + epsilon) return false;

    return true;
}

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

std::optional<QPointF> lineIntersectionByDirection(const QPointF& point,
                                                   const QPointF& direction,
                                                   const QPointF& lineA,
                                                   const QPointF& lineB,
                                                   double epsilon) {
    const QPointF lineDirection = lineB - lineA;
    const double det = cross(direction, lineDirection);
    if (std::abs(det) < epsilon) return std::nullopt;

    const QPointF delta = lineA - point;
    const double t = cross(delta, lineDirection) / det;
    return point + direction * t;
}

double pointSegmentDistance(const QPointF& point,
                            const QPointF& segmentA,
                            const QPointF& segmentB) {
    const QPointF segment = segmentB - segmentA;
    const double segmentLengthSquared = lengthSquared(segment);
    if (segmentLengthSquared <= 0.0) return distance(point, segmentA);

    const double t = std::clamp(dot(point - segmentA, segment) / segmentLengthSquared,
                                0.0,
                                1.0);
    const QPointF nearest = segmentA + segment * t;
    return distance(point, nearest);
}

double segmentDistance(const QPointF& a0,
                       const QPointF& a1,
                       const QPointF& b0,
                       const QPointF& b1) {
    QPointF ignored;
    if (QLineF(a0, a1).intersects(QLineF(b0, b1), &ignored)
        == QLineF::BoundedIntersection) {
        return 0.0;
    }

    return std::min({pointSegmentDistance(a0, b0, b1),
                     pointSegmentDistance(a1, b0, b1),
                     pointSegmentDistance(b0, a0, a1),
                     pointSegmentDistance(b1, a0, a1)});
}

bool pointInPolygon(const QVector<QPointF>& polygon, const QPointF& point) {
    int n = polygon.size();
    while (n > 1 && pointsNear(polygon.first(), polygon.at(n - 1), kGeometryEpsilon)) {
        --n;
    }

    if (n < 3) return false;

    bool inside = false;

    for (int i = 0, j = n - 1; i < n; j = i++) {
        const QPointF& a = polygon.at(j);
        const QPointF& b = polygon.at(i);

        if (pointOnSegment(point, a, b)) return true;

        const bool crossesY = (a.y() > point.y()) != (b.y() > point.y());
        if (!crossesY) continue;

        const double xCross =
            a.x() + (point.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
        if (point.x() < xCross) inside = !inside;
    }

    return inside;
}

QPointF polygonCentroid(const QPolygonF& polygon) {
    double twiceArea = 0.0;
    double cx = 0.0;
    double cy = 0.0;

    for (int i = 0; i < polygon.size(); ++i) {
        const QPointF a = polygon.at(i);
        const QPointF b = polygon.at((i + 1) % polygon.size());
        const double edgeCross = cross(a, b);
        twiceArea += edgeCross;
        cx += (a.x() + b.x()) * edgeCross;
        cy += (a.y() + b.y()) * edgeCross;
    }

    if (std::abs(twiceArea) > kGeometryEpsilon) {
        return QPointF(cx / (3.0 * twiceArea), cy / (3.0 * twiceArea));
    }

    QPointF average;
    for (const QPointF& point : polygon) average += point;
    return polygon.isEmpty() ? QPointF() : average / polygon.size();
}

QPointF polygonCentroid(const QVector<QPointF>& polygon) {
    return polygonCentroid(QPolygonF(polygon));
}

double polygonDistance(const QPolygonF& a, const QPolygonF& b) {
    if (a.isEmpty() || b.isEmpty()) return std::numeric_limits<double>::infinity();
    if (a.containsPoint(b.first(), Qt::OddEvenFill)
        || b.containsPoint(a.first(), Qt::OddEvenFill)) {
        return 0.0;
    }

    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < a.size(); ++i) {
        const QPointF a0 = a.at(i);
        const QPointF a1 = a.at((i + 1) % a.size());
        for (int j = 0; j < b.size(); ++j) {
            const double d = segmentDistance(a0, a1, b.at(j), b.at((j + 1) % b.size()));
            best = std::min(best, d);
            if (best <= 0.0) return 0.0;
        }
    }

    return best;
}

QVector<QPointF> normalizedPolygonRing(const QVector<QPointF>& points, double tolerance) {
    QVector<QPointF> ring;
    ring.reserve(points.size());

    for (const QPointF& point : points) {
        if (!ring.isEmpty() && pointsNear(ring.last(), point, tolerance)) continue;
        ring.push_back(point);
    }

    if (ring.size() > 1 && pointsNear(ring.first(), ring.last(), tolerance)) ring.removeLast();
    return ring;
}

bool rectsNear(const QRectF& a, const QRectF& b, double tolerance) {
    return !(a.right() + tolerance < b.left()
          || b.right() + tolerance < a.left()
          || a.bottom() + tolerance < b.top()
          || b.bottom() + tolerance < a.top());
}

} // namespace math
