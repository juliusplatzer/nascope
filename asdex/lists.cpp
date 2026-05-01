#include "lists.h"

#include "font.h"
#include "utils.h"

#include <QColor>
#include <QDateTime>
#include <QLatin1Char>
#include <QPainter>
#include <QPen>
#include <QString>
#include <QtGlobal>

namespace asdex {

namespace {

// Shared list text color — same green CRC uses everywhere in the radar lists.
const QColor  kListTextColor    (0, 248, 0);
constexpr int kCoastListFontSize = 2;
constexpr int kHeaderFieldWidth  = 12;  // chars — right-aligned date/time field
constexpr int kCoastLineSpacing  = 5;   // px between coast-list rows
// Future: aircraft rows live in a 15-char field starting at the same anchor.
// constexpr int kListFieldWidth  = 15;

QPointF scaleToWidget(QPointF ref, const QSize& widgetSize) {
    return {ref.x() * widgetSize.width()  / double(Lists::kReferenceWidth),
            ref.y() * widgetSize.height() / double(Lists::kReferenceHeight)};
}

} // namespace

void Lists::draw(QPainter& p, const QSize& widgetSize, BitmapFontRenderer& fonts) const {
    drawCoastList  (p, widgetSize, fonts);
    drawPreviewArea(p, widgetSize, fonts);
}

void Lists::drawCoastList(QPainter& p, const QSize& widgetSize,
                          BitmapFontRenderer& fonts) const {
    const QPointF anchor = scaleToWidget(coastListLocation_, widgetSize);

    // UTC — ASDE-X / ATC convention.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString dateLine = now.toString(QStringLiteral("MM/dd/yy"))
                                .rightJustified(kHeaderFieldWidth, QLatin1Char(' '));
    const QString timeLine = now.toString(QStringLiteral("HHmm/ss"))
                                .rightJustified(kHeaderFieldWidth, QLatin1Char(' '));

    const int x          = qRound(anchor.x());
    const int y          = qRound(anchor.y());
    const int rowAdvance = fonts.lineHeight(kCoastListFontSize) + kCoastLineSpacing;

    const QColor color = applyBrightness(kListTextColor, defaultBrightness());
    fonts.drawTextTopLeft(p, x, y,              dateLine, kCoastListFontSize, color);
    fonts.drawTextTopLeft(p, x, y + rowAdvance, timeLine, kCoastListFontSize, color);
}

void Lists::drawPreviewArea(QPainter& p, const QSize& widgetSize,
                            BitmapFontRenderer& fonts) const {
    const PreviewArea& pa = preview_;
    const QPointF anchor   = scaleToWidget(pa.location, widgetSize);
    const int     x        = qRound(anchor.x());
    const int     y0       = qRound(anchor.y());
    const int     charH    = fonts.lineHeight(pa.fontSize);
    const int     lineStep = charH + pa.lineSpacing;

    const QColor color = applyBrightness(kListTextColor, defaultBrightness());

    // Build the line list in render order. Empty entries (e.g. an absent
    // SystemResponse) are kept so warning + command lines stay on a fixed grid
    // — that's CRC's `num2` = 3 status slots + 1 per active warning.
    QStringList lines;
    lines << QStringLiteral("RWY CFG: %1").arg(pa.rwyConfig);
    lines << QStringLiteral("TWR CFG:%1") .arg(pa.twrConfig);   // intentional: no space after the colon
    lines << pa.systemResponse;
    if (pa.arrAlertsOff)
        lines << QStringLiteral("ARR ALERTS OFF:%1").arg(pa.arrAlertsOffPositions);
    if (pa.trkAlertInhib)
        lines << QStringLiteral("TRK ALERT INHIB");
    for (const QString& cmd : pa.commandLines) lines << cmd;

    // Cursor underline first so the text (z = mZCoord + 0.1) lands on top of
    // it (z = mZCoord). Currently always off; framework is ready for a future
    // input manager to flip showCursor + populate the column / line.
    if (pa.showCursor) {
        int statusLineCount = 3;                          // num2 starts at 3 (RWY/TWR/SR)
        if (pa.arrAlertsOff)  ++statusLineCount;
        if (pa.trkAlertInhib) ++statusLineCount;

        const int charWidth = fonts.measureText(QStringLiteral("M"), pa.fontSize).width();
        constexpr int kFontSpacing = 0;                   // ASDEX bitmap font is effectively monospace here

        const int cursorX = x + charWidth * pa.cursorColumn
                              + kFontSpacing * (pa.cursorColumn - 1);
        // Bumped down one line vs. CRC's literal formula — lands at the top
        // of the next line so the underline visually sits beneath the text.
        const int cursorY = y0 + lineStep * (pa.cursorLine + statusLineCount + 1);

        QPen pen(color, 1.0);
        pen.setStyle(Qt::SolidLine);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(pen);
        p.drawLine(cursorX, cursorY, cursorX + charWidth, cursorY);
        p.restore();
    }

    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].isEmpty()) continue;        // reserve the slot, draw nothing
        fonts.drawTextTopLeft(p, x, y0 + i * lineStep, lines[i], pa.fontSize, color);
    }
}

} // namespace asdex
