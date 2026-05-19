#include "asdex/tempdata.h"

#include "asdex/colors.h"
#include "math/core.h"
#include "math/geom.h"
#include "math/latlong.h"
#include "renderer/builders.h"
#include "renderer/cmdbuffer.h"
#include "renderer/font.h"
#include "renderer/tessellator.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace asdex {
namespace {

constexpr int kTempMapAreasMinBrightness = 20;
constexpr int kTempMapTextMinBrightness = 20;
constexpr int kTempTextLineSpacing = 2;
constexpr int kLeaderStartOffsetPx = 7;
constexpr int kLeaderStepPx = 15;
constexpr int kZeroLengthAnchorPx = 10;
constexpr double kFeetPerDegree = 364560.0;
constexpr double kAdjacentEdgeMaxDistanceFeet = 20.0;
constexpr double kAdjacentEdgeMaxAngleDegrees = 5.0;
constexpr double kAdjacentEdgeMinLengthRatio = 0.85;
constexpr double kAdjacentEdgeMinOverlapRatio = 0.85;

struct EdgeRecord {
    int meshIndex = -1;
    QPointF a;
    QPointF b;
};

struct BuiltTempText {
    QStringList lines;
    int maxLineWidth = 0;
    int longestHighestLineNumber = 0;
    int longestLowestLineNumber = 0;
};

const QVector<QPointF>& tempTextStarPointsNorthEastFeet() {
    static const QVector<QPointF> kPoints = {
        {9.9e-05 * kFeetPerDegree, 4.5e-06 * kFeetPerDegree},
        {2.25e-05 * kFeetPerDegree, 3.15e-05 * kFeetPerDegree},
        {2.25e-05 * kFeetPerDegree, 0.0001125 * kFeetPerDegree},
        {-2.7e-05 * kFeetPerDegree, 4.95e-05 * kFeetPerDegree},
        {-0.0001035 * kFeetPerDegree, 7.2e-05 * kFeetPerDegree},
        {-5.85e-05 * kFeetPerDegree, 4.5e-06 * kFeetPerDegree},
        {-0.0001035 * kFeetPerDegree, -6.3e-05 * kFeetPerDegree},
        {-2.7e-05 * kFeetPerDegree, -4.05e-05 * kFeetPerDegree},
        {2.25e-05 * kFeetPerDegree, -0.0001035 * kFeetPerDegree},
        {2.25e-05 * kFeetPerDegree, -2.25e-05 * kFeetPerDegree},
        {9.9e-05 * kFeetPerDegree, 4.5e-06 * kFeetPerDegree},
    };
    return kPoints;
}

int leaderHeadingDegrees(LeaderDirection direction) {
    switch (direction) {
        case LeaderDirection::N:
            return 360;
        case LeaderDirection::E:
            return 90;
        case LeaderDirection::SE:
            return 135;
        case LeaderDirection::S:
            return 180;
        case LeaderDirection::SW:
            return 225;
        case LeaderDirection::W:
            return 270;
        case LeaderDirection::NW:
            return 315;
        case LeaderDirection::NE:
            return 45;
    }
    return 45;
}

bool isLeftDataBlock(LeaderDirection direction) {
    return direction == LeaderDirection::SW
        || direction == LeaderDirection::W
        || direction == LeaderDirection::NW;
}

QPointF leaderDelta(double distancePx, int headingDegrees) {
    const double rad = math::degreesToRadians(headingDegrees);
    const double dx = distancePx * std::sin(rad);
    const double dy = distancePx * std::cos(rad);
    return QPointF(static_cast<int>(dx), -static_cast<int>(dy));
}

QPointF starOffsetToScreen(const QPointF& feetOffset, double logicalPixelsPerFoot) {
    const double northFeet = feetOffset.x();
    const double eastFeet = feetOffset.y();
    return QPointF(eastFeet * logicalPixelsPerFoot,
                   -northFeet * logicalPixelsPerFoot);
}

void addTempTextStar(renderer::LinesBuilder& builder,
                     const QPointF& anchor,
                     double logicalPixelsPerFoot) {
    const QVector<QPointF>& points = tempTextStarPointsNorthEastFeet();
    for (int i = 0; i + 1 < points.size(); ++i) {
        builder.addLine(anchor + starOffsetToScreen(points.at(i), logicalPixelsPerFoot),
                        anchor + starOffsetToScreen(points.at(i + 1), logicalPixelsPerFoot));
    }
}

void updateMeasuredWidth(BuiltTempText& block,
                         const QString& text,
                         int lineNumber,
                         int fontSize,
                         const renderer::BitmapFont& font) {
    const int width = font.measureText(QStringView(text), fontSize).width();
    if (width > block.maxLineWidth) {
        block.maxLineWidth = width;
        block.longestHighestLineNumber = lineNumber;
        block.longestLowestLineNumber = lineNumber;
    } else if (width == block.maxLineWidth) {
        block.longestLowestLineNumber = lineNumber;
    }
}

BuiltTempText buildTempTextBlock(const TempTextAnnotation& text,
                                 const DataBlockSettings& settings,
                                 const renderer::BitmapFont& font) {
    BuiltTempText block;
    block.lines << text.line1;
    if (!text.line2.trimmed().isEmpty()) block.lines << text.line2;

    for (int i = 0; i < block.lines.size(); ++i) {
        updateMeasuredWidth(block, block.lines.at(i), i, settings.fontSize, font);
    }

    return block;
}

void drawTempTextDataBlock(const TempTextAnnotation& text,
                           const QPointF& anchorScreen,
                           renderer::LinesBuilder& lineBuilder,
                           renderer::TextBuilder& textBuilder,
                           const renderer::BitmapFont& font,
                           std::uint32_t fontTextureId,
                           const DataBlockSettings& settings) {
    const int fontSize = settings.fontSize;
    const int height = font.lineHeight(fontSize);
    if (height <= 0) return;

    const BuiltTempText block = buildTempTextBlock(text, settings, font);
    if (block.maxLineWidth <= 0 || block.lines.isEmpty()) return;

    const LeaderDirection direction = settings.leaderDirection;
    const int heading = leaderHeadingDegrees(direction);
    const bool left = isLeftDataBlock(direction);
    const int leaderLengthPx = std::max(0, settings.leaderLength) * kLeaderStepPx;

    const QPointF leaderStart = anchorScreen + leaderDelta(kLeaderStartOffsetPx, heading);
    const QPointF leaderEnd =
        anchorScreen + leaderDelta(leaderLengthPx == 0 ? kZeroLengthAnchorPx : leaderLengthPx,
                                   heading);

    if (leaderLengthPx > 0) lineBuilder.addLine(leaderStart, leaderEnd);

    int verticalLeftOffset = 0;
    if (left) {
        const int selectedLine = direction != LeaderDirection::NW
            ? block.longestHighestLineNumber
            : block.longestLowestLineNumber;
        verticalLeftOffset = (height + kTempTextLineSpacing) * selectedLine;
    }

    const int textX = int(leaderEnd.x()) + (left ? (-2 - block.maxLineWidth) : 2);
    const int textY = int(leaderEnd.y()) - height / 2 - verticalLeftOffset;

    renderer::TextStyle style;
    style.size = fontSize;
    style.color = applyBrightness(QColor(255, 255, 255),
                                  settings.brightness,
                                  kTempMapTextMinBrightness);
    style.background = Qt::transparent;

    QPointF linePosition(textX, textY);
    for (const QString& line : block.lines) {
        if (!line.isEmpty()) textBuilder.addText(QStringView(line),
                                                 linePosition,
                                                 style,
                                                 fontTextureId);
        linePosition.ry() += height + kTempTextLineSpacing;
    }
}

struct DisjointSet {
    explicit DisjointSet(int size)
        : parent(size),
          rank(size, 0) {
        for (int i = 0; i < size; ++i) parent[i] = i;
    }

