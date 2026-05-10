#include "asdex/render/runway_closures.h"

#include "asdex/render/math.h"

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QRegularExpression>
#include <QTransform>
#include <QVector4D>

#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace asdex {
namespace {

constexpr double kCrossAngleDegrees = 15.0;

constexpr char kVertexShader[] = R"(
#version 330 core
layout(location = 0) in vec2 a_position;

uniform mat4 u_projection;

void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
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

QString normalizeRunwayId(QString id) {
    id = id.trimmed().toUpper();
    id.replace(QStringLiteral("-"), QStringLiteral("/"));

    static const QRegularExpression shortRwy(QStringLiteral("^(\\d)([LCR]?)$"));
    const QRegularExpressionMatch match = shortRwy.match(id);
    if (match.hasMatch()) {
        return QStringLiteral("0%1%2").arg(match.captured(1), match.captured(2));
    }

    return id;
}

QSet<QString> runwayTokens(QString id) {
    id = normalizeRunwayId(id);

    QSet<QString> out;
    out.insert(id);

    const QStringList parts = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString normalized = normalizeRunwayId(part);
        out.insert(normalized);

        if (normalized.size() >= 2 && normalized.at(0) == QLatin1Char('0')) {
            out.insert(normalized.mid(1));
        }
    }

    return out;
}

QPointF rotateStandard(const QPointF& v, double degrees) {
    const double radians = degrees * M_PI / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);

    return QPointF(v.x() * c - v.y() * s,
                   v.x() * s + v.y() * c);
}

QPointF rotateBearing(const QPointF& v, double bearingDegrees) {
    return rotateStandard(v, -bearingDegrees);
}

std::optional<QPointF> lineIntersection(const QPointF& a,
                                        const QPointF& dirA,
                                        const QPointF& b,
                                        const QPointF& c) {
    const QPointF dirB = c - b;
    const double det = dirA.x() * dirB.y() - dirA.y() * dirB.x();
    if (std::abs(det) < 1e-9) return std::nullopt;

    const QPointF delta = b - a;
    const double t = (delta.x() * dirB.y() - delta.y() * dirB.x()) / det;
    return a + dirA * t;
}

void appendLine(QVector<RunwayClosureRenderer::Vertex>& vertices,
                const QPointF& a,
                const QPointF& b) {
    vertices.push_back(RunwayClosureRenderer::Vertex{float(a.x()), float(a.y())});
    vertices.push_back(RunwayClosureRenderer::Vertex{float(b.x()), float(b.y())});
}

std::optional<std::array<QPointF, 4>> canonicalRunwayQuadForCrc(const QVector<QPointF>& polygon) {
    if (polygon.size() < 4) return std::nullopt;

    const std::array<QPointF, 4> p = {polygon[0], polygon[1], polygon[2], polygon[3]};

    const auto edgeLength2 = [&p](int i) {
        const QPointF d = p[(i + 1) % 4] - p[i];
        return d.x() * d.x() + d.y() * d.y();
    };

    int longestEdge = 0;
    double best = edgeLength2(0);
    for (int i = 1; i < 4; ++i) {
        const double length2 = edgeLength2(i);
        if (length2 > best) {
            best = length2;
            longestEdge = i;
        }
    }

    return std::array<QPointF, 4>{p[(longestEdge + 3) % 4],
                                  p[longestEdge],
                                  p[(longestEdge + 1) % 4],
                                  p[(longestEdge + 2) % 4]};
}

void appendClosedRunwayCross(QVector<RunwayClosureRenderer::Vertex>& vertices,
                             const QVector<QPointF>& polygon) {
    const auto quad = canonicalRunwayQuadForCrc(polygon);
    if (!quad) return;

    const QPointF p0 = (*quad)[0];
    const QPointF p1 = (*quad)[1];
    const QPointF p2 = (*quad)[2];
    const QPointF p3 = (*quad)[3];

    const QPointF rawBasis = p2 - p1;
    const double len = std::hypot(rawBasis.x(), rawBasis.y());
    if (len <= 1e-6) return;

    const QPointF basis(rawBasis.x() / len, rawBasis.y() / len);
    const QPointF plus15 = rotateBearing(basis, kCrossAngleDegrees);
    const QPointF minus15 = rotateBearing(basis, -kCrossAngleDegrees);

    const auto i0 = lineIntersection(p0, plus15, p1, p2);
    const auto i1 = lineIntersection(p1, minus15, p3, p0);
    const auto i2 = lineIntersection(p2, plus15, p3, p0);
    const auto i3 = lineIntersection(p3, minus15, p1, p2);

    if (i0) appendLine(vertices, p0, *i0);
    if (i1) appendLine(vertices, p1, *i1);
    if (i2) appendLine(vertices, p2, *i2);
    if (i3) appendLine(vertices, p3, *i3);
}

} // namespace

