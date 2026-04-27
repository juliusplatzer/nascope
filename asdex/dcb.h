#pragma once

#include <QPainter>
#include <QSize>

#include "font.h"

namespace asdex::dcb {

/**
 * Display Control Bar — the always-on-top toolbar around the scope edge. This
 * file contains only the foundational background-and-panel painter; buttons
 * land in a follow-up patch.
 *
 * Layout sits at the highest UI z-band: render after every other scope layer
 * (incl. the green window boundary). Logical z = -1.0; buttons would sit at
 * -0.99.
 */

enum class Position { Top, Bottom, Left, Right, Off };

struct Config {
    Position position     = Position::Top;
    int      prefSize     = 2;     // user-preferred char size, clamped 1..3
    int      brightness   = 95;    // 0..100, not yet applied to the colors
    bool     show         = true;
    int      scrollOffset = 0;     // px shift on the long axis when the panel doesn't fit
};

/**
 * Draws the DCB stripe + panel into the widget rect at `widget`. Caller owns
 * paint order — invoke this after everything else so the bar sits on top.
 *
 * The font reference is needed to derive button geometry from the chosen
 * char size; the render-time size is `min(largest-that-fits, cfg.prefSize)`,
 * starting at 3 and shrinking to 2 then 1 if the panel exceeds the widget on
 * its long axis.
 */
void render(QPainter& p, BitmapFontRenderer& font,
            const QSize& widget, const Config& cfg);

} // namespace asdex::dcb
