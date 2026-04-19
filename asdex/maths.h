#pragma once

#include <QPointF>
#include <QSize>
#include <QTransform>

namespace asdex {

/** Fraction of the widget edges kept empty around the fitted content. */
inline constexpr double kViewportMargin = 0.04;

/**
 * Flat-earth projection from (lon, lat) degrees to local nautical miles with
 * (0,0) at `anchorLonLat`. x = east NM, y = north NM.
 *
 * Valid for the scale of a single airport (errors < 0.1 % for a few NM around
 * the anchor); not for continental extents.
 */
QTransform lonLatToNm(const QPointF& anchorLonLat);

/**
 * World (NM) → widget pixel transform. `halfRangeNm` is the center-to-edge
 * distance on the *limiting* screen dimension — so the user-visible range on
 * the smaller axis is exactly `halfRangeNm` regardless of window aspect.
 * North-up: positive NM-north maps to screen-up.
 */
QTransform nmToScreen(const QPointF& centerNm, double halfRangeNm, const QSize& view);

} // namespace asdex
