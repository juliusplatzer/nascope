#ifndef RADAR_TOOLS_H_
#define RADAR_TOOLS_H_

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QTransform>

namespace radar {

inline constexpr double kViewportMargin = 0.04;

QTransform nmToScreen(const QPointF& centerNm, double halfRangeNm, const QSize& view);

struct ScopeView {
    QPointF centerFeet;
    double halfRangeFeet = 1.0;
    int rotationDegrees = 0;
};

struct ScopeViewport {
    QSize framebufferSize;
    double devicePixelRatio = 1.0;
};

class ScopeTransform {
public:
    ScopeTransform(ScopeView view, ScopeViewport viewport);

    double pixelsPerFoot() const;

    QPointF worldToFramebufferTopLeft(QPointF worldFeet) const;
    QPointF worldToLogicalScreen(QPointF worldFeet) const;
    QPointF logicalToFramebuffer(QPointF logicalPoint) const;
    QPointF logicalScreenToWorld(QPointF logicalPoint) const;
    QPointF framebufferDeltaToWorldDelta(QPointF framebufferDelta) const;

    QMatrix4x4 worldProjection() const;

private:
    ScopeView view_;
    ScopeViewport viewport_;
};

} // namespace radar

#endif  // RADAR_TOOLS_H_
