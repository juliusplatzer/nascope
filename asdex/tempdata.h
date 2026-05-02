#pragma once

#include <QPainter>
#include <QPolygonF>
#include <QTransform>

namespace asdex {

/**
 * Temporary-data overlays — restriction areas, closure areas, runway
 * closures. All three share a red palette (255,0,0 × `kTempDataBrightness`)
 * and a screen-space diagonal hatch, but each draws a different boundary on
 * top.
 *
 * Only restriction areas are implemented today; closures will be added here
 * as separate entry points.
 */

/// Renders one restriction area: 1px outline + diagonal hatch fill, both red.
/// `polyNm` is the area's outer ring in local NM; the hatch is computed in
/// screen pixels and phase-anchored at the polygon's first vertex so the
/// stripes stay locked to the area as the scope pans.
void drawRestrictionArea(QPainter& p, const QPolygonF& polyNm, const QTransform& nmToScreen);

} // namespace asdex
