#pragma once

#include <QPainter>
#include <QPolygonF>
#include <QTransform>

namespace asdex {

/**
 * Temporary-data overlays — restriction areas, closure areas, runway
 * closures.
 *
 * Restriction areas and runway closures are implemented; closure areas will
 * be added here next. Restriction areas use the shared red palette
 * (255,0,0 × `kTempDataBrightness`) with a screen-space diagonal hatch.
 * Runway closures are pure white 1px X markings with no brightness scaling.
 */

/// Renders one restriction area: 1px outline + diagonal hatch fill, both red.
/// `polyNm` is the area's outer ring in local NM; the hatch is computed in
/// screen pixels and phase-anchored at the polygon's first vertex so the
/// stripes stay locked to the area as the scope pans.
void drawRestrictionArea(QPainter& p, const QPolygonF& polyNm, const QTransform& nmToScreen);

/// Renders one runway closure marker: 4 white 1px segments at ±15° to the
/// runway axis, anchored on the polygon's four outer corners. Each segment
/// runs from its corner to the opposite long edge — the two front-corner
/// segments cross to form an X near the threshold, the back pair forms a
/// matching X at the rollout end.
///
/// `headingMagDeg` is the runway's *magnetic* heading. We treat it as true
/// heading; the few-degree declination error is below visual noise at scope
/// zoom. If pixel-accurate alignment with the videomap rectangle becomes
/// necessary, switch the call site to pass true heading — this function is
/// unchanged.
void drawRunwayClosure(QPainter& p,
                       const QPolygonF& polyNm,
                       double headingMagDeg,
                       const QTransform& nmToScreen);

} // namespace asdex
