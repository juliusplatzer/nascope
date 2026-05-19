#ifndef ASDEX_TEMPDATA_H_
#define ASDEX_TEMPDATA_H_

#include "asdex/datablock.h"
#include "asdex/dbareas.h"

#include <QMatrix4x4>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>
#include <cstdint>
#include <optional>

namespace renderer {
class BitmapFont;
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

struct TempTextAnnotation {
    QString id;
    QPointF locationFeet;
    QString line1;
    QString line2;

    bool hidden = false;
    bool highlighted = false;

    std::optional<bool> showDataBlockOverride;
    bool dbOffAreaOverride = false;

    std::optional<LeaderDirection> leaderDirectionOverride;
    std::optional<LeaderDirection> traitLeaderDirectionOverride;

    std::optional<DbAreaTraits> dbTraits;
};

class TempAreaGeometry {
public:
    void setAreas(QVector<TempArea> areas);
    void draw(renderer::CommandBuffer* commandBuffer,
              const QMatrix4x4& worldProjection,
              const std::function<QPointF(QPointF)>& worldToFramebufferTopLeft,
              int brightness = 95) const;
    void drawType(renderer::CommandBuffer* commandBuffer,
                  const QMatrix4x4& worldProjection,
                  const std::function<QPointF(QPointF)>& worldToFramebufferTopLeft,
                  TempAreaType type,
                  int brightness = 95) const;

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
                   const std::function<QPointF(QPointF)>& worldToFramebufferTopLeft,
                   int brightness = 95);

void drawTempTextAnnotations(
    const QVector<TempTextAnnotation>& texts,
    renderer::CommandBuffer* commandBuffer,
    const QMatrix4x4& screenProjection,
    const std::function<QPointF(QPointF)>& worldToScreen,
    double logicalPixelsPerFoot,
    const renderer::BitmapFont& font,
    const std::function<std::uint32_t(int)>& fontTextureForSize,
    const std::function<bool(const TempTextAnnotation&)>& isDataBlockVisible,
    const std::function<DataBlockSettings(const TempTextAnnotation&)>& settingsForText,
    int defaultBrightness = 95);

} // namespace asdex

#endif  // ASDEX_TEMPDATA_H_
