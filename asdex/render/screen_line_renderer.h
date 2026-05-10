#pragma once

#include <QColor>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPointF>

namespace asdex {

class ScreenLineRenderer {
public:
    ScreenLineRenderer();
    ~ScreenLineRenderer();

    ScreenLineRenderer(const ScreenLineRenderer&) = delete;
    ScreenLineRenderer& operator=(const ScreenLineRenderer&) = delete;

    void initialize();
    void deinitialize();

    void drawLine(const QPointF& a,
                  const QPointF& b,
                  const QColor& color,
                  const QMatrix4x4& screenProjection,
                  float width = 1.0f);

private:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
    };

    void initializeShaders();

    QOpenGLShaderProgram shader_;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    bool ready_ = false;
};

} // namespace asdex
