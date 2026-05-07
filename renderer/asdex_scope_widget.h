#pragma once

#include "renderer/asdex_colors.h"
#include "renderer/videomap.h"

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPointF>
#include <QVector>

#include <cstddef>

namespace renderer {

class AsdexScopeWidget : public QOpenGLWidget {
public:
    explicit AsdexScopeWidget(QString airport, QWidget* parent = nullptr);

    QString airport() const { return airport_; }

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

private:
    struct DrawBatch {
        asdex::VideoMap::Kind kind = asdex::VideoMap::Kind::Apron;
        int indexCount = 0;
        std::size_t indexOffsetBytes = 0;
    };

    void fitMapToView();
    void initializeShaders();
    void uploadMapGeometry();
    void renderVideoMap(const QSize& renderSize);
    QSize framebufferRenderSize() const;
    QMatrix4x4 viewProjection(const QSize& renderSize) const;
    QColor colorFor(asdex::VideoMap::Kind kind) const;

    QString airport_;
    asdex::VideoMap map_;
    QPointF centerFeet_;
    double halfRangeFeet_ = 1.0;
    asdex::Mode mode_ = asdex::Mode::Day;

    QOpenGLShaderProgram shader_;
    QOpenGLVertexArrayObject vertexArray_;
    QOpenGLBuffer vertexBuffer_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer indexBuffer_{QOpenGLBuffer::IndexBuffer};
    QVector<DrawBatch> drawBatches_;
    bool shaderReady_ = false;
    bool geometryUploaded_ = false;
};

} // namespace renderer
