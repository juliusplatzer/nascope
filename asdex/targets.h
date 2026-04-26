#pragma once

#include <QList>
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
 *
 * If `alert` is set, the fill flashes red (255, 0, 0) on a shared 1 s cycle
 * (500 ms red, 500 ms type color) driven by wall-clock time so every alert
 * target on screen flashes in phase. Heavy keeps its ×1.5 size regardless.
 * Repaint cadence is the caller's responsibility (trigger `update()` ≥2 Hz
 * while any target is in alert).
 */
void drawTarget(QPainter& p, const QTransform& nmToScreen,
                const QPointF& posNm, double headingDeg,
                TargetType type = TargetType::Normal,
                bool alert = false);

/**
 * Draws a 1 px white selection ring around `posNm`. The ring radius is fixed
 * in world NM (0.012 NM ≈ 73 ft) so it scales consistently with zoom.
 */
void drawHighlightRing(QPainter& p, const QTransform& nmToScreen,
                       const QPointF& posNm);

/**
 * Draws the position-history dots for one target — small filled grey discs at
 * each cached past position, fading from light (newest) to dark (oldest).
 * `historyPosNm` is in local NM, ordered oldest-first / newest-last (matches
 * the cache's posHistory layout once it's been projected through lonLatToNm).
 *
 * At most 7 dots are drawn — extras are ignored. Caller must invoke this
 * *before* drawTarget for the same key so the dots sit behind the live symbol
 * (logical z = target_z − 0.1).
 */
void drawHistoryDots(QPainter& p, const QTransform& nmToScreen,
                     const QList<QPointF>& historyPosNm);

} // namespace asdex
