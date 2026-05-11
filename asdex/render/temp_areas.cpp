#include "asdex/render/temp_areas.h"

#include "asdex/render/colors.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QtGlobal>
#include <QVector4D>

#include <algorithm>
#include <cmath>
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

namespace asdex {
namespace {

constexpr int kTempMapAreasBrightness = 95;
constexpr int kTempMapAreasMinBrightness = 20;
constexpr double kAdjacentEdgeMaxDistanceFeet = 20.0;
constexpr double kAdjacentEdgeMaxAngleDegrees = 5.0;
constexpr double kAdjacentEdgeMinLengthRatio = 0.85;
constexpr double kAdjacentEdgeMinOverlapRatio = 0.85;

constexpr char kHatchVertexShader[] = R"(
#version 330 core

layout(location = 0) in vec2 a_position;

uniform mat4 u_projection;

void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
}
)";

constexpr char kHatchFragmentShader[] = R"(
#version 330 core

const int yScale = 4;
const int spacing = 50;
const int width = 5;

uniform float u_offset;
uniform vec4 u_color;

out vec4 fragColor;

void main() {
    if (mod(u_offset + gl_FragCoord.x - (yScale * gl_FragCoord.y), spacing) > width) {
        discard;
    }

    fragColor = u_color;
}
)";

constexpr char kLineVertexShader[] = R"(
#version 330 core

layout(location = 0) in vec2 a_position;

uniform mat4 u_projection;

void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
}
)";

constexpr char kLineFragmentShader[] = R"(
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

QColor areaColor(TempAreaType type, bool highlighted) {
    QColor base;
    if (highlighted) {
        base = QColor(0, 0, 255);
    } else if (type == TempAreaType::ClosedArea) {
        base = QColor(255, 0, 0);
    } else {
        base = QColor(255, 255, 0);
    }

    return applyBrightness(base, kTempMapAreasBrightness, kTempMapAreasMinBrightness);
}

double dot(const QPointF& a, const QPointF& b) {
    return a.x() * b.x() + a.y() * b.y();
}

double cross(const QPointF& a, const QPointF& b) {
    return a.x() * b.y() - a.y() * b.x();
}

double length(const QPointF& v) {
    return std::hypot(v.x(), v.y());
}

double distanceSquared(const QPointF& a, const QPointF& b) {
    const QPointF d = b - a;
    return dot(d, d);
}

bool samePoint(const QPointF& a, const QPointF& b, double toleranceFeet = 1e-4) {
    return distanceSquared(a, b) <= toleranceFeet * toleranceFeet;
}

QVector<QPointF> normalizedRing(const QVector<QPointF>& polygonFeet) {
    QVector<QPointF> ring;
    ring.reserve(polygonFeet.size());

    for (const QPointF& point : polygonFeet) {
        if (!ring.isEmpty() && samePoint(ring.last(), point)) continue;
        ring.push_back(point);
    }

    if (ring.size() > 1 && samePoint(ring.first(), ring.last())) {
        ring.removeLast();
    }

    return ring;
}

struct EdgeRecord {
    int meshIndex = -1;
    QPointF a;
    QPointF b;
};

struct DisjointSet {
    explicit DisjointSet(int size)
        : parent(size),
          rank(size, 0) {
        for (int i = 0; i < size; ++i) {
            parent[i] = i;
        }
    }

    int find(int value) {
        if (parent[value] != value) {
            parent[value] = find(parent[value]);
        }
        return parent[value];
    }

    void unite(int a, int b) {
        int rootA = find(a);
        int rootB = find(b);
        if (rootA == rootB) return;

        if (rank[rootA] < rank[rootB]) {
            std::swap(rootA, rootB);
        }

        parent[rootB] = rootA;
        if (rank[rootA] == rank[rootB]) {
            ++rank[rootA];
        }
    }

    std::vector<int> parent;
    std::vector<int> rank;
};