    int find(int value) {
        if (parent[value] != value) parent[value] = find(parent[value]);
        return parent[value];
    }

    void unite(int a, int b) {
        int rootA = find(a);
        int rootB = find(b);
        if (rootA == rootB) return;
        if (rank[rootA] < rank[rootB]) std::swap(rootA, rootB);
        parent[rootB] = rootA;
        if (rank[rootA] == rank[rootB]) ++rank[rootA];
    }

    std::vector<int> parent;
    std::vector<int> rank;
};

bool edgesAreAdjacent(const EdgeRecord& first, const EdgeRecord& second) {
    const QPointF firstVector = first.b - first.a;
    const QPointF secondVector = second.b - second.a;
    const double firstLength = math::length(firstVector);
    const double secondLength = math::length(secondVector);
    if (firstLength <= 1e-6 || secondLength <= 1e-6) return false;

    const double lengthRatio =
        std::min(firstLength, secondLength) / std::max(firstLength, secondLength);
    if (lengthRatio < kAdjacentEdgeMinLengthRatio) return false;

    const double cosMaxAngle =
        std::cos(math::degreesToRadians(kAdjacentEdgeMaxAngleDegrees));
    const double parallel =
        std::abs(math::dot(firstVector, secondVector) / (firstLength * secondLength));
    if (parallel < cosMaxAngle) return false;

    const QPointF unitFirst(firstVector.x() / firstLength, firstVector.y() / firstLength);
    const QPointF unitSecond(secondVector.x() / secondLength, secondVector.y() / secondLength);

    const double firstStart = math::dot(first.a, unitFirst);
    const double firstEnd = math::dot(first.b, unitFirst);
    const double secondStart = math::dot(second.a, unitFirst);
    const double secondEnd = math::dot(second.b, unitFirst);
    const double overlap = std::min(std::max(firstStart, firstEnd),
                                    std::max(secondStart, secondEnd))
                         - std::max(std::min(firstStart, firstEnd),
                                    std::min(secondStart, secondEnd));
    if (overlap < std::min(firstLength, secondLength) * kAdjacentEdgeMinOverlapRatio)
        return false;

    const double firstLineDistanceA = std::abs(math::cross(unitFirst, second.a - first.a));
    const double firstLineDistanceB = std::abs(math::cross(unitFirst, second.b - first.a));
    if (std::max(firstLineDistanceA, firstLineDistanceB) > kAdjacentEdgeMaxDistanceFeet)
        return false;

    const double secondLineDistanceA = std::abs(math::cross(unitSecond, first.a - second.a));
    const double secondLineDistanceB = std::abs(math::cross(unitSecond, first.b - second.a));
    return std::max(secondLineDistanceA, secondLineDistanceB) <= kAdjacentEdgeMaxDistanceFeet;
}

QColor areaColor(TempAreaType type, bool highlighted, int brightness) {
    QColor base;
    if (highlighted)
        base = QColor(0, 0, 255);
    else if (type == TempAreaType::ClosedArea)
        base = QColor(255, 0, 0);
    else
        base = QColor(255, 255, 0);

    return applyBrightness(base, brightness, kTempMapAreasMinBrightness);
}

constexpr double kCrossAngleDegrees = 15.0;

QString normalizeRunwayId(QString id) {
    id = id.trimmed().toUpper();
    id.replace(QStringLiteral("-"), QStringLiteral("/"));

    static const QRegularExpression shortRwy(QStringLiteral("^(\\d)([LCR]?)$"));
    const QRegularExpressionMatch match = shortRwy.match(id);
    if (match.hasMatch()) return QStringLiteral("0%1%2").arg(match.captured(1), match.captured(2));
    return id;
}

QSet<QString> runwayTokens(QString id) {
    id = normalizeRunwayId(id);
    QSet<QString> out;
    out.insert(id);

    const QStringList parts = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString normalized = normalizeRunwayId(part);
        out.insert(normalized);
        if (normalized.size() >= 2 && normalized.at(0) == QLatin1Char('0'))
            out.insert(normalized.mid(1));
    }
    return out;
}

