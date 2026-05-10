#include "asdex/render/screen_line_renderer.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QVector4D>

namespace asdex {
namespace {

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

} // namespace

ScreenLineRenderer::ScreenLineRenderer() = default;

ScreenLineRenderer::~ScreenLineRenderer() = default;

void ScreenLineRenderer::initialize() {
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

void ScreenLineRenderer::deinitialize() {
    vao_.destroy();
    vbo_.destroy();
    shader_.removeAllShaders();
    ready_ = false;
}

void ScreenLineRenderer::initializeShaders() {
    if (!shader_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
        || !shader_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)
        || !shader_.link()) {
        qWarning().noquote() << "[asdex] screen line shader setup failed:" << shader_.log();
        ready_ = false;
        return;
    }

    ready_ = true;
}

void ScreenLineRenderer::drawLine(const QPointF& a,
                                  const QPointF& b,
                                  const QColor& color,
                                  const QMatrix4x4& screenProjection,
                                  float width) {
    if (!ready_) return;

    const Vertex vertices[2] = {
        {float(a.x()), float(a.y())},
        {float(b.x()), float(b.y())},
    };

    vao_.bind();
    vbo_.bind();
    vbo_.allocate(vertices, int(sizeof(vertices)));

    shader_.bind();
    shader_.setUniformValue("u_projection", screenProjection);
    shader_.setUniformValue("u_color", colorVector(color));

    QOpenGLFunctions* functions = QOpenGLContext::currentContext()->functions();
    functions->glLineWidth(width);
    functions->glDrawArrays(GL_LINES, 0, 2);

    shader_.release();

    vbo_.release();
    vao_.release();
}

} // namespace asdex
