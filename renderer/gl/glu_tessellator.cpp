#include "renderer/geometry/tessellator.h"

#include <QDebug>

#include <memory>
#include <vector>

#ifdef Q_OS_MACOS
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif

#ifndef CALLBACK
#define CALLBACK
#endif

namespace renderer {
namespace {

struct TessVertex {
    GLdouble coordinates[3] = {};
    std::uint32_t index = 0;
};

struct TessContext {
    TessellatedPolygon polygon;
    std::vector<std::unique_ptr<TessVertex>> ownedVertices;
    QVector<std::uint32_t> primitive;
    GLenum primitiveType = GL_TRIANGLES;
};

void appendTriangle(TessContext& context,
                    std::uint32_t a,
                    std::uint32_t b,
                    std::uint32_t c) {
    context.polygon.indices.append(a);
    context.polygon.indices.append(b);
    context.polygon.indices.append(c);
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
    vertex->coordinates[2] = 0.0;
    vertex->index = std::uint32_t(context->polygon.vertices.size());
    context->polygon.vertices.append(QPointF(coordinates[0], coordinates[1]));
    *outData = vertex.get();
    context->ownedVertices.push_back(std::move(vertex));
}

void CALLBACK tessError(GLenum error, void* polygonData) {
    Q_UNUSED(polygonData);
    qWarning().noquote() << "[renderer] GLU tessellation failed:" << gluErrorString(error);
}

} // namespace

TessellatedPolygon tessellatePolygon(const QVector<QPolygonF>& rings) {
    TessContext context;
    if (rings.isEmpty()) return context.polygon;

    GLUtesselator* tess = gluNewTess();
    if (!tess) return context.polygon;

    gluTessProperty(tess, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
    gluTessCallback(tess, GLU_TESS_BEGIN_DATA, reinterpret_cast<void (CALLBACK*)()>(tessBegin));
    gluTessCallback(tess, GLU_TESS_VERTEX_DATA, reinterpret_cast<void (CALLBACK*)()>(tessVertex));
    gluTessCallback(tess, GLU_TESS_END_DATA, reinterpret_cast<void (CALLBACK*)()>(tessEnd));
    gluTessCallback(tess, GLU_TESS_COMBINE_DATA, reinterpret_cast<void (CALLBACK*)()>(tessCombine));
    gluTessCallback(tess, GLU_TESS_ERROR_DATA, reinterpret_cast<void (CALLBACK*)()>(tessError));

    gluTessBeginPolygon(tess, &context);
    for (const QPolygonF& ring : rings) {
        if (ring.size() < 3) continue;

        gluTessBeginContour(tess);
        for (const QPointF& point : ring) {
            auto vertex = std::make_unique<TessVertex>();
            vertex->coordinates[0] = point.x();
            vertex->coordinates[1] = point.y();
            vertex->coordinates[2] = 0.0;
            vertex->index = std::uint32_t(context.polygon.vertices.size());
            context.polygon.vertices.append(point);

            TessVertex* raw = vertex.get();
            context.ownedVertices.push_back(std::move(vertex));
            gluTessVertex(tess, raw->coordinates, raw);
        }
        gluTessEndContour(tess);
    }
    gluTessEndPolygon(tess);
    gluDeleteTess(tess);

    if (context.polygon.indices.isEmpty()) context.polygon.vertices.clear();
    return context.polygon;
}

TessellatedPolygon tessellateSimplePolygon(const QVector<QPointF>& points) {
    QVector<QPolygonF> rings;
    rings.append(QPolygonF(points));
    return tessellatePolygon(rings);
}

} // namespace renderer