QPointF rotateBearing(const QPointF& v, double bearingDegrees) {
    return math::rotate(v, -bearingDegrees);
}

std::optional<std::array<QPointF, 4>> canonicalRunwayQuadForCrc(const QVector<QPointF>& polygon) {
    if (polygon.size() < 4) return std::nullopt;

    const std::array<QPointF, 4> p = {polygon[0], polygon[1], polygon[2], polygon[3]};
    const auto edgeLength2 = [&p](int i) {
        const QPointF d = p[(i + 1) % 4] - p[i];
        return d.x() * d.x() + d.y() * d.y();
    };

    int longestEdge = 0;
    double best = edgeLength2(0);
    for (int i = 1; i < 4; ++i) {
        const double length2 = edgeLength2(i);
        if (length2 > best) {
            best = length2;
            longestEdge = i;
        }
    }

    return std::array<QPointF, 4>{p[(longestEdge + 3) % 4],
                                  p[longestEdge],
                                  p[(longestEdge + 1) % 4],
                                  p[(longestEdge + 2) % 4]};
}

void appendLine(QVector<QPointF>& lines, const QPointF& a, const QPointF& b) {
    lines.push_back(a);
    lines.push_back(b);
}

void appendClosedRunwayCross(QVector<QPointF>& lines, const QVector<QPointF>& polygon) {
    const auto quad = canonicalRunwayQuadForCrc(polygon);
    if (!quad) return;

    const QPointF p0 = (*quad)[0];
    const QPointF p1 = (*quad)[1];
    const QPointF p2 = (*quad)[2];
    const QPointF p3 = (*quad)[3];

    const QPointF rawBasis = p2 - p1;
    const double len = std::hypot(rawBasis.x(), rawBasis.y());
    if (len <= 1e-6) return;

    const QPointF basis(rawBasis.x() / len, rawBasis.y() / len);
    const QPointF plus15 = rotateBearing(basis, kCrossAngleDegrees);
    const QPointF minus15 = rotateBearing(basis, -kCrossAngleDegrees);

    const auto i0 = math::lineIntersectionByDirection(p0, plus15, p1, p2);
    const auto i1 = math::lineIntersectionByDirection(p1, minus15, p3, p0);
    const auto i2 = math::lineIntersectionByDirection(p2, plus15, p3, p0);
    const auto i3 = math::lineIntersectionByDirection(p3, minus15, p1, p2);

    if (i0) appendLine(lines, p0, *i0);
    if (i1) appendLine(lines, p1, *i1);
    if (i2) appendLine(lines, p2, *i2);
    if (i3) appendLine(lines, p3, *i3);
}

} // namespace

