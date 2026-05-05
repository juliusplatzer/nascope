#include "cursors.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QList>
#include <QPixmap>
#include <QStringList>

namespace asdex {

namespace {

struct Mapping {
    const char* file;  // basename inside assetsDir
    const char* key;   // public name in the returned hash
};

// Filenames mirror CRC's AsdexWindowManager.CreateCursor calls; keys are kept
// in nascope's existing snake_case style to avoid churn at every call site.
constexpr Mapping kCursorFiles[] = {
    { "Asdex.cur",          "scope_cursor"     },
    { "AsdexDcb.cur",       "dcb_cursor"       },
    { "AsdexCaptured.cur",  "captured_cursor"  },
    { "AsdexSelect.cur",    "select_cursor"    },
    { "AsdexMove.cur",      "move_cursor"      },
    { "AsdexUpDown.cur",    "updown_cursor"    },
    { "AsdexLeftRight.cur", "leftright_cursor" },
};

// Little-endian readers for the on-disk .cur / DIB layout.
quint16 u16le(const QByteArray& b, qsizetype o) {
    return quint16(uchar(b[o])) |
           (quint16(uchar(b[o + 1])) << 8);
}
quint32 u32le(const QByteArray& b, qsizetype o) {
    return quint32(uchar(b[o])) |
           (quint32(uchar(b[o + 1])) << 8) |
           (quint32(uchar(b[o + 2])) << 16) |
           (quint32(uchar(b[o + 3])) << 24);
}
qint32 s32le(const QByteArray& b, qsizetype o) {
    return qint32(u32le(b, o));
}

// AND-mask bit at column x — MSB-first within each byte (Windows DIB convention).
bool maskBitIsSet(const uchar* row, int x) {
    return (row[x >> 3] & uchar(0x80 >> (x & 7))) != 0;
}

/**
 * Decodes a .cur file's first cursor entry into an ARGB image plus its
 * hotspot, treating the on-disk bytes the way Avalonia/CRC do — top half
 * of the DIB is the XOR color bitmap, bottom half is the AND transparency
 * mask, the RGBQUAD reserved byte in palette entries is ignored.
 *
 * Sidesteps Qt's ICO/CUR image plugin so the result matches CRC down to
 * the soft-edge pixel, where Qt's generic decoder occasionally interprets
 * the AND mask differently and produces a slightly softened edge.
 */
bool decodeCurImage(const QByteArray& bytes,
                    QImage& image, int& hotX, int& hotY,
                    QString* err) {
    if (bytes.size() < 22) {
        if (err) *err = QStringLiteral("truncated .cur");
        return false;
    }

    const quint16 reserved = u16le(bytes, 0);
    const quint16 type     = u16le(bytes, 2);
    const quint16 count    = u16le(bytes, 4);
    if (reserved != 0 || type != 2 || count < 1) {
        if (err) *err = QStringLiteral("not a valid .cur file");
        return false;
    }

    // Directory-entry width/height: a 0 byte means 256 in .cur convention.
    const int widthFromDir  = uchar(bytes[6]) == 0 ? 256 : uchar(bytes[6]);
    const int heightFromDir = uchar(bytes[7]) == 0 ? 256 : uchar(bytes[7]);

    hotX = int(u16le(bytes, 10));
    hotY = int(u16le(bytes, 12));

    const quint32 imageSize = u32le(bytes, 14);
    const quint32 imageOff  = u32le(bytes, 18);
    if (imageOff + imageSize > quint32(bytes.size()) || imageSize < 40) {
        if (err) *err = QStringLiteral("invalid .cur image bounds");
        return false;
    }

    const qsizetype dib = qsizetype(imageOff);

    // BITMAPINFOHEADER (40 bytes is the minimum supported size).
    const quint32 headerSize = u32le(bytes, dib + 0);
    if (headerSize < 40) {
        if (err) *err = QStringLiteral("unsupported DIB header");
        return false;
    }
    const int     dibW    = int(s32le(bytes, dib + 4));
    const int     dibH    = int(s32le(bytes, dib + 8));   // visual height × 2 (XOR + AND)
    const quint16 planes  = u16le(bytes, dib + 12);
    const quint16 bpp     = u16le(bytes, dib + 14);
    const quint32 comp    = u32le(bytes, dib + 16);
    const quint32 clrUsed = u32le(bytes, dib + 32);
    if (planes != 1 || comp != 0) {
        if (err) *err = QStringLiteral("unsupported compressed cursor DIB");
        return false;
    }

    const int w = dibW;
    const int h = qAbs(dibH) / 2;
    if (w <= 0 || h <= 0 || w != widthFromDir || h != heightFromDir) {
        if (err) *err = QStringLiteral("cursor size mismatch");
        return false;
    }
    if (bpp != 1 && bpp != 8 && bpp != 32) {
        if (err) *err = QStringLiteral("unsupported cursor depth: %1 bpp").arg(bpp);
        return false;
    }

    const qsizetype paletteCount =
        bpp <= 8 ? qsizetype(clrUsed ? clrUsed : (1u << bpp)) : 0;
    const qsizetype paletteOff = dib + qsizetype(headerSize);

    // DIB scanline strides are padded to 4 bytes.
    const qsizetype xorStride = ((qsizetype(w) * bpp + 31) / 32) * 4;
    const qsizetype andStride = ((qsizetype(w)        + 31) / 32) * 4;
    const qsizetype xorOff   = paletteOff + paletteCount * 4;
    const qsizetype xorBytes = xorStride * h;
    const qsizetype andOff   = xorOff + xorBytes;
    const qsizetype andBytes = andStride * h;
    if (andOff + andBytes > qsizetype(imageOff + imageSize)) {
        if (err) *err = QStringLiteral("truncated cursor bitmap data");
        return false;
    }

    QList<QRgb> palette;
    palette.reserve(int(paletteCount));
    for (qsizetype i = 0; i < paletteCount; ++i) {
        const qsizetype p = paletteOff + i * 4;
        const int b = uchar(bytes[p + 0]);
        const int g = uchar(bytes[p + 1]);
        const int r = uchar(bytes[p + 2]);
        // Ignore RGBQUAD's reserved byte — in real cursor files it's often 0
        // and treating it as alpha would silently zero out the entire glyph.
        palette.push_back(qRgb(r, g, b));
    }

    image = QImage(w, h, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    for (int y = 0; y < h; ++y) {
        // DIB rows are bottom-up.
        const int srcY = h - 1 - y;
        const uchar* xorRow = reinterpret_cast<const uchar*>(bytes.constData() + xorOff + xorStride * srcY);
        const uchar* andRow = reinterpret_cast<const uchar*>(bytes.constData() + andOff + andStride * srcY);
        QRgb* dst = reinterpret_cast<QRgb*>(image.scanLine(y));

        for (int x = 0; x < w; ++x) {
            const bool transparent = maskBitIsSet(andRow, x);

            if (bpp == 32) {
                const uchar* px = xorRow + x * 4;
                const int b = px[0];
                const int g = px[1];
                const int r = px[2];
                int a = px[3];
                if (transparent) a = 0;
                dst[x] = qRgba(r, g, b, a);
            } else if (bpp == 8) {
                const int index = xorRow[x];
                const QRgb c = qsizetype(index) < palette.size()
                             ? palette[index] : qRgb(0, 0, 0);
                dst[x] = transparent ? qRgba(0, 0, 0, 0)
                                     : qRgba(qRed(c), qGreen(c), qBlue(c), 255);
            } else {  // 1 bpp
                const int index = (xorRow[x >> 3] & uchar(0x80 >> (x & 7))) ? 1 : 0;
                const QRgb c = qsizetype(index) < palette.size()
                             ? palette[index] : qRgb(0, 0, 0);
                dst[x] = transparent ? qRgba(0, 0, 0, 0)
                                     : qRgba(qRed(c), qGreen(c), qBlue(c), 255);
            }
        }
    }

    return true;
}

bool loadOne(const QString& path, QCursor& out, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    const QByteArray bytes = f.readAll();

    QImage img;
    int    hotX = 0, hotY = 0;
    QString itemErr;
    if (!decodeCurImage(bytes, img, hotX, hotY, &itemErr)) {
        if (err) *err = QStringLiteral("%1: %2").arg(path, itemErr);
        return false;
    }

    QPixmap pix = QPixmap::fromImage(img);
    pix.setDevicePixelRatio(1.0);  // CRC's PixelPoint hotspot is in source pixels.
    out = QCursor(pix, hotX, hotY);
    return true;
}

} // namespace

QHash<QString, QCursor> loadCursors(const QString& assetsDir, QString* err) {
    QHash<QString, QCursor> out;
    QStringList errors;

    for (const Mapping& m : kCursorFiles) {
        const QString path = QDir(assetsDir).filePath(QString::fromUtf8(m.file));
        QCursor cur;
        QString itemErr;
        if (loadOne(path, cur, &itemErr)) {
            out.insert(QString::fromUtf8(m.key), cur);
        } else {
            errors << itemErr;
        }
    }

    if (err && !errors.isEmpty()) *err = errors.join(QStringLiteral("; "));
    return out;
}

} // namespace asdex
