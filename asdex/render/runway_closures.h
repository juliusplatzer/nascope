#pragma once

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>

namespace asdex {

class RunwayClosureRenderer {
public:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
    };

    RunwayClosureRenderer();
    ~RunwayClosureRenderer();

    RunwayClosureRenderer(const RunwayClosureRenderer&) = delete;
    RunwayClosureRenderer& operator=(const RunwayClosureRenderer&) = delete;

    void initialize();
    void deinitialize();

    bool loadSurfaceFile(const QString& path,
                         const QPointF& anchorLonLat,
                         QString* error = nullptr);

    void setClosedRunways(const QSet<QString>& runwayIds);

    void render(const QMatrix4x4& worldProjection);

private:
    struct Runway {
        QString id;
        QVector<QPointF> polygonFeet;
    };

    void initializeShaders();
    void rebuildSegments();
    void uploadSegments();

    bool runwayMatches(const Runway& runway, const QSet<QString>& requested) const;

    QOpenGLShaderProgram shader_;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};

    QVector<Runway> runways_;
    QSet<QString> closedRunways_;
    QVector<Vertex> lineVertices_;

    bool ready_ = false;
    bool dirty_ = true;
};

} // namespace asdex
