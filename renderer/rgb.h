#ifndef RENDERER_RGB_H_
#define RENDERER_RGB_H_

#include <QColor>

namespace renderer {

struct RGB {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    static RGB fromQColor(const QColor& color) {
        return RGB{float(color.redF()), float(color.greenF()), float(color.blueF())};
    }
};

struct RGBA {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    static RGBA fromQColor(const QColor& color) {
        return RGBA{float(color.redF()),
                    float(color.greenF()),
                    float(color.blueF()),
                    float(color.alphaF())};
    }

    QColor toQColor() const { return QColor::fromRgbF(r, g, b, a); }
};

} // namespace renderer

#endif  // RENDERER_RGB_H_
