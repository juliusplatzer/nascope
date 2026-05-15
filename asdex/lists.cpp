#include "asdex/lists.h"

#include "asdex/cmdslew.h"
#include "renderer/builders.h"
#include "renderer/font.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

namespace asdex {

QColor applyCrcBrightness(QColor color, int brightness, int minBrightness) {
    brightness = std::clamp(brightness, 0, 100);

    const float factor =
        (float(brightness * (100 - minBrightness)) / 100.0f
         + float(minBrightness)) / 100.0f;

    return QColor(int(color.red() * factor),
                  int(color.green() * factor),
                  int(color.blue() * factor),
                  color.alpha());
}

ScreenList::ScreenList(ScreenListStyle style)
    : style_(std::move(style)) {}

void ScreenList::setBrightness(int brightness) {
    style_.brightness = std::clamp(brightness, 0, 100);
}

void ScreenList::render(renderer::TextBuilder& textBuilder,
                        const renderer::BitmapFont& font,
                        std::uint32_t fontTextureId,
                        const TextBlock& block) const {
    const QPointF screenLocation = style_.location;
    const qreal lineSpacing =
        qreal(block.lineSpacing > 0 ? block.lineSpacing : style_.lineSpacing);

    QPointF cursor = screenLocation;
    const int lineHeight = font.lineHeight(style_.fontSize);

    for (const TextFragment& fragment : block.fragments) {
        const QColor baseColor =
            fragment.foreground.isValid() ? fragment.foreground : style_.baseTextColor;

        renderer::TextStyle textStyle;
        textStyle.size = style_.fontSize;
        textStyle.color = applyCrcBrightness(baseColor, style_.brightness, style_.minBrightness);
        textStyle.background = fragment.background;
        textStyle.underlined = fragment.underlined;

        if (!fragment.text.isEmpty()) {
            textBuilder.addText(QStringView(fragment.text), cursor, textStyle, fontTextureId);
        }

        if (fragment.newLine) {
            cursor.setX(screenLocation.x());
            cursor.setY(cursor.y() + lineHeight + lineSpacing);
        } else if (!fragment.text.isEmpty()) {
            cursor.setX(cursor.x() + font.measureText(QStringView(fragment.text),
                                                      style_.fontSize).width());
        }
    }
}

} // namespace asdex

namespace asdex {

CoastList::CoastList()
    : style_(ScreenListStyle{QPointF(1000.0, 150.0),
                             QSizeF(300.0, 500.0),
                             2,
                             95,
                             20,
                             5,
                             QColor(0, 248, 0)}) {}

void CoastList::setBrightness(int brightness) {
    style_.brightness = std::clamp(brightness, 0, 100);
}

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

void CoastList::render(renderer::TextBuilder& textBuilder,
                       const renderer::BitmapFont& font,
                       std::uint32_t fontTextureId,
                       QSize displaySize) const {
    if (!visible_) return;

    ScreenListStyle actualStyle = style_;
    actualStyle.location = locationForDisplay(displaySize);

    ScreenList list(actualStyle);
    list.render(textBuilder, font, fontTextureId, buildHeaderBlock(QDateTime::currentDateTimeUtc()));
}

namespace {

QString defaultObjectName(const QJsonArray& items) {
    QString firstName;

    for (const QJsonValue& value : items) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (firstName.isEmpty()) firstName = name;
        if (object.value(QStringLiteral("default")).toBool(false) && !name.isEmpty()) return name;
    }

    return firstName;
}

QStringList defaultObjectNames(const QJsonArray& items) {
    QStringList names;
    QString firstName;

    for (const QJsonValue& value : items) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (firstName.isEmpty()) firstName = name;
        if (object.value(QStringLiteral("default")).toBool(false) && !name.isEmpty()) {
            names << name;
        }
    }

    if (names.isEmpty() && !firstName.isEmpty()) names << firstName;
    return names;
}

QString normalizeRunwayId(QString value) {
    value = value.trimmed().toUpper();
    value.remove(QRegularExpression(QStringLiteral("\\bRWYS?\\b")));
    value.remove(QRegularExpression(QStringLiteral("\\s+")));

    static const QRegularExpression runwayRe(QStringLiteral("^0*([1-9]|[12][0-9]|3[0-6])([LRC]?)$"));
    const QRegularExpressionMatch match = runwayRe.match(value);
    if (!match.hasMatch()) return {};

    return QString::number(match.captured(1).toInt()) + match.captured(2);
}

QStringList normalizedRunways(const QStringList& values) {
    QStringList out;
    for (const QString& value : values) {
        const QString normalized = normalizeRunwayId(value);
        if (!normalized.isEmpty() && !out.contains(normalized)) out << normalized;
    }
    return out;
}

QStringList runwayIdsFromArray(const QJsonArray& values) {
    QStringList out;
    for (const QJsonValue& value : values) {
        const QString normalized = normalizeRunwayId(value.toString());
        if (!normalized.isEmpty() && !out.contains(normalized)) out << normalized;
    }
    return out;
}

bool sameRunwaySet(const QStringList& lhs, const QStringList& rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (const QString& runway : lhs) {
        if (!rhs.contains(runway)) return false;
    }
    return true;
}

} // namespace

PreviewArea::PreviewArea()
    : list_(ScreenListStyle{QPointF(50.0, 150.0),
                            QSizeF(300.0, 500.0),
                            2,
                            95,
                            20,
                            3,
                            QColor(0, 248, 0)}) {}

void PreviewArea::setBrightness(int brightness) {
    list_.setBrightness(brightness);
}

