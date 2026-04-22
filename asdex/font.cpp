#include "font.h"

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QPainter>

#include <cstring>
#include <utility>

namespace asdex {

namespace {

/*
Binary layout (little-endian), repeated (size, font) records until EOF:

  int32   size                (font identifier, e.g. 1..6)
  int32   lineHeight          (distance from top-of-line to baseline)
  int32   atlasWidth
  int32   atlasHeight
  bytes   bitmap[W*H]         (8-bit grayscale coverage, row-major, top-left origin)
  int32   glyphCount
  per glyph:
    int32 codepoint
    int32 width, height       (glyph rect in the atlas)
    int32 textureOffset       (x-offset in the atlas; y is always 0)
    int32 bearingX
    int32 bearingY            (glyph top above baseline)
    int32 advance             (pen advance in pixels)
*/

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

quint64 atlasKey(int size, const QColor& color) {
    return (quint64(size) << 32) | quint64(color.rgba());
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

    while (off < data.size()) {
        qint32 size = 0, lineHeight = 0, atlasW = 0, atlasH = 0;
        if (!readPod(data, off, size)       ||
            !readPod(data, off, lineHeight) ||
            !readPod(data, off, atlasW)     ||
            !readPod(data, off, atlasH))
            return fail(QStringLiteral("truncated font header"));

        if (atlasW < 0 || atlasH < 0)
            return fail(QStringLiteral("invalid atlas dimensions for size %1").arg(size));

        Font font;
        font.lineHeight  = int(lineHeight);
        font.atlasWidth  = int(atlasW);
        font.atlasHeight = int(atlasH);

        const qsizetype pixelCount = qsizetype(atlasW) * qsizetype(atlasH);
        if (!readBytes(data, off, font.bitmap, pixelCount))
            return fail(QStringLiteral("truncated atlas bitmap for size %1").arg(size));

        qint32 glyphCount = 0;
        if (!readPod(data, off, glyphCount))
            return fail(QStringLiteral("truncated glyph count for size %1").arg(size));

        font.glyphs.reserve(int(glyphCount));
        font.glyphIndex.reserve(int(glyphCount));
        for (qint32 gi = 0; gi < glyphCount; ++gi) {
            qint32 cp = 0, w = 0, h = 0, tex = 0, bx = 0, by = 0, adv = 0;
            if (!readPod(data, off, cp)  ||
                !readPod(data, off, w)   ||
                !readPod(data, off, h)   ||
                !readPod(data, off, tex) ||
                !readPod(data, off, bx)  ||
                !readPod(data, off, by)  ||
                !readPod(data, off, adv))
                return fail(QStringLiteral("truncated glyph record in size %1").arg(size));

            Glyph g;
            g.codepoint     = uint32_t(cp);
            g.width         = int(w);
            g.height        = int(h);
            g.textureOffset = int(tex);
            g.bearingX      = int(bx);
            g.bearingY      = int(by);
            g.advance       = int(adv);

            font.glyphIndex.insert(g.codepoint, font.glyphs.size());
            font.glyphs.push_back(g);
        }

        fonts_.insert(int(size), std::move(font));
    }

    if (err) err->clear();
    return true;
}

const BitmapFontRenderer::Font* BitmapFontRenderer::fontForSize(int size) const {
    auto it = fonts_.constFind(size);
    return (it == fonts_.cend()) ? nullptr : &it.value();
}

const QPixmap* BitmapFontRenderer::ensureAtlas(int size, const QColor& color) {
    const quint64 key = atlasKey(size, color);
    if (auto it = atlases_.find(key); it != atlases_.end())
        return &it.value();

    const Font* font = fontForSize(size);
    if (!font || font->bitmap.isEmpty()) return nullptr;

    const int w = font->atlasWidth;
    const int h = font->atlasHeight;
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);

    const int cr = color.red();
    const int cg = color.green();
    const int cb = color.blue();
    const int ca = color.alpha();
    const auto* rawBase = reinterpret_cast<const uchar*>(font->bitmap.constData());

    for (int y = 0; y < h; ++y) {
        const uchar* src = rawBase + qsizetype(y) * qsizetype(w);
        QRgb* dst = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            // Combine glyph coverage with the target color's alpha, then
            // premultiply RGB — required by Format_ARGB32_Premultiplied.
            const int a = int(src[x]) * ca / 255;
            dst[x] = qRgba(cr * a / 255, cg * a / 255, cb * a / 255, a);
        }
    }

    QPixmap pm = QPixmap::fromImage(std::move(image));
    if (pm.isNull()) return nullptr;

    auto ins = atlases_.insert(key, std::move(pm));
    return &ins.value();
}

int BitmapFontRenderer::drawTextBaseline(QPainter& p, int originX, int baselineY,
                                         QStringView text, int size, const QColor& color) {
    const QPixmap* atlas = ensureAtlas(size, color);
    if (!atlas) return 0;

    const Font* font = fontForSize(size);
    if (!font) return 0;

    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    int penX = originX;
    for (QChar ch : text) {
        const auto it = font->glyphIndex.constFind(ch.unicode());
        if (it == font->glyphIndex.cend()) continue;  // unknown glyphs take no space
        const Glyph& g = font->glyphs[it.value()];
        if (g.width > 0 && g.height > 0) {
            const int dstX = penX + g.bearingX;
            const int dstY = baselineY - g.bearingY;
            p.drawPixmap(dstX, dstY, *atlas,
                         g.textureOffset, 0, g.width, g.height);
        }
        penX += g.advance;
    }
    return penX - originX;
}

int BitmapFontRenderer::drawTextTopLeft(QPainter& p, int left, int top,
                                        QStringView text, int size, const QColor& color) {
    const Font* font = fontForSize(size);
    if (!font) return 0;
    return drawTextBaseline(p, left, top + font->lineHeight, text, size, color);
}

QSize BitmapFontRenderer::measureText(QStringView text, int size) const {
    const Font* font = fontForSize(size);
    if (!font) return {};

    int width = 0;
    for (QChar ch : text) {
        const auto it = font->glyphIndex.constFind(ch.unicode());
        if (it == font->glyphIndex.cend()) continue;
        width += font->glyphs[it.value()].advance;
    }
    return QSize(width, font->lineHeight);
}

int BitmapFontRenderer::lineHeight(int size) const {
    const Font* f = fontForSize(size);
    return f ? f->lineHeight : 0;
}

} // namespace asdex
