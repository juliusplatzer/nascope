#ifndef RENDERER_CMDBUFFER_H_
#define RENDERER_CMDBUFFER_H_

#include "renderer/rgb.h"

#include <QMatrix4x4>
#include <QPointF>
#include <QVector>

#include <cstdint>
#include <memory>
#include <vector>

namespace renderer {

enum class DrawMode {
    Solid,
    Hatched,
};

struct PointVertex {
    float x = 0.0f;
    float y = 0.0f;
};

struct ColoredVertex {
    float x = 0.0f;
    float y = 0.0f;
    RGB color;
};

struct TexturedVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

struct FontVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    RGBA color;
    RGBA background;
};

struct Command {
    enum class Type {
        ResetState,
        LoadProjectionMatrix,
        Clear,
        Viewport,
        Scissor,
        DisableScissor,
        Blend,
        DisableBlend,
        SetColor,
        LineWidth,
        DrawLines,
        DrawColoredLines,
        DrawTriangles,
        DrawColoredTriangles,
        DrawTexturedTriangles,
        DrawFontTriangles,
    };

    Type type = Type::ResetState;
    QMatrix4x4 matrix;
    RGBA color;
    float lineWidth = 1.0f;
    float hatchOffset = 0.0f;
    std::uint32_t textureId = 0;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    QVector<PointVertex> points;
    QVector<ColoredVertex> coloredPoints;
    QVector<TexturedVertex> texturedPoints;
    QVector<FontVertex> fontPoints;
    QVector<std::uint32_t> indices;
    DrawMode drawMode = DrawMode::Solid;
};

class CommandBuffer {
public:
    void reset();

    const std::vector<Command>& commands() const { return commands_; }

    void resetState();
    void loadProjectionMatrix(const QMatrix4x4& matrix);
    void clear(RGBA color);
    void viewport(int x, int y, int w, int h);
    void scissor(int x, int y, int w, int h);
    void disableScissor();
    void blend();
    void disableBlend();
    void setRgba(RGBA color);
    void setRgb(RGB color);
    void lineWidth(float width);

    void drawLines(QVector<PointVertex> points, QVector<std::uint32_t> indices);
    void drawColoredLines(QVector<ColoredVertex> points, QVector<std::uint32_t> indices);
    void drawTriangles(QVector<PointVertex> points,
                       QVector<std::uint32_t> indices,
                       DrawMode mode = DrawMode::Solid,
                       float hatchOffset = 0.0f);
    void drawColoredTriangles(QVector<ColoredVertex> points, QVector<std::uint32_t> indices);
    void drawTexturedTriangles(std::uint32_t textureId,
                               QVector<TexturedVertex> points,
                               QVector<std::uint32_t> indices);
    void drawFontTriangles(std::uint32_t textureId,
                           QVector<FontVertex> points,
                           QVector<std::uint32_t> indices);

private:
    std::vector<Command> commands_;
};

CommandBuffer* getCommandBuffer();
void returnCommandBuffer(CommandBuffer* commandBuffer);

} // namespace renderer

#endif  // RENDERER_CMDBUFFER_H_
