#ifndef RENDERER_FONT_H_
#define RENDERER_FONT_H_

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringView>

#include <cstdint>
#include <vector>

namespace renderer {

struct BitmapGlyph {
    std::uint32_t codepoint = 0;
    int width = 0;
    int height = 0;
    int textureOffset = 0;
    int bearingX = 0;
    int bearingY = 0;
    int advance = 0;
};

struct BitmapFontSize {
    int size = 0;
    int lineHeight = 0;
    int atlasWidth = 0;
    int atlasHeight = 0;
    QByteArray atlasR8;
    std::vector<BitmapGlyph> glyphs;
    QHash<std::uint32_t, int> glyphIndex;
};

class BitmapFont {
public:
    bool loadFromFile(const QString& path, QString* error = nullptr);

    const BitmapFontSize* fontSize(int size) const;
    QSize measureText(QStringView text, int size) const;
    int lineHeight(int size) const;
    QSize charSize(int size) const;
    int fontSpacing(int size) const;
    QImage atlasImage(int size) const;

private:
    QHash<int, BitmapFontSize> sizes_;
};

} // namespace renderer

#endif  // RENDERER_FONT_H_
