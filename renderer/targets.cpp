#include "renderer/targets.h"

#include "renderer/asdex_math.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QVector4D>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

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

constexpr double kFeetPerDegree = 364560.0;
constexpr int kMinTargetVectorSeconds = 1;
constexpr int kMaxTargetVectorSeconds = 20;
constexpr int kHistoryColors[] = {219, 187, 161, 138, 118, 101, 87};

constexpr char kVertexShader[] = R"(
#version 330 core
layout(location = 0) in vec2 a_position;

uniform mat4 u_projection;
uniform mat4 u_model;

void main() {
    gl_Position = u_projection * u_model * vec4(a_position, 0.0, 1.0);
}
)";

constexpr char kFragmentShader[] = R"(
#version 330 core

uniform vec4 u_color;

out vec4 fragColor;

void main() {
    fragColor = u_color;
}
)";

QVector4D colorVector(const QColor& color) {
    return QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF());
}

QColor normalTargetColor() {
    return applyBrightness(QColor(248, 248, 248));
}

QColor heavyTargetColor() {
    return applyBrightness(QColor(248, 128, 0));
}

QColor unknownTargetColor() {
    return applyBrightness(QColor(0, 255, 255));
}

QColor vectorColor() {
    return applyBrightness(QColor(140, 140, 140));
}

QColor highlightColor() {
    return applyBrightness(QColor(255, 255, 255));
}

QPointF vectorEndFeet(const QPointF& start,
                      double groundSpeedKnots,
                      double trackDegrees,
                      double vectorMinutes) {
    const double distanceNm = groundSpeedKnots * vectorMinutes / 60.0;
    const double distanceFeet = distanceNm * kFeetPerNm;
    const double rad = trackDegrees * M_PI / 180.0;

    return QPointF(start.x() + distanceFeet * std::sin(rad),
                   start.y() + distanceFeet * std::cos(rad));
}

QMatrix4x4 targetModel(const QPointF& positionFeet,
                       double headingDegrees,
                       double scale) {
    QMatrix4x4 model;
    model.translate(float(positionFeet.x()), float(positionFeet.y()));
    model.rotate(float(90.0 - headingDegrees), 0.0f, 0.0f, 1.0f);
    model.scale(float(scale), float(scale), 1.0f);
    return model;
}

QMatrix4x4 ringModel(const QPointF& positionFeet, double scale) {
    QMatrix4x4 model;
    model.translate(float(positionFeet.x()), float(positionFeet.y()));
    model.scale(float(scale), float(scale), 1.0f);
    return model;
}

QVector<QPointF> aircraftShapeFeet() {
    const std::pair<double, double> points[] = {
        {-0.000142545, 0.0},
        {-0.0001607125, 2.09625E-05},
        {-0.0001607125, 6.9875E-05},
        {-0.000142545, 6.9875E-05},
        {-0.0001118, 2.795E-05},
        {-5.59E-05, 2.795E-05},
        {-8.385E-05, 0.000151293},
        {-5.59E-05, 0.000151293},
        {1.3975E-05, 2.795E-05},
        {0.000120185, 2.38175E-05},
        {0.000137155, 1.53625E-05},
        {0.0001439425, 8.385E-06},
        {0.0001439425, -8.385E-06},
        {0.000137155, -1.53625E-05},
        {0.000120185, -2.38175E-05},
        {1.3975E-05, -2.795E-05},
        {-5.59E-05, -0.000151293},
        {-8.385E-05, -0.000151293},
        {-5.59E-05, -2.795E-05},
        {-0.0001118, -2.795E-05},
        {-0.000142545, -6.9875E-05},
        {-0.0001607125, -6.9875E-05},
        {-0.0001607125, -2.09625E-05},
    };

    QVector<QPointF> vertices;
    vertices.reserve(int(std::size(points)));

    for (const auto& [x, y] : points) {
        vertices.push_back(QPointF(x * kFeetPerDegree, y * kFeetPerDegree));
    }

    return vertices;
}

