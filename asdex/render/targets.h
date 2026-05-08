#pragma once

#include "asdex/render/colors.h"

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPointF>
#include <QString>
#include <QVector>

#include <cstdint>
#include <optional>

namespace asdex {

struct TargetHistoryPoint {
    QPointF positionFeet;
};

struct AsdexTarget {
    QString id;
    QString callsign;
    QString aircraftType;
    QString category;
    QString beaconCode;
    QString fix;
    QString scratchpad1;
    QString scratchpad2;

    QPointF positionFeet;

    // Aviation convention: 0 = north, 90 = east, 180 = south, 270 = west.
    double headingDegrees = 0.0;
    double groundTrackDegrees = 0.0;
    double groundSpeedKnots = 0.0;

    std::optional<double> altitudeTrue;

    bool correlated = true;
    bool heavy = false;
    bool duplicateBeaconCode = false;
    bool coasting = false;
    bool highlighted = false;

    QVector<TargetHistoryPoint> history;
};

class TargetRenderer {
public:
    TargetRenderer();
    ~TargetRenderer();

    TargetRenderer(const TargetRenderer&) = delete;
    TargetRenderer& operator=(const TargetRenderer&) = delete;

    void initialize();
    void deinitialize();

    void render(const QVector<AsdexTarget>& targets,
                const QMatrix4x4& worldProjection,
                Mode mode);

    void setVectorSeconds(int seconds);
    int vectorSeconds() const { return vectorSeconds_; }

private:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Mesh {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer ebo{QOpenGLBuffer::IndexBuffer};
        int indexCount = 0;
    };

    struct LineMesh {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        int vertexCount = 0;
    };

    void initializeShaders();
    void uploadAircraftMesh();
    void uploadUnknownMesh();
    void uploadHistoryDotMesh();
    void uploadHighlightRingMesh();

    void uploadPointMesh(Mesh& mesh,
                         const QVector<QPointF>& points,
                         const QVector<std::uint32_t>& indices);
    void uploadMesh(Mesh& mesh,
                    const QVector<Vertex>& vertices,
                    const QVector<std::uint32_t>& indices);
    void uploadLineMesh(LineMesh& mesh,
                        const QVector<QPointF>& points);

    void drawMesh(Mesh& mesh,
                  const QMatrix4x4& projection,
                  const QMatrix4x4& model,
                  const QColor& color);
    void drawLineMesh(LineMesh& mesh,
                      const QMatrix4x4& projection,
                      const QMatrix4x4& model,
                      const QColor& color,
                      float width = 1.0f);

    void renderTargetSymbols(const QVector<AsdexTarget>& targets,
                             const QMatrix4x4& projection,
                             Mode mode);
    void renderHighlightRings(const QVector<AsdexTarget>& targets,
                              const QMatrix4x4& projection);
    void renderVectorLines(const QVector<AsdexTarget>& targets,
                           const QMatrix4x4& projection);
    void renderHistoryDots(const QVector<AsdexTarget>& targets,
                           const QMatrix4x4& projection);

    QOpenGLShaderProgram shader_;

    Mesh aircraftMesh_;
    Mesh unknownMesh_;
    Mesh historyDotMesh_;
    LineMesh highlightRingMesh_;

    QOpenGLVertexArrayObject lineVao_;
    QOpenGLBuffer lineVbo_{QOpenGLBuffer::VertexBuffer};

    int vectorSeconds_ = 5;
    bool ready_ = false;
};

} // namespace asdex
