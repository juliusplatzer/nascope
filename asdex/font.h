#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QStringView>
#include <QVector>

#include <cstdint>

class QPainter;

namespace asdex {

/**
 * Loads the ASDE-X bitmap font atlas (asdex/assets/font.bin) and draws text at any
 * available size in any QColor. Each font ships a grayscale (8-bit coverage)
 * atlas; drawing with a specific color tints that atlas on first use and
 * caches the tinted QPixmap per (size, color).
 *
 * Construct after QApplication exists — atlas tinting allocates QPixmaps.
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

    int lineHeight(int size) const;
    int ascent(int size) const  { return lineHeight(size); }  // baseline is at top + lineHeight
    int descent(int) const      { return 0; }

private:
    struct Glyph {
        uint32_t codepoint = 0;
        int width = 0, height = 0;
        int textureOffset = 0;   // x-offset into this font's atlas (y is always 0)
        int bearingX = 0;
        int bearingY = 0;        // glyph top above baseline
        int advance = 0;
    };

    struct Font {
        int lineHeight  = 0;
        int atlasWidth  = 0;
        int atlasHeight = 0;
        QByteArray bitmap;       // atlasWidth * atlasHeight grayscale bytes, row-major, top-left origin
        QVector<Glyph> glyphs;
        QHash<uint32_t, int> glyphIndex;  // codepoint → index into glyphs
    };

    const Font* fontForSize(int size) const;
    const QPixmap* ensureAtlas(int size, const QColor& color);

    QHash<int, Font>         fonts_;     // keyed by size (from binary)
    QHash<quint64, QPixmap>  atlases_;   // key: (size << 32) | rgba
};

} // namespace asdex
