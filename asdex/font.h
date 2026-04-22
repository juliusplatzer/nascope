#pragma once

#include <QColor>
#include <QHash>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringView>
#include <QVector>

#include <cstdint>

class QPainter;

namespace asdex {

/**
 * Loads the ASDE-X bitmap font atlas (asdex/font.bin) and draws text at any of
 * the six ASDE-X sizes (1..6) in any QColor. Atlases are built lazily per
 * (size, color) pair and cached for subsequent draws.
 *
 * Construct after QApplication exists — atlas building allocates QPixmaps.
 */
class BitmapFontRenderer {
public:
    static constexpr int kMinSize = 1;
    static constexpr int kMaxSize = 6;

    bool load(const QString& path, QString* err = nullptr);
    bool isValid() const { return !fonts_.isEmpty(); }

    // Return horizontal advance in pixels; 0 if font/size unavailable.
    int drawTextBaseline(QPainter& p, int originX, int baselineY,
                         QStringView text, int size, const QColor& color);
    int drawTextTopLeft(QPainter& p, int left, int top,
                        QStringView text, int size, const QColor& color);

    QSize measureText(QStringView text, int size) const;

    int ascent(int size) const;
    int descent(int size) const;
    int lineHeight(int size) const;

private:
    struct Glyph {
        uint32_t codepoint = 0;
        int stepX = 0, width = 0, height = 0, offsetX = 0, offsetY = 0;
        QVector<uint32_t> rows;
    };

    struct Font {
        QString name;
        int pointSize = 0;
        int nominalWidth = 0;
        int nominalHeight = 0;
        int ascent = 0;
        int descent = 0;
        QVector<Glyph> glyphs;
        QHash<uint32_t, int> glyphIndex;  // codepoint → index into glyphs
    };

    struct AtlasGlyph {
        QRect srcRect;
        int stepX = 0, width = 0, height = 0, offsetX = 0, offsetY = 0;
    };

    struct Atlas {
        QPixmap pixmap;
        QHash<uint32_t, AtlasGlyph> glyphs;
    };

    const Font* fontForSize(int size) const;
    const Atlas* ensureAtlas(int size, const QColor& color);

    QVector<Font> fonts_;
    QHash<quint64, Atlas> atlases_;  // key: (size << 32) | rgba
};

} // namespace asdex
