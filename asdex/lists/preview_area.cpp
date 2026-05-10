#include "asdex/lists/preview_area.h"

#include "asdex/input/datablock_edit_command.h"
#include "asdex/render/screen_line_renderer.h"
#include "renderer/text/bitmap_font_renderer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
    if (!runwayConfigName.isEmpty()) nextState.runwayConfigName = runwayConfigName;

    const QStringList towerPositions =
        defaultObjectNames(root.value(QStringLiteral("towerPositions")).toArray());
    if (!towerPositions.isEmpty()) nextState.towerPositions = towerPositions;

    state_ = std::move(nextState);
    return true;
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
