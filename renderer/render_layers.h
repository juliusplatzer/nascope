#ifndef RENDERER_RENDER_LAYERS_H_
#define RENDERER_RENDER_LAYERS_H_

#include "renderer/command_buffer.h"
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

#endif  // RENDERER_RENDER_LAYERS_H_