QVector<QPointF> unknownShapeFeet() {
    const std::pair<double, double> points[] = {
        {-2.5E-05,   7.5E-05},
        {-0.000125,  0.0},
        {-2.5E-05,  -7.5E-05},
        { 0.000175,  0.0},
    };

    QVector<QPointF> vertices;
    vertices.reserve(int(std::size(points)));

    for (const auto& [x, y] : points) {
        vertices.push_back(QPointF(x * kFeetPerDegree, y * kFeetPerDegree));
    }

    return vertices;
}

QVector<std::uint32_t> triangleFanIndices(int vertexCount) {
    QVector<std::uint32_t> indices;
    if (vertexCount < 3) return indices;

    indices.reserve((vertexCount - 2) * 3);
    for (int i = 1; i + 1 < vertexCount; ++i) {
        indices.push_back(0);
        indices.push_back(std::uint32_t(i));
        indices.push_back(std::uint32_t(i + 1));
    }

    return indices;
}

QVector<QPointF> circleShapeFeet(double radiusFeet, int segments) {
    QVector<QPointF> vertices;
    vertices.reserve(segments + 1);

    vertices.push_back(QPointF(0.0, 0.0));
    for (int i = 0; i <= segments; ++i) {
        const double a = 2.0 * M_PI * double(i) / double(segments);
        vertices.push_back(QPointF(radiusFeet * std::cos(a), radiusFeet * std::sin(a)));
    }

    return vertices;
}

QVector<std::uint32_t> circleFanIndices(int segments) {
    QVector<std::uint32_t> indices;
    indices.reserve(segments * 3);

    for (int i = 1; i <= segments; ++i) {
        indices.push_back(0);
        indices.push_back(std::uint32_t(i));
        indices.push_back(std::uint32_t(i + 1));
    }

    return indices;
}

QVector<QPointF> regularRingFeet(int sides, double radiusFeet) {
    QVector<QPointF> points;
    points.reserve(sides + 1);

    for (int i = 0; i <= sides; ++i) {
        const double a = 2.0 * M_PI * double(i) / double(sides);
        points.push_back(QPointF(radiusFeet * std::cos(a),
                                 radiusFeet * std::sin(a)));
    }

    return points;
}

struct TessVertex {
    GLdouble coordinates[3] = {};
    std::uint32_t index = 0;
};

struct TessContext {
    QVector<QPointF> vertices;
    QVector<std::uint32_t> indices;
    std::vector<std::unique_ptr<TessVertex>> ownedVertices;
    QVector<std::uint32_t> primitive;
    GLenum primitiveType = GL_TRIANGLES;
};

void appendTriangle(TessContext& context,
                    std::uint32_t a,
                    std::uint32_t b,
                    std::uint32_t c) {
    context.indices.append(a);
    context.indices.append(b);
    context.indices.append(c);
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
    vertex->index = static_cast<std::uint32_t>(context->vertices.size());
    context->vertices.append(QPointF(coordinates[0], coordinates[1]));
    *outData = vertex.get();
    context->ownedVertices.push_back(std::move(vertex));
}

void CALLBACK tessError(GLenum error, void* polygonData) {
    Q_UNUSED(polygonData);
    qWarning().noquote() << "[renderer] target tessellation failed:" << gluErrorString(error);
}

