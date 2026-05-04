#include "targets.h"

#include <QColor>
#include <QDateTime>
#include <QLatin1Char>
#include <QPolygonF>
#include <QString>

#include <array>
#include <cmath>

#include "utils.h"

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

constexpr double kHeavyScale       = 1.5;
constexpr qint64 kAlertPeriodMs    = 1000;  // full flash cycle
constexpr qint64 kTimeShareCycleMs = 8000;  // line-2 SP ↕ F/G/H/I cycle (4 s each phase)

// Wall-clock derived so all alert targets rendered in the same frame (and
// across widgets, in case we ever render more than one) see the same phase.
bool alertRedPhase() {
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    return (ms % kAlertPeriodMs) < (kAlertPeriodMs / 2);
}

// True during the "scratchpad" half of the line-2 cycle. Same wall-clock
// trick as alertRedPhase so every datablock on screen flips in unison.
bool scratchpadPhase() {
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    return (ms % kTimeShareCycleMs) >= (kTimeShareCycleMs / 2);
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
        p.setBrush(applyBrightness(QColor(g, g, g), defaultBrightness()));
        p.drawEllipse(centerPx, radiusPx, radiusPx);
    }
    p.restore();
}

void drawVectorLine(QPainter& p, const QTransform& nmToScreen,
                    const QPointF& targetPosNm,
                    double headingDeg, double speedKts, int vectorSeconds) {
    if (speedKts <= 0.0) return;
    vectorSeconds = std::clamp(vectorSeconds, 1, 20);

    const double lengthNm = speedKts * vectorSeconds / 3600.0;

    // Compass heading θ (CW from north) → NM unit vector (sin θ, cos θ),
    // since +y in NM is north (cf. nmToScreen's `scale(pxPerNm, -pxPerNm)`).
    const double rad = headingDeg * M_PI / 180.0;
    const double dx  = std::sin(rad);
    const double dy  = std::cos(rad);
    const QPointF endNm = targetPosNm + QPointF(dx * lengthNm, dy * lengthNm);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(applyBrightness(QColor(140, 140, 140), defaultBrightness()), 1.0);
    pen.setStyle(Qt::SolidLine);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawLine(nmToScreen.map(targetPosNm), nmToScreen.map(endNm));
    p.restore();
}

QPointF drawLeaderLine(QPainter& p, const QTransform& nmToScreen,
                       const QPointF& targetPosNm,
                       double angleDeg, int lengthSteps) {
    constexpr double kStartOffsetPx      = 7.0;
    constexpr double kStepLengthPx       = 15.0;
    constexpr double kZeroLengthAnchorPx = 10.0;

    // Compass bearing → screen unit vector. Screen y grows downward, north is up.
    const double rad = angleDeg * M_PI / 180.0;
    const double dx  =  std::sin(rad);
    const double dy  = -std::cos(rad);

    const QPointF targetPx = nmToScreen.map(targetPosNm);

    if (lengthSteps <= 0) {
        return targetPx + QPointF(dx * kZeroLengthAnchorPx, dy * kZeroLengthAnchorPx);
    }

    const double endDist  = kStartOffsetPx + lengthSteps * kStepLengthPx;
    const QPointF startPx = targetPx + QPointF(dx * kStartOffsetPx, dy * kStartOffsetPx);
    const QPointF endPx   = targetPx + QPointF(dx * endDist,        dy * endDist);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(applyBrightness(QColor(0, 208, 0), defaultBrightness()), 1.0);
    pen.setStyle(Qt::SolidLine);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawLine(startPx, endPx);
    p.restore();

    return endPx;
}

void drawHighlightRing(QPainter& p, const QTransform& nmToScreen,
                       const QPointF& posNm, bool heavy) {
    constexpr double kHighlightRadiusNm = 0.012;  // ~73 ft — matches the 150 ft pick radius
    const double radiusNm = heavy ? kHighlightRadiusNm * kHeavyScale : kHighlightRadiusNm;

    // Derive the on-screen radius from the transform itself so any future
    // rotation / non-uniform scale in nmToScreen stays consistent.
    const QPointF centerPx = nmToScreen.map(posNm);
    const QPointF edgePx   = nmToScreen.map(posNm + QPointF(radiusNm, 0));
    const double  radiusPx = std::hypot(edgePx.x() - centerPx.x(),
                                        edgePx.y() - centerPx.y());

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(applyBrightness(QColor(255, 255, 255), defaultBrightness()), 1));
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
    p.setBrush(applyBrightness(fill, defaultBrightness()));
    p.drawPolygon(screen);
    p.restore();
}

