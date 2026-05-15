#ifndef RENDERER_GL_OPENGLRENDERER_H_
#define RENDERER_GL_OPENGLRENDERER_H_

#include "renderer/rgb.h"
#include "renderer/renderer.h"

#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLShaderProgram>

#include <QtGui/qopengl.h>

class QOpenGLFunctions_3_3_Core;

namespace renderer {

class OpenGLRenderer : public Renderer {
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    OpenGLRenderer(const OpenGLRenderer&) = delete;
    OpenGLRenderer& operator=(const OpenGLRenderer&) = delete;

    bool initialize(QString* error = nullptr) override;
    void deinitialize() override;

    std::uint32_t createTextureFromImage(const QImage& image, bool magNearest) override;
    std::uint32_t createTextureR8(int width,
                                  int height,
                                  const QByteArray& bytes,
                                  bool magNearest) override;
    void updateTextureFromImage(std::uint32_t id,
                                const QImage& image,
                                bool magNearest) override;
    void destroyTexture(std::uint32_t id) override;

    RendererStats renderCommandBuffer(CommandBuffer* commandBuffer) override;
    void readPixels(int x, int y, int w, int h, std::uint8_t* outRgba) override;

private:
    bool initializeShaders(QString* error);
    void resetState();
    void bindTexture(std::uint32_t id);

    QOpenGLFunctions_3_3_Core* functions_ = nullptr;
    QOpenGLShaderProgram solidShader_;
    QOpenGLShaderProgram hatchShader_;
    QOpenGLShaderProgram coloredShader_;
    QOpenGLShaderProgram texturedShader_;
    QOpenGLShaderProgram fontShader_;
    QHash<std::uint32_t, GLuint> textures_;
    std::uint32_t nextTextureId_ = 1;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    QMatrix4x4 projection_;
    RGBA currentColor_;
    float currentLineWidth_ = 1.0f;
    bool ready_ = false;
};

} // namespace renderer

#endif  // RENDERER_GL_OPENGLRENDERER_H_