bool edgesAreAdjacent(const EdgeRecord& first, const EdgeRecord& second) {
    const QPointF firstVector = first.b - first.a;
    const QPointF secondVector = second.b - second.a;

    const double firstLength = length(firstVector);
    const double secondLength = length(secondVector);
    if (firstLength <= 1e-6 || secondLength <= 1e-6) return false;

    const double lengthRatio = std::min(firstLength, secondLength)
                             / std::max(firstLength, secondLength);
    if (lengthRatio < kAdjacentEdgeMinLengthRatio) return false;

    const double cosMaxAngle =
        std::cos(kAdjacentEdgeMaxAngleDegrees * 3.14159265358979323846 / 180.0);
    const double parallel = std::abs(dot(firstVector, secondVector)
                                     / (firstLength * secondLength));
    if (parallel < cosMaxAngle) return false;

    const QPointF unitFirst(firstVector.x() / firstLength,
                            firstVector.y() / firstLength);
    const QPointF unitSecond(secondVector.x() / secondLength,
                             secondVector.y() / secondLength);

    const double firstStart = dot(first.a, unitFirst);
    const double firstEnd = dot(first.b, unitFirst);
    const double secondStart = dot(second.a, unitFirst);
    const double secondEnd = dot(second.b, unitFirst);

    const double overlap = std::min(std::max(firstStart, firstEnd),
                                    std::max(secondStart, secondEnd))
                         - std::max(std::min(firstStart, firstEnd),
                                    std::min(secondStart, secondEnd));
    if (overlap < std::min(firstLength, secondLength) * kAdjacentEdgeMinOverlapRatio) {
        return false;
    }

    const double firstLineDistanceA =
        std::abs(cross(unitFirst, second.a - first.a));
    const double firstLineDistanceB =
        std::abs(cross(unitFirst, second.b - first.a));
    if (std::max(firstLineDistanceA, firstLineDistanceB) > kAdjacentEdgeMaxDistanceFeet) {
        return false;
    }

    const double secondLineDistanceA =
        std::abs(cross(unitSecond, first.a - second.a));
    const double secondLineDistanceB =
        std::abs(cross(unitSecond, first.b - second.a));
    if (std::max(secondLineDistanceA, secondLineDistanceB) > kAdjacentEdgeMaxDistanceFeet) {
        return false;
    }

    return true;
}

struct TessellatedPolygon {
    QVector<TempAreaRenderer::Vertex> vertices;
    QVector<std::uint32_t> indices;
};

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
        for (qsizetype i = 0; i + 2 < p.size(); i += 3) {
            appendTriangle(context, p.at(i), p.at(i + 1), p.at(i + 2));
        }
    } else if (context.primitiveType == GL_TRIANGLE_FAN) {
        for (qsizetype i = 2; i < p.size(); ++i) {
            appendTriangle(context, p.at(0), p.at(i - 1), p.at(i));
        }
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
    vertex->index = static_cast<std::uint32_t>(context->polygon.vertices.size());
    context->polygon.vertices.append(TempAreaRenderer::Vertex{float(coordinates[0]),
                                                              float(coordinates[1])});
    *outData = vertex.get();
    context->ownedVertices.push_back(std::move(vertex));
}

void CALLBACK tessError(GLenum error, void* polygonData) {
    Q_UNUSED(polygonData);
    qWarning().noquote() << "[asdex] temp area tessellation failed:" << gluErrorString(error);
}

