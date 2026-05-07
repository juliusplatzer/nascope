#include "renderer/bitmap_font_renderer.h"

#include "renderer/asdex_resources.h"

#include <QFile>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QVector4D>

#include <cstddef>
#include <vector>

namespace renderer {
namespace {

QString readTextFile(const QString& relativePath, QString* error) {
    const QString path = asdex::findProjectRelativeFile(relativePath);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return {};
    }

    return QString::fromUtf8(file.readAll());
}

QVector4D colorVector(const QColor& color) {
    return QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF());
}

const BitmapGlyph* glyphFor(const BitmapFontSize& fontSizeData, std::uint32_t codepoint) {
    const auto it = fontSizeData.glyphIndex.constFind(codepoint);
    if (it == fontSizeData.glyphIndex.constEnd()) return nullptr;

    const int index = it.value();
    if (index < 0 || index >= static_cast<int>(fontSizeData.glyphs.size())) return nullptr;
    return &fontSizeData.glyphs[static_cast<std::size_t>(index)];
}

} // namespace

bool BitmapFontRenderer::initialize(const BitmapFont& font, QString* error) {
    font_ = &font;
    functions_ = QOpenGLContext::currentContext()
                   ? QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(
                         QOpenGLContext::currentContext())
                   : nullptr;
    if (!functions_) {
        if (error) *error = QStringLiteral("OpenGL 3.3 core functions are unavailable");
        return false;
    }
    functions_->initializeOpenGLFunctions();

    QString shaderError;
    const QString vertexSource = readTextFile(QStringLiteral("renderer/shaders/font.vert"), &shaderError);
    if (vertexSource.isEmpty()) {
        if (error) *error = shaderError;
        return false;
    }
    const QString fragmentSource = readTextFile(QStringLiteral("renderer/shaders/font.frag"), &shaderError);
    if (fragmentSource.isEmpty()) {
        if (error) *error = shaderError;
        return false;
    }

    if (!shader_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource)
        || !shader_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource)
        || !shader_.link()) {
        if (error) *error = QStringLiteral("font shader setup failed: %1").arg(shader_.log());
        return false;
    }

    functions_->glGenVertexArrays(1, &vao_);
    functions_->glGenBuffers(1, &vbo_);

    functions_->glBindVertexArray(vao_);
    functions_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    functions_->glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

    constexpr GLsizei stride = sizeof(FontVertex);
    functions_->glEnableVertexAttribArray(0);
    functions_->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<const void*>(offsetof(FontVertex, x)));
    functions_->glEnableVertexAttribArray(1);
    functions_->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<const void*>(offsetof(FontVertex, u)));
    functions_->glEnableVertexAttribArray(2);
    functions_->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<const void*>(offsetof(FontVertex, r)));
    functions_->glEnableVertexAttribArray(3);
    functions_->glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<const void*>(offsetof(FontVertex, br)));

    functions_->glBindBuffer(GL_ARRAY_BUFFER, 0);
    functions_->glBindVertexArray(0);
    vertexCapacity_ = 0;
    return true;
}

