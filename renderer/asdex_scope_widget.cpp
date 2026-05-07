#include "renderer/asdex_scope_widget.h"

#include "renderer/asdex_math.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QDebug>
#include <QSurfaceFormat>
#include <QVector4D>
#include <QtGlobal>

#include <algorithm>
#include <cstdint>

namespace renderer {
namespace {

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

} // namespace

AsdexScopeWidget::AsdexScopeWidget(QString airport, QWidget* parent)
    : QOpenGLWidget(parent),
      airport_(std::move(airport)),
      map_(asdex::VideoMap::load(airport_)) {
    QSurfaceFormat fmt = format();
    fmt.setSamples(0);
    setFormat(fmt);

    setMinimumSize(640, 480);
    setMouseTracking(true);
    fitMapToView();
}

void AsdexScopeWidget::initializeGL() {
    QOpenGLFunctions* functions = context()->functions();
    functions->initializeOpenGLFunctions();
    functions->glDisable(GL_DEPTH_TEST);
    functions->glDisable(GL_STENCIL_TEST);
    functions->glDisable(GL_MULTISAMPLE);
    functions->glDisable(GL_LINE_SMOOTH);
    functions->glDisable(GL_POLYGON_SMOOTH);
    functions->glDisable(GL_DITHER);
    functions->glDisable(GL_CULL_FACE);

    functions->glEnable(GL_BLEND);
    functions->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    initializeShaders();
    uploadMapGeometry();
}

void AsdexScopeWidget::resizeGL(int width, int height) {
    Q_UNUSED(width);
    Q_UNUSED(height);
    fitMapToView();
}

void AsdexScopeWidget::paintGL() {
    QOpenGLFunctions* functions = context()->functions();
    const QSize renderSize = framebufferRenderSize();
    functions->glViewport(0, 0, renderSize.width(), renderSize.height());

    const QColor background = asdex::backgroundColor(mode_);
    functions->glClearColor(background.redF(), background.greenF(), background.blueF(), 1.0f);
    functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderVideoMap(renderSize);
}

void AsdexScopeWidget::fitMapToView() {
    if (!map_.isValid()) return;

    const QRectF bounds = map_.boundsFeet();
    centerFeet_ = bounds.center();
    halfRangeFeet_ = 0.5 * std::max(bounds.width(), bounds.height());
    if (halfRangeFeet_ <= 0.0) halfRangeFeet_ = 1.0;
}

void AsdexScopeWidget::initializeShaders() {
    if (!shader_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
        || !shader_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)
        || !shader_.link()) {
        qWarning().noquote() << "[renderer] shader setup failed:" << shader_.log();
        shaderReady_ = false;
        return;
    }

    shaderReady_ = true;
}

void AsdexScopeWidget::uploadMapGeometry() {
    if (!shaderReady_ || !map_.isValid() || geometryUploaded_) return;

    QVector<asdex::VideoMap::Vertex> vertices;
    QVector<std::uint32_t> indices;
    drawBatches_.clear();

    for (const asdex::VideoMap::Mesh& mesh : map_.meshes()) {
        if (mesh.indices.isEmpty()) continue;

        const qsizetype batchStart = indices.size();
        const std::uint32_t vertexBase = static_cast<std::uint32_t>(vertices.size());
        vertices += mesh.vertices;
        for (const std::uint32_t index : mesh.indices) indices.append(vertexBase + index);
        const qsizetype batchCount = indices.size() - batchStart;

        if (batchCount > 0) {
            drawBatches_.append(DrawBatch{mesh.kind,
                                          static_cast<int>(batchCount),
                                          static_cast<std::size_t>(batchStart)
                                              * sizeof(std::uint32_t)});
        }
    }

    if (vertices.isEmpty() || indices.isEmpty()) return;

    vertexArray_.create();
    QOpenGLVertexArrayObject::Binder vaoBinder(&vertexArray_);

    vertexBuffer_.create();
    vertexBuffer_.bind();
    vertexBuffer_.allocate(vertices.constData(),
                           static_cast<int>(vertices.size() * sizeof(asdex::VideoMap::Vertex)));

    indexBuffer_.create();
    indexBuffer_.bind();
    indexBuffer_.allocate(indices.constData(),
                          static_cast<int>(indices.size() * sizeof(std::uint32_t)));

    shader_.bind();
    shader_.enableAttributeArray(0);
    shader_.setAttributeBuffer(0,
                               GL_FLOAT,
                               0,
                               2,
                               sizeof(asdex::VideoMap::Vertex));
    shader_.release();

    geometryUploaded_ = true;
}

void AsdexScopeWidget::renderVideoMap(const QSize& renderSize) {
    if (!shaderReady_ || !geometryUploaded_ || renderSize.isEmpty()) return;

    shader_.bind();
    QMatrix4x4 model;
    model.setToIdentity();
    shader_.setUniformValue("u_projection", viewProjection(renderSize));
    shader_.setUniformValue("u_model", model);

    QOpenGLVertexArrayObject::Binder vaoBinder(&vertexArray_);
    indexBuffer_.bind();

    QOpenGLFunctions* functions = context()->functions();
    for (const DrawBatch& batch : drawBatches_) {
        const QColor color = colorFor(batch.kind);
        shader_.setUniformValue("u_color",
                                QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF()));
        functions->glDrawElements(GL_TRIANGLES,
                                  batch.indexCount,
                                  GL_UNSIGNED_INT,
                                  reinterpret_cast<const void*>(batch.indexOffsetBytes));
    }

    shader_.release();
}

QSize AsdexScopeWidget::framebufferRenderSize() const {
    const qreal ratio = devicePixelRatioF();
    return QSize(qRound(width() * ratio), qRound(height() * ratio));
}

QMatrix4x4 AsdexScopeWidget::viewProjection(const QSize& renderSize) const {
    QMatrix4x4 matrix;
    matrix.setToIdentity();
    if (renderSize.isEmpty() || halfRangeFeet_ <= 0.0) return matrix;

    const double availW = renderSize.width() * (1.0 - 2.0 * asdex::kViewportMargin);
    const double availH = renderSize.height() * (1.0 - 2.0 * asdex::kViewportMargin);
    const double radiusPx = 0.5 * std::min(availW, availH);
    const double pxPerFoot = radiusPx / halfRangeFeet_;
    const double sx = 2.0 * pxPerFoot / renderSize.width();
    const double sy = 2.0 * pxPerFoot / renderSize.height();

    matrix(0, 0) = static_cast<float>(sx);
    matrix(0, 3) = static_cast<float>(-centerFeet_.x() * sx);
    matrix(1, 1) = static_cast<float>(sy);
    matrix(1, 3) = static_cast<float>(-centerFeet_.y() * sy);
    return matrix;
}

QColor AsdexScopeWidget::colorFor(asdex::VideoMap::Kind kind) const {
    const bool day = mode_ == asdex::Mode::Day;
    QColor base;

    switch (kind) {
        case asdex::VideoMap::Kind::Runway:
            base = QColor(0, 0, 0);
            break;
        case asdex::VideoMap::Kind::Taxiway:
            base = day ? QColor(47, 47, 47) : QColor(17, 39, 80);
            break;
        case asdex::VideoMap::Kind::Apron:
            base = day ? QColor(73, 73, 73) : QColor(18, 55, 97);
            break;
        case asdex::VideoMap::Kind::Structure:
            base = day ? QColor(100, 100, 100) : QColor(34, 63, 103);
            break;
    }

    return asdex::applyBrightness(base);
}

} // namespace renderer