TessellatedPolygon tessellateSimplePolygon(const QVector<QPointF>& polygonFeet) {
    TessContext context;
    if (polygonFeet.size() < 3) return context.polygon;

    GLUtesselator* tess = gluNewTess();
    if (!tess) return context.polygon;

    gluTessProperty(tess, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
    gluTessCallback(tess, GLU_TESS_BEGIN_DATA, reinterpret_cast<void (CALLBACK*)()>(tessBegin));
    gluTessCallback(tess, GLU_TESS_VERTEX_DATA, reinterpret_cast<void (CALLBACK*)()>(tessVertex));
    gluTessCallback(tess, GLU_TESS_END_DATA, reinterpret_cast<void (CALLBACK*)()>(tessEnd));
    gluTessCallback(tess, GLU_TESS_COMBINE_DATA, reinterpret_cast<void (CALLBACK*)()>(tessCombine));
    gluTessCallback(tess, GLU_TESS_ERROR_DATA, reinterpret_cast<void (CALLBACK*)()>(tessError));

    gluTessBeginPolygon(tess, &context);
    gluTessBeginContour(tess);
    for (const QPointF& point : polygonFeet) {
        auto vertex = std::make_unique<TessVertex>();
        vertex->coordinates[0] = point.x();
        vertex->coordinates[1] = point.y();
        vertex->coordinates[2] = 0.0;
        vertex->index = static_cast<std::uint32_t>(context.polygon.vertices.size());
        context.polygon.vertices.append(TempAreaRenderer::Vertex{float(point.x()),
                                                                 float(point.y())});

        TessVertex* rawVertex = vertex.get();
        context.ownedVertices.push_back(std::move(vertex));
        gluTessVertex(tess, rawVertex->coordinates, rawVertex);
    }
    gluTessEndContour(tess);
    gluTessEndPolygon(tess);
    gluDeleteTess(tess);

    if (context.polygon.indices.isEmpty()) context.polygon.vertices.clear();
    return context.polygon;
}

} // namespace

TempAreaRenderer::TempAreaRenderer() = default;
TempAreaRenderer::~TempAreaRenderer() = default;

void TempAreaRenderer::initialize() {
    initializeShaders();
}

void TempAreaRenderer::deinitialize() {
    clearMeshes();
    hatchShader_.removeAllShaders();
    lineShader_.removeAllShaders();
    ready_ = false;
    dirty_ = true;
}

void TempAreaRenderer::setAreas(QVector<TempArea> areas) {
    areas_ = std::move(areas);
    dirty_ = true;
}

void TempAreaRenderer::initializeShaders() {
    if (!hatchShader_.addShaderFromSourceCode(QOpenGLShader::Vertex, kHatchVertexShader)
        || !hatchShader_.addShaderFromSourceCode(QOpenGLShader::Fragment, kHatchFragmentShader)
        || !hatchShader_.link()) {
        qWarning().noquote() << "[asdex] temp area hatch shader setup failed:"
                             << hatchShader_.log();
        ready_ = false;
        return;
    }

    if (!lineShader_.addShaderFromSourceCode(QOpenGLShader::Vertex, kLineVertexShader)
        || !lineShader_.addShaderFromSourceCode(QOpenGLShader::Fragment, kLineFragmentShader)
        || !lineShader_.link()) {
        qWarning().noquote() << "[asdex] temp area line shader setup failed:"
                             << lineShader_.log();
        ready_ = false;
        return;
    }

    ready_ = true;
}

void TempAreaRenderer::clearMeshes() {
    for (std::unique_ptr<AreaMesh>& mesh : meshes_) {
        mesh->fillVao.destroy();
        mesh->fillVbo.destroy();
        mesh->fillEbo.destroy();
    }
    meshes_.clear();

    for (std::unique_ptr<AreaGroup>& group : groups_) {
        group->outlineVao.destroy();
        group->outlineVbo.destroy();
    }
    groups_.clear();
}

void TempAreaRenderer::rebuildMeshes() {
    clearMeshes();

    meshes_.reserve(std::size_t(areas_.size()));
    for (const TempArea& area : areas_) {
        if (area.polygonFeet.size() < 3) continue;

        auto mesh = std::make_unique<AreaMesh>();
        uploadAreaMesh(*mesh, area);
        if (mesh->fillIndexCount > 0) {
            meshes_.push_back(std::move(mesh));
        }
    }

    rebuildVisualGroups();

    dirty_ = false;
}