namespace {

// CRC-style: append " <token>" only if `token` is non-empty.
void appendField(QString& sb, const QString& token) {
    if (!token.isEmpty()) sb += QLatin1Char(' ') + token;
}

QString formatHundreds(std::optional<int> ft) {
    if (!ft) return QStringLiteral("XXX");
    const int hund = std::clamp(*ft / 100, 0, 999);
    return QStringLiteral("%1").arg(hund, 3, 10, QLatin1Char('0'));
}

QString formatTens(std::optional<int> kt) {
    if (!kt) return QString();
    const int tens = std::clamp(*kt / 10, 0, 99);
    return QStringLiteral("%1").arg(tens, 2, 10, QLatin1Char('0'));
}

bool isLeftLeader(double angleDeg) {
    // Round to the nearest 45 ° step and check against SW/W/NW.
    const int rounded = ((static_cast<int>(std::round(angleDeg)) % 360) + 360) % 360;
    return rounded == 225 || rounded == 270 || rounded == 315;
}

} // namespace

void drawDatablock(QPainter& p, BitmapFontRenderer& font,
                   const QPointF& anchorPx, double leaderAngleDeg,
                   const DatablockFields& f,
                   DatablockKind kind, int fontSize) {
    if (!font.isValid()) return;

    const bool full = (kind == DatablockKind::Full);

    // ---- Build line strings ------------------------------------------------
    // Each line builds a StringBuilder-style buffer with leading spaces between
    // fields, then trims — disabled / missing fields collapse without leaving
    // placeholder gaps.

    QString line0;
    if (f.dupBeacon) line0 = QStringLiteral("DUP BCN");

    QString line1 = f.hasFlightPlan ? f.callsign : f.beacon;  // fieldB or fieldC
    if (full) {
        line1 += QLatin1Char(' ') + formatHundreds(f.altitudeFt);          // fieldD
        line1 += f.coasted ? QStringLiteral(" CST") : QStringLiteral(" FUS");  // fieldE
    }
    line1 = line1.trimmed();

    // Build both candidates for line 2 (F/G/H/I set vs. SP1 SP2). When both
    // halves are populated, the wall-clock phase picks which one this frame
    // shows; when only one side has content, that side wins unconditionally
    // (no "blink to blank" if the user has set scratchpads on a target with
    // no aircraft type / wake / fix / speed).
    QString line2Fields;
    appendField(line2Fields, f.acType);                              // fieldF (always)
    if (full) appendField(line2Fields, f.category);                  // fieldG (Full only)
    appendField(line2Fields, f.exitFix);                             // fieldH (always)
    if (full) appendField(line2Fields, formatTens(f.speedKt));       // fieldI (Full only)
    line2Fields = line2Fields.trimmed();

    QString line2Sp;
    appendField(line2Sp, f.sp1);                                     // fieldJ (scratchpad 1)
    appendField(line2Sp, f.sp2);                                     // fieldK (scratchpad 2)
    line2Sp = line2Sp.trimmed();

    QString line2;
    if (line2Sp.isEmpty())          line2 = line2Fields;
    else if (line2Fields.isEmpty()) line2 = line2Sp;
    else                            line2 = scratchpadPhase() ? line2Sp : line2Fields;

    const QString lines[3] = { line0, line1, line2 };

    // ---- Measure -----------------------------------------------------------
    const int height = font.lineHeight(fontSize);
    int widths[3] = {0, 0, 0};
    int maxLineWidth   = 0;
    int longestLineIdx = 0;  // ties keep the topmost (CRC's "longestHighest" path)
    for (int i = 0; i < 3; ++i) {
        widths[i] = font.measureText(lines[i], fontSize).width();
        if (widths[i] > maxLineWidth) {
            maxLineWidth   = widths[i];
            longestLineIdx = i;
        }
    }
    if (maxLineWidth == 0) return;  // nothing to draw

    // ---- Position ----------------------------------------------------------
    constexpr int kLineSpacingPx = 2;     // gap between datablock lines (lists use 5)
    const     int lineStep       = height + kLineSpacingPx;

    const bool isLeft = isLeftLeader(leaderAngleDeg);

    // Right-default: leader endpoint sits at the centre of line 1 (middle).
    int x = static_cast<int>(std::round(anchorPx.x() + 2));
    int y = static_cast<int>(std::round(anchorPx.y() - height * 3.0 / 2.0 - kLineSpacingPx));

    if (isLeft) {
        x = static_cast<int>(std::round(anchorPx.x() - 2 - maxLineWidth));
        // Vertical correction so the longest line aligns with the leader endpoint
        // (CRC's `num4`). Only the != N branch is reachable here — we never
        // produce a left datablock for a north-pointing leader.
        const int verticalCorrection = lineStep * (-1 + longestLineIdx);
        y -= verticalCorrection;
    }

    // ---- Render ------------------------------------------------------------
    const QColor color = applyBrightness(QColor(0, 208, 0), defaultBrightness());
    for (int i = 0; i < 3; ++i) {
        if (lines[i].isEmpty()) continue;
        font.drawTextTopLeft(p, x, y + i * lineStep, lines[i], fontSize, color);
    }
}

} // namespace asdex
