#include "asdex/render/cursors.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QList>
#include <QPixmap>
#include <QStringList>

namespace asdex {
namespace {

struct CursorFile {
    CursorType type;
    const char* file;
};

constexpr CursorFile kCursorFiles[] = {
    {CursorType::Scope,     "Asdex.cur"},
    {CursorType::Dcb,       "AsdexDcb.cur"},
    {CursorType::Captured,  "AsdexCaptured.cur"},
    {CursorType::Select,    "AsdexSelect.cur"},
    {CursorType::Move,      "AsdexMove.cur"},
    {CursorType::UpDown,    "AsdexUpDown.cur"},
    {CursorType::LeftRight, "AsdexLeftRight.cur"},
};

quint16 u16le(const QByteArray& bytes, qsizetype offset) {
    return quint16(uchar(bytes[offset]))
         | (quint16(uchar(bytes[offset + 1])) << 8);
}

quint32 u32le(const QByteArray& bytes, qsizetype offset) {
    return quint32(uchar(bytes[offset]))
         | (quint32(uchar(bytes[offset + 1])) << 8)
         | (quint32(uchar(bytes[offset + 2])) << 16)
         | (quint32(uchar(bytes[offset + 3])) << 24);
}

qint32 s32le(const QByteArray& bytes, qsizetype offset) {
    return qint32(u32le(bytes, offset));
}

bool maskBitIsSet(const uchar* row, int x) {
    return (row[x >> 3] & uchar(0x80 >> (x & 7))) != 0;
}

bool decodeCurImage(const QByteArray& bytes, DecodedCursor& decoded, QString* error) {
    if (bytes.size() < 22) {
        if (error) *error = QStringLiteral("truncated .cur");
        return false;
    }

    const quint16 reserved = u16le(bytes, 0);
    const quint16 type = u16le(bytes, 2);
    const quint16 count = u16le(bytes, 4);
    if (reserved != 0 || type != 2 || count < 1) {
        if (error) *error = QStringLiteral("not a valid .cur file");
        return false;
    }

    const int widthFromDir = uchar(bytes[6]) == 0 ? 256 : uchar(bytes[6]);
    const int heightFromDir = uchar(bytes[7]) == 0 ? 256 : uchar(bytes[7]);
    decoded.hotspot = QPoint(int(u16le(bytes, 10)), int(u16le(bytes, 12)));

    const quint32 imageSize = u32le(bytes, 14);
    const quint32 imageOffset = u32le(bytes, 18);
    if (imageOffset + imageSize > quint32(bytes.size()) || imageSize < 40) {
        if (error) *error = QStringLiteral("invalid .cur image bounds");
        return false;
    }

    const qsizetype dib = qsizetype(imageOffset);
    const quint32 headerSize = u32le(bytes, dib + 0);
    if (headerSize < 40) {
        if (error) *error = QStringLiteral("unsupported DIB header");
        return false;
    }

    const int dibWidth = int(s32le(bytes, dib + 4));
    const int dibHeight = int(s32le(bytes, dib + 8));
    const quint16 planes = u16le(bytes, dib + 12);
    const quint16 bpp = u16le(bytes, dib + 14);
    const quint32 compression = u32le(bytes, dib + 16);
    const quint32 colorsUsed = u32le(bytes, dib + 32);

    if (planes != 1 || compression != 0) {
        if (error) *error = QStringLiteral("unsupported compressed cursor DIB");
        return false;
    }

    const int width = dibWidth;
    const int height = qAbs(dibHeight) / 2;
    if (width <= 0 || height <= 0 || width != widthFromDir || height != heightFromDir) {
        if (error) *error = QStringLiteral("cursor size mismatch");
        return false;
    }

    if (bpp != 1 && bpp != 8 && bpp != 32) {
        if (error) *error = QStringLiteral("unsupported cursor depth: %1 bpp").arg(bpp);
        return false;
    }

    const qsizetype paletteCount =
        bpp <= 8 ? qsizetype(colorsUsed ? colorsUsed : (1u << bpp)) : 0;
    const qsizetype paletteOffset = dib + qsizetype(headerSize);
    const qsizetype xorStride = ((qsizetype(width) * bpp + 31) / 32) * 4;
    const qsizetype andStride = ((qsizetype(width) + 31) / 32) * 4;
    const qsizetype xorOffset = paletteOffset + paletteCount * 4;
    const qsizetype xorBytes = xorStride * height;
    const qsizetype andOffset = xorOffset + xorBytes;
    const qsizetype andBytes = andStride * height;
    if (andOffset + andBytes > qsizetype(imageOffset + imageSize)) {
        if (error) *error = QStringLiteral("truncated cursor bitmap data");
        return false;
    }

    QList<QRgb> palette;
    palette.reserve(int(paletteCount));
    for (qsizetype i = 0; i < paletteCount; ++i) {
        const qsizetype p = paletteOffset + i * 4;
        const int b = uchar(bytes[p + 0]);
        const int g = uchar(bytes[p + 1]);
        const int r = uchar(bytes[p + 2]);
        palette.push_back(qRgb(r, g, b));
    }

    decoded.image = QImage(width, height, QImage::Format_ARGB32);
    decoded.image.fill(Qt::transparent);

    for (int y = 0; y < height; ++y) {
        const int srcY = height - 1 - y;
        const auto* xorRow =
            reinterpret_cast<const uchar*>(bytes.constData() + xorOffset + xorStride * srcY);
        const auto* andRow =
            reinterpret_cast<const uchar*>(bytes.constData() + andOffset + andStride * srcY);
        auto* dst = reinterpret_cast<QRgb*>(decoded.image.scanLine(y));

        for (int x = 0; x < width; ++x) {
            const bool transparent = maskBitIsSet(andRow, x);

            if (bpp == 32) {
                const uchar* px = xorRow + x * 4;
                const int b = px[0];
                const int g = px[1];
                const int r = px[2];
                const int a = transparent ? 0 : px[3];
                dst[x] = qRgba(r, g, b, a);
            } else if (bpp == 8) {
                const int index = xorRow[x];
                const QRgb color = qsizetype(index) < palette.size()
                                 ? palette[index]
                                 : qRgb(0, 0, 0);
                dst[x] = transparent ? qRgba(0, 0, 0, 0)
                                     : qRgba(qRed(color), qGreen(color), qBlue(color), 255);
            } else {
                const int index = maskBitIsSet(xorRow, x) ? 1 : 0;
                const QRgb color = qsizetype(index) < palette.size()
                                 ? palette[index]
                                 : qRgb(0, 0, 0);
                dst[x] = transparent ? qRgba(0, 0, 0, 0)
                                     : qRgba(qRed(color), qGreen(color), qBlue(color), 255);
            }
        }
    }

    return true;
}

bool loadCursor(const QString& path, QCursor& cursor, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return false;
    }

