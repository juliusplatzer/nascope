#include "asdex/lists/preview_area.h"

#include "asdex/input/datablock_edit_command.h"
#include "asdex/render/screen_line_renderer.h"
#include "renderer/text/bitmap_font_renderer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <utility>

namespace asdex {
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

void PreviewArea::render(renderer::BitmapFontRenderer& textRenderer,
                         const QStringList& commandLines) const {
    list_.render(textRenderer, buildTextBlock(commandLines));
}

void PreviewArea::renderCommandCursor(ScreenLineRenderer& lineRenderer,
                                      const renderer::BitmapFontRenderer& textRenderer,
                                      const DatablockEditCommand& command,
                                      const QMatrix4x4& screenProjection) const {
    const ScreenListStyle& style = list_.style();
    const QSize charSize = textRenderer.charSize(style.fontSize);
    const int charWidth = charSize.width();
    const int lineHeight = textRenderer.lineHeight(style.fontSize);
    if (charWidth <= 0 || lineHeight <= 0) return;

    const int fontSpacing = textRenderer.fontSpacing(style.fontSize);
    const int column = command.cursorColumn();
    const int line = command.cursorLine();
    const QPointF location = style.location;

    const double x = location.x()
        + charWidth * column
        + fontSpacing * (column - 1);
    // Match preview text row pitch: font line height plus preview line spacing.
    const double y = location.y()
        + (lineHeight + style.lineSpacing) * (line + baseLineCount());

    lineRenderer.drawLine(QPointF(x, y),
                          QPointF(x + charWidth, y),
                          textColor(),
                          screenProjection,
                          1.0f);
}

int PreviewArea::baseLineCount() const {
    return 3;
}

QColor PreviewArea::textColor() const {
    const ScreenListStyle& style = list_.style();
    return applyCrcBrightness(style.baseTextColor, style.brightness, style.minBrightness);
}

} // namespace asdex
