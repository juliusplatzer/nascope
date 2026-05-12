#include "asdex/targets.h"

#include "utils/math.h"
#include "renderer/builders.h"
#include "renderer/command_buffer.h"
#include "renderer/geometry/tessellator.h"

#include <algorithm>
#include <cmath>

namespace asdex {
namespace {

constexpr double kFeetPerDegree = 364560.0;
constexpr int kMinTargetVectorSeconds = 1;
constexpr int kMaxTargetVectorSeconds = 20;
constexpr int kHistoryColors[] = {219, 187, 161, 138, 118, 101, 87};

QColor normalTargetColor() { return applyBrightness(QColor(248, 248, 248)); }
QColor heavyTargetColor() { return applyBrightness(QColor(248, 128, 0)); }
QColor unknownTargetColor() { return applyBrightness(QColor(0, 255, 255)); }
QColor vectorColor() { return applyBrightness(QColor(140, 140, 140)); }
QColor highlightColor() { return applyBrightness(QColor(255, 255, 255)); }

QPointF transformPoint(const QPointF& point,
                       const QPointF& translate,
                       double rotationDegrees,
                       double scale) {
    const double radians = rotationDegrees * M_PI / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    const double x = point.x() * scale;
    const double y = point.y() * scale;
    return QPointF(translate.x() + x * c - y * s,
                   translate.y() + x * s + y * c);
}

QPointF vectorEndFeet(const QPointF& start,
                      double groundSpeedKnots,
                      double trackDegrees,
                      double vectorSeconds) {
    const double distanceNm = groundSpeedKnots * vectorSeconds / 3600.0;
    const double distanceFeet = distanceNm * utils::kFeetPerNm;
    const double rad = trackDegrees * M_PI / 180.0;
    return QPointF(start.x() + distanceFeet * std::sin(rad),
                   start.y() + distanceFeet * std::cos(rad));
}

QVector<QPointF> aircraftShapeFeet() {
    const std::pair<double, double> points[] = {
        {-0.000142545, 0.0},        {-0.0001607125, 2.09625E-05},
        {-0.0001607125, 6.9875E-05},{-0.000142545, 6.9875E-05},
        {-0.0001118, 2.795E-05},   {-5.59E-05, 2.795E-05},
        {-8.385E-05, 0.000151293}, {-5.59E-05, 0.000151293},
        {1.3975E-05, 2.795E-05},   {0.000120185, 2.38175E-05},
        {0.000137155, 1.53625E-05},{0.0001439425, 8.385E-06},
        {0.0001439425, -8.385E-06},{0.000137155, -1.53625E-05},
        {0.000120185, -2.38175E-05},{1.3975E-05, -2.795E-05},
        {-5.59E-05, -0.000151293},{-8.385E-05, -0.000151293},
        {-5.59E-05, -2.795E-05},  {-0.0001118, -2.795E-05},
        {-0.000142545, -6.9875E-05},{-0.0001607125, -6.9875E-05},
        {-0.0001607125, -2.09625E-05},
    };

    QVector<QPointF> vertices;
    vertices.reserve(int(std::size(points)));
    for (const auto& [x, y] : points) {
        vertices.push_back(QPointF(x * kFeetPerDegree, y * kFeetPerDegree));
    }
    return vertices;
}

QVector<QPointF> unknownShapeFeet() {
    const std::pair<double, double> points[] = {
        {-2.5E-05, 7.5E-05},
        {-0.000125, 0.0},
        {-2.5E-05, -7.5E-05},
        {0.000175, 0.0},
    };

    QVector<QPointF> vertices;
    vertices.reserve(int(std::size(points)));
    for (const auto& [x, y] : points) {
        vertices.push_back(QPointF(x * kFeetPerDegree, y * kFeetPerDegree));
    }
    return vertices;
}

QVector<std::uint32_t> triangleFanIndices(int vertexCount) {
    QVector<std::uint32_t> indices;
    for (int i = 1; i + 1 < vertexCount; ++i) {
        indices.push_back(0);
        indices.push_back(std::uint32_t(i));
        indices.push_back(std::uint32_t(i + 1));
    }
    return indices;
}

QVector<QPointF> circleShapeFeet(double radiusFeet, int segments) {
    QVector<QPointF> vertices;
    vertices.reserve(segments + 1);
    vertices.push_back(QPointF(0.0, 0.0));
    for (int i = 0; i <= segments; ++i) {
        const double a = 2.0 * M_PI * double(i) / double(segments);
        vertices.push_back(QPointF(radiusFeet * std::cos(a), radiusFeet * std::sin(a)));
    }
    return vertices;
}

QVector<std::uint32_t> circleFanIndices(int segments) {
    QVector<std::uint32_t> indices;
    for (int i = 1; i <= segments; ++i) {
        indices.push_back(0);
        indices.push_back(std::uint32_t(i));
        indices.push_back(std::uint32_t(i + 1));
    }
    return indices;
}

QVector<QPointF> regularRingFeet(int sides, double radiusFeet) {
    QVector<QPointF> points;
    points.reserve(sides + 1);
    for (int i = 0; i <= sides; ++i) {
        const double a = 2.0 * M_PI * double(i) / double(sides);
        points.push_back(QPointF(radiusFeet * std::cos(a), radiusFeet * std::sin(a)));
    }
    return points;
}

void addTransformedIndexed(renderer::TrianglesBuilder& builder,
                           const QVector<QPointF>& points,
                           const QVector<std::uint32_t>& indices,
                           const QPointF& position,
                           double heading,
                           double scale) {
    QVector<QPointF> transformed;
    transformed.reserve(points.size());
    for (const QPointF& point : points) {
        transformed.push_back(transformPoint(point, position, 90.0 - heading, scale));
    }
    builder.addIndexed(transformed, indices);
}

void addTargetSymbols(const QVector<AsdexTarget>& targets, renderer::CommandBuffer* cb) {
    const QVector<QPointF> aircraftPoints = aircraftShapeFeet();
    renderer::TessellatedPolygon aircraftTess = renderer::tessellateSimplePolygon(aircraftPoints);
    if (aircraftTess.indices.isEmpty()) {
        aircraftTess.vertices = aircraftPoints;
        aircraftTess.indices = triangleFanIndices(aircraftPoints.size());
    }

    const QVector<QPointF> unknownPoints = unknownShapeFeet();
    const QVector<std::uint32_t> unknownIndices = triangleFanIndices(unknownPoints.size());

    renderer::TrianglesBuilder* normalBuilder = renderer::getTrianglesBuilder();
    renderer::TrianglesBuilder* heavyBuilder = renderer::getTrianglesBuilder();
    renderer::TrianglesBuilder* unknownBuilder = renderer::getTrianglesBuilder();

    for (const AsdexTarget& target : targets) {
        if (target.correlated) {
            renderer::TrianglesBuilder& builder = target.heavy ? *heavyBuilder : *normalBuilder;
            addTransformedIndexed(builder,
                                  aircraftTess.vertices,
                                  aircraftTess.indices,
                                  target.positionFeet,
                                  target.headingDegrees,
                                  target.heavy ? 1.5 : 1.0);
        } else {
            addTransformedIndexed(*unknownBuilder,
                                  unknownPoints,
                                  unknownIndices,
                                  target.positionFeet,
                                  target.groundTrackDegrees,
                                  1.0);
        }
    }

    cb->setRgba(renderer::RGBA::fromQColor(normalTargetColor()));
    normalBuilder->generateCommands(cb);
    cb->setRgba(renderer::RGBA::fromQColor(heavyTargetColor()));
    heavyBuilder->generateCommands(cb);
    cb->setRgba(renderer::RGBA::fromQColor(unknownTargetColor()));
    unknownBuilder->generateCommands(cb);

    renderer::returnTrianglesBuilder(unknownBuilder);
    renderer::returnTrianglesBuilder(heavyBuilder);
    renderer::returnTrianglesBuilder(normalBuilder);
}

} // namespace

int clampedTargetVectorSeconds(int seconds) {
    return std::clamp(seconds, kMinTargetVectorSeconds, kMaxTargetVectorSeconds);
}

void drawTargets(const QVector<AsdexTarget>& targets,
                 renderer::CommandBuffer* commandBuffer,
                 const QMatrix4x4& worldProjection,
                 Mode mode,
                 int vectorSeconds) {
    Q_UNUSED(mode);
    if (!commandBuffer) return;

    commandBuffer->loadProjectionMatrix(worldProjection);
    vectorSeconds = clampedTargetVectorSeconds(vectorSeconds);

    constexpr int kHistoryColorCount = int(sizeof(kHistoryColors) / sizeof(kHistoryColors[0]));
    const QVector<QPointF> dotPoints = circleShapeFeet(0.003 * utils::kFeetPerNm, 12);
    const QVector<std::uint32_t> dotIndices = circleFanIndices(12);

    for (int colorIndex = 0; colorIndex < kHistoryColorCount; ++colorIndex) {
        renderer::TrianglesBuilder* builder = renderer::getTrianglesBuilder();
        for (const AsdexTarget& target : targets) {
            const int count = std::min(int(target.history.size()), kHistoryColorCount);
            for (int i = 0; i < count; ++i) {
                const int ageFromNewest = count - 1 - i;
                if (ageFromNewest != colorIndex) continue;

                QVector<QPointF> translated;
                translated.reserve(dotPoints.size());
                for (const QPointF& point : dotPoints)
                    translated.push_back(point + target.history[i].positionFeet);
                builder->addIndexed(translated, dotIndices);
            }
        }
        const int value = kHistoryColors[colorIndex];
        commandBuffer->setRgba(renderer::RGBA::fromQColor(applyBrightness(QColor(value, value, value))));
        builder->generateCommands(commandBuffer);
        renderer::returnTrianglesBuilder(builder);
    }

    renderer::LinesBuilder* ringBuilder = renderer::getLinesBuilder();
    const QVector<QPointF> ring = regularRingFeet(20, 0.012 * utils::kFeetPerNm);
    for (const AsdexTarget& target : targets) {
        if (!target.highlighted) continue;

        QVector<QPointF> transformed;
        transformed.reserve(ring.size());
        for (const QPointF& point : ring)
            transformed.push_back(target.positionFeet + point * (target.heavy ? 1.5 : 1.0));
        ringBuilder->addLineStrip(transformed);
    }
    commandBuffer->setRgba(renderer::RGBA::fromQColor(highlightColor()));
    commandBuffer->lineWidth(1.0f);
    ringBuilder->generateCommands(commandBuffer);
    renderer::returnLinesBuilder(ringBuilder);

    addTargetSymbols(targets, commandBuffer);

    renderer::LinesBuilder* vectorBuilder = renderer::getLinesBuilder();
    for (const AsdexTarget& target : targets) {
        if (target.groundSpeedKnots <= 0.0) continue;
        vectorBuilder->addLine(target.positionFeet,
                               vectorEndFeet(target.positionFeet,
                                             target.groundSpeedKnots,
                                             target.groundTrackDegrees,
                                             vectorSeconds));
    }
    commandBuffer->setRgba(renderer::RGBA::fromQColor(vectorColor()));
    commandBuffer->lineWidth(1.0f);
    vectorBuilder->generateCommands(commandBuffer);
    renderer::returnLinesBuilder(vectorBuilder);
}

} // namespace asdex
