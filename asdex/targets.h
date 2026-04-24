#pragma once

#include <QPainter>
#include <QPointF>
#include <QTransform>

namespace asdex {

enum class TargetType { Normal, Heavy, Unknown };

/**
 * Draws a target symbol centered at `posNm` (local NM, same frame as the
 * videomap), rotated so the nose points along `headingDeg` (compass, degrees
 * CW from north). The symbol outline is defined in NM-sized offsets from the
 * target reference point with the nose at +Y at heading 0; we rotate into the
 * NM frame ourselves, then let `nmToScreen` do the pixel map.
 *   Normal   → white aircraft outline, ×1.0
 *   Heavy    → orange aircraft outline, ×1.5
 *   Unknown  → cyan kite (non-aircraft / unidentified), ×1.0
 */
void drawTarget(QPainter& p, const QTransform& nmToScreen,
                const QPointF& posNm, double headingDeg,
                TargetType type = TargetType::Normal);

/**
 * Draws a 1 px white selection ring around `posNm`. The ring radius is fixed
 * in world NM (0.012 NM ≈ 73 ft) so it scales consistently with zoom.
 */
void drawHighlightRing(QPainter& p, const QTransform& nmToScreen,
                       const QPointF& posNm);

} // namespace asdex