RunwayClosureRenderer::RunwayClosureRenderer() = default;
RunwayClosureRenderer::~RunwayClosureRenderer() = default;

void RunwayClosureRenderer::initialize() {
    initializeShaders();
    if (!ready_) return;

    vao_.create();
    QOpenGLVertexArrayObject::Binder binder(&vao_);

    vbo_.create();
    vbo_.bind();

    shader_.bind();
    shader_.enableAttributeArray(0);
    shader_.setAttributeBuffer(0, GL_FLOAT, 0, 2, sizeof(Vertex));
    shader_.release();

    vbo_.release();
}

void RunwayClosureRenderer::deinitialize() {
    vao_.destroy();
    vbo_.destroy();
    shader_.removeAllShaders();
    ready_ = false;
}

void RunwayClosureRenderer::initializeShaders() {
    if (!shader_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
        || !shader_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)
        || !shader_.link()) {
        qWarning().noquote() << "[asdex] runway closure shader setup failed:" << shader_.log();
        ready_ = false;
        return;
    }

    ready_ = true;
}

bool RunwayClosureRenderer::loadSurfaceFile(const QString& path,
                                            const QPointF& anchorLonLat,
                                            QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("invalid runway surface JSON %1: %2")
                         .arg(path, parseError.errorString());
        }
        return false;
    }

    const QTransform toFeet = lonLatToFeet(anchorLonLat);
    const QJsonArray runways = document.object().value(QStringLiteral("rwys")).toArray();

    runways_.clear();
    runways_.reserve(runways.size());

    for (const QJsonValue& value : runways) {
        const QJsonObject object = value.toObject();

        Runway runway;
        runway.id = object.value(QStringLiteral("id")).toString().trimmed().toUpper();

        const QJsonArray polygon = object.value(QStringLiteral("polygon")).toArray();
        runway.polygonFeet.reserve(polygon.size());
        for (const QJsonValue& pointValue : polygon) {
            const QJsonArray point = pointValue.toArray();
            if (point.size() < 2) continue;

            runway.polygonFeet.push_back(toFeet.map(QPointF(point.at(0).toDouble(),
                                                            point.at(1).toDouble())));
        }

        if (!runway.id.isEmpty() && runway.polygonFeet.size() >= 4) {
            runways_.push_back(std::move(runway));
        }
    }

    dirty_ = true;
    return true;
}

void RunwayClosureRenderer::setClosedRunways(const QSet<QString>& runwayIds) {
    QSet<QString> normalized;
    for (const QString& id : runwayIds) {
        const QSet<QString> tokens = runwayTokens(id);
        for (const QString& token : tokens) normalized.insert(token);
    }

    if (closedRunways_ == normalized) return;

    closedRunways_ = normalized;
    dirty_ = true;
}

bool RunwayClosureRenderer::runwayMatches(const Runway& runway,
                                          const QSet<QString>& requested) const {
    const QSet<QString> tokens = runwayTokens(runway.id);
    for (const QString& token : tokens) {
        if (requested.contains(token)) return true;
    }
    return false;
}

void RunwayClosureRenderer::rebuildSegments() {
    lineVertices_.clear();

    for (const Runway& runway : runways_) {
        if (runwayMatches(runway, closedRunways_)) {
            appendClosedRunwayCross(lineVertices_, runway.polygonFeet);
        }
    }

    dirty_ = false;
}

void RunwayClosureRenderer::uploadSegments() {
    if (!ready_) return;

    vao_.bind();
    vbo_.bind();
    vbo_.allocate(lineVertices_.isEmpty() ? nullptr : lineVertices_.constData(),
                  int(lineVertices_.size() * sizeof(Vertex)));
    vbo_.release();
    vao_.release();
}

void RunwayClosureRenderer::render(const QMatrix4x4& worldProjection) {
    if (!ready_) return;

    if (dirty_) {
        rebuildSegments();
        uploadSegments();
    }

    if (lineVertices_.isEmpty()) return;

    shader_.bind();
    shader_.setUniformValue("u_projection", worldProjection);
    shader_.setUniformValue("u_color", colorVector(QColor(255, 255, 255)));

    QOpenGLVertexArrayObject::Binder binder(&vao_);

    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    f->glDisable(GL_MULTISAMPLE);
    f->glDisable(GL_LINE_SMOOTH);
    f->glDisable(GL_POLYGON_SMOOTH);
    f->glDisable(GL_DITHER);
    f->glDisable(GL_DEPTH_TEST);
    f->glLineWidth(1.0f);
    f->glDrawArrays(GL_LINES, 0, int(lineVertices_.size()));

    shader_.release();
}

} // namespace asdex