void TempAreaRenderer::uploadAreaMesh(AreaMesh& mesh, const TempArea& area) {
    mesh.id = area.id;
    mesh.type = area.type;
    mesh.polygonFeet = area.polygonFeet;
    mesh.highlighted = area.highlighted;

    const TessellatedPolygon fill = tessellateSimplePolygon(area.polygonFeet);
    if (!fill.vertices.isEmpty() && !fill.indices.isEmpty()) {
        mesh.fillVao.create();
        QOpenGLVertexArrayObject::Binder fillBinder(&mesh.fillVao);

        mesh.fillVbo.create();
        mesh.fillVbo.bind();
        mesh.fillVbo.allocate(fill.vertices.constData(), int(fill.vertices.size() * sizeof(Vertex)));

        mesh.fillEbo.create();
        mesh.fillEbo.bind();
        mesh.fillEbo.allocate(fill.indices.constData(), int(fill.indices.size() * sizeof(std::uint32_t)));

        hatchShader_.bind();
        hatchShader_.enableAttributeArray(0);
        hatchShader_.setAttributeBuffer(0, GL_FLOAT, 0, 2, sizeof(Vertex));
        hatchShader_.release();

        mesh.fillIndexCount = int(fill.indices.size());
    }
}

void TempAreaRenderer::rebuildVisualGroups() {
    groups_.clear();

    const int meshCount = int(meshes_.size());
    if (meshCount <= 0) return;

    QVector<EdgeRecord> edges;

    for (int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
        QVector<QPointF> ring = normalizedRing(meshes_[meshIndex]->polygonFeet);
        if (ring.size() < 3) continue;

        for (int i = 0; i < ring.size(); ++i) {
            edges.push_back(EdgeRecord{
                meshIndex,
                ring.at(i),
                ring.at((i + 1) % ring.size()),
            });
        }
    }

    DisjointSet disjointSet(meshCount);
    std::vector<bool> internalEdges(std::size_t(edges.size()), false);

    for (qsizetype i = 0; i < edges.size(); ++i) {
        for (qsizetype j = i + 1; j < edges.size(); ++j) {
            const EdgeRecord& first = edges.at(i);
            const EdgeRecord& second = edges.at(j);
            if (first.meshIndex == second.meshIndex) continue;

            const AreaMesh& firstMesh = *meshes_[std::size_t(first.meshIndex)];
            const AreaMesh& secondMesh = *meshes_[std::size_t(second.meshIndex)];

            if (firstMesh.type != secondMesh.type) continue;
            if (firstMesh.highlighted != secondMesh.highlighted) continue;
            if (!edgesAreAdjacent(first, second)) continue;

            disjointSet.unite(first.meshIndex, second.meshIndex);
            internalEdges[std::size_t(i)] = true;
            internalEdges[std::size_t(j)] = true;
        }
    }

    std::vector<int> roots;
    std::vector<int> groupForMesh(std::size_t(meshCount), -1);

    for (int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
        const int root = disjointSet.find(meshIndex);
        auto it = std::find(roots.begin(), roots.end(), root);
        int groupIndex = -1;

        if (it == roots.end()) {
            groupIndex = int(roots.size());
            roots.push_back(root);
            groups_.push_back(std::make_unique<AreaGroup>());
        } else {
            groupIndex = int(std::distance(roots.begin(), it));
        }

        groupForMesh[std::size_t(meshIndex)] = groupIndex;
        meshes_[meshIndex]->groupIndex = groupIndex;

        AreaGroup& group = *groups_[std::size_t(groupIndex)];
        group.type = meshes_[meshIndex]->type;
        group.meshIndices.push_back(meshIndex);
        group.highlighted = group.highlighted || meshes_[meshIndex]->highlighted;
        if (group.meshIndices.size() == 1 && !meshes_[meshIndex]->polygonFeet.isEmpty()) {
            group.hatchOriginFeet = meshes_[meshIndex]->polygonFeet.first();
        }
    }

    QVector<QVector<Vertex>> outlineVerticesByGroup;
    outlineVerticesByGroup.resize(qsizetype(groups_.size()));

    for (qsizetype edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
        if (internalEdges[std::size_t(edgeIndex)]) continue;

        const EdgeRecord& edge = edges.at(edgeIndex);
        const int groupIndex = groupForMesh[std::size_t(edge.meshIndex)];
        if (groupIndex < 0) continue;

        outlineVerticesByGroup[groupIndex].push_back(Vertex{float(edge.a.x()), float(edge.a.y())});
        outlineVerticesByGroup[groupIndex].push_back(Vertex{float(edge.b.x()), float(edge.b.y())});
    }

    for (qsizetype groupIndex = 0; groupIndex < outlineVerticesByGroup.size(); ++groupIndex) {
        uploadGroupOutline(*groups_[std::size_t(groupIndex)],
                           outlineVerticesByGroup.at(groupIndex));
    }
}