bool tessellateSimplePolygon(const QVector<QPointF>& points,
                             QVector<QPointF>* vertices,
                             QVector<std::uint32_t>* indices) {
    if (points.size() < 3) return false;

    TessContext context;
    GLUtesselator* tess = gluNewTess();
    if (!tess) return false;

    gluTessProperty(tess, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
    gluTessCallback(tess, GLU_TESS_BEGIN_DATA, reinterpret_cast<void (CALLBACK*)()>(tessBegin));
    gluTessCallback(tess, GLU_TESS_VERTEX_DATA, reinterpret_cast<void (CALLBACK*)()>(tessVertex));
    gluTessCallback(tess, GLU_TESS_END_DATA, reinterpret_cast<void (CALLBACK*)()>(tessEnd));
    gluTessCallback(tess, GLU_TESS_COMBINE_DATA, reinterpret_cast<void (CALLBACK*)()>(tessCombine));
    gluTessCallback(tess, GLU_TESS_ERROR_DATA, reinterpret_cast<void (CALLBACK*)()>(tessError));

    gluTessBeginPolygon(tess, &context);
    gluTessBeginContour(tess);
    for (const QPointF& point : points) {
        auto vertex = std::make_unique<TessVertex>();
        vertex->coordinates[0] = point.x();
        vertex->coordinates[1] = point.y();
        vertex->coordinates[2] = 0.0;
        vertex->index = static_cast<std::uint32_t>(context.vertices.size());
        context.vertices.append(point);

        TessVertex* rawVertex = vertex.get();
        context.ownedVertices.push_back(std::move(vertex));
        gluTessVertex(tess, rawVertex->coordinates, rawVertex);
    }
    gluTessEndContour(tess);
    gluTessEndPolygon(tess);
    gluDeleteTess(tess);

    if (context.indices.isEmpty()) return false;

    *vertices = std::move(context.vertices);
    *indices = std::move(context.indices);
    return true;
}

} // namespace

TargetRenderer::TargetRenderer() = default;

TargetRenderer::~TargetRenderer() = default;

void TargetRenderer::initialize() {
    initializeShaders();
    if (!ready_) return;

    uploadAircraftMesh();
    uploadUnknownMesh();
    uploadHistoryDotMesh();
    uploadHighlightRingMesh();

    lineVao_.create();
    QOpenGLVertexArrayObject::Binder lineBinder(&lineVao_);

    lineVbo_.create();
    lineVbo_.bind();

    shader_.bind();
    shader_.enableAttributeArray(0);
    shader_.setAttributeBuffer(0, GL_FLOAT, 0, 2, sizeof(Vertex));
    shader_.release();

    lineVbo_.release();
}

void TargetRenderer::setVectorSeconds(int seconds) {
    vectorSeconds_ = std::clamp(seconds, kMinTargetVectorSeconds, kMaxTargetVectorSeconds);
}

void TargetRenderer::deinitialize() {
    aircraftMesh_.vao.destroy();
    aircraftMesh_.vbo.destroy();
    aircraftMesh_.ebo.destroy();
    aircraftMesh_.indexCount = 0;

    unknownMesh_.vao.destroy();
    unknownMesh_.vbo.destroy();
    unknownMesh_.ebo.destroy();
    unknownMesh_.indexCount = 0;

    historyDotMesh_.vao.destroy();
    historyDotMesh_.vbo.destroy();
    historyDotMesh_.ebo.destroy();
    historyDotMesh_.indexCount = 0;

    highlightRingMesh_.vao.destroy();
    highlightRingMesh_.vbo.destroy();
    highlightRingMesh_.vertexCount = 0;

    lineVao_.destroy();
    lineVbo_.destroy();

    shader_.removeAllShaders();
    ready_ = false;
}

void TargetRenderer::initializeShaders() {
    if (!shader_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
        || !shader_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)
        || !shader_.link()) {
        qWarning().noquote() << "[renderer] target shader setup failed:" << shader_.log();
        ready_ = false;
        return;
    }

    ready_ = true;
}

void TargetRenderer::uploadPointMesh(Mesh& mesh,
                                     const QVector<QPointF>& points,
                                     const QVector<std::uint32_t>& indices) {
    QVector<Vertex> vertices;
    vertices.reserve(points.size());

    for (const QPointF& point : points) {
        vertices.push_back(Vertex{float(point.x()), float(point.y())});
    }

    uploadMesh(mesh, vertices, indices);
}

