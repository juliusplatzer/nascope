#pragma once

#include <QPointF>
#include <QSize>

class QPainter;

namespace asdex {

class BitmapFontRenderer;

/**
 * Draws the ASDE-X radar-scope lists (coast / dep / arr / etc.). Anchors are
 * defined against a fixed 1300x900 reference layout and scaled to the current
 * widget size at draw time.
 *
 * For now only the coast list is implemented, and only its header (date/time).
 * Aircraft rows and additional list types will follow.
 */
class Lists {
public:
    static constexpr int kReferenceWidth  = 1300;
    static constexpr int kReferenceHeight = 900;

    Lists() = default;

    // Render every list onto `p` using `fonts` for glyphs.
    void draw(QPainter& p, const QSize& widgetSize, BitmapFontRenderer& fonts) const;

    // Left anchor of the coast list, in the 1300x900 reference layout.
    // Repositionable at runtime — reserved for a future drag-to-move UI.
    QPointF coastListLocation() const { return coastListLocation_; }
    void    setCoastListLocation(QPointF p) { coastListLocation_ = p; }

private:
    QPointF coastListLocation_ {1000.0, 150.0};

    void drawCoastList(QPainter& p, const QSize& widgetSize,
                       BitmapFontRenderer& fonts) const;
};

} // namespace asdex
