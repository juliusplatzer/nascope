#pragma once

#include <QMatrix4x4>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>

namespace renderer {
class CommandBuffer;
}

namespace asdex {

class RunwayClosureGeometry {
public:
    bool loadSurfaceFile(const QString& path,
                         const QPointF& anchorLonLat,
                         QString* error = nullptr);
    void setClosedRunways(const QSet<QString>& runwayIds);
    const QVector<QPointF>& lineVertices() const;

private:
    struct Runway {
        QString id;
        QVector<QPointF> polygonFeet;
    };

    void rebuildSegments() const;
    bool runwayMatches(const Runway& runway, const QSet<QString>& requested) const;

    QVector<Runway> runways_;
    QSet<QString> closedRunways_;
    mutable QVector<QPointF> lineVertices_;
    mutable bool dirty_ = true;
};

void drawRunwayClosures(const RunwayClosureGeometry& geometry,
                        renderer::CommandBuffer* commandBuffer,
                        const QMatrix4x4& worldProjection);

} // namespace asdex
