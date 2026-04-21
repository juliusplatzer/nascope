#pragma once

#include <QCursor>
#include <QHash>
#include <QString>

namespace asdex {

/**
 * Load the ASDE-X white-on-transparent bitmap cursors from a packed binary.
 *
 * Binary layout (little-endian):
 *   magic[8]     = "ASDXCUR1"
 *   uint32       = cursorCount
 *   repeated cursorCount times:
 *     uint16     = nameLen
 *     bytes[nameLen] UTF-8 cursor name
 *     uint16     = hotspotX, hotspotY, width, height
 *     uint32     = stepX
 *     uint32 + uint32[] = foreground rows (ignored — always rendered white)
 *     uint32 + uint32[] = mask rows (left-aligned MSB-first 1-bpp)
 *
 * Returned cursors paint white where mask == 1, transparent elsewhere.
 * On parse failure an empty hash is returned and `*err` (if non-null) is set.
 */
QHash<QString, QCursor> loadCursors(const QString& path, QString* err = nullptr);

} // namespace asdex
