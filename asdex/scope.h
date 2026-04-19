#pragma once

#include <QPoint>
#include <QPointF>
#include <QWidget>

#include "videomaps.h"

namespace asdex {

/**
 * ASDE-X scope widget: teal surface background + an airport videomap rendered
 * to fit the widget, honoring Day/Night palette.
 *
 * Interaction: right-click-drag pans, wheel/trackpad scroll zooms around the
 * cursor.
 */
class Scope : public QWidget {
public:
    explicit Scope(VideoMap map, QWidget* parent = nullptr);

    void setMode(Mode m);
    Mode mode() const { return mode_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*)  override;
    void mouseMoveEvent(QMouseEvent*)   override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*)       override;

private:
    void applyBackground();

    VideoMap map_;
    Mode     mode_ = Mode::Day;

    // View transform: screen = pan_ + zoom_ * baseFit(geo).
    double   zoom_ = 1.0;
    QPointF  pan_  {0.0, 0.0};

    bool     panning_     = false;
    QPointF  lastPanPos_  {0.0, 0.0};
};

} // namespace asdex
