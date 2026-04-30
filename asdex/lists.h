#pragma once

#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>

class QPainter;

namespace asdex {

class BitmapFontRenderer;

/**
 * Draws the ASDE-X radar-scope lists (coast / preview area / dep / arr / …).
 * Anchors are defined against a fixed 1300x900 reference layout and scaled to
 * the current widget size at draw time.
 *
 * Currently implemented:
 *   - coast list (date/time header)
 *   - preview area (RWY CFG / TWR CFG status lines + command cursor framework)
 */
class Lists {
public:
    static constexpr int kReferenceWidth  = 1300;
    static constexpr int kReferenceHeight = 900;

    /**
     * Mutable state for the preview area (top-left status / command preview
     * block — known internally as a "list" in ASDE-X parlance even though it
     * doesn't actually look like one).
     *
     * Lines 0..2 are always present (RWY CFG / TWR CFG / SystemResponse) — an
     * empty SystemResponse still reserves its vertical slot so that warning
     * and command-preview lines below it stay on a fixed grid (CRC's `num2`
     * = 3 + warnings convention).
     */
    struct PreviewArea {
        QPointF location {50.0, 150.0};   // 1300×900 reference anchor
        int     fontSize     = 2;
        int     lineSpacing  = 3;

        // Fixed status lines.
        QString rwyConfig    = QStringLiteral("LIMITED");  // line 0 — placeholder
        QString twrConfig    = QStringLiteral("GC");       // line 1 — placeholder ("GC" or "LC")
        QString systemResponse;                            // line 2 — usually empty

        // Optional warning lines (each adds 1 to `num2`).
        bool    arrAlertsOff = false;
        QString arrAlertsOffPositions;
        bool    trkAlertInhib = false;

        // Command-driven lines + cursor — populated by a future input manager.
        QStringList commandLines;
        bool        showCursor   = false;
        int         cursorLine   = 0;  // index within commandLines
        int         cursorColumn = 0;  // char column on that line
    };

    Lists() = default;

    // Render every list onto `p` using `fonts` for glyphs.
    void draw(QPainter& p, const QSize& widgetSize, BitmapFontRenderer& fonts) const;

    // Left anchor of the coast list, in the 1300x900 reference layout.
    // Repositionable at runtime — reserved for a future drag-to-move UI.
    QPointF coastListLocation() const { return coastListLocation_; }
    void    setCoastListLocation(QPointF p) { coastListLocation_ = p; }

    // Mutable access to the preview-area state (RWY CFG / TWR CFG strings,
    // optional warnings, command-driven lines + cursor).
    PreviewArea&       preview()       { return preview_; }
    const PreviewArea& preview() const { return preview_; }

private:
    QPointF     coastListLocation_ {1000.0, 150.0};
    PreviewArea preview_;

    void drawCoastList  (QPainter& p, const QSize& widgetSize, BitmapFontRenderer& fonts) const;
    void drawPreviewArea(QPainter& p, const QSize& widgetSize, BitmapFontRenderer& fonts) const;
};

} // namespace asdex
