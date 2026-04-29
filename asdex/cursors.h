#pragma once

#include <QCursor>
#include <QHash>
#include <QString>

namespace asdex {

/**
 * Loads the ASDE-X .cur cursor files from `assetsDir` (e.g. "asdex/assets").
 * Mirrors CRC's AsdexWindowManager.CreateCursor: parses the .cur header for
 * the hotspot (UInt16 x at offset 10, UInt16 y at offset 12, both LE) and
 * decodes the embedded image via Qt's ICO image plugin.
 *
 * Returned hash keys (paired with the file they're loaded from):
 *   "scope_cursor"      ← Asdex.cur
 *   "dcb_cursor"        ← AsdexDcb.cur
 *   "captured_cursor"   ← AsdexCaptured.cur
 *   "select_cursor"     ← AsdexSelect.cur
 *   "move_cursor"       ← AsdexMove.cur
 *   "updown_cursor"     ← AsdexUpDown.cur
 *   "leftright_cursor"  ← AsdexLeftRight.cur
 *
 * Files that fail to load are skipped with a message appended to `*err` (if
 * non-null); the surviving cursors are still returned.
 */
QHash<QString, QCursor> loadCursors(const QString& assetsDir, QString* err = nullptr);

} // namespace asdex
