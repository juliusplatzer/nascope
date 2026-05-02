#pragma once

#include <QList>
#include <QPainter>
#include <QPointF>
#include <QString>
#include <QTransform>

#include <optional>

#include "font.h"

namespace asdex {

enum class TargetType    { Normal, Heavy, Unknown };
enum class DatablockKind { Limited, Full };

/**
 * Per-target source data for the datablock — fully decoupled from the cache so
 * `targets.{h,cpp}` doesn't depend on `tgtcache.h`. Field letters refer to the
 * CRC datablock spec (fieldA = DUP BCN, fieldB = callsign, etc).
 */
struct DatablockFields {
    bool    dupBeacon     = false;     // line 0: fieldA (DUP BCN warning)
    QString callsign;                  // line 1: fieldB
    QString beacon;                    // line 1: fieldC (used in place of B when !hasFlightPlan)
    bool    hasFlightPlan = false;     // chooses fieldB vs fieldC
    std::optional<int> altitudeFt;     // line 1: fieldD (rendered as 100s of ft, "XXX" if absent)
    bool    coasted       = false;     // line 1: fieldE (CST if true, FUS otherwise)
    QString acType;                    // line 2: fieldF
    QString category;                  // line 2: fieldG (wake / CWT)
    QString exitFix;                   // line 2: fieldH
    std::optional<int> speedKt;        // line 2: fieldI (rendered as 10s of knots)
    // fieldJ / fieldK (scratchpads) — not yet implemented.
};

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
 * Draws a 1 px white selection ring around `posNm`. Base radius is fixed in
 * world NM (0.012 NM ≈ 73 ft) so it scales consistently with zoom; when
 * `heavy` is true the ring is enlarged by ×1.5 to match a heavy aircraft's
 * outline scale.
 */
void drawHighlightRing(QPainter& p, const QTransform& nmToScreen,
                       const QPointF& posNm, bool heavy = false);

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

/**
 * Draws a target's velocity vector line — a 1 px solid segment from the
 * target outward along its current track. Length is `gs * seconds / 3600`
 * in NM, so a 200 kt aircraft with the default 5 s horizon projects ~0.28 NM.
 *
 * `headingDeg` is a compass bearing (CW from north), `speedKts` ground speed,
 * `vectorSeconds` the projection horizon (clamped to 1..20). Color is
 * rgb(140, 140, 140) brightness-scaled with the standard 20% floor.
 *
 * Caller draws this *after* drawTarget so the line lands on top of the symbol
 * (CRC's WindowElementTargets queues DrawAircraftModel then DrawVectorLine
 * at the same z). Skip for Unknown targets — caller's responsibility.
 */
void drawVectorLine(QPainter& p, const QTransform& nmToScreen,
                    const QPointF& targetPosNm,
                    double headingDeg, double speedKts,
                    int vectorSeconds = 5);

/**
 * Draws the leader line from a target out toward its datablock and returns
 * the datablock anchor in screen pixels.
 *
 * `angleDeg` is a compass bearing (CW from north) — 45 = NE, 90 = E, 135 = SE,
 * 180 = S, 225 = SW, 270 = W, 315 = NW, 360 = N. Default is NE.
 * `lengthSteps` is how many 15 px segments the leader extends after the 7 px
 * gap from the target. So:
 *   - leader start  = target + 7 px in `angleDeg`
 *   - leader end    = target + (7 + 15·lengthSteps) px in `angleDeg`
 *
 * If `lengthSteps == 0` the line is not drawn, but the datablock anchor is
 * still placed 10 px away in the chosen direction so the datablock floats
 * just clear of the target.
 *
 * Line style: solid, 1 px, rgb(0, 208, 0). Caller must invoke this *after*
 * drawTarget so the leader sits above the symbol (logical z = target_z + 0.2).
 * Leader lines are not drawn for Unknown targets — that's a caller decision.
 */
QPointF drawLeaderLine(QPainter& p, const QTransform& nmToScreen,
                       const QPointF& targetPosNm,
                       double angleDeg = 45.0,
                       int lengthSteps = 2);

/**
 * Draws a 3-line datablock anchored at `anchorPx` (the leader-line endpoint
 * returned by `drawLeaderLine`). Color matches the leader (rgb 0, 208, 0).
 *
 * Layout per CRC:
 *   line 0: fieldA                                       (always reserved slot)
 *   line 1: B|C [D] [E]      (Full)        B|C           (Limited)
 *   line 2: F [G] H [I]      (Full)        F H           (Limited)
 *
 * Each visible field is appended with a single leading space and the line is
 * trimmed at the end, so disabled / missing fields collapse cleanly without
 * leaving placeholder gaps.
 *
 * Positioning depends on `leaderAngleDeg`:
 *   - Left datablock for SW (225), W (270), NW (315) — anchored on right edge.
 *   - Right datablock for everything else — anchored on left edge.
 *
 * Caller decides which targets get a datablock (e.g. skip Unknown).
 */
void drawDatablock(QPainter& p, BitmapFontRenderer& font,
                   const QPointF& anchorPx, double leaderAngleDeg,
                   const DatablockFields& fields,
                   DatablockKind kind = DatablockKind::Limited,
                   int fontSize = 2);

} // namespace asdex
