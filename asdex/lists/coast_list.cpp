#include "asdex/lists/coast_list.h"

#include <algorithm>

namespace asdex {

CoastList::CoastList()
    : style_(ScreenListStyle{QPointF(1000.0, 150.0),
                             QSizeF(300.0, 500.0),
                             2,
                             95,
                             20,
                             5,
                             QColor(0, 248, 0)}) {}

QPointF CoastList::locationForDisplay(QSize displaySize) const {
    const double x = std::max(0.0, double(displaySize.width()) - 300.0);
    return QPointF(x, 150.0);
}

TextBlock CoastList::buildHeaderBlock(QDateTime utcNow) const {
    utcNow = utcNow.toUTC();

    const QColor color = style_.baseTextColor;
    const QString dateLine =
        utcNow.date().toString(QStringLiteral("MM/dd/yy")).rightJustified(12, QLatin1Char(' '));
    const QString timeLine =
        utcNow.time().toString(QStringLiteral("HHmm/ss")).rightJustified(12, QLatin1Char(' '));

    TextBlock block;
    block.lineSpacing = 5;
    block.fragments.push_back(TextFragment{dateLine, color, Qt::transparent, false, true});
    block.fragments.push_back(TextFragment{timeLine, color, Qt::transparent, false, true});
    return block;
}

QChar CoastList::entryChar(CoastListEntryStatus status) const {
    switch (status) {
    case CoastListEntryStatus::Dropped:
        return QLatin1Char('D');
    case CoastListEntryStatus::Suspended:
        return QLatin1Char('S');
    case CoastListEntryStatus::Coasting:
    default:
        return QLatin1Char('C');
    }
}

QString CoastList::entryLine(const CoastListEntry& entry) const {
    const QString id = entry.trackId.left(3).leftJustified(3, QLatin1Char(' '));

    QString label;
    if (!entry.callsign.trimmed().isEmpty()) {
        label = entry.callsign.trimmed();
    } else if (!entry.beaconCode.trimmed().isEmpty()) {
        label = entry.beaconCode.trimmed().rightJustified(4, QLatin1Char('0'));
    } else {
        label = QStringLiteral("NO DATA");
    }

    label = label.left(8).leftJustified(8, QLatin1Char(' '));

    return QStringLiteral("%1 %2 %3").arg(entryChar(entry.status)).arg(id).arg(label);
}

TextBlock CoastList::buildFullBlock(QDateTime utcNow) const {
    TextBlock block = buildHeaderBlock(utcNow);
    const QColor color = style_.baseTextColor;

    QVector<CoastListEntry> ordered = entries_;
    std::stable_sort(ordered.begin(), ordered.end(), [](const CoastListEntry& a,
                                                        const CoastListEntry& b) {
        auto rank = [](CoastListEntryStatus status) {
            switch (status) {
            case CoastListEntryStatus::Coasting:
                return 0;
            case CoastListEntryStatus::Suspended:
                return 1;
            case CoastListEntryStatus::Dropped:
                return 2;
            }
            return 3;
        };

        const int ar = rank(a.status);
        const int br = rank(b.status);
        if (ar != br) return ar < br;
        return a.timeoutSeconds > b.timeoutSeconds;
    });

    for (const CoastListEntry& entry : ordered) {
        block.fragments.push_back(TextFragment{entryLine(entry),
                                               entry.selected ? QColor(255, 255, 255) : color,
                                               Qt::transparent,
                                               false,
                                               true});
    }

    return block;
}

void CoastList::render(renderer::BitmapFontRenderer& textRenderer, QSize displaySize) const {
    if (!visible_) return;

    ScreenListStyle actualStyle = style_;
    actualStyle.location = locationForDisplay(displaySize);

    ScreenList list(actualStyle);
    list.render(textRenderer, buildHeaderBlock(QDateTime::currentDateTimeUtc()));
}

} // namespace asdex
