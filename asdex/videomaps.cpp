#include "videomaps.h"

#include "brightness.h"
#include "maths.h"

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPolygonF>

#include <optional>

#include <zlib.h>

namespace asdex {

namespace {

QColor colorFor(VideoMap::Kind k, Mode m) {
    const bool day = (m == Mode::Day);
    QColor base;
    switch (k) {
        case VideoMap::Kind::Runway:    base = QColor(0, 0, 0); break;
        case VideoMap::Kind::Taxiway:   base = day ? QColor(47, 47, 47)   : QColor(17, 39, 80);  break;
        case VideoMap::Kind::Apron:     base = day ? QColor(73, 73, 73)   : QColor(18, 55, 97);  break;
        case VideoMap::Kind::Structure: base = day ? QColor(100, 100, 100): QColor(34, 63, 103); break;
    }
    return applyBrightness(base, defaultBrightness());
}

std::optional<VideoMap::Kind> classify(const QString& asdex) {
    if (asdex == QLatin1String("runway"))    return VideoMap::Kind::Runway;
    if (asdex == QLatin1String("taxiway"))   return VideoMap::Kind::Taxiway;
    if (asdex == QLatin1String("apron"))     return VideoMap::Kind::Apron;
    if (asdex == QLatin1String("structure")) return VideoMap::Kind::Structure;
    if (asdex == QLatin1String("building"))  return VideoMap::Kind::Structure;
    return std::nullopt;
}

QByteArray gunzip(const QByteArray& gz) {
    if (gz.isEmpty()) return {};

    z_stream s{};
    if (inflateInit2(&s, 32 + MAX_WBITS) != Z_OK) return {};  // auto gzip/zlib

    s.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(gz.constData()));
    s.avail_in = static_cast<uInt>(gz.size());

    constexpr int kChunk = 64 * 1024;
    QByteArray out;
    out.reserve(gz.size() * 6);
    QByteArray chunk(kChunk, Qt::Uninitialized);

    int rc;
    do {
        s.next_out  = reinterpret_cast<Bytef*>(chunk.data());
        s.avail_out = kChunk;
        rc = inflate(&s, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) { inflateEnd(&s); return {}; }
        out.append(chunk.constData(), kChunk - static_cast<int>(s.avail_out));
    } while (rc != Z_STREAM_END);

    inflateEnd(&s);
    return out;
}

QPolygonF parseRing(const QJsonArray& coords) {
    QPolygonF poly;
    poly.reserve(coords.size());
    for (const auto& v : coords) {
        const QJsonArray pt = v.toArray();
        if (pt.size() < 2) continue;
        poly << QPointF(pt.at(0).toDouble(), pt.at(1).toDouble());
    }
    return poly;
}

void addRingToPath(QPainterPath& path, const QPolygonF& ring, QRectF& bounds, bool& firstRing) {
    if (ring.isEmpty()) return;
    path.addPolygon(ring);
    path.closeSubpath();
    const QRectF rb = ring.boundingRect();
    bounds = firstRing ? rb : bounds.united(rb);
    firstRing = false;
}

void addPolygonToPath(QPainterPath& path, const QJsonArray& rings, QRectF& bounds, bool& firstRing) {
    for (const auto& r : rings) {
        if (r.isArray()) addRingToPath(path, parseRing(r.toArray()), bounds, firstRing);
    }
}

} // namespace

VideoMap VideoMap::load(const QString& icao) {
    VideoMap m;

    const QString path = QStringLiteral("resources/videomaps/asdex/%1.geojson.gz").arg(icao);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[videomap] cannot open" << path << f.errorString();
        return m;
    }

    const QByteArray raw = gunzip(f.readAll());
    if (raw.isEmpty()) {
        qWarning() << "[videomap] gunzip failed for" << icao;
        return m;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[videomap] json parse:" << err.errorString();
        return m;
    }

    // OddEvenFill handles GeoJSON holes correctly regardless of ring winding.
    for (auto& p : m.paths_) p.setFillRule(Qt::OddEvenFill);

    const QJsonArray features = doc.object().value("features").toArray();
    bool firstRing = true;

    for (const auto& fv : features) {
        const QJsonObject obj  = fv.toObject();
        const auto kind = classify(obj.value("properties").toObject().value("asdex").toString());
        if (!kind) continue;

        QPainterPath& dest = m.paths_[static_cast<int>(*kind)];
        const QJsonObject geom = obj.value("geometry").toObject();
        const QString     gt   = geom.value("type").toString();
        const QJsonArray  c    = geom.value("coordinates").toArray();

        if (gt == QLatin1String("Polygon")) {
            addPolygonToPath(dest, c, m.bounds_, firstRing);
            m.hasAny_ = true;
        } else if (gt == QLatin1String("MultiPolygon")) {
            for (const auto& poly : c) {
                if (poly.isArray()) addPolygonToPath(dest, poly.toArray(), m.bounds_, firstRing);
            }
            m.hasAny_ = true;
        }
        // LineStrings / Points: not part of the filled surface palette.
    }

    if (m.hasAny_) {
        // Reproject everything from lon/lat into local NM anchored at the map
        // centroid. Downstream code (scope, targets) operates in NM only.
        m.anchor_ = m.bounds_.center();
        const QTransform t = lonLatToNm(m.anchor_);
        for (auto& p : m.paths_) {
            if (!p.isEmpty()) p = t.map(p);
        }
        m.bounds_ = t.mapRect(m.bounds_);
    }

    return m;
}

void VideoMap::render(QPainter& p, const QTransform& nmToScreen, Mode mode) const {
    if (!hasAny_) return;

    p.setPen(Qt::NoPen);

    // Map each path to screen coords ourselves (identity painter transform).
    // Qt's raster engine can silently drop fills whose post-transform bbox
    // exceeds its internal fixed-point range at high zoom; doing the transform
    // up-front keeps the coords Qt actually rasterizes bounded by the clip rect.
    for (int i = 0; i < kKindCount; ++i) {
        if (paths_[i].isEmpty()) continue;
        p.setBrush(colorFor(static_cast<Kind>(i), mode));
        p.drawPath(nmToScreen.map(paths_[i]));
    }
}

} // namespace asdex
