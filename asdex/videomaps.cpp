#include "asdex/videomaps.h"

#include "utils/math.h"
#include "utils/resources.h"
#include "renderer/builders.h"
#include "renderer/commandbuffer.h"
#include "renderer/tessellator.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPolygonF>

#include <algorithm>
#include <optional>
#include <zlib.h>

namespace asdex {
namespace {

std::optional<VideoMap::Kind> classify(const QString& asdexKind) {
    if (asdexKind == QLatin1String("runway")) return VideoMap::Kind::Runway;
    if (asdexKind == QLatin1String("taxiway")) return VideoMap::Kind::Taxiway;
    if (asdexKind == QLatin1String("apron")) return VideoMap::Kind::Apron;
    if (asdexKind == QLatin1String("structure")) return VideoMap::Kind::Structure;
    if (asdexKind == QLatin1String("building")) return VideoMap::Kind::Structure;
    return std::nullopt;
}

float videoMapZ(VideoMap::Kind kind) {
    constexpr float base = -9.0f;

    switch (kind) {
        case VideoMap::Kind::Structure:
            return base + 0.4f;
        case VideoMap::Kind::Runway:
            return base + 0.3f;
        case VideoMap::Kind::Taxiway:
            return base + 0.2f;
        case VideoMap::Kind::Apron:
            return base + 0.1f;
    }

    return base;
}

QByteArray gunzip(const QByteArray& gz) {
    if (gz.isEmpty()) return {};

    z_stream stream{};
    if (inflateInit2(&stream, 32 + MAX_WBITS) != Z_OK) return {};

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(gz.constData()));
    stream.avail_in = static_cast<uInt>(gz.size());

    constexpr int kChunkSize = 64 * 1024;
    QByteArray out;
    out.reserve(gz.size() * 6);
    QByteArray chunk(kChunkSize, Qt::Uninitialized);

    int result = Z_OK;
    while (result != Z_STREAM_END) {
        stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream.avail_out = kChunkSize;
        result = inflate(&stream, Z_NO_FLUSH);

        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            return {};
        }

        out.append(chunk.constData(), kChunkSize - static_cast<int>(stream.avail_out));
    }

    inflateEnd(&stream);
    return out;
}

QPolygonF parseRing(const QJsonArray& coordinates) {
    QPolygonF polygon;
    polygon.reserve(coordinates.size());

    for (const QJsonValue& value : coordinates) {
        const QJsonArray point = value.toArray();
        if (point.size() < 2) continue;
        polygon << QPointF(point.at(0).toDouble(), point.at(1).toDouble());
    }

    if (polygon.size() >= 2) {
        const QPointF first = polygon.first();
        const QPointF last = polygon.last();
        const bool closed = qAbs(first.x() - last.x()) < 1e-12
                         && qAbs(first.y() - last.y()) < 1e-12;
        if (closed) polygon.removeLast();
    }

    return polygon;
}

void updateBounds(const QPolygonF& ring, QRectF& bounds, bool& firstRing) {
    if (ring.isEmpty()) return;

    const QRectF ringBounds = ring.boundingRect();
    bounds = firstRing ? ringBounds : bounds.united(ringBounds);
    firstRing = false;
}

struct PolygonRings {
    VideoMap::Kind kind = VideoMap::Kind::Apron;
    QVector<QPolygonF> rings;
};

void addRingToPolygon(PolygonRings& polygon,
                      const QPolygonF& ring,
                      QRectF& lonLatBounds,
                      bool& firstRing) {
    if (ring.isEmpty()) return;

    polygon.rings.append(ring);
    updateBounds(ring, lonLatBounds, firstRing);
}

void addPolygon(QVector<PolygonRings>& polygons,
                VideoMap::Kind kind,
                const QJsonArray& rings,
                QRectF& lonLatBounds,
                bool& firstRing) {
    PolygonRings polygon;
    polygon.kind = kind;

    for (const QJsonValue& ring : rings) {
        if (ring.isArray())
            addRingToPolygon(polygon, parseRing(ring.toArray()), lonLatBounds, firstRing);
    }

    if (!polygon.rings.isEmpty()) polygons.append(std::move(polygon));
}

VideoMap::Mesh tessellate(const PolygonRings& polygon) {
    VideoMap::Mesh mesh;
    mesh.kind = polygon.kind;
    mesh.z = videoMapZ(polygon.kind);

    const renderer::TessellatedPolygon tessellated = renderer::tessellatePolygon(polygon.rings);
    mesh.indices = tessellated.indices;
    mesh.vertices.reserve(tessellated.vertices.size());
    for (const QPointF& point : tessellated.vertices) {
        mesh.vertices.append(VideoMap::Vertex{float(point.x()), float(point.y())});
    }
    if (mesh.indices.isEmpty()) mesh.vertices.clear();
    return mesh;
}

} // namespace

