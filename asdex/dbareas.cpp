#include "asdex/dbareas.h"

#include "renderer/builders.h"
#include "renderer/cmdbuffer.h"

#include <QColor>

#include <utility>

namespace asdex {
namespace {

QColor areaColor(DbAreaKind kind) {
    return kind == DbAreaKind::Off ? QColor(255, 0, 0) : QColor(0, 255, 0);
}

void addPolyline(renderer::LinesBuilder& builder, const QVector<QPointF>& points) {
    if (points.size() < 2) return;

    for (int i = 0; i + 1 < points.size(); ++i) {
        builder.addLine(points[i], points[i + 1]);
    }
}

}  // namespace

void DbAreaStore::add(DbArea area) {
    areas_.push_back(std::move(area));
}

void DbAreaStore::clear() {
    areas_.clear();
}

bool DbAreaStore::pointInsideOffArea(const QPointF& pointFeet) const {
    for (const DbArea& area : areas_) {
        if (area.kind == DbAreaKind::Off && pointInPolygon(area.polygonFeet, pointFeet)) {
            return true;
        }
    }

    return false;
}

const DbArea* DbAreaStore::firstAreaContaining(const QPointF& pointFeet) const {
    for (const DbArea& area : areas_) {
        if (pointInPolygon(area.polygonFeet, pointFeet)) return &area;
    }

    return nullptr;
}

bool pointInPolygon(const QVector<QPointF>& polygon, const QPointF& point) {
    if (polygon.size() < 3) return false;

    bool inside = false;
    const int n = polygon.size();

    for (int i = 0, j = n - 1; i < n; j = i++) {
        const QPointF& a = polygon[i];
        const QPointF& b = polygon[j];
        const bool crosses =
            ((a.y() > point.y()) != (b.y() > point.y()))
            && (point.x() < (b.x() - a.x()) * (point.y() - a.y())
                         / ((b.y() - a.y()) + 1e-12)
                         + a.x());

        if (crosses) inside = !inside;
    }

    return inside;
}

void drawDbAreas(const DbAreaStore& store, renderer::CommandBuffer* commandBuffer) {
    if (!commandBuffer) return;

    for (const DbArea& area : store.areas()) {
        if (area.polygonFeet.size() < 2) continue;

        commandBuffer->setRgba(renderer::RGBA::fromQColor(areaColor(area.kind)));
        commandBuffer->lineWidth(1.0f);

        renderer::LinesBuilder* builder = renderer::getLinesBuilder();
        addPolyline(*builder, area.polygonFeet);
        builder->generateCommands(commandBuffer);
        renderer::returnLinesBuilder(builder);
    }
}

void drawDbAreaDraft(const QVector<QPointF>& committedPoints,
                     std::optional<QPointF> mousePoint,
                     renderer::CommandBuffer* commandBuffer) {
    if (!commandBuffer) return;
    if (committedPoints.isEmpty() && !mousePoint) return;

    QVector<QPointF> points = committedPoints;
    if (mousePoint) points.push_back(*mousePoint);
    if (points.size() < 2) return;

    commandBuffer->setRgba(renderer::RGBA::fromQColor(QColor(255, 255, 255)));
    commandBuffer->lineWidth(1.0f);

    renderer::LinesBuilder* builder = renderer::getLinesBuilder();
    addPolyline(*builder, points);
    builder->generateCommands(commandBuffer);
    renderer::returnLinesBuilder(builder);
}

}  // namespace asdex
