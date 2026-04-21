#include "cursors.h"

#include <QBitmap>
#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QVector>

#include <cstdint>
#include <cstring>

namespace asdex {

namespace {

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

bool readRows(const QByteArray& data, qsizetype& off, QVector<uint32_t>& rows) {
    quint32 count = 0;
    if (!readPod(data, off, count)) return false;
    rows.resize(int(count));
    for (quint32 i = 0; i < count; ++i) {
        if (!readPod(data, off, rows[int(i)])) return false;
    }
    return true;
}

/** Pack 32-bit MSB-first rows into a QBitmap (1-bpp, MSB-first byte order). */
QBitmap rowsToBitmap(const QVector<uint32_t>& rows, int w, int h) {
    if (w <= 0 || h <= 0) return {};

    const int bytesPerRow = (w + 7) / 8;
    QByteArray packed(h * bytesPerRow, '\0');

    for (int y = 0; y < h; ++y) {
        const uint32_t row = (y < rows.size()) ? rows[y] : 0u;
        uchar* dst = reinterpret_cast<uchar*>(packed.data() + y * bytesPerRow);
        if (bytesPerRow >= 1) dst[0] = uchar((row >> 24) & 0xFF);
        if (bytesPerRow >= 2) dst[1] = uchar((row >> 16) & 0xFF);
        if (bytesPerRow >= 3) dst[2] = uchar((row >>  8) & 0xFF);
        if (bytesPerRow >= 4) dst[3] = uchar((row >>  0) & 0xFF);

        // Zero out the unused low bits in the last byte so stray 1s in the
        // encoded row past `width` don't paint outside the cursor.
        const int rem = w & 7;
        if (rem != 0) dst[bytesPerRow - 1] &= uchar(0xFFu << (8 - rem));
    }

    return QBitmap::fromData(QSize(w, h),
                             reinterpret_cast<const uchar*>(packed.constData()),
                             QImage::Format_Mono);
}

} // namespace

QHash<QString, QCursor> loadCursors(const QString& path, QString* err) {
    const auto fail = [&](QString msg) {
        if (err) *err = std::move(msg);
        return QHash<QString, QCursor>{};
    };

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot open %1: %2").arg(path, f.errorString()));

    const QByteArray data = f.readAll();
    qsizetype off = 0;

    QByteArray magic;
    if (!readBytes(data, off, magic, 8) || magic != QByteArray("ASDXCUR1", 8))
        return fail(QStringLiteral("invalid cursor binary magic"));

    quint32 count = 0;
    if (!readPod(data, off, count))
        return fail(QStringLiteral("truncated cursor binary header"));

    QHash<QString, QCursor> out;
    out.reserve(int(count));

    for (quint32 i = 0; i < count; ++i) {
        quint16 nameLen = 0;
        if (!readPod(data, off, nameLen))
            return fail(QStringLiteral("truncated cursor name length"));

        QByteArray nameBytes;
        if (!readBytes(data, off, nameBytes, qsizetype(nameLen)))
            return fail(QStringLiteral("truncated cursor name"));
        const QString name = QString::fromUtf8(nameBytes);

        quint16 hotX = 0, hotY = 0, width = 0, height = 0;
        quint32 stepX = 0;
        if (!readPod(data, off, hotX) ||
            !readPod(data, off, hotY) ||
            !readPod(data, off, width) ||
            !readPod(data, off, height) ||
            !readPod(data, off, stepX))
            return fail(QStringLiteral("truncated cursor metadata for %1").arg(name));

        QVector<uint32_t> fgRows, maskRows;
        if (!readRows(data, off, fgRows))
            return fail(QStringLiteral("truncated foreground rows for %1").arg(name));
        if (!readRows(data, off, maskRows))
            return fail(QStringLiteral("truncated mask rows for %1").arg(name));

        // White-on-transparent: bitmap all 0 (→ color0 = white for visible
        // pixels), mask selects which pixels are drawn.
        QBitmap bitmap{int(width), int(height)};
        bitmap.fill(Qt::color0);
        const QBitmap mask = rowsToBitmap(maskRows, int(width), int(height));

        out.insert(name, QCursor(bitmap, mask, int(hotX), int(hotY)));
    }

    return out;
}

} // namespace asdex
