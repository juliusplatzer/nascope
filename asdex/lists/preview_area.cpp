#include "asdex/lists/preview_area.h"

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

TextBlock PreviewArea::buildTextBlock() const {
    const QColor textColor(0, 248, 0);

    TextBlock block;
    block.lineSpacing = 3;
    block.fragments.push_back(TextFragment{QStringLiteral("RWY CFG: ") + state_.runwayConfigName,
                                           textColor,
                                           Qt::transparent,
                                           false,
                                           true});
    block.fragments.push_back(TextFragment{QStringLiteral("TWR CFG:") + state_.towerPositions.join(","),
                                           textColor,
                                           Qt::transparent,
                                           false,
                                           true});
    block.fragments.push_back(TextFragment{state_.systemResponse,
                                           textColor,
                                           Qt::transparent,
                                           false,
                                           true});
    return block;
}

void PreviewArea::render(renderer::BitmapFontRenderer& textRenderer) const {
    list_.render(textRenderer, buildTextBlock());
}

} // namespace asdex