bool RunwayClosureGeometry::loadSurfaceFile(const QString& path,
                                            const QPointF& anchorLonLat,
                                            QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("invalid runway surface JSON %1: %2")
                         .arg(path, parseError.errorString());
        }
        return false;
    }

    const QTransform toFeet = math::lonLatToFeet(anchorLonLat);
    const QJsonArray runways = document.object().value(QStringLiteral("rwys")).toArray();
    runways_.clear();
    runways_.reserve(runways.size());

    for (const QJsonValue& value : runways) {
        const QJsonObject object = value.toObject();
        Runway runway;
        runway.id = object.value(QStringLiteral("id")).toString().trimmed().toUpper();

        const QJsonArray polygon = object.value(QStringLiteral("polygon")).toArray();
        runway.polygonFeet.reserve(polygon.size());
        for (const QJsonValue& pointValue : polygon) {
            const QJsonArray point = pointValue.toArray();
            if (point.size() < 2) continue;
            runway.polygonFeet.push_back(toFeet.map(QPointF(point.at(0).toDouble(),
                                                            point.at(1).toDouble())));
        }

        if (!runway.id.isEmpty() && runway.polygonFeet.size() >= 4)
            runways_.push_back(std::move(runway));
    }

    dirty_ = true;
    return true;
}

void RunwayClosureGeometry::setClosedRunways(const QSet<QString>& runwayIds) {
    QSet<QString> normalized;
    for (const QString& id : runwayIds) {
        const QSet<QString> tokens = runwayTokens(id);
        for (const QString& token : tokens) normalized.insert(token);
    }

    if (closedRunways_ == normalized) return;
    closedRunways_ = normalized;
    dirty_ = true;
}

bool RunwayClosureGeometry::runwayMatches(const Runway& runway,
                                          const QSet<QString>& requested) const {
    const QSet<QString> tokens = runwayTokens(runway.id);
    for (const QString& token : tokens) {
        if (requested.contains(token)) return true;
    }
    return false;
}

void RunwayClosureGeometry::rebuildSegments() const {
    lineVertices_.clear();
    for (const Runway& runway : runways_) {
        if (runwayMatches(runway, closedRunways_))
            appendClosedRunwayCross(lineVertices_, runway.polygonFeet);
    }
    dirty_ = false;
}

const QVector<QPointF>& RunwayClosureGeometry::lineVertices() const {
    if (dirty_) rebuildSegments();
    return lineVertices_;
}

void drawRunwayClosures(const RunwayClosureGeometry& geometry,
                        renderer::CommandBuffer* commandBuffer,
                        const QMatrix4x4& worldProjection) {
    if (!commandBuffer) return;

    const QVector<QPointF>& vertices = geometry.lineVertices();
    if (vertices.size() < 2) return;

    commandBuffer->loadProjectionMatrix(worldProjection);
    commandBuffer->setRgba(renderer::RGBA::fromQColor(QColor(255, 255, 255)));
    commandBuffer->lineWidth(1.0f);

    renderer::LinesBuilder* builder = renderer::getLinesBuilder();
    for (int i = 0; i + 1 < vertices.size(); i += 2) {
        builder->addLine(vertices.at(i), vertices.at(i + 1));
    }
    builder->generateCommands(commandBuffer);
    renderer::returnLinesBuilder(builder);
}

