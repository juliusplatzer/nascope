#include "targets.h"

#include <QColor>
#include <QDateTime>
#include <QPolygonF>

#include <array>
#include <cmath>

namespace asdex {

namespace {

struct GeoPoint { double x, y; };

// Aircraft target outline, in *degrees* of lat/lon from the aircraft ref point
// (despite the source's NM labeling — the magnitudes only make sense as
// degrees; see kDegToNm). Nose at +Y (north) at heading 0.
constexpr double kDegToNm = 60.0;  // 1° of latitude ≈ 60 NM

constexpr std::array<GeoPoint, 23> kPolygonTgt = {{
    {  0.0,          -0.000142545  },
    {  0.0000209625, -0.0001607125 },
    {  0.000069875,  -0.0001607125 },
    {  0.000069875,  -0.000142545  },
    {  0.00002795,   -0.0001118    },
    {  0.00002795,   -0.0000559    },
    {  0.000151293,  -0.00008385   },
    {  0.000151293,  -0.0000559    },
    {  0.00002795,    0.000013975  },
    {  0.0000238175,  0.000120185  },
    {  0.0000153625,  0.000137155  },
    {  0.000008385,   0.0001439425 },
    { -0.000008385,   0.0001439425 },
    { -0.0000153625,  0.000137155  },
    { -0.0000238175,  0.000120185  },
    { -0.00002795,    0.000013975  },
    { -0.000151293,  -0.0000559    },
    { -0.000151293,  -0.00008385   },
    { -0.00002795,   -0.0000559    },
    { -0.00002795,   -0.0001118    },
    { -0.000069875,  -0.000142545  },
    { -0.000069875,  -0.0001607125 },
    { -0.0000209625, -0.0001607125 },
}};

// Unknown / non-aircraft target — kite pointing at +Y (north) at heading 0.
constexpr std::array<GeoPoint, 4> kPolygonUnkTgt = {{
    {  0.000075, -0.000025 },
    {  0.0,      -0.000125 },
    { -0.000075, -0.000025 },
    {  0.0,       0.000175 },
}};

constexpr double kHeavyScale     = 1.5;
constexpr qint64 kAlertPeriodMs  = 1000;  // full flash cycle

// Wall-clock derived so all alert targets rendered in the same frame (and
// across widgets, in case we ever render more than one) see the same phase.
bool alertRedPhase() {
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    return (ms % kAlertPeriodMs) < (kAlertPeriodMs / 2);
}

template <std::size_t N>
QPolygonF rotatedPolygonNm(const std::array<GeoPoint, N>& pts,
                           const QPointF& posNm, double headingDeg, double scale) {
    const double h = headingDeg * M_PI / 180.0;
    const double c = std::cos(h);
    const double s = std::sin(h);
    const double k = kDegToNm * scale;
    // Heading is CW from north; for a point (x, y) in the target's local NM
    // frame (nose at +Y), the world NM coords are:
    //   xw = xt + x*cos(h) + y*sin(h)
    //   yw = yt - x*sin(h) + y*cos(h)
    QPolygonF out;
    out.reserve(pts.size());
    for (const auto& pt : pts) {
        const double x = pt.x * k;
        const double y = pt.y * k;
        const double xw = posNm.x() + x * c + y * s;
        const double yw = posNm.y() - x * s + y * c;
        out << QPointF(xw, yw);
    }
    return out;
}

} // namespace

void drawHistoryDots(QPainter& p, const QTransform& nmToScreen,
                     const QList<QPointF>& historyPosNm) {
    constexpr double kHistRadiusNm = 0.003;   // ~18 ft — distinctly smaller than a target symbol
    constexpr int    kMaxDots      = 7;
    // Grey gradient indexed by *age*: kGrays[0] is the newest (lightest), kGrays[6]
    // the oldest (dimmest). With fewer than 7 dots we use only the bright end.
    static constexpr int kGrays[kMaxDots] = { 219, 187, 161, 138, 118, 101, 87 };

    const int n = std::min(static_cast<int>(historyPosNm.size()), kMaxDots);
    if (n == 0) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < n; ++i) {
        // historyPosNm[0] = oldest, [n-1] = newest. Color is by distance-from-newest
        // so the just-dropped point gets kGrays[0] and the trail darkens going back.
        const int g = kGrays[(n - 1) - i];
        const QPointF& posNm = historyPosNm.at(i);
        const QPointF centerPx = nmToScreen.map(posNm);
        const QPointF edgePx   = nmToScreen.map(posNm + QPointF(kHistRadiusNm, 0));
        const double  radiusPx = std::hypot(edgePx.x() - centerPx.x(),
                                            edgePx.y() - centerPx.y());
        p.setBrush(QColor(g, g, g));
        p.drawEllipse(centerPx, radiusPx, radiusPx);
    }
    p.restore();
}

void drawHighlightRing(QPainter& p, const QTransform& nmToScreen,
                       const QPointF& posNm) {
    constexpr double kHighlightRadiusNm = 0.012;  // ~73 ft — matches the 150 ft pick radius

    // Derive the on-screen radius from the transform itself so any future
    // rotation / non-uniform scale in nmToScreen stays consistent.
    const QPointF centerPx = nmToScreen.map(posNm);
    const QPointF edgePx   = nmToScreen.map(posNm + QPointF(kHighlightRadiusNm, 0));
    const double  radiusPx = std::hypot(edgePx.x() - centerPx.x(),
                                        edgePx.y() - centerPx.y());

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(255, 255, 255), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(centerPx, radiusPx, radiusPx);
    p.restore();
}

void drawTarget(QPainter& p, const QTransform& nmToScreen,
                const QPointF& posNm, double headingDeg, TargetType type, bool alert) {
    QPolygonF screen;
    QColor    typeFill;
    switch (type) {
        case TargetType::Normal:
            screen   = nmToScreen.map(rotatedPolygonNm(kPolygonTgt, posNm, headingDeg, 1.0));
            typeFill = QColor(248, 248, 248);
            break;
        case TargetType::Heavy:
            screen   = nmToScreen.map(rotatedPolygonNm(kPolygonTgt, posNm, headingDeg, kHeavyScale));
            typeFill = QColor(248, 128, 0);
            break;
        case TargetType::Unknown:
            screen   = nmToScreen.map(rotatedPolygonNm(kPolygonUnkTgt, posNm, headingDeg, 1.0));
            typeFill = QColor(0, 255, 255);
            break;
    }

    const QColor fill = (alert && alertRedPhase()) ? QColor(255, 0, 0) : typeFill;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPolygon(screen);
    p.restore();
}

} // namespace asdex
