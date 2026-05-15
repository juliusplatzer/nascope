#ifndef RENDERER_GL_DISPLAY_EMULATION_PASS_H_
#define RENDERER_GL_DISPLAY_EMULATION_PASS_H_

#include "renderer/display_emulation.h"

#include <QOpenGLShaderProgram>
#include <QSize>

#include <QtGui/qopengl.h>

class QOpenGLFunctions_3_3_Core;

namespace renderer {

class DisplayEmulationPass {
public:
    DisplayEmulationPass();
    ~DisplayEmulationPass();

    DisplayEmulationPass(const DisplayEmulationPass&) = delete;
    DisplayEmulationPass& operator=(const DisplayEmulationPass&) = delete;

    bool initialize(QOpenGLFunctions_3_3_Core* functions, QString* error = nullptr);
    void deinitialize();

    bool beginScene(QSize sceneSize);
    void endSceneToFramebuffer(GLint destinationFramebuffer,
                               QSize outputFramebufferSize,
                               const DisplayEmulationSettings& settings);

private:
    bool ensureSceneResources(QSize sceneSize);
    void deleteSceneResources();

    QOpenGLFunctions_3_3_Core* functions_ = nullptr;
    QOpenGLShaderProgram necShader_;
    GLuint sceneFbo_[2] = {0, 0};
    GLuint sceneTexture_[2] = {0, 0};
    GLuint compositeVao_ = 0;
    QSize sceneSize_;
    int writeIndex_ = 0;
    bool historyValid_ = false;
    bool ready_ = false;
};

}  // namespace renderer

#endif  // RENDERER_GL_DISPLAY_EMULATION_PASS_H_
