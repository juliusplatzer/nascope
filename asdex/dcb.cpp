#include "dcb.h"

#include <QColor>
#include <QPoint>
#include <QRect>

#include <algorithm>

#include "brightness.h"

namespace asdex::dcb {

namespace {

constexpr int kColumns       = 14;
constexpr int kButtonSpacing = 3;     // gap between buttons in horizontal mode

// Paint colors. Brightness in Config is plumbed but not applied yet.
constexpr QColor kBgColor   (56,  56,  56);
constexpr QColor kPanelColor(100, 100, 100);

bool isHorizontal(Position p) { return p == Position::Top  || p == Position::Bottom; }
bool isVertical  (Position p) { return p == Position::Left || p == Position::Right;  }

struct Layout {
    int charHeight   = 0;
    int buttonHeight = 0;
    int buttonWidth  = 0;
    int menuWidth    = 0;
    int menuHeight   = 0;
};

Layout layoutFor(const BitmapFontRenderer& font, int charSize, Position pos) {
    Layout L;
    L.charHeight   = font.lineHeight(charSize);
    L.buttonHeight = 2 * L.charHeight + 9;
    L.buttonWidth  = 3 * L.buttonHeight;

    if (pos == Position::Off) {
        L.menuWidth  = L.buttonWidth + 6;
        L.menuHeight = L.buttonHeight * 2 + 9;
    } else if (isHorizontal(pos)) {
        L.menuWidth  = (L.buttonWidth + kButtonSpacing) * kColumns + kButtonSpacing;
        L.menuHeight = L.buttonHeight * 2 + 9;
    } else {  // vertical
        L.menuWidth  = L.buttonWidth + 6;
        L.menuHeight = L.buttonHeight * kColumns * 2 + 87;
    }
    return L;
}

bool fitsAt(const Layout& L, Position pos, QSize widget) {
    if (pos == Position::Off) return true;
    return isHorizontal(pos) ? (L.menuWidth  <= widget.width())
                             : (L.menuHeight <= widget.height());
}

// Largest of {3, 2, 1} for which the layout fits, then capped to user pref.
int chooseCharSize(const BitmapFontRenderer& font, Position pos,
                   QSize widget, int prefSize) {
    int num = 3;
    while (num > 1 && !fitsAt(layoutFor(font, num, pos), pos, widget)) --num;
    return std::clamp(std::min(num, prefSize), 1, 3);
}

QRect stripeRect(Position pos, QSize widget, const Layout& L) {
    switch (pos) {
        case Position::Top:    return { 0, 0,                          widget.width(),  L.menuHeight  };
        case Position::Bottom: return { 0, widget.height() - L.menuHeight, widget.width(),  L.menuHeight  };
        case Position::Left:   return { 0, 0,                          L.menuWidth,     widget.height() };
        case Position::Right:  return { widget.width() - L.menuWidth, 0, L.menuWidth,    widget.height() };
        case Position::Off:    return {};
    }
    return {};
}

QPoint panelOrigin(Position pos, QSize widget, const Layout& L, int scrollOffset) {
    if (pos == Position::Off) {
        return { widget.width() - L.menuWidth, 0 };
    }

    const bool fits = isHorizontal(pos) ? (L.menuWidth  <= widget.width())
                                        : (L.menuHeight <= widget.height());
    int x = 0, y = 0;
    switch (pos) {
        case Position::Top:
            x = fits ? (widget.width() - L.menuWidth) / 2 : -scrollOffset;
            y = 0;
            break;
        case Position::Bottom:
            x = fits ? (widget.width() - L.menuWidth) / 2 : -scrollOffset;
            y = widget.height() - L.menuHeight;
            break;
        case Position::Left:
            x = 0;
            y = fits ? (widget.height() - L.menuHeight) / 2 : -scrollOffset;
            break;
        case Position::Right:
            x = widget.width() - L.menuWidth;
            y = fits ? (widget.height() - L.menuHeight) / 2 : -scrollOffset;
            break;
        case Position::Off:
            break;  // handled above
    }
    return { x, y };
}

} // namespace

void render(QPainter& p, BitmapFontRenderer& font,
            const QSize& widget, const Config& cfg) {
    if (!cfg.show || widget.isEmpty() || !font.isValid()) return;

    const int     size = chooseCharSize(font, cfg.position, widget, cfg.prefSize);
    const Layout  L    = layoutFor(font, size, cfg.position);
    if (L.buttonHeight <= 0) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);

    const QColor bgColor    = applyBrightness(kBgColor,    cfg.brightness);
    const QColor panelColor = applyBrightness(kPanelColor, cfg.brightness);

    // Off mode: skip the long stripe — just paint the small top-right box.
    if (cfg.position != Position::Off) {
        p.fillRect(stripeRect(cfg.position, widget, L), bgColor);
    }
    const QPoint origin = panelOrigin(cfg.position, widget, L, cfg.scrollOffset);
    p.fillRect(QRect(origin.x(), origin.y(), L.menuWidth, L.menuHeight), panelColor);

    // (Buttons live at z = -0.99 — they'll be rendered here in a follow-up.)

    p.restore();
}

} // namespace asdex::dcb
