#include "renderer/videomap.h"

#include "renderer/asdex_math.h"
#include "renderer/asdex_resources.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPolygonF>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>
#include <zlib.h>

#ifdef Q_OS_MACOS
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif

#ifndef CALLBACK
#define CALLBACK
#endif

namespace renderer::asdex {
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

struct TessVertex {
    GLdouble coordinates[3] = {};
    std::uint32_t index = 0;
};

struct TessContext {
    VideoMap::Mesh mesh;
    std::vector<std::unique_ptr<TessVertex>> ownedVertices;
    QVector<std::uint32_t> primitive;
    GLenum primitiveType = GL_TRIANGLES;
};

void appendTriangle(TessContext& context,
                    std::uint32_t a,
                    std::uint32_t b,
                    std::uint32_t c) {
    context.mesh.indices.append(a);
    context.mesh.indices.append(b);
    context.mesh.indices.append(c);
}

void flushPrimitive(TessContext& context) {
    const QVector<std::uint32_t>& p = context.primitive;

    if (context.primitiveType == GL_TRIANGLES) {
        for (qsizetype i = 0; i + 2 < p.size(); i += 3)
            appendTriangle(context, p.at(i), p.at(i + 1), p.at(i + 2));
    } else if (context.primitiveType == GL_TRIANGLE_FAN) {
        for (qsizetype i = 2; i < p.size(); ++i)
            appendTriangle(context, p.at(0), p.at(i - 1), p.at(i));
    } else if (context.primitiveType == GL_TRIANGLE_STRIP) {
        for (qsizetype i = 2; i < p.size(); ++i) {
            if ((i % 2) == 0)
                appendTriangle(context, p.at(i - 2), p.at(i - 1), p.at(i));
            else
                appendTriangle(context, p.at(i - 1), p.at(i - 2), p.at(i));
        }
    }

    context.primitive.clear();
}

void CALLBACK tessBegin(GLenum type, void* polygonData) {
    auto* context = static_cast<TessContext*>(polygonData);
    context->primitiveType = type;
    context->primitive.clear();
}

void CALLBACK tessVertex(void* vertexData, void* polygonData) {
    auto* context = static_cast<TessContext*>(polygonData);
    auto* vertex = static_cast<TessVertex*>(vertexData);
    context->primitive.append(vertex->index);
}

void CALLBACK tessEnd(void* polygonData) {
    flushPrimitive(*static_cast<TessContext*>(polygonData));
}

void CALLBACK tessCombine(GLdouble coordinates[3],
                          void* vertexData[4],
                          GLfloat weight[4],
                          void** outData,
                          void* polygonData) {
    Q_UNUSED(vertexData);
    Q_UNUSED(weight);

    auto* context = static_cast<TessContext*>(polygonData);
    auto vertex = std::make_unique<TessVertex>();
    vertex->coordinates[0] = coordinates[0];
    vertex->coordinates[1] = coordinates[1];
    vertex->coordinates[2] = coordinates[2];
    vertex->index = static_cast<std::uint32_t>(context->mesh.vertices.size());
    context->mesh.vertices.append(VideoMap::Vertex{static_cast<float>(coordinates[0]),
                                                   static_cast<float>(coordinates[1])});
    *outData = vertex.get();
    context->ownedVertices.push_back(std::move(vertex));
}

void CALLBACK tessError(GLenum error, void* polygonData) {
    Q_UNUSED(polygonData);
    qWarning().noquote() << "[renderer] GLU tessellation failed:" << gluErrorString(error);
}

VideoMap::Mesh tessellate(const PolygonRings& polygon) {
    TessContext context;
    context.mesh.kind = polygon.kind;
    context.mesh.z = videoMapZ(polygon.kind);

    GLUtesselator* tess = gluNewTess();
    if (!tess) return context.mesh;

    gluTessProperty(tess, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
    gluTessCallback(tess, GLU_TESS_BEGIN_DATA, reinterpret_cast<void (CALLBACK*)()>(tessBegin));
    gluTessCallback(tess, GLU_TESS_VERTEX_DATA, reinterpret_cast<void (CALLBACK*)()>(tessVertex));
    gluTessCallback(tess, GLU_TESS_END_DATA, reinterpret_cast<void (CALLBACK*)()>(tessEnd));
    gluTessCallback(tess, GLU_TESS_COMBINE_DATA, reinterpret_cast<void (CALLBACK*)()>(tessCombine));
    gluTessCallback(tess, GLU_TESS_ERROR_DATA, reinterpret_cast<void (CALLBACK*)()>(tessError));

    gluTessBeginPolygon(tess, &context);
    for (const QPolygonF& ring : polygon.rings) {
        if (ring.size() < 3) continue;

        gluTessBeginContour(tess);
        for (const QPointF& point : ring) {
            auto vertex = std::make_unique<TessVertex>();
            vertex->coordinates[0] = point.x();
            vertex->coordinates[1] = point.y();
            vertex->coordinates[2] = context.mesh.z;
            vertex->index = static_cast<std::uint32_t>(context.mesh.vertices.size());
            context.mesh.vertices.append(VideoMap::Vertex{static_cast<float>(point.x()),
                                                          static_cast<float>(point.y())});

            TessVertex* rawVertex = vertex.get();
            context.ownedVertices.push_back(std::move(vertex));
            gluTessVertex(tess, rawVertex->coordinates, rawVertex);
        }
        gluTessEndContour(tess);
    }
    gluTessEndPolygon(tess);
    gluDeleteTess(tess);

    if (context.mesh.indices.isEmpty()) context.mesh.vertices.clear();
    return context.mesh;
}

} // namespace

VideoMap VideoMap::load(const QString& icao) {
    VideoMap map;

    const QString path = videomapPath(icao);
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
        const QTransform toFeet = lonLatToFeet(map.anchor_);
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

} // namespace renderer::asdex
