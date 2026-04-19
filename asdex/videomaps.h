#pragma once

#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QTransform>

namespace asdex {

enum class Mode { Day, Night };

/**
 * ASDE-X surface videomap loaded from a gzipped GeoJSON FeatureCollection.
 *
 * Performance: at load time all features of the same kind are flattened into a
 * single `QPainterPath` stored in geo coordinates. Rendering applies the
 * caller-supplied geo→screen transform to the painter once and issues one
 * `drawPath` per kind (4 draw calls total), so pan/zoom only mutate the
 * transform — no vertex re-projection per frame.
 *
 * Paint order back-to-front: structures → aprons → taxiways → runways.
 * Buildings share the structure palette.
 */
class VideoMap {
public:
    enum class Kind { Structure = 0, Apron = 1, Taxiway = 2, Runway = 3 };
    static constexpr int kKindCount = 4;

    static VideoMap load(const QString& icao);

    bool    isValid()   const { return hasAny_; }
    QRectF  geoBounds() const { return bounds_; }

    /** `geoToScreen` maps lon/lat (x,y) to widget coords. */
    void render(QPainter& p, const QTransform& geoToScreen, Mode mode) const;

private:
    QPainterPath paths_[kKindCount];
    QRectF       bounds_;
    bool         hasAny_ = false;
};

} // namespace asdex