void TargetRenderer::uploadMesh(Mesh& mesh,
                                const QVector<Vertex>& vertices,
                                const QVector<std::uint32_t>& indices) {
    if (vertices.isEmpty() || indices.isEmpty()) return;

    mesh.vao.create();
    QOpenGLVertexArrayObject::Binder binder(&mesh.vao);

    mesh.vbo.create();
    mesh.vbo.bind();
    mesh.vbo.allocate(vertices.constData(), int(vertices.size() * sizeof(Vertex)));

    mesh.ebo.create();
    mesh.ebo.bind();
    mesh.ebo.allocate(indices.constData(), int(indices.size() * sizeof(std::uint32_t)));

    shader_.bind();
    shader_.enableAttributeArray(0);
    shader_.setAttributeBuffer(0, GL_FLOAT, 0, 2, sizeof(Vertex));
    shader_.release();

    mesh.indexCount = indices.size();
}

void TargetRenderer::uploadLineMesh(LineMesh& mesh, const QVector<QPointF>& points) {
    if (points.size() < 2) return;

    QVector<Vertex> vertices;
    vertices.reserve(points.size());

    for (const QPointF& point : points) {
        vertices.push_back(Vertex{float(point.x()), float(point.y())});
    }

    mesh.vao.create();
    QOpenGLVertexArrayObject::Binder binder(&mesh.vao);

    mesh.vbo.create();
    mesh.vbo.bind();
    mesh.vbo.allocate(vertices.constData(), int(vertices.size() * sizeof(Vertex)));

    shader_.bind();
    shader_.enableAttributeArray(0);
    shader_.setAttributeBuffer(0, GL_FLOAT, 0, 2, sizeof(Vertex));
    shader_.release();

    mesh.vertexCount = vertices.size();
    mesh.vbo.release();
}

void TargetRenderer::uploadAircraftMesh() {
    const QVector<QPointF> points = aircraftShapeFeet();
    QVector<QPointF> vertices;
    QVector<std::uint32_t> indices;

    if (!tessellateSimplePolygon(points, &vertices, &indices)) {
        vertices = points;
        indices = triangleFanIndices(vertices.size());
    }

    uploadPointMesh(aircraftMesh_, vertices, indices);
}

void TargetRenderer::uploadUnknownMesh() {
    const QVector<QPointF> vertices = unknownShapeFeet();
    uploadPointMesh(unknownMesh_, vertices, triangleFanIndices(vertices.size()));
}

void TargetRenderer::uploadHistoryDotMesh() {
    constexpr double kHistoryDotRadiusFeet = 0.003 * kFeetPerNm;
    constexpr int kSegments = 12;

    const QVector<QPointF> vertices = circleShapeFeet(kHistoryDotRadiusFeet, kSegments);
    uploadPointMesh(historyDotMesh_, vertices, circleFanIndices(kSegments));
}

void TargetRenderer::uploadHighlightRingMesh() {
    constexpr int kHighlightRingSides = 20;
    constexpr double kHighlightRingRadiusFeet = 0.012 * kFeetPerNm;

    uploadLineMesh(highlightRingMesh_,
                   regularRingFeet(kHighlightRingSides, kHighlightRingRadiusFeet));
}

void TargetRenderer::drawMesh(Mesh& mesh,
                              const QMatrix4x4& projection,
                              const QMatrix4x4& model,
                              const QColor& color) {
    if (mesh.indexCount <= 0) return;

    shader_.bind();
    shader_.setUniformValue("u_projection", projection);
    shader_.setUniformValue("u_model", model);
    shader_.setUniformValue("u_color", colorVector(color));

    QOpenGLVertexArrayObject::Binder binder(&mesh.vao);
    mesh.ebo.bind();

    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    f->glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);

    shader_.release();
}

void TargetRenderer::drawLineMesh(LineMesh& mesh,
                                  const QMatrix4x4& projection,
                                  const QMatrix4x4& model,
                                  const QColor& color,
                                  float width) {
    if (mesh.vertexCount <= 0) return;

    shader_.bind();
    shader_.setUniformValue("u_projection", projection);
    shader_.setUniformValue("u_model", model);
    shader_.setUniformValue("u_color", colorVector(color));

    QOpenGLVertexArrayObject::Binder binder(&mesh.vao);

    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    f->glLineWidth(width);
    f->glDrawArrays(GL_LINE_STRIP, 0, mesh.vertexCount);

    shader_.release();
}

