#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include <cstdint>
#include <memory>

namespace renderer {

class CommandBuffer;

struct RendererStats {
    int nBuffers = 0;
    int bufferBytes = 0;
    int nDrawCalls = 0;
    int nPoints = 0;
    int nLines = 0;
    int nTriangles = 0;

    void merge(const RendererStats& other) {
        nBuffers += other.nBuffers;
        bufferBytes += other.bufferBytes;
        nDrawCalls += other.nDrawCalls;
        nPoints += other.nPoints;
        nLines += other.nLines;
        nTriangles += other.nTriangles;
    }
};

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool initialize(QString* error = nullptr) = 0;
    virtual void deinitialize() = 0;

    virtual std::uint32_t createTextureFromImage(const QImage& image, bool magNearest) = 0;
    virtual std::uint32_t createTextureR8(int width,
                                          int height,
                                          const QByteArray& bytes,
                                          bool magNearest) = 0;
    virtual void updateTextureFromImage(std::uint32_t id,
                                        const QImage& image,
                                        bool magNearest) = 0;
    virtual void destroyTexture(std::uint32_t id) = 0;

    virtual RendererStats renderCommandBuffer(CommandBuffer* commandBuffer) = 0;
    virtual void readPixels(int x, int y, int w, int h, std::uint8_t* outRgba) = 0;
};

std::unique_ptr<Renderer> makeOpenGLRenderer();

} // namespace renderer
