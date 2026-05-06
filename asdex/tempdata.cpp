#include "tempdata.h"

#include "maths.h"
#include "utils.h"

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QProcess>
#include <QRectF>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace asdex {

namespace {

// applyBrightness is on the 1..99 integer scale (kBrightnessMax = 99). 95
// = the kBrightnessDefault — same brightness the videomap, targets, and
// vector lines pick up via defaultBrightness() so the closure overlays
// match the rest of the scope.
constexpr int kTempDataBrightness = 95;

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

namespace {

// Long-axis direction for a 4-corner runway rectangle. Among the 6 pairwise
// distances of 4 corners, sorted ascending we expect (W, W, L, L, D, D);
// index 2 is one of the two long edges, whose direction = the runway axis.
// Robust against magnetic-vs-true heading mismatch and arbitrary corner
// ordering, which a heading-driven axis at e.g. KSFO (~14° declination)
// gets badly wrong — the perp projection there gets dominated by length
// instead of width and the segment length explodes.
QPointF longAxisDir(const QPolygonF& c) {
    if (c.size() != 4) return QPointF(0.0, 1.0);

    struct Edge { int i, j; double distSq; };
    Edge edges[6];
    int k = 0;
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            const QPointF d = c[j] - c[i];
            edges[k++] = { i, j, d.x() * d.x() + d.y() * d.y() };
        }
    }
    std::sort(edges, edges + 6,
              [](const Edge& a, const Edge& b) { return a.distSq < b.distSq; });

    // edges[0..1] are the short edges (W), [2..3] long (L), [4..5] diagonals.
    const QPointF v   = c[edges[2].j] - c[edges[2].i];
    const double  len = std::hypot(v.x(), v.y());
    return (len > 0.0) ? QPointF(v.x() / len, v.y() / len) : QPointF(0.0, 1.0);
}

} // namespace

