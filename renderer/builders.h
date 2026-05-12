#ifndef RENDERER_BUILDERS_H_
#define RENDERER_BUILDERS_H_

#include "renderer/command_buffer.h"
#include "renderer/font.h"

#include <QColor>
#include <QPointF>
#include <QStringView>

#include <cstdint>
#include <memory>
#include <vector>

namespace renderer {

struct TextStyle {
    int size = 3;
    QColor color = Qt::white;
    QColor background = Qt::transparent;
    bool underlined = false;
};

class LinesBuilder {
public:
    void reset();
    void addLine(QPointF a, QPointF b);
    void addLineStrip(const QVector<QPointF>& points);
    void addLineLoop(const QVector<QPointF>& points);
    void addCircle(QPointF center, float radius, int segments);
    void generateCommands(CommandBuffer* commandBuffer);

private:
    QVector<PointVertex> points_;
    QVector<std::uint32_t> indices_;
};

class ColoredLinesBuilder {
public:
    void reset();
    void addLine(QPointF a, QColor colorA, QPointF b, QColor colorB);
    void addLine(QPointF a, QPointF b, QColor color);
    void generateCommands(CommandBuffer* commandBuffer);

private:
    QVector<ColoredVertex> points_;
    QVector<std::uint32_t> indices_;
};

class TrianglesBuilder {
public:
    void reset();
    void addTriangle(QPointF a, QPointF b, QPointF c);
    void addQuad(QPointF a, QPointF b, QPointF c, QPointF d);
    void addCircle(QPointF center, float radius, int segments);
    void addIndexed(const QVector<QPointF>& points, const QVector<std::uint32_t>& indices);
    void generateCommands(CommandBuffer* commandBuffer,
                          DrawMode mode = DrawMode::Solid,
                          float hatchOffset = 0.0f);

private:
    QVector<PointVertex> points_;
    QVector<std::uint32_t> indices_;
};

class ColoredTrianglesBuilder {
public:
    void reset();
    void addTriangle(QPointF a,
                     QColor colorA,
                     QPointF b,
                     QColor colorB,
                     QPointF c,
                     QColor colorC);
    void addTriangle(QPointF a, QPointF b, QPointF c, QColor color);
    void addQuad(QPointF a, QPointF b, QPointF c, QPointF d, QColor color);
    void generateCommands(CommandBuffer* commandBuffer);

private:
    QVector<ColoredVertex> points_;
    QVector<std::uint32_t> indices_;
};

class TexturedTrianglesBuilder {
public:
    void reset();
    void addTriangle(QPointF a,
                     QPointF uvA,
                     QPointF b,
                     QPointF uvB,
                     QPointF c,
                     QPointF uvC);
    void addQuad(QPointF a,
                 QPointF uvA,
                 QPointF b,
                 QPointF uvB,
                 QPointF c,
                 QPointF uvC,
                 QPointF d,
                 QPointF uvD);
    void generateCommands(CommandBuffer* commandBuffer, std::uint32_t textureId);

private:
    QVector<TexturedVertex> points_;
    QVector<std::uint32_t> indices_;
};

class TextBuilder {
public:
    explicit TextBuilder(const BitmapFont* font = nullptr);

    void setFont(const BitmapFont* font) { font_ = font; }
    void reset();
    void addText(QStringView text,
                 QPointF position,
                 const TextStyle& style,
                 std::uint32_t fontTextureId);
    void generateCommands(CommandBuffer* commandBuffer);

private:
    void appendGlyph(const BitmapFontSize& fontSizeData,
                     const BitmapGlyph& glyph,
                     QPointF topLeft,
                     RGBA color,
                     RGBA background);

    const BitmapFont* font_ = nullptr;
    std::uint32_t textureId_ = 0;
    QVector<FontVertex> vertices_;
    QVector<std::uint32_t> indices_;
};

LinesBuilder* getLinesBuilder();
void returnLinesBuilder(LinesBuilder* builder);

ColoredLinesBuilder* getColoredLinesBuilder();
void returnColoredLinesBuilder(ColoredLinesBuilder* builder);

TrianglesBuilder* getTrianglesBuilder();
void returnTrianglesBuilder(TrianglesBuilder* builder);

ColoredTrianglesBuilder* getColoredTrianglesBuilder();
void returnColoredTrianglesBuilder(ColoredTrianglesBuilder* builder);

TexturedTrianglesBuilder* getTexturedTrianglesBuilder();
void returnTexturedTrianglesBuilder(TexturedTrianglesBuilder* builder);

TextBuilder* getTextBuilder();
void returnTextBuilder(TextBuilder* builder);

} // namespace renderer

#endif  // RENDERER_BUILDERS_H_
