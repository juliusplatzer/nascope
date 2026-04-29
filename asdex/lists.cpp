#include "lists.h"

#include "font.h"
#include "utils.h"

#include <QColor>
#include <QDateTime>
#include <QLatin1Char>
#include <QPainter>
#include <QString>
#include <QtGlobal>

namespace asdex {

namespace {

const QColor  kCoastListColor   (0, 248, 0);
constexpr int kCoastListFontSize = 2;
constexpr int kHeaderFieldWidth  = 12;  // chars — right-aligned date/time field
constexpr int kLineSpacing       = 5;   // px between list rows (shared by all lists)
// Future: aircraft rows live in a 15-char field starting at the same anchor.
// constexpr int kListFieldWidth  = 15;

QPointF scaleToWidget(QPointF ref, const QSize& widgetSize) {
    return {ref.x() * widgetSize.width()  / double(Lists::kReferenceWidth),
            ref.y() * widgetSize.height() / double(Lists::kReferenceHeight)};
}

} // namespace

void Lists::draw(QPainter& p, const QSize& widgetSize, BitmapFontRenderer& fonts) const {
    drawCoastList(p, widgetSize, fonts);
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
    const int rowAdvance = fonts.lineHeight(kCoastListFontSize) + kLineSpacing;

    const QColor color = applyBrightness(kCoastListColor, defaultBrightness());
    fonts.drawTextTopLeft(p, x, y,              dateLine, kCoastListFontSize, color);
    fonts.drawTextTopLeft(p, x, y + rowAdvance, timeLine, kCoastListFontSize, color);
}

} // namespace asdex
