#pragma once

#include <QCursor>
#include <QHash>
#include <QString>

namespace asdex {

/**
 * Loads the ASDE-X .cur cursor files from `assetsDir` (e.g. "asdex/assets").
 * Mirrors CRC's AsdexWindowManager.CreateCursor end-to-end: reads the
 * UInt16-LE hotspot at offsets 10 & 12, then decodes the DIB payload by
 * hand — XOR color bitmap on top, AND transparency mask on bottom (so the
 * DIB header's height = visual height × 2), RGBQUAD reserved byte ignored.
 *
 * Decoding manually instead of going through Qt's generic ICO plugin keeps
 * the .cur semantics (especially the AND-mask transparency) identical to
 * Avalonia's, which is what CRC ships — the plugin path occasionally
 * softens edges that should stay binary-transparent.
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
