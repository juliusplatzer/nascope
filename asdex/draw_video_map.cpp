#include "asdex/draw_video_map.h"

#include "renderer/builders.h"
#include "renderer/command_buffer.h"

namespace asdex {
namespace {

QColor colorFor(VideoMap::Kind kind, Mode mode) {
    const bool day = mode == Mode::Day;
    QColor base;

    switch (kind) {
    case VideoMap::Kind::Runway:
        base = QColor(0, 0, 0);
        break;
    case VideoMap::Kind::Taxiway:
        base = day ? QColor(47, 47, 47) : QColor(17, 39, 80);
        break;
    case VideoMap::Kind::Apron:
        base = day ? QColor(73, 73, 73) : QColor(18, 55, 97);
        break;
    case VideoMap::Kind::Structure:
        base = day ? QColor(100, 100, 100) : QColor(34, 63, 103);
        break;
    }

    return applyBrightness(base);
}

} // namespace

void drawVideoMap(const VideoMap& map, renderer::CommandBuffer* commandBuffer, Mode mode) {
    if (!commandBuffer || !map.isValid()) return;

    for (const VideoMap::Mesh& mesh : map.meshes()) {
        if (mesh.vertices.isEmpty() || mesh.indices.isEmpty()) continue;

        QVector<QPointF> points;
        points.reserve(mesh.vertices.size());
        for (const VideoMap::Vertex& vertex : mesh.vertices) {
            points.push_back(QPointF(vertex.x, vertex.y));
        }

        commandBuffer->setRgba(renderer::RGBA::fromQColor(colorFor(mesh.kind, mode)));
        renderer::TrianglesBuilder* builder = renderer::getTrianglesBuilder();
        builder->addIndexed(points, mesh.indices);
        builder->generateCommands(commandBuffer);
        renderer::returnTrianglesBuilder(builder);
    }
}

} // namespace asdex
