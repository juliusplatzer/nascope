#pragma once

#include "asdex/datablock_types.h"
#include "asdex/targets/asdex_target.h"

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

void drawDatablocks(const QVector<AsdexTarget>& targets,
                    renderer::CommandBuffer* commandBuffer,
                    const QMatrix4x4& screenProjection,
                    const std::function<QPointF(QPointF)>& worldToScreen,
                    const std::function<bool(const AsdexTarget&)>& isVisible,
                    const renderer::BitmapFont& font,
                    std::uint32_t fontTextureId,
                    const DataBlockSettings& settings);

} // namespace asdex
