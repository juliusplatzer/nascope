#ifndef ASDEX_VIDEOMAPS_H_
#define ASDEX_VIDEOMAPS_H_

#include "asdex/colors.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <cstdint>

namespace renderer {
class CommandBuffer;
}

namespace asdex {

class VideoMap {
public:
    enum class Kind { Apron = 0, Structure = 1, Taxiway = 2, Runway = 3 };
    static constexpr int kKindCount = 4;

    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Mesh {
        Kind kind = Kind::Apron;
        float z = 0.0f;
        QVector<Vertex> vertices;
        QVector<std::uint32_t> indices;
    };

    static VideoMap load(const QString& icao);

    bool isValid() const { return hasAny_; }
    QRectF boundsFeet() const { return bounds_; }
    QPointF anchorLonLat() const { return anchor_; }
    const QVector<Mesh>& meshes() const { return meshes_; }

private:
    QVector<Mesh> meshes_;
    QRectF bounds_;
    QPointF anchor_;
    bool hasAny_ = false;
};

void drawVideoMap(const VideoMap& map, renderer::CommandBuffer* commandBuffer, Mode mode);

} // namespace asdex

#endif  // ASDEX_VIDEOMAPS_H_