    DecodedCursor decoded;
    QString itemError;
    if (!decodeCurImage(file.readAll(), decoded, &itemError)) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, itemError);
        return false;
    }

    QPixmap pixmap = QPixmap::fromImage(decoded.image);
    pixmap.setDevicePixelRatio(1.0);
    cursor = QCursor(pixmap, decoded.hotspot.x(), decoded.hotspot.y());
    return true;
}

} // namespace

bool CursorSet::loadFromAssetsDir(const QString& assetsDir, QString* error) {
    QHash<CursorType, QCursor> loaded;
    QStringList errors;

    for (const CursorFile& cursorFile : kCursorFiles) {
        const QString path = QDir(assetsDir).filePath(QString::fromUtf8(cursorFile.file));
        QCursor cursor;
        QString itemError;
        if (loadCursor(path, cursor, &itemError)) {
            loaded.insert(cursorFile.type, cursor);
        } else {
            errors << itemError;
        }
    }

    if (loaded.isEmpty()) {
        if (error) *error = errors.join(QStringLiteral("; "));
        return false;
    }

    cursors_ = std::move(loaded);
    if (error && !errors.isEmpty()) *error = errors.join(QStringLiteral("; "));
    return errors.isEmpty();
}

bool CursorSet::has(CursorType type) const {
    return cursors_.contains(type);
}

const QCursor& CursorSet::cursor(CursorType type) const {
    auto it = cursors_.constFind(type);
    if (it != cursors_.constEnd()) return it.value();

    static const QCursor fallback(Qt::ArrowCursor);
    return fallback;
}

} // namespace asdex
