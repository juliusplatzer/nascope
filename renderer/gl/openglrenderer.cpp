#include "renderer/gl/openglrenderer.h"

#include "renderer/commandbuffer.h"
#include "renderer/gl/shaders.h"

#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QVector4D>

#include <cstddef>

namespace renderer {
namespace {

QVector4D colorVector(RGBA color) {
    return QVector4D(color.r, color.g, color.b, color.a);
}

template <typename Vertex>
int byteSize(const QVector<Vertex>& vertices) {
    return int(vertices.size() * qsizetype(sizeof(Vertex)));
}

int byteSize(const QVector<std::uint32_t>& indices) {
    return int(indices.size() * qsizetype(sizeof(std::uint32_t)));
}

} // namespace

OpenGLRenderer::OpenGLRenderer() = default;

OpenGLRenderer::~OpenGLRenderer() {
    deinitialize();
}

bool OpenGLRenderer::initialize(QString* error) {
    functions_ = QOpenGLContext::currentContext()
                   ? QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(
                         QOpenGLContext::currentContext())
                   : nullptr;
    if (!functions_) {
        if (error) *error = QStringLiteral("OpenGL 3.3 core functions are unavailable");
        return false;
    }

    functions_->initializeOpenGLFunctions();

    if (!initializeShaders(error)) return false;

    functions_->glGenVertexArrays(1, &vao_);
    functions_->glGenBuffers(1, &vbo_);
    functions_->glGenBuffers(1, &ebo_);
    projection_.setToIdentity();
    resetState();
    ready_ = true;
    return true;
}

void OpenGLRenderer::deinitialize() {
    if (!functions_) return;

    for (const GLuint texture : textures_) {
        GLuint textureToDelete = texture;
        functions_->glDeleteTextures(1, &textureToDelete);
    }
    textures_.clear();

    if (ebo_) {
        functions_->glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }
    if (vbo_) {
        functions_->glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_) {
        functions_->glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    solidShader_.removeAllShaders();
    hatchShader_.removeAllShaders();
    coloredShader_.removeAllShaders();
    texturedShader_.removeAllShaders();
    fontShader_.removeAllShaders();
    ready_ = false;
    functions_ = nullptr;
}

bool OpenGLRenderer::initializeShaders(QString* error) {
    const auto compile = [error](QOpenGLShaderProgram& program,
                                const char* vertex,
                                const char* fragment,
                                const QString& label) {
        if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
            || !program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)
            || !program.link()) {
            if (error) *error = QStringLiteral("%1 shader setup failed: %2")
                                    .arg(label, program.log());
            return false;
        }
        return true;
    };

    return compile(solidShader_, gl::kSolidVertexShader, gl::kSolidFragmentShader, QStringLiteral("solid"))
        && compile(hatchShader_, gl::kSolidVertexShader, gl::kHatchFragmentShader, QStringLiteral("hatch"))
        && compile(coloredShader_, gl::kColoredVertexShader, gl::kColoredFragmentShader, QStringLiteral("colored"))
        && compile(texturedShader_, gl::kTexturedVertexShader, gl::kTexturedFragmentShader, QStringLiteral("textured"))
        && compile(fontShader_, gl::kFontVertexShader, gl::kFontFragmentShader, QStringLiteral("font"));
}

void OpenGLRenderer::resetState() {
    if (!functions_) return;

    functions_->glDisable(GL_DEPTH_TEST);
    functions_->glDisable(GL_STENCIL_TEST);
    functions_->glDisable(GL_MULTISAMPLE);
    functions_->glDisable(GL_LINE_SMOOTH);
    functions_->glDisable(GL_POLYGON_SMOOTH);
    functions_->glDisable(GL_DITHER);
    functions_->glDisable(GL_CULL_FACE);
    functions_->glDisable(GL_SCISSOR_TEST);
    functions_->glEnable(GL_BLEND);
    functions_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    functions_->glBindVertexArray(0);
    functions_->glBindBuffer(GL_ARRAY_BUFFER, 0);
    functions_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    functions_->glUseProgram(0);
    currentColor_ = RGBA{};
    currentLineWidth_ = 1.0f;
}

