#pragma once

#include "asdex/colors.h"
#include "asdex/videomap.h"

namespace renderer {
class CommandBuffer;
}

namespace asdex {

void drawVideoMap(const VideoMap& map, renderer::CommandBuffer* commandBuffer, Mode mode);

} // namespace asdex
