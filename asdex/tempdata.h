#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QTransform>

QT_FORWARD_DECLARE_CLASS(QProcess)

namespace asdex {

/**
 * Temporary-data overlays — restriction areas, closure areas, runway
 * closures.
 *
 * Restriction areas and runway closures are implemented; closure areas will
 * be added here next. Restriction areas use the shared red palette
 * (255,0,0 × `kTempDataBrightness`) with a screen-space diagonal hatch.
 * Runway closures are pure white 1px X markings with no brightness scaling.
 *
 * `ClosureCache` (below) owns the data side of the same overlay set: it
 * runs the NOTAM scraper subprocess, parses its compact JSON output,
 * combines those active closures with the static airport surface data
 * (resources/surface/asdex/<ICAO>.json), and exposes the resulting NM-space
 * polygons for the scope's paintEvent to feed into the rendering helpers.
 */

/// Renders one restriction area: 1px outline + diagonal hatch fill, both red.
/// `polyNm` is the area's outer ring in local NM; the hatch is computed in
/// screen pixels and phase-anchored at the polygon's first vertex so the
/// stripes stay locked to the area as the scope pans.
void drawRestrictionArea(QPainter& p, const QPolygonF& polyNm, const QTransform& nmToScreen);

/// Renders one runway closure marker: 4 pure-white 1px segments at ±15° to
/// the runway's long axis, anchored on the polygon's four outer corners.
/// Each segment runs from its corner to the opposite long edge — the two
/// front-corner segments cross to form an X near the threshold, the back
/// pair forms a matching X at the rollout end.
///
/// `cornersNm` must contain exactly the four runway corners (any order),
/// in local NM coords. The long axis is derived from the polygon itself
/// (longest non-diagonal edge) — robust against magnetic-vs-true heading
/// mismatch, which at airports like KSFO (~14° declination) is large
/// enough to misalign a heading-driven axis with the actual rectangle and
/// blow up the arm length. Color is pure white (255,255,255) with no
/// brightness scaling.
void drawRunwayClosure(QPainter& p,
                       const QPolygonF& cornersNm,
                       const QTransform& nmToScreen);

/**
 * Closure cache + scraper driver.
 *
 * Lifecycle:
 *   1. `switchAirport(icao, anchor)` — called from the scope's constructor
 *      and `setFacility()`. Loads `resources/surface/asdex/<icao>.json`,
 *      projects runway polygons to NM using the videomap's lon/lat anchor,
 *      reads any existing cache file (so the scope shows last-known closures
 *      while a fresh scrape is in flight), and kicks the scraper subprocess.
 *   2. Every 30 minutes a timer kicks a fresh scrape.
 *   3. When the subprocess finishes, the cache JSON is re-read, the derived
 *      render list is rebuilt, and `changed()` fires so the scope repaints.
 *
 * v1 only resolves runway closures; taxiway closures are recorded in the
 * cache JSON but skipped here. v2 will add taxiway resolution + rendering.
 */
class ClosureCache : public QObject {
    Q_OBJECT
public:
    explicit ClosureCache(QObject* parent = nullptr);

    /** Re-target at a new airport. Drops everything cached for the previous
     *  facility, loads the new surface JSON, and kicks a scrape. */
    void switchAirport(const QString& icao, QPointF anchorLonLat);

    /** Resolved closure polygons in local NM. v1 only contains runway
     *  closures; v2 will discriminate runway vs taxiway entries. */
    const QList<QPolygonF>& renderItems() const { return items_; }

signals:
    void changed();

private:
    void kickScrape();
    void onScrapeFinished(QProcess* proc, int exitCode);
    void loadSurface(const QString& icao, QPointF anchorLonLat);
    void parseScrapeOutput(const QByteArray& bytes);
    void rebuildItems();

    QString                  icao_;
    QPointF                  anchorLonLat_ {0.0, 0.0};
    QHash<QString, QPolygonF> rwys_;        // id → 4-corner polygon in NM
    QStringList              rwyClosures_;  // ids from the latest scrape
    QList<QPolygonF>         items_;        // resolved closure polygons (NM)
    QTimer                   refreshTimer_;
};

} // namespace asdex