std::uint32_t OpenGLRenderer::createTextureFromImage(const QImage& image, bool magNearest) {
    if (!functions_ || image.isNull()) return 0;

    GLuint texture = 0;
    functions_->glGenTextures(1, &texture);
    const std::uint32_t id = nextTextureId_++;
    textures_.insert(id, texture);
    updateTextureFromImage(id, image, magNearest);
    return id;
}

std::uint32_t OpenGLRenderer::createTextureR8(int width,
                                              int height,
                                              const QByteArray& bytes,
                                              bool magNearest) {
    if (!functions_ || width <= 0 || height <= 0) return 0;
    if (bytes.size() < width * height) return 0;

    GLuint texture = 0;
    functions_->glGenTextures(1, &texture);

    const std::uint32_t id = nextTextureId_++;
    textures_.insert(id, texture);

    functions_->glBindTexture(GL_TEXTURE_2D, texture);
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    functions_->glTexParameteri(GL_TEXTURE_2D,
                                GL_TEXTURE_MAG_FILTER,
                                magNearest ? GL_NEAREST : GL_LINEAR);
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    functions_->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    functions_->glTexImage2D(GL_TEXTURE_2D,
                             0,
                             GL_R8,
                             width,
                             height,
                             0,
                             GL_RED,
                             GL_UNSIGNED_BYTE,
                             bytes.constData());
    functions_->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    functions_->glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

void OpenGLRenderer::updateTextureFromImage(std::uint32_t id,
                                            const QImage& image,
                                            bool magNearest) {
    if (!functions_ || image.isNull() || !textures_.contains(id)) return;

    functions_->glBindTexture(GL_TEXTURE_2D, textures_.value(id));
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    functions_->glTexParameteri(GL_TEXTURE_2D,
                                GL_TEXTURE_MAG_FILTER,
                                magNearest ? GL_NEAREST : GL_LINEAR);
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (image.format() == QImage::Format_Grayscale8) {
        const QImage grayscale = image.convertToFormat(QImage::Format_Grayscale8);
        functions_->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        functions_->glTexImage2D(GL_TEXTURE_2D,
                                 0,
                                 GL_R8,
                                 grayscale.width(),
                                 grayscale.height(),
                                 0,
                                 GL_RED,
                                 GL_UNSIGNED_BYTE,
                                 grayscale.constBits());
        functions_->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    } else {
        const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
        functions_->glTexImage2D(GL_TEXTURE_2D,
                                 0,
                                 GL_RGBA8,
                                 rgba.width(),
                                 rgba.height(),
                                 0,
                                 GL_RGBA,
                                 GL_UNSIGNED_BYTE,
                                 rgba.constBits());
    }

    functions_->glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderer::destroyTexture(std::uint32_t id) {
    if (!functions_ || !textures_.contains(id)) return;

    GLuint texture = textures_.take(id);
    functions_->glDeleteTextures(1, &texture);
}

void OpenGLRenderer::bindTexture(std::uint32_t id) {
    functions_->glActiveTexture(GL_TEXTURE0);
    functions_->glBindTexture(GL_TEXTURE_2D, textures_.value(id, 0));
}

RendererStats OpenGLRenderer::renderCommandBuffer(CommandBuffer* commandBuffer) {
    RendererStats stats;
    if (!ready_ || !functions_ || !commandBuffer) return stats;

    ++stats.nBuffers;
    resetState();

    for (const Command& command : commandBuffer->commands()) {
        switch (command.type) {
        case Command::Type::ResetState:
            resetState();
            break;
        case Command::Type::LoadProjectionMatrix:
            projection_ = command.matrix;
            break;
        case Command::Type::Clear:
            functions_->glClearColor(command.color.r,
                                     command.color.g,
                                     command.color.b,
                                     command.color.a);
            functions_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            break;
        case Command::Type::Viewport:
            functions_->glViewport(command.x, command.y, command.w, command.h);
            break;
        case Command::Type::Scissor:
            functions_->glEnable(GL_SCISSOR_TEST);
            functions_->glScissor(command.x, command.y, command.w, command.h);
            break;
        case Command::Type::DisableScissor:
            functions_->glDisable(GL_SCISSOR_TEST);
            break;
        case Command::Type::Blend:
            functions_->glEnable(GL_BLEND);
            functions_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case Command::Type::DisableBlend:
            functions_->glDisable(GL_BLEND);
            break;
        case Command::Type::SetColor:
            currentColor_ = command.color;
            break;
        case Command::Type::LineWidth:
            currentLineWidth_ = command.lineWidth;
            functions_->glLineWidth(currentLineWidth_);
            break;
        case Command::Type::DrawLines:
            if (command.points.isEmpty() || command.indices.isEmpty()) break;

            solidShader_.bind();
            solidShader_.setUniformValue("u_projection", projection_);
            solidShader_.setUniformValue("u_color", colorVector(currentColor_));
            functions_->glBindVertexArray(vao_);
            functions_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            functions_->glBufferData(GL_ARRAY_BUFFER,
                                     byteSize(command.points),
                                     command.points.constData(),
                                     GL_DYNAMIC_DRAW);
            functions_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
            functions_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                     byteSize(command.indices),
                                     command.indices.constData(),
                                     GL_DYNAMIC_DRAW);
            functions_->glEnableVertexAttribArray(0);
            functions_->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PointVertex), nullptr);
            functions_->glLineWidth(currentLineWidth_);
            functions_->glDrawElements(GL_LINES,
                                       GLsizei(command.indices.size()),
                                       GL_UNSIGNED_INT,
                                       nullptr);
            functions_->glDisableVertexAttribArray(0);
            solidShader_.release();
            ++stats.nDrawCalls;
            stats.nLines += int(command.indices.size() / 2);
            break;
        case Command::Type::DrawTriangles: {
            if (command.points.isEmpty() || command.indices.isEmpty()) break;

            QOpenGLShaderProgram& shader =
                command.drawMode == DrawMode::Hatched ? hatchShader_ : solidShader_;
            shader.bind();
            shader.setUniformValue("u_projection", projection_);
            shader.setUniformValue("u_color", colorVector(currentColor_));
            if (command.drawMode == DrawMode::Hatched)
                shader.setUniformValue("u_offset", command.hatchOffset);

            functions_->glBindVertexArray(vao_);
            functions_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            functions_->glBufferData(GL_ARRAY_BUFFER,
                                     byteSize(command.points),
                                     command.points.constData(),
                                     GL_DYNAMIC_DRAW);
            functions_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
            functions_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                     byteSize(command.indices),
                                     command.indices.constData(),
                                     GL_DYNAMIC_DRAW);
            functions_->glEnableVertexAttribArray(0);
            functions_->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PointVertex), nullptr);
            functions_->glDrawElements(GL_TRIANGLES,
                                       GLsizei(command.indices.size()),
                                       GL_UNSIGNED_INT,
                                       nullptr);
            functions_->glDisableVertexAttribArray(0);
            shader.release();
            ++stats.nDrawCalls;
            stats.nTriangles += int(command.indices.size() / 3);
            break;
        }
        case Command::Type::DrawColoredLines:
            if (command.coloredPoints.isEmpty() || command.indices.isEmpty()) break;

            coloredShader_.bind();
            coloredShader_.setUniformValue("u_projection", projection_);
            functions_->glBindVertexArray(vao_);
            functions_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            functions_->glBufferData(GL_ARRAY_BUFFER,
                                     byteSize(command.coloredPoints),
                                     command.coloredPoints.constData(),
                                     GL_DYNAMIC_DRAW);
            functions_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
            functions_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                     byteSize(command.indices),
                                     command.indices.constData(),
                                     GL_DYNAMIC_DRAW);
            functions_->glEnableVertexAttribArray(0);
            functions_->glVertexAttribPointer(0,
                                              2,
                                              GL_FLOAT,
                                              GL_FALSE,
                                              sizeof(ColoredVertex),
                                              reinterpret_cast<void*>(offsetof(ColoredVertex, x)));
            functions_->glEnableVertexAttribArray(1);
            functions_->glVertexAttribPointer(1,
                                              3,
                                              GL_FLOAT,
                                              GL_FALSE,
                                              sizeof(ColoredVertex),
                                              reinterpret_cast<void*>(offsetof(ColoredVertex, color)));
            functions_->glLineWidth(currentLineWidth_);
            functions_->glDrawElements(GL_LINES,
                                       GLsizei(command.indices.size()),
                                       GL_UNSIGNED_INT,
                                       nullptr);
            functions_->glDisableVertexAttribArray(0);
            functions_->glDisableVertexAttribArray(1);
            coloredShader_.release();
            ++stats.nDrawCalls;
            stats.nLines += int(command.indices.size() / 2);
            break;
        case Command::Type::DrawColoredTriangles:
            if (command.coloredPoints.isEmpty() || command.indices.isEmpty()) break;

            coloredShader_.bind();
            coloredShader_.setUniformValue("u_projection", projection_);
            functions_->glBindVertexArray(vao_);
            functions_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            functions_->glBufferData(GL_ARRAY_BUFFER,
                                     byteSize(command.coloredPoints),
                                     command.coloredPoints.constData(),
                                     GL_DYNAMIC_DRAW);
            functions_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
            functions_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                     byteSize(command.indices),
                                     command.indices.constData(),
                                     GL_DYNAMIC_DRAW);
            functions_->glEnableVertexAttribArray(0);
            functions_->glVertexAttribPointer(0,
                                              2,
                                              GL_FLOAT,
                                              GL_FALSE,
                                              sizeof(ColoredVertex),
                                              reinterpret_cast<void*>(offsetof(ColoredVertex, x)));
            functions_->glEnableVertexAttribArray(1);
            functions_->glVertexAttribPointer(1,
                                              3,
                                              GL_FLOAT,
                                              GL_FALSE,
                                              sizeof(ColoredVertex),
                                              reinterpret_cast<void*>(offsetof(ColoredVertex, color)));
            functions_->glDrawElements(GL_TRIANGLES,
                                       GLsizei(command.indices.size()),
                                       GL_UNSIGNED_INT,
                                       nullptr);
            functions_->glDisableVertexAttribArray(0);
            functions_->glDisableVertexAttribArray(1);
            coloredShader_.release();
            ++stats.nDrawCalls;
            stats.nTriangles += int(command.indices.size() / 3);
            break;
        case Command::Type::DrawTexturedTriangles:
            if (command.texturedPoints.isEmpty() || command.indices.isEmpty()) break;

            texturedShader_.bind();
            texturedShader_.setUniformValue("u_projection", projection_);
            texturedShader_.setUniformValue("u_texture", 0);
            texturedShader_.setUniformValue("u_color", colorVector(currentColor_));
            bindTexture(command.textureId);
            functions_->glBindVertexArray(vao_);
            functions_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            functions_->glBufferData(GL_ARRAY_BUFFER,
                                     byteSize(command.texturedPoints),
                                     command.texturedPoints.constData(),
                                     GL_DYNAMIC_DRAW);
            functions_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
            functions_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                     byteSize(command.indices),
                                     command.indices.constData(),
                                     GL_DYNAMIC_DRAW);
            functions_->glEnableVertexAttribArray(0);
            functions_->glVertexAttribPointer(0,
                                              2,
                                              GL_FLOAT,
                                              GL_FALSE,
                                              sizeof(TexturedVertex),
                                              reinterpret_cast<void*>(offsetof(TexturedVertex, x)));
            functions_->glEnableVertexAttribArray(1);
            functions_->glVertexAttribPointer(1,
                                              2,
                                              GL_FLOAT,
                                              GL_FALSE,
                                              sizeof(TexturedVertex),
                                              reinterpret_cast<void*>(offsetof(TexturedVertex, u)));
            functions_->glDrawElements(GL_TRIANGLES,
                                       GLsizei(command.indices.size()),
                                       GL_UNSIGNED_INT,
                                       nullptr);
            functions_->glDisableVertexAttribArray(0);
            functions_->glDisableVertexAttribArray(1);
            functions_->glBindTexture(GL_TEXTURE_2D, 0);
            texturedShader_.release();
            ++stats.nDrawCalls;
            stats.nTriangles += int(command.indices.size() / 3);
            break;
        case Command::Type::DrawFontTriangles:
            if (command.fontPoints.isEmpty() || command.indices.isEmpty()) break;

            fontShader_.bind();
            fontShader_.setUniformValue("u_projection", projection_);
            fontShader_.setUniformValue("u_fontAtlas", 0);
            bindTexture(command.textureId);
            functions_->glBindVertexArray(vao_);
            functions_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            functions_->glBufferData(GL_ARRAY_BUFFER,
                                     byteSize(command.fontPoints),
                                     command.fontPoints.constData(),
                                     GL_DYNAMIC_DRAW);
            functions_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
            functions_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                     byteSize(command.indices),
                                     command.indices.constData(),
                                     GL_DYNAMIC_DRAW);
            functions_->glEnableVertexAttribArray(0);
            functions_->glVertexAttribPointer(0,
                                              2,
                                              GL_FLOAT,
                                              GL_FALSE,
                                              sizeof(FontVertex),
                                              reinterpret_cast<void*>(offsetof(FontVertex, x)));
            functions_->glEnableVertexAttribArray(1);
            functions_->glVertexAttribPointer(1,
                                              2,
                                              GL_FLOAT,
                                              GL_FALSE,
                                              sizeof(FontVertex),
                                              reinterpret_cast<void*>(offsetof(FontVertex, u)));
            functions_->glEnableVertexAttribArray(2);
            functions_->glVertexAttribPointer(2,
                                              4,
                                              GL_FLOAT,
                                              GL_FALSE,
                                              sizeof(FontVertex),
                                              reinterpret_cast<void*>(offsetof(FontVertex, color)));
            functions_->glEnableVertexAttribArray(3);
            functions_->glVertexAttribPointer(3,
                                              4,
                                              GL_FLOAT,
                                              GL_FALSE,
                                              sizeof(FontVertex),
                                              reinterpret_cast<void*>(offsetof(FontVertex, background)));
            functions_->glDrawElements(GL_TRIANGLES,
                                       GLsizei(command.indices.size()),
                                       GL_UNSIGNED_INT,
                                       nullptr);
            functions_->glDisableVertexAttribArray(0);
            functions_->glDisableVertexAttribArray(1);
            functions_->glDisableVertexAttribArray(2);
            functions_->glDisableVertexAttribArray(3);
            functions_->glBindTexture(GL_TEXTURE_2D, 0);
            fontShader_.release();
            ++stats.nDrawCalls;
            stats.nTriangles += int(command.indices.size() / 3);
            break;
        }
    }

    functions_->glBindBuffer(GL_ARRAY_BUFFER, 0);
    functions_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    functions_->glBindVertexArray(0);
    functions_->glUseProgram(0);
    return stats;
}

void OpenGLRenderer::readPixels(int x, int y, int w, int h, std::uint8_t* outRgba) {
    if (!functions_ || !outRgba) return;
    functions_->glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, outRgba);
}

std::unique_ptr<Renderer> makeOpenGLRenderer() {
    return std::make_unique<OpenGLRenderer>();
}

} // namespace renderer