void TargetRenderer::renderTargetSymbols(const QVector<AsdexTarget>& targets,
                                         const QMatrix4x4& projection,
                                         Mode mode) {
    Q_UNUSED(mode);

    for (const AsdexTarget& target : targets) {
        if (target.correlated) {
            const double scale = target.heavy ? 1.5 : 1.0;
            const QColor color = target.heavy ? heavyTargetColor() : normalTargetColor();

            drawMesh(aircraftMesh_,
                     projection,
                     targetModel(target.positionFeet, target.headingDegrees, scale),
                     color);
        } else {
            drawMesh(unknownMesh_,
                     projection,
                     targetModel(target.positionFeet, target.groundTrackDegrees, 1.0),
                     unknownTargetColor());
        }
    }
}

void TargetRenderer::renderHighlightRings(const QVector<AsdexTarget>& targets,
                                          const QMatrix4x4& projection) {
    for (const AsdexTarget& target : targets) {
        if (!target.highlighted) continue;

        const double scale = target.heavy ? 1.5 : 1.0;
        drawLineMesh(highlightRingMesh_,
                     projection,
                     ringModel(target.positionFeet, scale),
                     highlightColor(),
                     1.0f);
    }
}

void TargetRenderer::renderVectorLines(const QVector<AsdexTarget>& targets,
                                       const QMatrix4x4& projection) {
    QVector<Vertex> vertices;
    vertices.reserve(targets.size() * 2);

    for (const AsdexTarget& target : targets) {
        if (target.groundSpeedKnots <= 0.0) continue;

        const QPointF end = vectorEndFeet(target.positionFeet,
                                          target.groundSpeedKnots,
                                          target.groundTrackDegrees,
                                          double(vectorSeconds_) / 60.0);

        vertices.push_back(Vertex{float(target.positionFeet.x()), float(target.positionFeet.y())});
        vertices.push_back(Vertex{float(end.x()), float(end.y())});
    }

    if (vertices.isEmpty()) return;

    lineVao_.bind();
    lineVbo_.bind();
    lineVbo_.allocate(vertices.constData(), int(vertices.size() * sizeof(Vertex)));

    shader_.bind();

    QMatrix4x4 model;
    model.setToIdentity();

    shader_.setUniformValue("u_projection", projection);
    shader_.setUniformValue("u_model", model);
    shader_.setUniformValue("u_color", colorVector(vectorColor()));

    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    f->glLineWidth(1.0f);
    f->glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));

    shader_.release();

    lineVbo_.release();
    lineVao_.release();
}

void TargetRenderer::renderHistoryDots(const QVector<AsdexTarget>& targets,
                                       const QMatrix4x4& projection) {
    constexpr int kHistoryColorCount = int(sizeof(kHistoryColors) / sizeof(kHistoryColors[0]));

    for (const AsdexTarget& target : targets) {
        const int count = std::min(static_cast<int>(target.history.size()), kHistoryColorCount);

        for (int i = 0; i < count; ++i) {
            const int value = kHistoryColors[i];

            QMatrix4x4 model;
            model.translate(float(target.history[i].positionFeet.x()),
                            float(target.history[i].positionFeet.y()));

            drawMesh(historyDotMesh_,
                     projection,
                     model,
                     applyBrightness(QColor(value, value, value)));
        }
    }
}

void TargetRenderer::render(const QVector<AsdexTarget>& targets,
                            const QMatrix4x4& worldProjection,
                            Mode mode) {
    if (!ready_) return;

    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    f->glDisable(GL_MULTISAMPLE);
    f->glDisable(GL_LINE_SMOOTH);
    f->glDisable(GL_POLYGON_SMOOTH);
    f->glDisable(GL_DITHER);
    f->glDisable(GL_DEPTH_TEST);

    renderHistoryDots(targets, worldProjection);
    renderHighlightRings(targets, worldProjection);
    renderTargetSymbols(targets, worldProjection, mode);
    renderVectorLines(targets, worldProjection);
}

} // namespace renderer::asdex
