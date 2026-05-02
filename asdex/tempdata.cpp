#include "tempdata.h"

#include "maths.h"

#include <QColor>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>

#include <cmath>

namespace asdex {

namespace {

constexpr double kTempDataBrightness = 0.95;

// Hatch parameters — match CRC's fragment shader literally so the visual
// cadence (slope, stripe thickness, gap) is identical.
constexpr int kHatchYScale  = 4;   // x-pixels of slant per y-pixel
constexpr int kHatchSpacing = 50;  // distance between successive stripes
constexpr int kHatchWidth   = 5;   // thickness of each stripe (along x)

QColor tempDataColor() {
    return applyBrightness(QColor(255, 0, 0), kTempDataBrightness);
}

} // namespace

void drawRestrictionArea(QPainter& p, const QPolygonF& polyNm, const QTransform& nmToScreen) {
    if (polyNm.size() < 3) return;

    const QPolygonF poly  = nmToScreen.map(polyNm);
    const QColor    color = tempDataColor();

    p.save();
    // CRC's hatch is per-pixel discard; aliased rendering here keeps the
    // stripe edges crisp instead of fading them.
    p.setRenderHint(QPainter::Antialiasing, false);

    // Outline pass — 1px red around the polygon's outer ring.
    QPen pen(color);
    pen.setCosmetic(true);
    pen.setWidthF(1.0);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(poly);

    // Hatch pass — clip to the polygon, then sweep parallelogram bands across
    // its screen-space bbox. Each band corresponds to one filled stripe of
    // CRC's shader: { (x,y) : 50k ≤ offset + x − 4y ≤ 50k + 5 }.
    QPainterPath clipPath;
    clipPath.addPolygon(poly);
    clipPath.closeSubpath();
    p.setClipPath(clipPath, Qt::IntersectClip);

    // Per-area phase, anchored at the first polygon point in screen coords.
    // Matches CRC's `offset = -((4*p.Y + p.X) % 50)`. Purpose is phase-lock:
    // as the area pans the stripes pan with it instead of sliding past.
    const QPointF p0 = poly.first();
    const double  offset = -std::fmod(kHatchYScale * p0.y() + p0.x(), kHatchSpacing);

    const QRectF bbox    = poly.boundingRect();
    const double yTop    = bbox.top();
    const double yBottom = bbox.bottom();
    const double xLeft   = bbox.left();
    const double xRight  = bbox.right();

    // Range of stripe indices whose band intersects the bbox. At y=yTop the
    // band's leading edge is xMin = 50k − offset + 4·yTop, which must be
    // ≤ xRight; at y=yBottom the trailing edge xMax = 50k − offset + 5 +
    // 4·yBottom must be ≥ xLeft. Solving for k:
    const int kMin = static_cast<int>(std::floor(
        (xLeft  - kHatchYScale * yBottom - kHatchWidth + offset) / kHatchSpacing));
    const int kMax = static_cast<int>(std::ceil(
        (xRight - kHatchYScale * yTop                  + offset) / kHatchSpacing));

    p.setPen(Qt::NoPen);
    p.setBrush(color);

    for (int k = kMin; k <= kMax; ++k) {
        const double base = static_cast<double>(k) * kHatchSpacing - offset;
        QPolygonF band;
        band << QPointF(base                + kHatchYScale * yTop,    yTop)
             << QPointF(base + kHatchWidth  + kHatchYScale * yTop,    yTop)
             << QPointF(base + kHatchWidth  + kHatchYScale * yBottom, yBottom)
             << QPointF(base                + kHatchYScale * yBottom, yBottom);
        p.drawPolygon(band);
    }

    p.restore();
}

} // namespace asdex
