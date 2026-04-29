#pragma once

#include <QCursor>
#include <QHash>
#include <QPoint>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QWidget>

#include <optional>

#include "dcb.h"
#include "font.h"
#include "lists.h"
#include "videomaps.h"

namespace asdex {

class TgtCache;

/**
 * ASDE-X scope widget: teal surface background + an airport videomap rendered
 * to fit the widget, honoring Day/Night palette.
 *
 * Viewport is stored in local NM (centerNm_ = scope center in NM, halfRangeNm_
 * = half the visible extent on the limiting screen axis). Right-click-drag
 * pans in NM; the wheel zooms in discrete 100 ft steps anchored at the scope
 * center — matching VATSIM CRC behavior.
 */
class Scope : public QWidget {
public:
    explicit Scope(VideoMap map, TgtCache* cache, QWidget* parent = nullptr);

    void setMode(Mode m);
    Mode mode() const { return mode_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*)  override;
    void mouseMoveEvent(QMouseEvent*)   override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*)       override;
    void leaveEvent(QEvent*)            override;

private:
    void applyBackground();

    // Returns the cache key of the non-Unknown target whose center in NM is
    // closest to `pxPos` (widget coords) within the 150 ft pick radius, or
    // nullopt if no target qualifies. Used by the left-click toggle.
    std::optional<QString> pickClosestTargetKey(QPointF pxPos) const;

    // Picks the appropriate cursor for the current hover position — dcb_cursor
    // when over the DCB stripe, scope_cursor otherwise.
    void updateHoverCursor();

    VideoMap   map_;
    TgtCache*  cache_ = nullptr;
    Mode       mode_  = Mode::Day;

    // Viewport in local NM.
    QPointF  centerNm_    {0.0, 0.0};
    double   halfRangeNm_ = 1.0;
    int      wheelRemainder_ = 0;  // unconsumed angleDelta units

    bool     panning_     = false;
    QPointF  lastPanPos_  {0.0, 0.0};

    // Cursor in widget coords; nullopt while the pointer is outside the widget.
    // Used to pick the closest target for the highlight ring.
    std::optional<QPointF> cursorPx_;

    // Loaded once at startup; looked up by name for click/hover transitions.
    QHash<QString, QCursor> cursors_;

    // Bitmap font atlas + UI lists (coast, dep, arr, …).
    BitmapFontRenderer fontRenderer_;
    Lists              lists_;

    // Display Control Bar — top-of-screen toolbar by default. Rendered last so
    // it sits above everything (scope content, lists, the green border).
    dcb::Config        dcbCfg_;

    // Per-target leader-line + datablock visibility — keys with the symbol
    // suppressed via a left-click toggle. Default for every non-Unknown
    // target is "shown".
    QSet<QString>      hiddenDatablocks_;
};

} // namespace asdex