void TempAreaRenderer::uploadGroupOutline(AreaGroup& group,
                                          const QVector<Vertex>& vertices) {
    if (vertices.size() < 2) return;

    group.outlineVao.create();
    QOpenGLVertexArrayObject::Binder outlineBinder(&group.outlineVao);

    group.outlineVbo.create();
    group.outlineVbo.bind();
    group.outlineVbo.allocate(vertices.constData(), int(vertices.size() * sizeof(Vertex)));

    lineShader_.bind();
    lineShader_.enableAttributeArray(0);
    lineShader_.setAttributeBuffer(0, GL_FLOAT, 0, 2, sizeof(Vertex));
    lineShader_.release();

    group.outlineVertexCount = vertices.size();
}

void TempAreaRenderer::renderAreas(
    const QMatrix4x4& worldProjection,
    const std::function<QPointF(QPointF)>& worldToFramebufferTopLeft) {
    if (!ready_) return;

    if (dirty_) rebuildMeshes();
    if (meshes_.empty()) return;

    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    f->glDisable(GL_MULTISAMPLE);
    f->glDisable(GL_LINE_SMOOTH);
    f->glDisable(GL_POLYGON_SMOOTH);
    f->glDisable(GL_DITHER);
    f->glDisable(GL_DEPTH_TEST);

    auto renderType = [&](TempAreaType type) {
        for (std::unique_ptr<AreaGroup>& group : groups_) {
            if (group->meshIndices.isEmpty()) continue;
            if (group->type != type) continue;

            const QColor color = areaColor(group->type, group->highlighted);
            const QPointF firstScreen = worldToFramebufferTopLeft(group->hatchOriginFeet);
            const float offset =
                -std::fmod(float(firstScreen.x() + 4.0 * firstScreen.y()), 50.0f);

            drawOutline(*group, worldProjection, color);

            for (int meshIndex : group->meshIndices) {
                if (meshIndex < 0 || meshIndex >= int(meshes_.size())) continue;
                drawFill(*meshes_[std::size_t(meshIndex)], worldProjection, color, offset);
            }
        }
    };

    renderType(TempAreaType::RestrictedArea);
    renderType(TempAreaType::ClosedArea);
}

void TempAreaRenderer::drawFill(AreaMesh& mesh,
                                const QMatrix4x4& worldProjection,
                                const QColor& color,
                                float offset) {
    if (mesh.fillIndexCount <= 0) return;

    hatchShader_.bind();
    hatchShader_.setUniformValue("u_projection", worldProjection);
    hatchShader_.setUniformValue("u_offset", offset);
    hatchShader_.setUniformValue("u_color", colorVector(color));

    QOpenGLVertexArrayObject::Binder binder(&mesh.fillVao);
    mesh.fillEbo.bind();

    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    f->glDrawElements(GL_TRIANGLES, mesh.fillIndexCount, GL_UNSIGNED_INT, nullptr);

    hatchShader_.release();
}

void TempAreaRenderer::drawOutline(AreaGroup& group,
                                   const QMatrix4x4& worldProjection,
                                   const QColor& color) {
    if (group.outlineVertexCount <= 1) return;

    lineShader_.bind();
    lineShader_.setUniformValue("u_projection", worldProjection);
    lineShader_.setUniformValue("u_color", colorVector(color));

    QOpenGLVertexArrayObject::Binder binder(&group.outlineVao);

    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    f->glLineWidth(1.0f);
    f->glDrawArrays(GL_LINES, 0, group.outlineVertexCount);

    lineShader_.release();
}

} // namespace asdex
