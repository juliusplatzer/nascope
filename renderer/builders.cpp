#include "renderer/builders.h"

#include <algorithm>
#include <cmath>

namespace renderer {
namespace {

PointVertex pointVertex(QPointF point) {
    return PointVertex{float(point.x()), float(point.y())};
}

ColoredVertex coloredVertex(QPointF point, QColor color) {
    return ColoredVertex{float(point.x()), float(point.y()), RGB::fromQColor(color)};
}

TexturedVertex texturedVertex(QPointF point, QPointF uv) {
    return TexturedVertex{float(point.x()), float(point.y()), float(uv.x()), float(uv.y())};
}

const BitmapGlyph* glyphFor(const BitmapFontSize& fontSizeData, std::uint32_t codepoint) {
    const auto it = fontSizeData.glyphIndex.constFind(codepoint);
    if (it == fontSizeData.glyphIndex.constEnd()) return nullptr;

    const int index = it.value();
    if (index < 0 || index >= int(fontSizeData.glyphs.size())) return nullptr;
    return &fontSizeData.glyphs[std::size_t(index)];
}

template <typename Builder>
Builder* takeFromPool(std::vector<std::unique_ptr<Builder>>& pool) {
    if (pool.empty()) return new Builder();

    std::unique_ptr<Builder> builder = std::move(pool.back());
    pool.pop_back();
    builder->reset();
    return builder.release();
}

template <typename Builder>
void returnToPool(std::vector<std::unique_ptr<Builder>>& pool, Builder* builder) {
    if (!builder) return;

    builder->reset();
    pool.push_back(std::unique_ptr<Builder>(builder));
}

std::vector<std::unique_ptr<LinesBuilder>>& linesBuilderPool() {
    thread_local std::vector<std::unique_ptr<LinesBuilder>> pool;
    return pool;
}

std::vector<std::unique_ptr<ColoredLinesBuilder>>& coloredLinesBuilderPool() {
    thread_local std::vector<std::unique_ptr<ColoredLinesBuilder>> pool;
    return pool;
}

std::vector<std::unique_ptr<TrianglesBuilder>>& trianglesBuilderPool() {
    thread_local std::vector<std::unique_ptr<TrianglesBuilder>> pool;
    return pool;
}

std::vector<std::unique_ptr<ColoredTrianglesBuilder>>& coloredTrianglesBuilderPool() {
    thread_local std::vector<std::unique_ptr<ColoredTrianglesBuilder>> pool;
    return pool;
}

std::vector<std::unique_ptr<TexturedTrianglesBuilder>>& texturedTrianglesBuilderPool() {
    thread_local std::vector<std::unique_ptr<TexturedTrianglesBuilder>> pool;
    return pool;
}

std::vector<std::unique_ptr<TextBuilder>>& textBuilderPool() {
    thread_local std::vector<std::unique_ptr<TextBuilder>> pool;
    return pool;
}

} // namespace

void LinesBuilder::reset() {
    points_.clear();
    indices_.clear();
}

void LinesBuilder::addLine(QPointF a, QPointF b) {
    const std::uint32_t base = std::uint32_t(points_.size());
    points_.push_back(pointVertex(a));
    points_.push_back(pointVertex(b));
    indices_.push_back(base);
    indices_.push_back(base + 1);
}

void LinesBuilder::addLineStrip(const QVector<QPointF>& points) {
    if (points.size() < 2) return;
    for (int i = 1; i < points.size(); ++i) addLine(points.at(i - 1), points.at(i));
}

void LinesBuilder::addLineLoop(const QVector<QPointF>& points) {
    if (points.size() < 2) return;
    addLineStrip(points);
    addLine(points.last(), points.first());
}

void LinesBuilder::addCircle(QPointF center, float radius, int segments) {
    if (segments < 3 || radius <= 0.0f) return;

    QVector<QPointF> points;
    points.reserve(segments + 1);
    for (int i = 0; i < segments; ++i) {
        const double a = 2.0 * M_PI * double(i) / double(segments);
        points.push_back(QPointF(center.x() + radius * std::cos(a),
                                 center.y() + radius * std::sin(a)));
    }
    addLineLoop(points);
}

void LinesBuilder::generateCommands(CommandBuffer* commandBuffer) {
    if (!commandBuffer || points_.isEmpty() || indices_.isEmpty()) return;
    commandBuffer->drawLines(points_, indices_);
}

void ColoredLinesBuilder::reset() {
    points_.clear();
    indices_.clear();
}

void ColoredLinesBuilder::addLine(QPointF a, QColor colorA, QPointF b, QColor colorB) {
    const std::uint32_t base = std::uint32_t(points_.size());
    points_.push_back(coloredVertex(a, colorA));
    points_.push_back(coloredVertex(b, colorB));
    indices_.push_back(base);
    indices_.push_back(base + 1);
}

void ColoredLinesBuilder::addLine(QPointF a, QPointF b, QColor color) {
    addLine(a, color, b, color);
}

void ColoredLinesBuilder::generateCommands(CommandBuffer* commandBuffer) {
    if (!commandBuffer || points_.isEmpty() || indices_.isEmpty()) return;
    commandBuffer->drawColoredLines(points_, indices_);
}

void TrianglesBuilder::reset() {
    points_.clear();
    indices_.clear();
}

void TrianglesBuilder::addTriangle(QPointF a, QPointF b, QPointF c) {
    const std::uint32_t base = std::uint32_t(points_.size());
    points_.push_back(pointVertex(a));
    points_.push_back(pointVertex(b));
    points_.push_back(pointVertex(c));
    indices_.push_back(base);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
}

void TrianglesBuilder::addQuad(QPointF a, QPointF b, QPointF c, QPointF d) {
    const std::uint32_t base = std::uint32_t(points_.size());
    points_.push_back(pointVertex(a));
    points_.push_back(pointVertex(b));
    points_.push_back(pointVertex(c));
    points_.push_back(pointVertex(d));
    indices_.push_back(base);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void TrianglesBuilder::addCircle(QPointF center, float radius, int segments) {
    if (segments < 3 || radius <= 0.0f) return;

    const std::uint32_t centerIndex = std::uint32_t(points_.size());
    points_.push_back(pointVertex(center));
    for (int i = 0; i <= segments; ++i) {
        const double a = 2.0 * M_PI * double(i) / double(segments);
        points_.push_back(pointVertex(QPointF(center.x() + radius * std::cos(a),
                                              center.y() + radius * std::sin(a))));
    }

    for (int i = 1; i <= segments; ++i) {
        indices_.push_back(centerIndex);
        indices_.push_back(centerIndex + std::uint32_t(i));
        indices_.push_back(centerIndex + std::uint32_t(i + 1));
    }
}

void TrianglesBuilder::addIndexed(const QVector<QPointF>& points,
                                  const QVector<std::uint32_t>& indices) {
    if (points.isEmpty() || indices.isEmpty()) return;

    const std::uint32_t base = std::uint32_t(points_.size());
    for (const QPointF& point : points) points_.push_back(pointVertex(point));
    for (const std::uint32_t index : indices) indices_.push_back(base + index);
}

void TrianglesBuilder::generateCommands(CommandBuffer* commandBuffer,
                                        DrawMode mode,
                                        float hatchOffset) {
    if (!commandBuffer || points_.isEmpty() || indices_.isEmpty()) return;
    commandBuffer->drawTriangles(points_, indices_, mode, hatchOffset);
}

void ColoredTrianglesBuilder::reset() {
    points_.clear();
    indices_.clear();
}

void ColoredTrianglesBuilder::addTriangle(QPointF a,
                                          QColor colorA,
                                          QPointF b,
                                          QColor colorB,
                                          QPointF c,
                                          QColor colorC) {
    const std::uint32_t base = std::uint32_t(points_.size());
    points_.push_back(coloredVertex(a, colorA));
    points_.push_back(coloredVertex(b, colorB));
    points_.push_back(coloredVertex(c, colorC));
    indices_.push_back(base);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
}

void ColoredTrianglesBuilder::addTriangle(QPointF a, QPointF b, QPointF c, QColor color) {
    addTriangle(a, color, b, color, c, color);
}

void ColoredTrianglesBuilder::addQuad(QPointF a,
                                      QPointF b,
                                      QPointF c,
                                      QPointF d,
                                      QColor color) {
    const std::uint32_t base = std::uint32_t(points_.size());
    points_.push_back(coloredVertex(a, color));
    points_.push_back(coloredVertex(b, color));
    points_.push_back(coloredVertex(c, color));
    points_.push_back(coloredVertex(d, color));
    indices_.push_back(base);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void ColoredTrianglesBuilder::generateCommands(CommandBuffer* commandBuffer) {
    if (!commandBuffer || points_.isEmpty() || indices_.isEmpty()) return;
    commandBuffer->drawColoredTriangles(points_, indices_);
}

void TexturedTrianglesBuilder::reset() {
    points_.clear();
    indices_.clear();
}

void TexturedTrianglesBuilder::addTriangle(QPointF a,
                                           QPointF uvA,
                                           QPointF b,
                                           QPointF uvB,
                                           QPointF c,
                                           QPointF uvC) {
    const std::uint32_t base = std::uint32_t(points_.size());
    points_.push_back(texturedVertex(a, uvA));
    points_.push_back(texturedVertex(b, uvB));
    points_.push_back(texturedVertex(c, uvC));
    indices_.push_back(base);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
}

void TexturedTrianglesBuilder::addQuad(QPointF a,
                                       QPointF uvA,
                                       QPointF b,
                                       QPointF uvB,
                                       QPointF c,
                                       QPointF uvC,
                                       QPointF d,
                                       QPointF uvD) {
    const std::uint32_t base = std::uint32_t(points_.size());
    points_.push_back(texturedVertex(a, uvA));
    points_.push_back(texturedVertex(b, uvB));
    points_.push_back(texturedVertex(c, uvC));
    points_.push_back(texturedVertex(d, uvD));
    indices_.push_back(base);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void TexturedTrianglesBuilder::generateCommands(CommandBuffer* commandBuffer,
                                                std::uint32_t textureId) {
    if (!commandBuffer || textureId == 0 || points_.isEmpty() || indices_.isEmpty()) return;
    commandBuffer->drawTexturedTriangles(textureId, points_, indices_);
}

TextBuilder::TextBuilder(const BitmapFont* font)
    : font_(font) {}

void TextBuilder::reset() {
    vertices_.clear();
    indices_.clear();
    textureId_ = 0;
}

void TextBuilder::appendGlyph(const BitmapFontSize& fontSizeData,
                              const BitmapGlyph& glyph,
                              QPointF topLeft,
                              RGBA color,
                              RGBA background) {
    if (glyph.width <= 0 || glyph.height <= 0) return;

    const float x0 = float(topLeft.x());
    const float y0 = float(topLeft.y());
    const float x1 = x0 + float(glyph.width);
    const float y1 = y0 + float(glyph.height);
    const float invW = 1.0f / float(fontSizeData.atlasWidth);
    const float invH = 1.0f / float(fontSizeData.atlasHeight);
    const float u0 = float(glyph.textureOffset) * invW;
    const float u1 = float(glyph.textureOffset + glyph.width) * invW;
    const float v0 = 0.0f;
    const float v1 = float(glyph.height) * invH;

    const std::uint32_t base = std::uint32_t(vertices_.size());
    vertices_.push_back(FontVertex{x0, y0, u0, v0, color, background});
    vertices_.push_back(FontVertex{x1, y0, u1, v0, color, background});
    vertices_.push_back(FontVertex{x1, y1, u1, v1, color, background});
    vertices_.push_back(FontVertex{x0, y1, u0, v1, color, background});

    indices_.push_back(base);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void TextBuilder::addText(QStringView text,
                          QPointF position,
                          const TextStyle& style,
                          std::uint32_t fontTextureId) {
    if (!font_ || text.isEmpty() || fontTextureId == 0) return;

    if (textureId_ == 0) textureId_ = fontTextureId;
    if (textureId_ != fontTextureId) return;

    const BitmapFontSize* fontSizeData = font_->fontSize(style.size);
    if (!fontSizeData) return;

    const auto codepoints = text.toString().toUcs4();
    const auto lastGlyphOnLine = [&](qsizetype index) {
        for (qsizetype next = index + 1; next < codepoints.size(); ++next) {
            const char32_t nextCodepoint = char32_t(codepoints.at(next));
            if (nextCodepoint == U'\r') continue;
            if (nextCodepoint == U'\n') return true;
            if (glyphFor(*fontSizeData, std::uint32_t(nextCodepoint))) return false;
        }
        return true;
    };

    float penX = 0.0f;
    float penY = 0.0f;
    const RGBA color = RGBA::fromQColor(style.color);
    const RGBA background = RGBA::fromQColor(style.background);

    for (qsizetype i = 0; i < codepoints.size(); ++i) {
        const char32_t codepoint = char32_t(codepoints.at(i));
        if (codepoint == U'\r') continue;
        if (codepoint == U'\n') {
            penX = 0.0f;
            penY += float(fontSizeData->lineHeight);
            continue;
        }

        const BitmapGlyph* glyph = glyphFor(*fontSizeData, std::uint32_t(codepoint));
        if (!glyph) continue;

        const QPointF glyphTopLeft(position.x() + penX + glyph->bearingX,
                                   position.y() + penY + fontSizeData->lineHeight
                                       - glyph->bearingY);
        appendGlyph(*fontSizeData, *glyph, glyphTopLeft, color, background);
        penX += float(lastGlyphOnLine(i) ? glyph->width : glyph->advance);
    }
}

void TextBuilder::generateCommands(CommandBuffer* commandBuffer) {
    if (!commandBuffer || vertices_.isEmpty() || indices_.isEmpty() || textureId_ == 0) return;
    commandBuffer->drawFontTriangles(textureId_, vertices_, indices_);
}

LinesBuilder* getLinesBuilder() {
    return takeFromPool(linesBuilderPool());
}

void returnLinesBuilder(LinesBuilder* builder) {
    returnToPool(linesBuilderPool(), builder);
}

ColoredLinesBuilder* getColoredLinesBuilder() {
    return takeFromPool(coloredLinesBuilderPool());
}

void returnColoredLinesBuilder(ColoredLinesBuilder* builder) {
    returnToPool(coloredLinesBuilderPool(), builder);
}

TrianglesBuilder* getTrianglesBuilder() {
    return takeFromPool(trianglesBuilderPool());
}

void returnTrianglesBuilder(TrianglesBuilder* builder) {
    returnToPool(trianglesBuilderPool(), builder);
}

ColoredTrianglesBuilder* getColoredTrianglesBuilder() {
    return takeFromPool(coloredTrianglesBuilderPool());
}

void returnColoredTrianglesBuilder(ColoredTrianglesBuilder* builder) {
    returnToPool(coloredTrianglesBuilderPool(), builder);
}

TexturedTrianglesBuilder* getTexturedTrianglesBuilder() {
    return takeFromPool(texturedTrianglesBuilderPool());
}

void returnTexturedTrianglesBuilder(TexturedTrianglesBuilder* builder) {
    returnToPool(texturedTrianglesBuilderPool(), builder);
}

TextBuilder* getTextBuilder() {
    return takeFromPool(textBuilderPool());
}

void returnTextBuilder(TextBuilder* builder) {
    returnToPool(textBuilderPool(), builder);
}

} // namespace renderer
