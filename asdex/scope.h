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
    void keyPressEvent(QKeyEvent*)      override;

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

    // Global datablock visibility (toggled by F6). When false, no leader/
    // datablock is drawn regardless of `hiddenDatablocks_`. Per-target
    // overrides are preserved across F6 presses.
    bool               showAllDatablocks_ = true;

    // ---- Datablock editor ---------------------------------------------------
    // Right-click on a non-Unknown target opens a 7-line edit form in the
    // preview area (A/C, BCN, CAT, TYP, FIX, SP1, SP2). Cursor cycles with
    // wheel / arrow keys / Enter. Backspace clears, printable keys append.
    // Enter on the last field exits edit mode. SP1/SP2 are visible but
    // currently read-only — the cache doesn't carry scratchpad fields yet.
    enum class EditField { Aircraft = 0, Beacon, Category, Type, Fix, Sp1, Sp2 };
    static constexpr int kEditFieldCount = 7;

    struct EditState {
        bool       active = false;
        QString    key;                     // target cache key being edited
        EditField  field = EditField::Aircraft;
        QString    values[kEditFieldCount]; // current edit buffers, indexed by EditField
    };
    EditState edit_;

    void enterEditMode(const QString& key);
    void commitEdit();                           // writes edit_ buffers → cache and exits (Enter on SP2)
    void exitEditMode();                         // cleanup only — no commit (Esc, target gone, etc.)
    void cycleEditField(int delta);              // +1 = next, -1 = prev; clamps at boundaries
    void clearCurrentEditField();                // Backspace handler
    void appendToCurrentEditField(QChar c);      // printable-key handler (no-op for read-only fields)
    void refreshPreviewFromEdit();               // pushes edit_ → lists_.preview() so the next paint reflects state
};

} // namespace asdex
