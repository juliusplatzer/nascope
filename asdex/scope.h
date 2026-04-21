#pragma once

#include <QCursor>
#include <QHash>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QWidget>

#include "videomaps.h"

namespace asdex {

/**
 * ASDE-X scope widget: teal surface background + an airport videomap rendered
 * to fit the widget, honoring Day/Night palette.
 *
 * Viewport is stored in local NM (centerNm_ = scope center in NM, halfRangeNm_
 * = half the visible extent on the limiting screen axis). Right-click-drag
 * pans in NM; the wheel zooms in discrete 0.01 NM steps anchored at the scope
 * center — matching VATSIM CRC behavior.
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

    // Viewport in local NM.
    QPointF  centerNm_    {0.0, 0.0};
    double   halfRangeNm_ = 1.0;
    int      wheelRemainder_ = 0;  // unconsumed angleDelta units

    bool     panning_     = false;
    QPointF  lastPanPos_  {0.0, 0.0};

    // Loaded once at startup; looked up by name for click/hover transitions.
    QHash<QString, QCursor> cursors_;
};

} // namespace asdex
