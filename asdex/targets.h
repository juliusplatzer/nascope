#ifndef ASDEX_TARGETS_H_
#define ASDEX_TARGETS_H_

#include "asdex/colors.h"
#include "asdex/targetcache.h"

#include <QMatrix4x4>
#include <QVector>

namespace renderer {
class CommandBuffer;
}

namespace asdex {

void drawTargets(const QVector<AsdexTarget>& targets,
                 renderer::CommandBuffer* commandBuffer,
                 const QMatrix4x4& worldProjection,
                 Mode mode,
                 int vectorSeconds);

int clampedTargetVectorSeconds(int seconds);

} // namespace asdex

#endif  // ASDEX_TARGETS_H_
