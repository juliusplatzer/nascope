#pragma once

#include "asdex/lists/asdex_text.h"

#include <QColor>
#include <QPointF>
#include <QSizeF>

namespace renderer {
class BitmapFontRenderer;
}

namespace asdex {

struct ScreenListStyle {
    QPointF location = QPointF(50.0, 150.0);
    QSizeF repositionSize = QSizeF(300.0, 500.0);

    int fontSize = 2;
    int brightness = 95;
    int minBrightness = 20;
    int lineSpacing = 3;

    QColor baseTextColor = QColor(0, 248, 0);
};

class ScreenList {
public:
    explicit ScreenList(ScreenListStyle style);

    void setLocation(QPointF location) { style_.location = location; }
    QPointF location() const { return style_.location; }

    const ScreenListStyle& style() const { return style_; }

    void render(renderer::BitmapFontRenderer& textRenderer, const TextBlock& block) const;

private:
    ScreenListStyle style_;
};

QColor applyCrcBrightness(QColor color, int brightness, int minBrightness = 20);

} // namespace asdex
