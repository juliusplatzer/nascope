#ifndef RENDERER_GEOMETRY_TESSELLATOR_H_
#define RENDERER_GEOMETRY_TESSELLATOR_H_

#include <QPointF>
#include <QPolygonF>
#include <QVector>

#include <cstdint>

namespace renderer {

struct TessellatedPolygon {
    QVector<QPointF> vertices;
    QVector<std::uint32_t> indices;
};

TessellatedPolygon tessellatePolygon(const QVector<QPolygonF>& rings);
TessellatedPolygon tessellateSimplePolygon(const QVector<QPointF>& points);

} // namespace renderer

#endif  // RENDERER_GEOMETRY_TESSELLATOR_H_
