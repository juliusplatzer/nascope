#pragma once

#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>

namespace asdex {

enum class Mode { Day, Night };

/**
 * ASDE-X surface videomap loaded from a gzipped GeoJSON FeatureCollection.
 *
 * At load time every feature is flattened by kind into a single `QPainterPath`
 * and projected from lon/lat into **local nautical miles** anchored at the
 * map's bbox centroid, via `asdex::lonLatToNm`. Rendering takes a NM→screen
 * transform and issues one `drawPath` per kind (4 calls total).
 *
 * Paint order back-to-front: aprons → structures → taxiways → runways.
 * Structures sit *over* the apron so terminal buildings reaching into the
 * apron stay visible. Buildings share the structure palette.
 */
class VideoMap {
public:
    enum class Kind { Apron = 0, Structure = 1, Taxiway = 2, Runway = 3 };
    static constexpr int kKindCount = 4;

    static VideoMap load(const QString& icao);

    bool    isValid()      const { return hasAny_; }
    QRectF  boundsNm()     const { return bounds_; }
    /** lon/lat anchor used for the NM projection — reuse for any (lon, lat)
     *  that needs to land in the same NM frame as this map's paths. */
    QPointF anchorLonLat() const { return anchor_; }

    /** `nmToScreen` maps local NM (x=east, y=north) to widget coords. */
    void render(QPainter& p, const QTransform& nmToScreen, Mode mode) const;

private:
    QPainterPath paths_[kKindCount];
    QRectF       bounds_;  // in local NM
    QPointF      anchor_;  // lon/lat anchor used for the NM projection
    bool         hasAny_ = false;
};

} // namespace asdex
