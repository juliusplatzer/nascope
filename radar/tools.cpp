#include "radar/tools.h"

#include "math/core.h"

#include <algorithm>
#include <cmath>

namespace radar {

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

ScopeTransform::ScopeTransform(ScopeView view, ScopeViewport viewport)
    : view_(view),
      viewport_(viewport) {}

double ScopeTransform::pixelsPerFoot() const {
    if (viewport_.framebufferSize.isEmpty() || view_.halfRangeFeet <= 0.0) return 1.0;

    const double availW = viewport_.framebufferSize.width() * (1.0 - 2.0 * kViewportMargin);
    const double availH = viewport_.framebufferSize.height() * (1.0 - 2.0 * kViewportMargin);
    const double radiusPx = 0.5 * std::min(availW, availH);
    return radiusPx / view_.halfRangeFeet;
}

QPointF ScopeTransform::worldToFramebufferTopLeft(QPointF worldFeet) const {
    const double ppf = pixelsPerFoot();
    const double theta = math::degreesToRadians(math::normalizedDegrees(view_.rotationDegrees));
    const double c = std::cos(theta);
    const double s = std::sin(theta);

    const double dx = worldFeet.x() - view_.centerFeet.x();
    const double dy = worldFeet.y() - view_.centerFeet.y();
    const double rx = c * dx - s * dy;
    const double ry = s * dx + c * dy;

    return QPointF(viewport_.framebufferSize.width() * 0.5 + rx * ppf,
                   viewport_.framebufferSize.height() * 0.5 - ry * ppf);
}

QPointF ScopeTransform::worldToLogicalScreen(QPointF worldFeet) const {
    const QPointF framebufferPoint = worldToFramebufferTopLeft(worldFeet);
    return QPointF(framebufferPoint.x() / viewport_.devicePixelRatio,
                   framebufferPoint.y() / viewport_.devicePixelRatio);
}

QPointF ScopeTransform::logicalToFramebuffer(QPointF logicalPoint) const {
    return QPointF(logicalPoint.x() * viewport_.devicePixelRatio,
                   logicalPoint.y() * viewport_.devicePixelRatio);
}

QPointF ScopeTransform::logicalScreenToWorld(QPointF logicalPoint) const {
    const QPointF point = logicalToFramebuffer(logicalPoint);
    const double ppf = pixelsPerFoot();

    if (ppf <= 0.0) return view_.centerFeet;

    const double rx = (point.x() - viewport_.framebufferSize.width() * 0.5) / ppf;
    const double ry = (viewport_.framebufferSize.height() * 0.5 - point.y()) / ppf;

    const double theta = math::degreesToRadians(math::normalizedDegrees(view_.rotationDegrees));
    const double c = std::cos(theta);
    const double s = std::sin(theta);

    const double dx = c * rx + s * ry;
    const double dy = -s * rx + c * ry;

    return QPointF(view_.centerFeet.x() + dx, view_.centerFeet.y() + dy);
}

QPointF ScopeTransform::framebufferDeltaToWorldDelta(QPointF framebufferDelta) const {
    const double ppf = pixelsPerFoot();
    if (ppf <= 0.0) return QPointF();

    const double rx = framebufferDelta.x() / ppf;
    const double ry = -framebufferDelta.y() / ppf;

    const double theta = math::degreesToRadians(math::normalizedDegrees(view_.rotationDegrees));
    const double c = std::cos(theta);
    const double s = std::sin(theta);

    const double dx = c * rx + s * ry;
    const double dy = -s * rx + c * ry;
    return QPointF(dx, dy);
}

QMatrix4x4 ScopeTransform::worldProjection() const {
    QMatrix4x4 matrix;
    matrix.setToIdentity();
    if (viewport_.framebufferSize.isEmpty() || view_.halfRangeFeet <= 0.0) return matrix;

    const double ppf = pixelsPerFoot();
    const double sx = 2.0 * ppf / viewport_.framebufferSize.width();
    const double sy = 2.0 * ppf / viewport_.framebufferSize.height();

    const double theta = math::degreesToRadians(math::normalizedDegrees(view_.rotationDegrees));
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    const double cx = view_.centerFeet.x();
    const double cy = view_.centerFeet.y();

    matrix(0, 0) = static_cast<float>(sx * c);
    matrix(0, 1) = static_cast<float>(-sx * s);
    matrix(0, 3) = static_cast<float>(sx * (-c * cx + s * cy));
    matrix(1, 0) = static_cast<float>(sy * s);
    matrix(1, 1) = static_cast<float>(sy * c);
    matrix(1, 3) = static_cast<float>(sy * (-s * cx - c * cy));
    return matrix;
}

} // namespace radar
