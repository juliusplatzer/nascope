#include "renderer/commandbuffer.h"

namespace renderer {
namespace {

std::vector<std::unique_ptr<CommandBuffer>>& commandBufferPool() {
    thread_local std::vector<std::unique_ptr<CommandBuffer>> pool;
    return pool;
}

} // namespace

void CommandBuffer::reset() {
    commands_.clear();
}

void CommandBuffer::resetState() {
    Command command;
    command.type = Command::Type::ResetState;
    commands_.push_back(std::move(command));
}

void CommandBuffer::loadProjectionMatrix(const QMatrix4x4& matrix) {
    Command command;
    command.type = Command::Type::LoadProjectionMatrix;
    command.matrix = matrix;
    commands_.push_back(std::move(command));
}

void CommandBuffer::clear(RGBA color) {
    Command command;
    command.type = Command::Type::Clear;
    command.color = color;
    commands_.push_back(std::move(command));
}

void CommandBuffer::viewport(int x, int y, int w, int h) {
    Command command;
    command.type = Command::Type::Viewport;
    command.x = x;
    command.y = y;
    command.w = w;
    command.h = h;
    commands_.push_back(std::move(command));
}

void CommandBuffer::scissor(int x, int y, int w, int h) {
    Command command;
    command.type = Command::Type::Scissor;
    command.x = x;
    command.y = y;
    command.w = w;
    command.h = h;
    commands_.push_back(std::move(command));
}

void CommandBuffer::disableScissor() {
    Command command;
    command.type = Command::Type::DisableScissor;
    commands_.push_back(std::move(command));
}

void CommandBuffer::blend() {
    Command command;
    command.type = Command::Type::Blend;
    commands_.push_back(std::move(command));
}

void CommandBuffer::disableBlend() {
    Command command;
    command.type = Command::Type::DisableBlend;
    commands_.push_back(std::move(command));
}

void CommandBuffer::setRgba(RGBA color) {
    Command command;
    command.type = Command::Type::SetColor;
    command.color = color;
    commands_.push_back(std::move(command));
}

void CommandBuffer::setRgb(RGB color) {
    setRgba(RGBA{color.r, color.g, color.b, 1.0f});
}

void CommandBuffer::lineWidth(float width) {
    Command command;
    command.type = Command::Type::LineWidth;
    command.lineWidth = width;
    commands_.push_back(std::move(command));
}

void CommandBuffer::drawLines(QVector<PointVertex> points,
                              QVector<std::uint32_t> indices) {
    Command command;
    command.type = Command::Type::DrawLines;
    command.points = std::move(points);
    command.indices = std::move(indices);
    commands_.push_back(std::move(command));
}

void CommandBuffer::drawColoredLines(QVector<ColoredVertex> points,
                                     QVector<std::uint32_t> indices) {
    Command command;
    command.type = Command::Type::DrawColoredLines;
    command.coloredPoints = std::move(points);
    command.indices = std::move(indices);
    commands_.push_back(std::move(command));
}

void CommandBuffer::drawTriangles(QVector<PointVertex> points,
                                  QVector<std::uint32_t> indices,
                                  DrawMode mode,
                                  float hatchOffset) {
    Command command;
    command.type = Command::Type::DrawTriangles;
    command.points = std::move(points);
    command.indices = std::move(indices);
    command.drawMode = mode;
    command.hatchOffset = hatchOffset;
    commands_.push_back(std::move(command));
}

void CommandBuffer::drawColoredTriangles(QVector<ColoredVertex> points,
                                         QVector<std::uint32_t> indices) {
    Command command;
    command.type = Command::Type::DrawColoredTriangles;
    command.coloredPoints = std::move(points);
    command.indices = std::move(indices);
    commands_.push_back(std::move(command));
}

void CommandBuffer::drawTexturedTriangles(std::uint32_t textureId,
                                          QVector<TexturedVertex> points,
                                          QVector<std::uint32_t> indices) {
    Command command;
    command.type = Command::Type::DrawTexturedTriangles;
    command.textureId = textureId;
    command.texturedPoints = std::move(points);
    command.indices = std::move(indices);
    commands_.push_back(std::move(command));
}

void CommandBuffer::drawFontTriangles(std::uint32_t textureId,
                                      QVector<FontVertex> points,
                                      QVector<std::uint32_t> indices) {
    Command command;
    command.type = Command::Type::DrawFontTriangles;
    command.textureId = textureId;
    command.fontPoints = std::move(points);
    command.indices = std::move(indices);
    commands_.push_back(std::move(command));
}

CommandBuffer* getCommandBuffer() {
    std::vector<std::unique_ptr<CommandBuffer>>& pool = commandBufferPool();
    if (pool.empty()) return new CommandBuffer();

    std::unique_ptr<CommandBuffer> commandBuffer = std::move(pool.back());
    pool.pop_back();
    commandBuffer->reset();
    return commandBuffer.release();
}

void returnCommandBuffer(CommandBuffer* commandBuffer) {
    if (!commandBuffer) return;

    commandBuffer->reset();
    commandBufferPool().push_back(std::unique_ptr<CommandBuffer>(commandBuffer));
}

} // namespace renderer
