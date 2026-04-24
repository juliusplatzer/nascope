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

QPolygonF rotatedPolygonNm(const QPointF& posNm, double headingDeg) {
    const double h = headingDeg * M_PI / 180.0;
    const double c = std::cos(h);
    const double s = std::sin(h);
    // Heading is CW from north; for a point (x, y) in the aircraft's local NM
    // frame (nose at +Y), the world NM coords are:
    //   xw = xt + x*cos(h) + y*sin(h)
    //   yw = yt - x*sin(h) + y*cos(h)
    QPolygonF out;
    out.reserve(kPolygonTgt.size());
    for (const auto& pt : kPolygonTgt) {
        const double x = pt.x * kDegToNm;
        const double y = pt.y * kDegToNm;
        const double xw = posNm.x() + x * c + y * s;
        const double yw = posNm.y() - x * s + y * c;
        out << QPointF(xw, yw);
    }
    return out;
}

} // namespace

void drawAircraft(QPainter& p, const QTransform& nmToScreen,
                  const QPointF& posNm, double headingDeg) {
    const QPolygonF screen = nmToScreen.map(rotatedPolygonNm(posNm, headingDeg));

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(248, 248, 248));
    p.drawPolygon(screen);
    p.restore();
}

} // namespace asdex
