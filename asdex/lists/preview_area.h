#pragma once

#include "asdex/lists/screen_list.h"

#include <QMatrix4x4>
#include <QString>
#include <QStringList>

namespace renderer {
class BitmapFontRenderer;
}

namespace asdex {

class DatablockEditCommand;
class ScreenLineRenderer;

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
    void setSystemResponse(QString response);

    bool loadDefaultStateFromConfigFile(const QString& path, QString* error = nullptr);

    void render(renderer::BitmapFontRenderer& textRenderer,
                const QStringList& commandLines = {}) const;
    void renderCommandCursor(ScreenLineRenderer& lineRenderer,
                             const renderer::BitmapFontRenderer& textRenderer,
                             const DatablockEditCommand& command,
                             const QMatrix4x4& screenProjection) const;

private:
    TextBlock buildTextBlock(const QStringList& commandLines) const;
    int baseLineCount() const;
    QColor textColor() const;

    ScreenList list_;
    PreviewAreaState state_;
};

} // namespace asdex
