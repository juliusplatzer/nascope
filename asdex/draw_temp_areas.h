#pragma once

#include <QMatrix4x4>
#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>

namespace renderer {
class CommandBuffer;
}

namespace asdex {

enum class TempAreaType {
    RestrictedArea,
    ClosedArea,
};

struct TempArea {
    QString id;
    TempAreaType type = TempAreaType::ClosedArea;
    QVector<QPointF> polygonFeet;
    bool highlighted = false;
};

class TempAreaGeometry {
public:
    void setAreas(QVector<TempArea> areas);
    void draw(renderer::CommandBuffer* commandBuffer,
              const QMatrix4x4& worldProjection,
              const std::function<QPointF(QPointF)>& worldToFramebufferTopLeft) const;

private:
    struct AreaMesh {
        QString id;
        TempAreaType type = TempAreaType::ClosedArea;
        QVector<QPointF> polygonFeet;
        bool highlighted = false;
        int groupIndex = -1;
        QVector<QPointF> fillVertices;
        QVector<std::uint32_t> fillIndices;
    };

    struct AreaGroup {
        TempAreaType type = TempAreaType::ClosedArea;
        QVector<int> meshIndices;
        QPointF hatchOriginFeet;
        bool highlighted = false;
        QVector<QPointF> outlineVertices;
    };

    void rebuild() const;

    QVector<TempArea> areas_;
    mutable QVector<AreaMesh> meshes_;
    mutable QVector<AreaGroup> groups_;
    mutable bool dirty_ = true;
};

void drawTempAreas(const TempAreaGeometry& geometry,
                   renderer::CommandBuffer* commandBuffer,
                   const QMatrix4x4& worldProjection,
                   const std::function<QPointF(QPointF)>& worldToFramebufferTopLeft);

} // namespace asdex
