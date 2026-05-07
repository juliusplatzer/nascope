#pragma once

#include "asdex/lists/screen_list.h"

#include <QString>
#include <QStringList>

namespace renderer {
class BitmapFontRenderer;
}

namespace asdex {

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

    bool loadDefaultStateFromConfigFile(const QString& path, QString* error = nullptr);

    void render(renderer::BitmapFontRenderer& textRenderer) const;

private:
    TextBlock buildTextBlock() const;

    ScreenList list_;
    PreviewAreaState state_;
};

} // namespace asdex