void drawRunwayClosure(QPainter& p,
                       const QPolygonF& cornersNm,
                       const QTransform& nmToScreen) {
    if (cornersNm.size() != 4) return;

    constexpr double kClosureAngleDeg = 15.0;

    const QPointF axis = longAxisDir(cornersNm);
    const QPointF perp(axis.y(), -axis.x());  // 90° to the right of axis

    // With exactly 4 corners + an axis derived from the polygon's own long
    // edge, each (±axis, ±perp) extremum picks a unique, real corner.
    auto findCorner = [&](double axisSign, double perpSign) {
        QPointF best;
        double  bestScore = -std::numeric_limits<double>::infinity();
        for (const QPointF& v : cornersNm) {
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
    for (const QPointF& v : cornersNm) {
        const double pp = QPointF::dotProduct(v, perp);
        perpMin = std::min(perpMin, pp);
        perpMax = std::max(perpMax, pp);
    }
    const double width = perpMax - perpMin;
    if (width <= 0.0) return;

    // Each arm leaves its corner at ±15° to axis, going inward, and stops
    // when it crosses the opposite long edge. With angle 15° to axis the
    // segment length is `width / sin(15°)` — total path length to traverse
    // the full perpendicular extent at that slant.
    const double aRad   = qDegreesToRadians(kClosureAngleDeg);
    const double cosA   = std::cos(aRad);
    const double sinA   = std::sin(aRad);
    const double segLen = width / sinA;

    auto inward = [&](double signAxis, double signPerp) {
        return signAxis * cosA * axis + signPerp * sinA * perp;
    };

    struct Seg { QPointF start; QPointF dir; };
    const Seg segs[4] = {
        { cornerFL, inward(-1, +1) },  // FL → back-right (crosses FR's arm at front-end midline)
        { cornerFR, inward(-1, -1) },  // FR → back-left
        { cornerBL, inward(+1, +1) },  // BL → front-right (crosses BR's arm at rollout-end midline)
        { cornerBR, inward(+1, -1) },  // BR → front-left
    };

    p.save();
    // Pure white, no brightness scaling — closure markings are spec'd at
    // 100% intensity to stay legible against the runway fill.
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

// ===========================================================================
// ClosureCache — drives the NOTAM scraper subprocess, parses its compact
// JSON output, joins it with the static airport surface data, and exposes a
// list of NM-space render items the scope's paintEvent feeds into the helpers
// above. v1 only resolves runway closures — taxiway closures are recorded by
// the scraper (visible in the cache JSON) but skipped here.
// ===========================================================================

namespace {

constexpr int kRefreshIntervalMs = 30 * 60 * 1000;  // 30 min
constexpr char kSurfaceDirRel[]  = "asdex/surface";
constexpr char kScraperRel[]     = "reader/notams/scrape.py";

QString surfacePathFor(const QString& icao) {
    return QStringLiteral("%1/%2.json").arg(QString::fromLatin1(kSurfaceDirRel), icao);
}

} // namespace

ClosureCache::ClosureCache(QObject* parent)
    : QObject(parent) {
    refreshTimer_.setInterval(kRefreshIntervalMs);
    connect(&refreshTimer_, &QTimer::timeout, this, &ClosureCache::kickScrape);
}

void ClosureCache::switchAirport(const QString& icao, QPointF anchorLonLat) {
    icao_         = icao;
    anchorLonLat_ = anchorLonLat;

    rwys_.clear();
    rwyClosures_.clear();
    items_.clear();

    if (icao.isEmpty()) {
        emit changed();
        return;
    }

    loadSurface(icao, anchorLonLat);
    // No on-disk cache: we just have an empty closure list until the first
    // scrape completes (~15 s after switchAirport). Emit changed() now so
    // the scope repaints with the new airport's surface in scope, and
    // again from onScrapeFinished when closures arrive.
    emit changed();

    refreshTimer_.start();
    kickScrape();
}

void ClosureCache::loadSurface(const QString& icao, QPointF anchorLonLat) {
    const QString path = surfacePathFor(icao);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "[closures] no surface data for" << icao
                             << "—" << f.errorString();
        return;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning().noquote() << "[closures] surface json parse:" << err.errorString();
        return;
    }

    const QTransform lonLatT = lonLatToNm(anchorLonLat);
    const QJsonArray rwys    = doc.object().value(QStringLiteral("rwys")).toArray();
    for (const QJsonValue& v : rwys) {
        const QJsonObject obj = v.toObject();
        const QString     id  = obj.value(QStringLiteral("id")).toString();
        const QJsonArray  pts = obj.value(QStringLiteral("polygon")).toArray();
        if (id.isEmpty() || pts.size() != 4) continue;  // expect exactly 4 corners

        QPolygonF lonLat;
        lonLat.reserve(4);
        for (const QJsonValue& p : pts) {
            const QJsonArray a = p.toArray();
            if (a.size() < 2) continue;
            lonLat << QPointF(a.at(0).toDouble(), a.at(1).toDouble());
        }
        if (lonLat.size() != 4) continue;

        rwys_.insert(id, lonLatT.map(lonLat));
    }

    qDebug().noquote() << "[closures]" << icao << "surface loaded:"
                       << rwys_.size() << "runway(s)";
}

void ClosureCache::parseScrapeOutput(const QByteArray& bytes) {
    rwyClosures_.clear();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning().noquote() << "[closures] scrape json parse:" << err.errorString();
        return;
    }

    const QJsonArray rwys = doc.object().value(QStringLiteral("rwyClosures")).toArray();
    for (const QJsonValue& v : rwys) {
        const QString id = v.toString();
        if (!id.isEmpty()) rwyClosures_ << id;
    }
}

void ClosureCache::rebuildItems() {
    items_.clear();
    items_.reserve(rwyClosures_.size());
    for (const QString& id : rwyClosures_) {
        const auto it = rwys_.constFind(id);
        if (it == rwys_.constEnd()) {
            qDebug().noquote() << "[closures] no surface match for runway closure" << id;
            continue;
        }
        items_.append(it.value());
    }
}

void ClosureCache::kickScrape() {
    if (icao_.isEmpty()) return;

    auto* proc = new QProcess(this);
    proc->setProgram(QStringLiteral("python3"));
    // `--output -` writes the compact JSON payload to stdout; we read it
    // back in onScrapeFinished and parse without ever touching the disk.
    proc->setArguments({
        QString::fromLatin1(kScraperRel),
        icao_,
        QStringLiteral("--output"),
        QStringLiteral("-"),
    });

    connect(proc, &QProcess::finished, this,
            [this, proc](int exitCode, QProcess::ExitStatus /*status*/) {
                onScrapeFinished(proc, exitCode);
            });

    proc->start();
}

void ClosureCache::onScrapeFinished(QProcess* proc, int exitCode) {
    if (exitCode == 0) {
        parseScrapeOutput(proc->readAllStandardOutput());
        rebuildItems();
        emit changed();
        qDebug().noquote() << "[closures]" << icao_ << "scrape ok:"
                           << items_.size() << "rendered closure(s)";
    } else {
        const QByteArray serr = proc->readAllStandardError().trimmed();
        qWarning().noquote() << "[closures]" << icao_
                             << "scrape failed (exit" << exitCode << ")"
                             << QString::fromLocal8Bit(serr);
    }
    proc->deleteLater();
}

} // namespace asdex
