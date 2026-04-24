#pragma once

#include <QPainter>
#include <QPointF>
#include <QTransform>

namespace asdex {

/**
 * Draws an aircraft target symbol centered at `posNm` (local NM, same frame as
 * the videomap), rotated so the nose points along `headingDeg` (compass,
 * degrees CW from north). The symbol outline is defined in NM-sized offsets
 * from the aircraft reference point with the nose at +Y at heading 0; we
 * rotate into the NM frame ourselves, then let `nmToScreen` do the pixel map.
 */
void drawAircraft(QPainter& p, const QTransform& nmToScreen,
                  const QPointF& posNm, double headingDeg);

} // namespace asdex