void BitmapFontRenderer::deinitialize() {
    if (!functions_) return;

    for (const GpuFontSize& gpu : gpuSizes_) {
        if (gpu.texture) {
            GLuint texture = gpu.texture;
            functions_->glDeleteTextures(1, &texture);
        }
    }
    gpuSizes_.clear();

    if (vbo_) {
        functions_->glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_) {
        functions_->glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    shader_.removeAllShaders();
    vertexCapacity_ = 0;
}

bool BitmapFontRenderer::ensureGpuFontSize(int fontSize, QString* error) {
    if (gpuSizes_.contains(fontSize)) return true;
    if (!font_ || !functions_) return false;

    const BitmapFontSize* fontSizeData = font_->fontSize(fontSize);
    if (!fontSizeData) {
        if (error) *error = QStringLiteral("font size %1 is unavailable").arg(fontSize);
        return false;
    }

    GLuint texture = 0;
    functions_->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    functions_->glGenTextures(1, &texture);
    functions_->glBindTexture(GL_TEXTURE_2D, texture);
    functions_->glTexImage2D(GL_TEXTURE_2D,
                             0,
                             GL_R8,
                             fontSizeData->atlasWidth,
                             fontSizeData->atlasHeight,
                             0,
                             GL_RED,
                             GL_UNSIGNED_BYTE,
                             fontSizeData->atlasR8.constData());
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    functions_->glBindTexture(GL_TEXTURE_2D, 0);
    functions_->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    gpuSizes_.insert(fontSize,
                     GpuFontSize{texture, fontSizeData->atlasWidth, fontSizeData->atlasHeight});
    return true;
}

void BitmapFontRenderer::appendGlyphVertices(std::vector<FontVertex>& vertices,
                                             const BitmapFontSize& fontSizeData,
                                             const BitmapGlyph& glyph,
                                             QPointF topLeft,
                                             QColor color,
                                             QColor background) const {
    if (glyph.width <= 0 || glyph.height <= 0) return;

    const float x0 = static_cast<float>(topLeft.x());
    const float y0 = static_cast<float>(topLeft.y());
    const float x1 = x0 + static_cast<float>(glyph.width);
    const float y1 = y0 + static_cast<float>(glyph.height);
    const float invW = 1.0f / static_cast<float>(fontSizeData.atlasWidth);
    const float invH = 1.0f / static_cast<float>(fontSizeData.atlasHeight);
    const float u0 = static_cast<float>(glyph.textureOffset) * invW;
    const float u1 = static_cast<float>(glyph.textureOffset + glyph.width) * invW;
    const float v0 = 0.0f;
    const float v1 = static_cast<float>(glyph.height) * invH;
    const QVector4D c = colorVector(color);
    const QVector4D bg = colorVector(background);

    const auto vertex = [&](float x, float y, float u, float v) {
        return FontVertex{x, y,
                          u, v,
                          c.x(), c.y(), c.z(), c.w(),
                          bg.x(), bg.y(), bg.z(), bg.w()};
    };

    vertices.push_back(vertex(x0, y0, u0, v0));
    vertices.push_back(vertex(x1, y0, u1, v0));
    vertices.push_back(vertex(x1, y1, u1, v1));
    vertices.push_back(vertex(x0, y0, u0, v0));
    vertices.push_back(vertex(x1, y1, u1, v1));
    vertices.push_back(vertex(x0, y1, u0, v1));
}

void BitmapFontRenderer::renderTextTopLeft(QStringView text,
                                           QPointF position,
                                           int fontSize,
                                           QColor color,
                                           const QMatrix4x4& screenProjection) {
    if (!font_ || !functions_ || !vao_ || text.isEmpty()) return;
    if (!ensureGpuFontSize(fontSize)) return;

    const BitmapFontSize* fontSizeData = font_->fontSize(fontSize);
    if (!fontSizeData) return;

    std::vector<FontVertex> vertices;
    vertices.reserve(static_cast<std::size_t>(text.size()) * 6);

    const QColor background(Qt::transparent);
    float penX = 0.0f;
    float penY = 0.0f;

    for (const char32_t codepoint : text.toUcs4()) {
        if (codepoint == U'\r') continue;
        if (codepoint == U'\n') {
            penX = 0.0f;
            penY += static_cast<float>(fontSizeData->lineHeight);
            continue;
        }

        const BitmapGlyph* glyph = glyphFor(*fontSizeData, static_cast<std::uint32_t>(codepoint));
        if (!glyph) continue;

        const QPointF glyphTopLeft(position.x() + penX + glyph->bearingX,
                                   position.y() + penY + fontSizeData->lineHeight - glyph->bearingY);
        appendGlyphVertices(vertices, *fontSizeData, *glyph, glyphTopLeft, color, background);
        penX += static_cast<float>(glyph->advance);
    }

    if (vertices.empty()) return;

    const int vertexBytes = static_cast<int>(vertices.size() * sizeof(FontVertex));
    functions_->glBindVertexArray(vao_);
    functions_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    if (vertexBytes > vertexCapacity_) {
        functions_->glBufferData(GL_ARRAY_BUFFER, vertexBytes, vertices.data(), GL_DYNAMIC_DRAW);
        vertexCapacity_ = vertexBytes;
    } else {
        functions_->glBufferSubData(GL_ARRAY_BUFFER, 0, vertexBytes, vertices.data());
    }

    const GpuFontSize gpu = gpuSizes_.value(fontSize);
    shader_.bind();
    shader_.setUniformValue("u_projection", screenProjection);
    shader_.setUniformValue("u_fontAtlas", 0);
    functions_->glActiveTexture(GL_TEXTURE0);
    functions_->glBindTexture(GL_TEXTURE_2D, gpu.texture);
    functions_->glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    functions_->glBindTexture(GL_TEXTURE_2D, 0);
    shader_.release();

    functions_->glBindBuffer(GL_ARRAY_BUFFER, 0);
    functions_->glBindVertexArray(0);
}

QSize BitmapFontRenderer::measureText(QStringView text, int fontSize) const {
    return font_ ? font_->measureText(text, fontSize) : QSize();
}

} // namespace renderer
