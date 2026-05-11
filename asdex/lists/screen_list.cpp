#include "asdex/lists/screen_list.h"

#include "renderer/builders.h"
#include "renderer/font.h"

#include <algorithm>
#include <utility>

namespace asdex {

QColor applyCrcBrightness(QColor color, int brightness, int minBrightness) {
    brightness = std::clamp(brightness, 0, 100);

    const float factor =
        (float(brightness * (100 - minBrightness)) / 100.0f
         + float(minBrightness)) / 100.0f;

    return QColor(int(color.red() * factor),
                  int(color.green() * factor),
                  int(color.blue() * factor),
                  color.alpha());
}

ScreenList::ScreenList(ScreenListStyle style)
    : style_(std::move(style)) {}

void ScreenList::render(renderer::TextBuilder& textBuilder,
                        const renderer::BitmapFont& font,
                        std::uint32_t fontTextureId,
                        const TextBlock& block) const {
    const QPointF screenLocation = style_.location;
    const qreal lineSpacing =
        qreal(block.lineSpacing > 0 ? block.lineSpacing : style_.lineSpacing);

    QPointF cursor = screenLocation;
    const int lineHeight = font.lineHeight(style_.fontSize);

    for (const TextFragment& fragment : block.fragments) {
        const QColor baseColor =
            fragment.foreground.isValid() ? fragment.foreground : style_.baseTextColor;

        renderer::TextStyle textStyle;
        textStyle.size = style_.fontSize;
        textStyle.color = applyCrcBrightness(baseColor, style_.brightness, style_.minBrightness);
        textStyle.background = fragment.background;
        textStyle.underlined = fragment.underlined;

        if (!fragment.text.isEmpty()) {
            textBuilder.addText(QStringView(fragment.text), cursor, textStyle, fontTextureId);
        }

        if (fragment.newLine) {
            cursor.setX(screenLocation.x());
            cursor.setY(cursor.y() + lineHeight + lineSpacing);
        } else if (!fragment.text.isEmpty()) {
            cursor.setX(cursor.x() + font.measureText(QStringView(fragment.text),
                                                      style_.fontSize).width());
        }
    }
}

} // namespace asdex
