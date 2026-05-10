#pragma once

#include <QColor>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPointF>
#include <QString>
#include <QVector>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace asdex {

struct TempArea {
    QString id;
    QVector<QPointF> polygonFeet;
    bool highlighted = false;
};

class TempAreaRenderer {
public:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
    };

    TempAreaRenderer();
    ~TempAreaRenderer();

    TempAreaRenderer(const TempAreaRenderer&) = delete;
    TempAreaRenderer& operator=(const TempAreaRenderer&) = delete;

    void initialize();
    void deinitialize();

    void setClosedAreas(QVector<TempArea> areas);

    void renderClosedAreas(
        const QMatrix4x4& worldProjection,
        const std::function<QPointF(QPointF)>& worldToFramebufferTopLeft);

private:
    struct AreaMesh {
        QString id;
        QVector<QPointF> polygonFeet;
        bool highlighted = false;
        int groupIndex = -1;

        QOpenGLVertexArrayObject fillVao;
        QOpenGLBuffer fillVbo{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer fillEbo{QOpenGLBuffer::IndexBuffer};
        int fillIndexCount = 0;
    };

    struct AreaGroup {
        QVector<int> meshIndices;
        QPointF hatchOriginFeet;
        bool highlighted = false;
        QOpenGLVertexArrayObject outlineVao;
        QOpenGLBuffer outlineVbo{QOpenGLBuffer::VertexBuffer};
        int outlineVertexCount = 0;
    };

    void initializeShaders();
    void rebuildMeshes();
    void clearMeshes();
    void uploadAreaMesh(AreaMesh& mesh, const TempArea& area);
    void rebuildVisualGroups();

    void drawFill(AreaMesh& mesh,
                  const QMatrix4x4& worldProjection,
                  const QColor& color,
                  float offset);

    void uploadGroupOutline(AreaGroup& group,
                            const QVector<Vertex>& vertices);

    void drawOutline(AreaGroup& group,
                     const QMatrix4x4& worldProjection,
                     const QColor& color);

    QVector<TempArea> closedAreas_;
    std::vector<std::unique_ptr<AreaMesh>> meshes_;
    std::vector<std::unique_ptr<AreaGroup>> groups_;

    QOpenGLShaderProgram hatchShader_;
    QOpenGLShaderProgram lineShader_;

    bool ready_ = false;
    bool dirty_ = true;
};

} // namespace asdex