bool PreviewArea::loadDefaultStateFromConfigFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("invalid ASDE-X config %1: %2")
                                .arg(path, parseError.errorString());
        return false;
    }

    const QJsonObject root = document.object();
    PreviewAreaState nextState = state_;

    const QString runwayConfigName =
        defaultObjectName(root.value(QStringLiteral("runwayConfigurations")).toArray());
    if (!runwayConfigName.isEmpty()) {
        nextState.runwayConfigName = runwayConfigName;
        defaultRunwayConfigName_ = runwayConfigName;
    }

    runwayConfigurations_.clear();
    const QJsonArray runwayConfigs = root.value(QStringLiteral("runwayConfigurations")).toArray();
    runwayConfigurations_.reserve(runwayConfigs.size());
    for (const QJsonValue& value : runwayConfigs) {
        const QJsonObject object = value.toObject();
        RunwayConfiguration config;
        config.name = object.value(QStringLiteral("name")).toString();
        config.arrivalRunwayIds =
            runwayIdsFromArray(object.value(QStringLiteral("arrivalRunwayIds")).toArray());
        config.departureRunwayIds =
            runwayIdsFromArray(object.value(QStringLiteral("departureRunwayIds")).toArray());
        if (!config.name.isEmpty()) runwayConfigurations_.push_back(std::move(config));
    }

    const QStringList towerPositions =
        defaultObjectNames(root.value(QStringLiteral("towerPositions")).toArray());
    if (!towerPositions.isEmpty()) nextState.towerPositions = towerPositions;

    state_ = std::move(nextState);
    return true;
}

bool PreviewArea::setRunwayConfigName(QString name) {
    name = name.trimmed();
    if (name.isEmpty() || name == state_.runwayConfigName) return false;

    state_.runwayConfigName = std::move(name);
    return true;
}

bool PreviewArea::updateRunwayConfigFromRunways(const QStringList& landingRunways,
                                                const QStringList& departureRunways) {
    QString name = matchedRunwayConfigName(landingRunways, departureRunways);
    if (name.isEmpty()) name = defaultRunwayConfigName_;
    return setRunwayConfigName(name);
}

void PreviewArea::setSystemResponse(QString response) {
    state_.systemResponse = std::move(response);
}

TextBlock PreviewArea::buildTextBlock(const QStringList& commandLines) const {
    const QColor color = list_.style().baseTextColor;

    TextBlock block;
    block.lineSpacing = 3;
    block.fragments.push_back(TextFragment{QStringLiteral("RWY CFG: ") + state_.runwayConfigName,
                                           color,
                                           Qt::transparent,
                                           false,
                                           true});
    block.fragments.push_back(TextFragment{QStringLiteral("TWR CFG:") + state_.towerPositions.join(","),
                                           color,
                                           Qt::transparent,
                                           false,
                                           true});
    block.fragments.push_back(TextFragment{state_.systemResponse,
                                           color,
                                           Qt::transparent,
                                           false,
                                           true});
    for (const QString& line : commandLines) {
        block.fragments.push_back(TextFragment{line,
                                               color,
                                               Qt::transparent,
                                               false,
                                               true});
    }
    return block;
}

QString PreviewArea::matchedRunwayConfigName(const QStringList& landingRunways,
                                             const QStringList& departureRunways) const {
    const QStringList landing = normalizedRunways(landingRunways);
    const QStringList departure = normalizedRunways(departureRunways);
    if (landing.isEmpty() || departure.isEmpty()) return {};

    for (const RunwayConfiguration& config : runwayConfigurations_) {
        if (sameRunwaySet(config.arrivalRunwayIds, landing)
            && sameRunwaySet(config.departureRunwayIds, departure)) {
            return config.name;
        }
    }

    return {};
}

void PreviewArea::render(renderer::TextBuilder& textBuilder,
                         const renderer::BitmapFont& font,
                         std::uint32_t fontTextureId,
                         const QStringList& commandLines) const {
    list_.render(textBuilder, font, fontTextureId, buildTextBlock(commandLines));
}

void PreviewArea::renderCommandCursor(renderer::LinesBuilder& lineBuilder,
                                      const renderer::BitmapFont& font,
                                      const DatablockEditCommand& command,
                                      const QMatrix4x4& screenProjection) const {
    renderCommandCursor(lineBuilder,
                        font,
                        command.cursorLine(),
                        command.cursorColumn(),
                        screenProjection);
}

void PreviewArea::renderCommandCursor(renderer::LinesBuilder& lineBuilder,
                                      const renderer::BitmapFont& font,
                                      int cursorLine,
                                      int cursorColumn,
                                      const QMatrix4x4& screenProjection) const {
    Q_UNUSED(screenProjection);

    const ScreenListStyle& style = list_.style();
    const QSize charSize = font.charSize(style.fontSize);
    const int charWidth = charSize.width();
    const int lineHeight = font.lineHeight(style.fontSize);
    if (charWidth <= 0 || lineHeight <= 0) return;

    const int fontSpacing = font.fontSpacing(style.fontSize);
    const int column = cursorColumn;
    const int line = cursorLine;
    const QPointF location = style.location;

    const double x = location.x()
        + charWidth * column
        + fontSpacing * std::max(0, column - 1);
    const double y = location.y()
        + (lineHeight + style.lineSpacing) * (line + baseLineCount());

    lineBuilder.addLine(QPointF(x, y), QPointF(x + charWidth, y));
}

int PreviewArea::baseLineCount() const {
    return 3;
}

QColor PreviewArea::textColor() const {
    const ScreenListStyle& style = list_.style();
    return applyCrcBrightness(style.baseTextColor, style.brightness, style.minBrightness);
}

} // namespace asdex
