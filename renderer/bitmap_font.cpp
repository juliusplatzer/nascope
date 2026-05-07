#include "renderer/bitmap_font.h"

#include <QFile>

#include <algorithm>

namespace renderer {
namespace {

bool readI32(const QByteArray& bytes, qsizetype& offset, qint32& value) {
    if (offset + 4 > bytes.size()) return false;

    value = qint32(quint32(uchar(bytes[offset + 0]))
                 | (quint32(uchar(bytes[offset + 1])) << 8)
                 | (quint32(uchar(bytes[offset + 2])) << 16)
                 | (quint32(uchar(bytes[offset + 3])) << 24));
    offset += 4;
    return true;
}

const BitmapGlyph* glyphFor(const BitmapFontSize& fontSize, std::uint32_t codepoint) {
    const auto it = fontSize.glyphIndex.constFind(codepoint);
    if (it == fontSize.glyphIndex.constEnd()) return nullptr;

    const int index = it.value();
    if (index < 0 || index >= static_cast<int>(fontSize.glyphs.size())) return nullptr;
    return &fontSize.glyphs[static_cast<std::size_t>(index)];
}

} // namespace

bool BitmapFont::loadFromFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return false;
    }

    const QByteArray bytes = file.readAll();
    qsizetype offset = 0;
    QHash<int, BitmapFontSize> loaded;

    while (offset < bytes.size()) {
        BitmapFontSize fontSize;
        qint32 value = 0;

        if (!readI32(bytes, offset, value)) break;
        fontSize.size = value;
        if (!readI32(bytes, offset, value)) {
            if (error) *error = QStringLiteral("truncated font header");
            return false;
        }
        fontSize.lineHeight = value;
        if (!readI32(bytes, offset, value)) {
            if (error) *error = QStringLiteral("truncated font header");
            return false;
        }
        fontSize.atlasWidth = value;
        if (!readI32(bytes, offset, value)) {
            if (error) *error = QStringLiteral("truncated font header");
            return false;
        }
        fontSize.atlasHeight = value;

        if (fontSize.size <= 0 || fontSize.lineHeight <= 0
            || fontSize.atlasWidth <= 0 || fontSize.atlasHeight <= 0) {
            if (error) *error = QStringLiteral("invalid font size entry");
            return false;
        }

        const qsizetype atlasBytes =
            qsizetype(fontSize.atlasWidth) * qsizetype(fontSize.atlasHeight);
        if (atlasBytes < 0 || offset + atlasBytes > bytes.size()) {
            if (error) *error = QStringLiteral("truncated font atlas");
            return false;
        }

        fontSize.atlasR8 = bytes.mid(offset, atlasBytes);
        offset += atlasBytes;

        qint32 glyphCount = 0;
        if (!readI32(bytes, offset, glyphCount) || glyphCount < 0) {
            if (error) *error = QStringLiteral("invalid font glyph count");
            return false;
        }

        fontSize.glyphs.reserve(static_cast<std::size_t>(glyphCount));
        for (qint32 i = 0; i < glyphCount; ++i) {
            BitmapGlyph glyph;

            if (!readI32(bytes, offset, value)) {
                if (error) *error = QStringLiteral("truncated glyph data");
                return false;
            }
            glyph.codepoint = static_cast<std::uint32_t>(value);
            if (!readI32(bytes, offset, value)) return false;
            glyph.width = value;
            if (!readI32(bytes, offset, value)) return false;
            glyph.height = value;
            if (!readI32(bytes, offset, value)) return false;
            glyph.textureOffset = value;
            if (!readI32(bytes, offset, value)) return false;
            glyph.bearingX = value;
            if (!readI32(bytes, offset, value)) return false;
            glyph.bearingY = value;
            if (!readI32(bytes, offset, value)) return false;
            glyph.advance = value;

            fontSize.glyphIndex.insert(glyph.codepoint, static_cast<int>(fontSize.glyphs.size()));
            fontSize.glyphs.push_back(glyph);
        }

        loaded.insert(fontSize.size, std::move(fontSize));
    }

    if (offset != bytes.size()) {
        if (error) *error = QStringLiteral("trailing partial font data");
        return false;
    }

    if (loaded.isEmpty()) {
        if (error) *error = QStringLiteral("font file contained no sizes");
        return false;
    }

    sizes_ = std::move(loaded);
    return true;
}

const BitmapFontSize* BitmapFont::fontSize(int size) const {
    const auto it = sizes_.constFind(size);
    return it == sizes_.constEnd() ? nullptr : &it.value();
}

QSize BitmapFont::measureText(QStringView text, int size) const {
    const BitmapFontSize* fontSizeData = fontSize(size);
    if (!fontSizeData) return {};

    int maxWidth = 0;
    int penX = 0;
    int lines = 1;
    const BitmapGlyph* pendingGlyph = nullptr;

    const auto flushLine = [&]() {
        const int width = penX + (pendingGlyph ? pendingGlyph->width : 0);
        maxWidth = std::max(maxWidth, width);
        penX = 0;
        pendingGlyph = nullptr;
    };

    for (const char32_t codepoint : text.toUcs4()) {
        if (codepoint == U'\r') continue;
        if (codepoint == U'\n') {
            flushLine();
            ++lines;
            continue;
        }

        const BitmapGlyph* glyph = glyphFor(*fontSizeData, static_cast<std::uint32_t>(codepoint));
        if (!glyph) continue;

        if (pendingGlyph) penX += pendingGlyph->advance;
        pendingGlyph = glyph;
    }

    flushLine();
    return QSize(maxWidth, lines * fontSizeData->lineHeight);
}

int BitmapFont::lineHeight(int size) const {
    const BitmapFontSize* fontSizeData = fontSize(size);
    return fontSizeData ? fontSizeData->lineHeight : 0;
}

} // namespace renderer
