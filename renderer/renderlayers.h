#ifndef RENDERER_RENDERLAYERS_H_
#define RENDERER_RENDERLAYERS_H_

#include "renderer/cmdbuffer.h"
#include "renderer/renderer.h"

#include <map>

namespace renderer {

class LayeredCommandBuffer {
public:
    CommandBuffer& layer(int z) { return layers_[z]; }

    void clear() { layers_.clear(); }

    RendererStats flushTo(Renderer* renderer) {
        RendererStats stats;
        if (!renderer) return stats;

        for (auto& [unusedZ, buffer] : layers_) {
            (void)unusedZ;
            stats.merge(renderer->renderCommandBuffer(&buffer));
        }

        return stats;
    }

private:
    std::map<int, CommandBuffer> layers_;
};

}  // namespace renderer

#endif  // RENDERER_RENDERLAYERS_H_