VideoMap VideoMap::load(const QString& icao) {
    VideoMap map;

    const QString path = utils::videomapPath(icao);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "[renderer] cannot open videomap" << path << file.errorString();
        return map;
    }

    const QByteArray raw = gunzip(file.readAll());
    if (raw.isEmpty()) {
        qWarning().noquote() << "[renderer] gunzip failed for videomap" << path;
        return map;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning().noquote() << "[renderer] videomap JSON parse failed:" << error.errorString();
        return map;
    }

    QVector<PolygonRings> polygons;
    QRectF lonLatBounds;

    const QJsonArray features = document.object().value(QStringLiteral("features")).toArray();
    bool firstRing = true;

    for (const QJsonValue& featureValue : features) {
        const QJsonObject feature = featureValue.toObject();
        const QString asdexKind = feature.value(QStringLiteral("properties"))
                                      .toObject()
                                      .value(QStringLiteral("asdex"))
                                      .toString();
        const std::optional<VideoMap::Kind> kind = classify(asdexKind);
        if (!kind) continue;

        const QJsonObject geometry = feature.value(QStringLiteral("geometry")).toObject();
        const QString geometryType = geometry.value(QStringLiteral("type")).toString();
        const QJsonArray coordinates = geometry.value(QStringLiteral("coordinates")).toArray();

        if (geometryType == QLatin1String("Polygon")) {
            addPolygon(polygons, *kind, coordinates, lonLatBounds, firstRing);
            map.hasAny_ = true;
        } else if (geometryType == QLatin1String("MultiPolygon")) {
            for (const QJsonValue& polygon : coordinates) {
                if (polygon.isArray())
                    addPolygon(polygons, *kind, polygon.toArray(), lonLatBounds, firstRing);
            }
            map.hasAny_ = true;
        }
    }

    if (map.hasAny_) {
        map.anchor_ = lonLatBounds.center();
        const QTransform toFeet = utils::lonLatToFeet(map.anchor_);
        bool firstFeetRing = true;

        map.meshes_.reserve(polygons.size());
        for (PolygonRings& polygon : polygons) {
            for (QPolygonF& ring : polygon.rings) {
                ring = toFeet.map(ring);
                updateBounds(ring, map.bounds_, firstFeetRing);
            }

            VideoMap::Mesh mesh = tessellate(polygon);
            if (!mesh.indices.isEmpty()) map.meshes_.append(std::move(mesh));
        }

        std::sort(map.meshes_.begin(), map.meshes_.end(), [](const VideoMap::Mesh& a,
                                                             const VideoMap::Mesh& b) {
            return a.z < b.z;
        });
        map.hasAny_ = !map.meshes_.isEmpty();
    }

    return map;
}

QColor colorFor(VideoMap::Kind kind, Mode mode) {
    const bool day = mode == Mode::Day;
    QColor base;

    switch (kind) {
    case VideoMap::Kind::Runway:
        base = QColor(0, 0, 0);
        break;
    case VideoMap::Kind::Taxiway:
        base = day ? QColor(47, 47, 47) : QColor(17, 39, 80);
        break;
    case VideoMap::Kind::Apron:
        base = day ? QColor(73, 73, 73) : QColor(18, 55, 97);
        break;
    case VideoMap::Kind::Structure:
        base = day ? QColor(100, 100, 100) : QColor(34, 63, 103);
        break;
    }

    return applyBrightness(base);
}

void drawVideoMap(const VideoMap& map, renderer::CommandBuffer* commandBuffer, Mode mode) {
    if (!commandBuffer || !map.isValid()) return;

    for (const VideoMap::Mesh& mesh : map.meshes()) {
        if (mesh.vertices.isEmpty() || mesh.indices.isEmpty()) continue;

        QVector<QPointF> points;
        points.reserve(mesh.vertices.size());
        for (const VideoMap::Vertex& vertex : mesh.vertices) {
            points.push_back(QPointF(vertex.x, vertex.y));
        }

        commandBuffer->setRgba(renderer::RGBA::fromQColor(colorFor(mesh.kind, mode)));
        renderer::TrianglesBuilder* builder = renderer::getTrianglesBuilder();
        builder->addIndexed(points, mesh.indices);
        builder->generateCommands(commandBuffer);
        renderer::returnTrianglesBuilder(builder);
    }
}

} // namespace asdex
