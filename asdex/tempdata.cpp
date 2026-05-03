#include "tempdata.h"

#include "maths.h"

#include <QColor>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

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

void drawRunwayClosure(QPainter& p,
                       const QPolygonF& polyNm,
                       double headingMagDeg,
                       const QTransform& nmToScreen) {
    if (polyNm.size() < 4) return;

    constexpr double kClosureAngleDeg = 15.0;

    // Runway axis in NM (y = north). Heading θ rotates from N toward E so the
    // axis vector is (sinθ, cosθ); the transverse vector 90° to the right of
    // it is (cosθ, −sinθ). Magnetic heading is treated as true here — see the
    // header for why.
    const double thRad = qDegreesToRadians(headingMagDeg);
    const QPointF axis(std::sin(thRad),  std::cos(thRad));
    const QPointF perp(std::cos(thRad), -std::sin(thRad));

    // Pick the four "outermost" polygon vertices in (axis, perp) coords.
    // For a runway-shaped quadrilateral aligned with the heading, these are
    // the four actual corners; for a polygon with extra vertices they are
    // still the convex extremes in each runway-local quadrant.
    auto findCorner = [&](double axisSign, double perpSign) {
        QPointF best;
        double  bestScore = -std::numeric_limits<double>::infinity();
        for (const QPointF& v : polyNm) {
            const double score = axisSign * QPointF::dotProduct(v, axis)
                               + perpSign * QPointF::dotProduct(v, perp);
            if (score > bestScore) { bestScore = score; best = v; }
        }
        return best;
    };
    const QPointF cornerFL = findCorner(+1, -1);  // front, left of axis
    const QPointF cornerFR = findCorner(+1, +1);  // front, right
    const QPointF cornerBL = findCorner(-1, -1);  // back,  left
    const QPointF cornerBR = findCorner(-1, +1);  // back,  right

    // Runway width (perpendicular extent) — also the perp distance each X arm
    // has to cover from its corner to the opposite long edge.
    double perpMin =  std::numeric_limits<double>::infinity();
    double perpMax = -std::numeric_limits<double>::infinity();
    for (const QPointF& v : polyNm) {
        const double pp = QPointF::dotProduct(v, perp);
        perpMin = std::min(perpMin, pp);
        perpMax = std::max(perpMax, pp);
    }
    const double width = perpMax - perpMin;
    if (width <= 0.0) return;

    // Each arm leaves its corner at ±15° to axis, going inward, and stops
    // when it crosses the opposite long edge. With angle 15° to axis the
    // segment length is `width / sin(15°)` — that's the total path length
    // needed to traverse the full perpendicular extent at that slant.
    const double aRad   = qDegreesToRadians(kClosureAngleDeg);
    const double cosA   = std::cos(aRad);
    const double sinA   = std::sin(aRad);
    const double segLen = width / sinA;

    auto inward = [&](double signAxis, double signPerp) {
        return signAxis * cosA * axis + signPerp * sinA * perp;
    };

    struct Seg { QPointF start; QPointF dir; };
    const Seg segs[4] = {
        { cornerFL, inward(-1, +1) },  // FL → toward back-right
        { cornerFR, inward(-1, -1) },  // FR → toward back-left   (crosses FL's arm at front-end midline)
        { cornerBL, inward(+1, +1) },  // BL → toward front-right
        { cornerBR, inward(+1, -1) },  // BR → toward front-left  (crosses BL's arm at back-end midline)
    };

    p.save();
    QPen pen(QColor(255, 255, 255));
    pen.setCosmetic(true);
    pen.setWidthF(1.0);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    for (const Seg& s : segs) {
        const QPointF endNm = s.start + s.dir * segLen;
        p.drawLine(nmToScreen.map(s.start), nmToScreen.map(endNm));
    }

    p.restore();
}

} // namespace asdex
