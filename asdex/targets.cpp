#include "targets.h"

#include <QColor>
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

constexpr double kHeavyScale = 1.5;

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

void drawTarget(QPainter& p, const QTransform& nmToScreen,
                  const QPointF& posNm, double headingDeg, TargetType type) {
    QPolygonF screen;
    QColor    fill;
    switch (type) {
        case TargetType::Normal:
            screen = nmToScreen.map(rotatedPolygonNm(kPolygonTgt, posNm, headingDeg, 1.0));
            fill   = QColor(248, 248, 248);
            break;
        case TargetType::Heavy:
            screen = nmToScreen.map(rotatedPolygonNm(kPolygonTgt, posNm, headingDeg, kHeavyScale));
            fill   = QColor(248, 128, 0);
            break;
        case TargetType::Unknown:
            screen = nmToScreen.map(rotatedPolygonNm(kPolygonUnkTgt, posNm, headingDeg, 1.0));
            fill   = QColor(0, 255, 255);
            break;
    }

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPolygon(screen);
    p.restore();
}

} // namespace asdex
