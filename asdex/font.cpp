#include "font.h"

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cstring>

namespace asdex {

namespace {

/*
Binary layout (little-endian):

  magic[8]  = "ASDXBFNT"
  uint32    version = 1
  uint32    fontCount

  per font:
    uint16        fontNameLen
    bytes[len]    UTF-8 font name
    uint32        pointSize, nominalWidth, nominalHeight, glyphCount

    per glyph:
      uint32      codepoint
      uint16      glyphNameLen
      bytes[len]  UTF-8 glyph name
      int32       stepX, width, height, offsetX, offsetY
      uint32      rowCount
      uint32[rowCount]  rows (left-aligned MSB-first 1-bpp)
*/

constexpr char     kMagic[8]  = {'A','S','D','X','B','F','N','T'};
constexpr quint32  kVersion   = 1;
constexpr int      kAtlasPad  = 1;

template <typename T>
bool readPod(const QByteArray& data, qsizetype& off, T& out) {
    const qsizetype n = qsizetype(sizeof(T));
    if (off < 0 || off + n > data.size()) return false;
    std::memcpy(&out, data.constData() + off, size_t(n));
    off += n;
    return true;
}

bool readBytes(const QByteArray& data, qsizetype& off, QByteArray& out, qsizetype n) {
    if (n < 0 || off < 0 || off + n > data.size()) return false;
    out = QByteArray(data.constData() + off, n);
    off += n;
    return true;
}

bool readUtf8(const QByteArray& data, qsizetype& off, QString& out) {
    quint16 len = 0;
    if (!readPod(data, off, len)) return false;
    QByteArray bytes;
    if (!readBytes(data, off, bytes, qsizetype(len))) return false;
    out = QString::fromUtf8(bytes);
    return true;
}

quint64 atlasKey(int size, const QColor& color) {
    return (quint64(size) << 32) | quint64(color.rgba());
}

inline bool rowBitSet(uint32_t row, int x) {
    return ((row >> (31 - x)) & 1u) != 0u;
}

} // namespace

bool BitmapFontRenderer::load(const QString& path, QString* err) {
    fonts_.clear();
    atlases_.clear();

    const auto fail = [&](QString msg) {
        fonts_.clear();
        if (err) *err = std::move(msg);
        return false;
    };

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot open %1: %2").arg(path, f.errorString()));

    const QByteArray data = f.readAll();
    qsizetype off = 0;

    QByteArray magic;
    if (!readBytes(data, off, magic, 8) || std::memcmp(magic.constData(), kMagic, 8) != 0)
        return fail(QStringLiteral("invalid font binary magic"));

    quint32 version = 0, fontCount = 0;
    if (!readPod(data, off, version) || !readPod(data, off, fontCount))
        return fail(QStringLiteral("truncated font binary header"));
    if (version != kVersion)
        return fail(QStringLiteral("unsupported font binary version %1").arg(version));

    fonts_.reserve(int(fontCount));

    for (quint32 fi = 0; fi < fontCount; ++fi) {
        Font font;
        quint32 pointSize = 0, nomW = 0, nomH = 0, glyphCount = 0;
        if (!readUtf8(data, off, font.name) ||
            !readPod(data, off, pointSize)  ||
            !readPod(data, off, nomW)       ||
            !readPod(data, off, nomH)       ||
            !readPod(data, off, glyphCount))
            return fail(QStringLiteral("truncated font record"));

        font.pointSize     = int(pointSize);
        font.nominalWidth  = int(nomW);
        font.nominalHeight = int(nomH);
        font.glyphs.reserve(int(glyphCount));
        font.glyphIndex.reserve(int(glyphCount));

        for (quint32 gi = 0; gi < glyphCount; ++gi) {
            Glyph g;
            QString glyphName;
            qint32 stepX = 0, w = 0, h = 0, ox = 0, oy = 0;
            quint32 rowCount = 0;
            if (!readPod(data, off, g.codepoint) ||
                !readUtf8(data, off, glyphName)  ||
                !readPod(data, off, stepX)       ||
                !readPod(data, off, w)           ||
                !readPod(data, off, h)           ||
                !readPod(data, off, ox)          ||
                !readPod(data, off, oy)          ||
                !readPod(data, off, rowCount))
                return fail(QStringLiteral("truncated glyph record in %1").arg(font.name));

            g.stepX = stepX; g.width = w; g.height = h;
            g.offsetX = ox;  g.offsetY = oy;
            g.rows.resize(int(rowCount));
            for (quint32 ri = 0; ri < rowCount; ++ri) {
                if (!readPod(data, off, g.rows[int(ri)]))
                    return fail(QStringLiteral("truncated glyph rows in %1").arg(font.name));
            }

            font.ascent  = std::max(font.ascent,  g.offsetY + g.height);
            font.descent = std::max(font.descent, -g.offsetY);

            font.glyphIndex.insert(g.codepoint, font.glyphs.size());
            font.glyphs.push_back(std::move(g));
        }

        fonts_.push_back(std::move(font));
    }

    if (err) err->clear();
    return true;
}

const BitmapFontRenderer::Font* BitmapFontRenderer::fontForSize(int size) const {
    if (size < kMinSize || size > kMaxSize) return nullptr;

    const QString wanted = QStringLiteral("Asdex-%1").arg(size);
    for (const Font& f : fonts_) {
        if (f.name == wanted) return &f;
    }
    // Fall back to matching by point size if the name convention differs.
    for (const Font& f : fonts_) {
        if (f.pointSize == size) return &f;
    }
    return nullptr;
}

const BitmapFontRenderer::Atlas* BitmapFontRenderer::ensureAtlas(int size, const QColor& color) {
    const quint64 key = atlasKey(size, color);
    if (auto it = atlases_.find(key); it != atlases_.end())
        return &it.value();

    const Font* font = fontForSize(size);
    if (!font) return nullptr;

    int atlasW = kAtlasPad;
    int atlasH = 0;
    for (const Glyph& g : font->glyphs) {
        atlasW += std::max(g.width, 0) + kAtlasPad;
        atlasH  = std::max(atlasH, g.height);
    }
    atlasH += 2 * kAtlasPad;
    atlasW  = std::max(atlasW, 1);
    atlasH  = std::max(atlasH, 1);

    QImage image(atlasW, atlasH, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const QRgb fg = color.rgba();
    int cursorX = kAtlasPad;

    Atlas atlas;
    atlas.glyphs.reserve(font->glyphs.size());

    for (const Glyph& g : font->glyphs) {
        AtlasGlyph e;
        e.srcRect = QRect(cursorX, kAtlasPad,
                          std::max(g.width, 0), std::max(g.height, 0));
        e.stepX   = g.stepX;
        e.width   = g.width;
        e.height  = g.height;
        e.offsetX = g.offsetX;
        e.offsetY = g.offsetY;
        atlas.glyphs.insert(g.codepoint, e);

        for (int y = 0; y < g.height; ++y) {
            const uint32_t row = (y < g.rows.size()) ? g.rows[y] : 0u;
            auto* scan = reinterpret_cast<QRgb*>(image.scanLine(kAtlasPad + y));
            for (int x = 0; x < g.width; ++x) {
                if (rowBitSet(row, x)) scan[cursorX + x] = fg;
            }
        }
        cursorX += std::max(g.width, 0) + kAtlasPad;
    }

    atlas.pixmap = QPixmap::fromImage(image);
    if (atlas.pixmap.isNull()) return nullptr;

    auto ins = atlases_.insert(key, std::move(atlas));
    return &ins.value();
}

int BitmapFontRenderer::drawTextBaseline(QPainter& p, int originX, int baselineY,
                                         QStringView text, int size, const QColor& color) {
    const Atlas* atlas = ensureAtlas(size, color);
    if (!atlas) return 0;

    const Font* font = fontForSize(size);
    const int fallbackAdvance = font ? font->nominalWidth : 0;

    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    int penX = originX;
    for (QChar ch : text) {
        const uint32_t cp = ch.unicode();
        const auto it = atlas->glyphs.constFind(cp);
        if (it == atlas->glyphs.cend()) {
            penX += fallbackAdvance;
            continue;
        }
        const AtlasGlyph& g = it.value();
        if (!g.srcRect.isEmpty()) {
            const int dstX = penX + g.offsetX;
            const int dstY = baselineY - g.offsetY - g.height;
            p.drawPixmap(dstX, dstY, atlas->pixmap,
                         g.srcRect.x(), g.srcRect.y(),
                         g.srcRect.width(), g.srcRect.height());
        }
        penX += g.stepX;
    }
    return penX - originX;
}

int BitmapFontRenderer::drawTextTopLeft(QPainter& p, int left, int top,
                                        QStringView text, int size, const QColor& color) {
    return drawTextBaseline(p, left, top + ascent(size), text, size, color);
}

QSize BitmapFontRenderer::measureText(QStringView text, int size) const {
    const Font* font = fontForSize(size);
    if (!font) return {};

    int width = 0;
    for (QChar ch : text) {
        const auto it = font->glyphIndex.constFind(ch.unicode());
        width += (it == font->glyphIndex.cend())
               ? font->nominalWidth
               : font->glyphs[it.value()].stepX;
    }
    return QSize(width, font->ascent + font->descent);
}

int BitmapFontRenderer::ascent(int size) const {
    const Font* f = fontForSize(size);
    return f ? f->ascent : 0;
}

int BitmapFontRenderer::descent(int size) const {
    const Font* f = fontForSize(size);
    return f ? f->descent : 0;
}

int BitmapFontRenderer::lineHeight(int size) const {
    const Font* f = fontForSize(size);
    return f ? (f->ascent + f->descent) : 0;
}

} // namespace asdex