void TempAreaGeometry::setAreas(QVector<TempArea> areas) {
    areas_ = std::move(areas);
    dirty_ = true;
}

void TempAreaGeometry::rebuild() const {
    meshes_.clear();
    groups_.clear();

    meshes_.reserve(areas_.size());
    for (const TempArea& area : areas_) {
        if (area.polygonFeet.size() < 3) continue;

        const renderer::TessellatedPolygon fill =
            renderer::tessellateSimplePolygon(area.polygonFeet);
        if (fill.indices.isEmpty()) continue;

        AreaMesh mesh;
        mesh.id = area.id;
        mesh.type = area.type;
        mesh.polygonFeet = area.polygonFeet;
        mesh.highlighted = area.highlighted;
        mesh.fillVertices = fill.vertices;
        mesh.fillIndices = fill.indices;
        meshes_.push_back(std::move(mesh));
    }

    const int meshCount = meshes_.size();
    QVector<EdgeRecord> edges;
    for (int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
        const QVector<QPointF> ring =
            math::normalizedPolygonRing(meshes_[meshIndex].polygonFeet, 1e-4);
        if (ring.size() < 3) continue;

        for (int i = 0; i < ring.size(); ++i) {
            edges.push_back(EdgeRecord{meshIndex, ring.at(i), ring.at((i + 1) % ring.size())});
        }
    }

    DisjointSet disjointSet(meshCount);
    std::vector<bool> internalEdges(std::size_t(edges.size()), false);

    for (qsizetype i = 0; i < edges.size(); ++i) {
        for (qsizetype j = i + 1; j < edges.size(); ++j) {
            const EdgeRecord& first = edges.at(i);
            const EdgeRecord& second = edges.at(j);
            if (first.meshIndex == second.meshIndex) continue;

            const AreaMesh& firstMesh = meshes_.at(first.meshIndex);
            const AreaMesh& secondMesh = meshes_.at(second.meshIndex);
            if (firstMesh.type != secondMesh.type) continue;
            if (firstMesh.highlighted != secondMesh.highlighted) continue;
            if (!edgesAreAdjacent(first, second)) continue;

            disjointSet.unite(first.meshIndex, second.meshIndex);
            internalEdges[std::size_t(i)] = true;
            internalEdges[std::size_t(j)] = true;
        }
    }

    std::vector<int> roots;
    std::vector<int> groupForMesh(std::size_t(meshCount), -1);
    for (int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
        const int root = disjointSet.find(meshIndex);
        auto it = std::find(roots.begin(), roots.end(), root);
        int groupIndex = -1;

        if (it == roots.end()) {
            groupIndex = int(roots.size());
            roots.push_back(root);
            groups_.push_back(AreaGroup{});
        } else {
            groupIndex = int(std::distance(roots.begin(), it));
        }

        groupForMesh[std::size_t(meshIndex)] = groupIndex;
        meshes_[meshIndex].groupIndex = groupIndex;

        AreaGroup& group = groups_[groupIndex];
        group.type = meshes_[meshIndex].type;
        group.meshIndices.push_back(meshIndex);
        group.highlighted = group.highlighted || meshes_[meshIndex].highlighted;
        if (group.meshIndices.size() == 1 && !meshes_[meshIndex].polygonFeet.isEmpty())
            group.hatchOriginFeet = meshes_[meshIndex].polygonFeet.first();
    }

    for (qsizetype edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
        if (internalEdges[std::size_t(edgeIndex)]) continue;

        const EdgeRecord& edge = edges.at(edgeIndex);
        const int groupIndex = groupForMesh[std::size_t(edge.meshIndex)];
        if (groupIndex < 0) continue;
        groups_[groupIndex].outlineVertices.push_back(edge.a);
        groups_[groupIndex].outlineVertices.push_back(edge.b);
    }

    dirty_ = false;
}

void TempAreaGeometry::draw(
    renderer::CommandBuffer* commandBuffer,
    const QMatrix4x4& worldProjection,
    const std::function<QPointF(QPointF)>& worldToFramebufferTopLeft,
    int brightness) const {
    drawType(commandBuffer,
             worldProjection,
             worldToFramebufferTopLeft,
             TempAreaType::RestrictedArea,
             brightness);
    drawType(commandBuffer,
             worldProjection,
             worldToFramebufferTopLeft,
             TempAreaType::ClosedArea,
             brightness);
}

