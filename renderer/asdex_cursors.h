#pragma once

#include <QCursor>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QString>

#include <type_traits>

namespace renderer::asdex {

enum class CursorType {
    Scope,
    Dcb,
    Captured,
    Select,
    Move,
    UpDown,
    LeftRight,
};

inline size_t qHash(CursorType type, size_t seed = 0) noexcept {
    using Underlying = std::underlying_type_t<CursorType>;
    return ::qHash(static_cast<Underlying>(type), seed);
}

struct DecodedCursor {
    QImage image;
    QPoint hotspot;
};

class CursorSet {
public:
    bool loadFromAssetsDir(const QString& assetsDir, QString* error = nullptr);

    bool has(CursorType type) const;
    const QCursor& cursor(CursorType type) const;

private:
    QHash<CursorType, QCursor> cursors_;
};

} // namespace renderer::asdex
