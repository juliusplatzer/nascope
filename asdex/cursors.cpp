#include "cursors.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPixmap>
#include <QStringList>

#include <cstdint>

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

quint16 readU16LE(const QByteArray& bytes, qsizetype off) {
    return static_cast<quint16>(static_cast<uchar>(bytes[off])) |
           (static_cast<quint16>(static_cast<uchar>(bytes[off + 1])) << 8);
}

bool loadOne(const QString& path, QCursor& out, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    const QByteArray bytes = f.readAll();
    // .cur header: ICONDIR (6) + ICONDIRENTRY[0] partial (4) = 10 bytes, then
    // hotspotX (2), hotspotY (2). CRC reads exactly the same way.
    if (bytes.size() < 14) {
        if (err) *err = QStringLiteral("%1: truncated .cur header").arg(path);
        return false;
    }
    const int hotX = readU16LE(bytes, 10);
    const int hotY = readU16LE(bytes, 12);

    // Qt's ICO image plugin handles .cur (same on-disk shape as .ico). Try
    // an explicit format hint first, then fall back to auto-detect.
    QPixmap pix(path, "ico");
    if (pix.isNull()) pix = QPixmap(path);
    if (pix.isNull()) {
        if (err) *err = QStringLiteral("%1: cannot decode .cur image (Qt ICO plugin missing?)").arg(path);
        return false;
    }

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
