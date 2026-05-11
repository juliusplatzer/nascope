#pragma once

#include "asdex/lists/screen_list.h"

#include <QMatrix4x4>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

namespace renderer {
class BitmapFont;
class LinesBuilder;
class TextBuilder;
}

namespace asdex {

class DatablockEditCommand;

struct PreviewAreaState {
    QString runwayConfigName = QStringLiteral("WEST");
    QStringList towerPositions = {QStringLiteral("LC"), QStringLiteral("GC")};
    QString systemResponse;
};

class PreviewArea {
public:
    PreviewArea();

    void setState(PreviewAreaState state) { state_ = std::move(state); }
    const PreviewAreaState& state() const { return state_; }
    bool setRunwayConfigName(QString name);
    bool updateRunwayConfigFromRunways(const QStringList& landingRunways,
                                       const QStringList& departureRunways);
    void setSystemResponse(QString response);

    bool loadDefaultStateFromConfigFile(const QString& path, QString* error = nullptr);

    void render(renderer::TextBuilder& textBuilder,
                const renderer::BitmapFont& font,
                std::uint32_t fontTextureId,
                const QStringList& commandLines = {}) const;
    void renderCommandCursor(renderer::LinesBuilder& lineBuilder,
                             const renderer::BitmapFont& font,
                             const DatablockEditCommand& command,
                             const QMatrix4x4& screenProjection) const;

private:
    struct RunwayConfiguration {
        QString name;
        QStringList arrivalRunwayIds;
        QStringList departureRunwayIds;
    };

    TextBlock buildTextBlock(const QStringList& commandLines) const;
    QString matchedRunwayConfigName(const QStringList& landingRunways,
                                    const QStringList& departureRunways) const;
    int baseLineCount() const;
    QColor textColor() const;

    ScreenList list_;
    PreviewAreaState state_;
    QString defaultRunwayConfigName_;
    QVector<RunwayConfiguration> runwayConfigurations_;
};

} // namespace asdex