void TempAreaGeometry::drawType(
    renderer::CommandBuffer* commandBuffer,
    const QMatrix4x4& worldProjection,
    const std::function<QPointF(QPointF)>& worldToFramebufferTopLeft,
    TempAreaType type,
    int brightness) const {
    if (!commandBuffer) return;
    if (dirty_) rebuild();
    if (groups_.isEmpty()) return;

    commandBuffer->loadProjectionMatrix(worldProjection);

    for (const AreaGroup& group : groups_) {
        if (group.meshIndices.isEmpty() || group.type != type) continue;

        const QColor color = areaColor(group.type, group.highlighted, brightness);
        const QPointF firstScreen = worldToFramebufferTopLeft(group.hatchOriginFeet);
        const float offset =
            -std::fmod(float(firstScreen.x() + 4.0 * firstScreen.y()), 50.0f);

        commandBuffer->setRgba(renderer::RGBA::fromQColor(color));
        commandBuffer->lineWidth(1.0f);

        renderer::LinesBuilder* lines = renderer::getLinesBuilder();
        for (int i = 0; i + 1 < group.outlineVertices.size(); i += 2)
            lines->addLine(group.outlineVertices.at(i), group.outlineVertices.at(i + 1));
        lines->generateCommands(commandBuffer);
        renderer::returnLinesBuilder(lines);

        renderer::TrianglesBuilder* triangles = renderer::getTrianglesBuilder();
        for (const int meshIndex : group.meshIndices) {
            if (meshIndex < 0 || meshIndex >= meshes_.size()) continue;
            triangles->addIndexed(meshes_.at(meshIndex).fillVertices,
                                  meshes_.at(meshIndex).fillIndices);
        }
        triangles->generateCommands(commandBuffer, renderer::DrawMode::Hatched, offset);
        renderer::returnTrianglesBuilder(triangles);
    }
}

void drawTempAreas(const TempAreaGeometry& geometry,
                   renderer::CommandBuffer* commandBuffer,
                   const QMatrix4x4& worldProjection,
                   const std::function<QPointF(QPointF)>& worldToFramebufferTopLeft,
                   int brightness) {
    geometry.draw(commandBuffer, worldProjection, worldToFramebufferTopLeft, brightness);
}

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
    int defaultBrightness) {
    if (!commandBuffer) return;

    commandBuffer->loadProjectionMatrix(screenProjection);

    for (const TempTextAnnotation& text : texts) {
        if (text.hidden) continue;

        const QPointF anchor = worldToScreen ? worldToScreen(text.locationFeet)
                                             : text.locationFeet;

        renderer::LinesBuilder* starBuilder = renderer::getLinesBuilder();
        addTempTextStar(*starBuilder, anchor, logicalPixelsPerFoot);
        const QColor anchorColor = text.highlighted ? QColor(0, 0, 255)
                                                    : QColor(255, 255, 255);
        commandBuffer->setRgba(renderer::RGBA::fromQColor(
            applyBrightness(anchorColor, defaultBrightness, kTempMapTextMinBrightness)));
        commandBuffer->lineWidth(1.0f);
        starBuilder->generateCommands(commandBuffer);
        renderer::returnLinesBuilder(starBuilder);

        const DataBlockSettings settings = settingsForText ? settingsForText(text)
                                                           : DataBlockSettings{};
        const bool visible = isDataBlockVisible ? isDataBlockVisible(text)
                                                : settings.showDataBlocks;
        if (!visible) continue;

        const std::uint32_t fontTextureId =
            fontTextureForSize ? fontTextureForSize(settings.fontSize) : 0;
        if (fontTextureId == 0) continue;

        renderer::LinesBuilder* lineBuilder = renderer::getLinesBuilder();
        renderer::TextBuilder* textBuilder = renderer::getTextBuilder();
        textBuilder->setFont(&font);

        drawTempTextDataBlock(text,
                              anchor,
                              *lineBuilder,
                              *textBuilder,
                              font,
                              fontTextureId,
                              settings);

        commandBuffer->setRgba(renderer::RGBA::fromQColor(
            applyBrightness(QColor(255, 255, 255),
                            settings.brightness,
                            kTempMapTextMinBrightness)));
        commandBuffer->lineWidth(1.0f);
        lineBuilder->generateCommands(commandBuffer);
        textBuilder->generateCommands(commandBuffer);

        renderer::returnTextBuilder(textBuilder);
        renderer::returnLinesBuilder(lineBuilder);
    }
}

} // namespace asdex
