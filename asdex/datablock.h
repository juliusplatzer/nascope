#ifndef ASDEX_DATABLOCK_H_
#define ASDEX_DATABLOCK_H_

#include "asdex/target.h"

#include <QMatrix4x4>
#include <QPointF>
#include <QVector>

#include <cstdint>
#include <functional>

namespace renderer {
class BitmapFont;
class CommandBuffer;
}

namespace asdex {

enum class LeaderDirection {
    NE,
    N,
    E,
    SE,
    S,
    SW,
    W,
    NW,
};

enum class DataBlockVisibility {
    Inherit,
    ForceOn,
    ForceOff,
};

struct DataBlockSettings {
    bool showDataBlocks = true;
    bool fullDataBlocks = true;

    int fontSize = 2;
    int brightness = 95;
    int leaderLength = 2;
    LeaderDirection leaderDirection = LeaderDirection::NE;
    bool timesharePrimary = true;
    bool alertInProgress = false;

    bool showAltitude = false;
    bool showAircraftType = true;
    bool showSensors = false;
    bool showAircraftCategory = false;
    bool showFix = true;
    bool showVelocity = false;
    bool showScratchpads = true;
};

void drawDatablocks(const QVector<AsdexTarget>& targets,
                    renderer::CommandBuffer* commandBuffer,
                    const QMatrix4x4& screenProjection,
                    const std::function<QPointF(QPointF)>& worldToScreen,
                    const std::function<bool(const AsdexTarget&)>& isVisible,
                    const std::function<DataBlockSettings(const AsdexTarget&)>& settingsForTarget,
                    const std::function<std::uint32_t(int)>& fontTextureForSize,
                    const renderer::BitmapFont& font);

} // namespace asdex

#endif  // ASDEX_DATABLOCK_H_
