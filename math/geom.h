#ifndef MATH_GEOM_H_
#define MATH_GEOM_H_

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QVector>

#include <optional>

namespace math {

double dot(const QPointF& a, const QPointF& b);
double cross(const QPointF& a, const QPointF& b);
double lengthSquared(const QPointF& v);
double length(const QPointF& v);
double distanceSquared(const QPointF& a, const QPointF& b);
double distance(const QPointF& a, const QPointF& b);
bool pointsNear(const QPointF& a, const QPointF& b, double tolerance);

QPointF rotate(const QPointF& point, double degrees);
QPointF rotateRadians(const QPointF& point, double radians);
QPointF rotateScaleTranslate(const QPointF& point,
                             const QPointF& translate,
                             double rotationDegrees,
                             double scale);

bool pointOnSegment(const QPointF& point,
                    const QPointF& segmentA,
                    const QPointF& segmentB,
                    double epsilon = 1e-9);

bool lineSegmentsIntersect(const QPointF& a,
                           const QPointF& b,
                           const QPointF& c,
                           const QPointF& d);

std::optional<QPointF> lineIntersectionByDirection(const QPointF& point,
                                                   const QPointF& direction,
                                                   const QPointF& lineA,
                                                   const QPointF& lineB,
                                                   double epsilon = 1e-9);

double pointSegmentDistance(const QPointF& point,
                            const QPointF& segmentA,
                            const QPointF& segmentB);
double segmentDistance(const QPointF& a0,
                       const QPointF& a1,
                       const QPointF& b0,
                       const QPointF& b1);

bool pointInPolygon(const QVector<QPointF>& polygon, const QPointF& point);

QPointF polygonCentroid(const QPolygonF& polygon);
QPointF polygonCentroid(const QVector<QPointF>& polygon);
double polygonDistance(const QPolygonF& a, const QPolygonF& b);

QVector<QPointF> normalizedPolygonRing(const QVector<QPointF>& points,
                                       double tolerance = 1e-9);

bool rectsNear(const QRectF& a, const QRectF& b, double tolerance);

} // namespace math

#endif  // MATH_GEOM_H_
