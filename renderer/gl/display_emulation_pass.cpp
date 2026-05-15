#include "renderer/gl/display_emulation_pass.h"

#include <QOpenGLFunctions_3_3_Core>
#include <QRect>
#include <QVector2D>
#include <QVector3D>

#include <algorithm>
#include <cmath>

namespace renderer {
namespace {

constexpr char kPostVertexShader[] = R"(
#version 330 core

out vec2 v_uv;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 p = positions[gl_VertexID];
    v_uv = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

constexpr char kNecFragmentShader[] = R"(
#version 330 core

uniform sampler2D u_currentFrame;
uniform sampler2D u_previousFrame;
uniform bool u_historyValid;

uniform vec2 u_panelSize;

uniform float u_blackLift;
uniform float u_contrastScale;
uniform float u_saturationScale;
uniform vec3 u_whiteBalance;
uniform float u_pixelGridStrength;
uniform float u_temporalBlend;

in vec2 v_uv;
out vec4 fragColor;

vec3 srgbToLinear(vec3 c) {
    bvec3 cutoff = lessThanEqual(c, vec3(0.04045));
    vec3 low = c / 12.92;
    vec3 high = pow((c + vec3(0.055)) / 1.055, vec3(2.4));
    return mix(high, low, vec3(cutoff));
}

vec3 linearToSrgb(vec3 c) {
    bvec3 cutoff = lessThanEqual(c, vec3(0.0031308));
    vec3 low = c * 12.92;
    vec3 high = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - vec3(0.055);
    return mix(high, low, vec3(cutoff));
}

vec3 emulateNec(vec3 srgb, vec2 uv) {
    vec3 lin = srgbToLinear(srgb);

    lin = lin * (1.0 - u_blackLift) + vec3(u_blackLift);
    lin = (lin - vec3(0.5)) * u_contrastScale + vec3(0.5);

    float y = dot(lin, vec3(0.2126, 0.7152, 0.0722));
    lin = mix(vec3(y), lin, u_saturationScale);
    lin *= u_whiteBalance;

    vec2 panelPx = uv * u_panelSize;
    vec2 cell = fract(panelPx);
    float gridX = smoothstep(0.92, 1.0, cell.x);
    float gridY = smoothstep(0.92, 1.0, cell.y);
    float grid = max(gridX, gridY);
    lin *= 1.0 - u_pixelGridStrength * grid;

    return linearToSrgb(clamp(lin, 0.0, 1.0));
}

void main() {
    vec3 current = emulateNec(texture(u_currentFrame, v_uv).rgb, v_uv);

    if (u_historyValid) {
        vec3 previous = emulateNec(texture(u_previousFrame, v_uv).rgb, v_uv);
        current = mix(current, previous, u_temporalBlend);
    }

    fragColor = vec4(current, 1.0);
}
)";

QRect destinationRect(QSize source, QSize output, DisplayScaleMode scaleMode) {
    if (source.isEmpty() || output.isEmpty()) return QRect();

    if (scaleMode == DisplayScaleMode::Stretch) {
        return QRect(0, 0, output.width(), output.height());
    }

    const double sx = double(output.width()) / double(source.width());
    const double sy = double(output.height()) / double(source.height());
    const double scale = std::min(sx, sy);

    const int width = int(std::round(source.width() * scale));
    const int height = int(std::round(source.height() * scale));
    const int x = (output.width() - width) / 2;
    const int y = (output.height() - height) / 2;
    return QRect(x, y, width, height);
}

}  // namespace

DisplayEmulationPass::DisplayEmulationPass() = default;

DisplayEmulationPass::~DisplayEmulationPass() {
    deinitialize();
}

bool DisplayEmulationPass::initialize(QOpenGLFunctions_3_3_Core* functions, QString* error) {
    functions_ = functions;
    if (!functions_) {
        if (error) *error = QStringLiteral("OpenGL functions are unavailable");
        return false;
    }

    if (!necShader_.addShaderFromSourceCode(QOpenGLShader::Vertex, kPostVertexShader)
        || !necShader_.addShaderFromSourceCode(QOpenGLShader::Fragment, kNecFragmentShader)
        || !necShader_.link()) {
        if (error) *error = QStringLiteral("display emulation shader setup failed: %1")
                                .arg(necShader_.log());
        return false;
    }

    functions_->glGenVertexArrays(1, &compositeVao_);
    ready_ = true;
    return true;
}

void DisplayEmulationPass::deinitialize() {
    if (!functions_) return;

    deleteSceneResources();

    if (compositeVao_) {
        functions_->glDeleteVertexArrays(1, &compositeVao_);
        compositeVao_ = 0;
    }

    necShader_.removeAllShaders();
    ready_ = false;
    functions_ = nullptr;
}

bool DisplayEmulationPass::beginScene(QSize sceneSize) {
    if (!ready_ || sceneSize.isEmpty()) return false;
    if (!ensureSceneResources(sceneSize)) return false;

    functions_->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo_[writeIndex_]);
    functions_->glViewport(0, 0, sceneSize.width(), sceneSize.height());
    functions_->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    functions_->glClear(GL_COLOR_BUFFER_BIT);
    return true;
}

void DisplayEmulationPass::endSceneToFramebuffer(
    GLint destinationFramebuffer,
    QSize outputFramebufferSize,
    const DisplayEmulationSettings& settings) {
    if (!ready_ || sceneSize_.isEmpty() || outputFramebufferSize.isEmpty()) return;

    functions_->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(destinationFramebuffer));
    functions_->glViewport(0, 0, outputFramebufferSize.width(), outputFramebufferSize.height());
    functions_->glDisable(GL_DEPTH_TEST);
    functions_->glDisable(GL_STENCIL_TEST);
    functions_->glDisable(GL_SCISSOR_TEST);
    functions_->glDisable(GL_BLEND);
    functions_->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    functions_->glClear(GL_COLOR_BUFFER_BIT);

    const QRect dst = destinationRect(sceneSize_, outputFramebufferSize, settings.scaleMode);
    if (dst.isEmpty()) return;

    functions_->glViewport(dst.x(), dst.y(), dst.width(), dst.height());

    necShader_.bind();
    necShader_.setUniformValue("u_currentFrame", 0);
    necShader_.setUniformValue("u_previousFrame", 1);
    necShader_.setUniformValue("u_historyValid", historyValid_);
    necShader_.setUniformValue("u_panelSize", QVector2D(sceneSize_.width(), sceneSize_.height()));
    necShader_.setUniformValue("u_blackLift", settings.blackLift);
    necShader_.setUniformValue("u_contrastScale", settings.contrastScale);
    necShader_.setUniformValue("u_saturationScale", settings.saturationScale);
    necShader_.setUniformValue("u_whiteBalance",
                               QVector3D(settings.whiteBalanceR,
                                         settings.whiteBalanceG,
                                         settings.whiteBalanceB));
    necShader_.setUniformValue("u_pixelGridStrength", settings.pixelGridStrength);
    necShader_.setUniformValue("u_temporalBlend", settings.temporalBlend);

    functions_->glActiveTexture(GL_TEXTURE0);
    functions_->glBindTexture(GL_TEXTURE_2D, sceneTexture_[writeIndex_]);
    functions_->glActiveTexture(GL_TEXTURE1);
    functions_->glBindTexture(GL_TEXTURE_2D, sceneTexture_[1 - writeIndex_]);

    functions_->glBindVertexArray(compositeVao_);
    functions_->glDrawArrays(GL_TRIANGLES, 0, 3);
    functions_->glBindVertexArray(0);

    functions_->glActiveTexture(GL_TEXTURE1);
    functions_->glBindTexture(GL_TEXTURE_2D, 0);
    functions_->glActiveTexture(GL_TEXTURE0);
    functions_->glBindTexture(GL_TEXTURE_2D, 0);
    necShader_.release();

    historyValid_ = true;
    writeIndex_ = 1 - writeIndex_;
}

bool DisplayEmulationPass::ensureSceneResources(QSize sceneSize) {
    if (sceneSize == sceneSize_ && sceneFbo_[0] != 0 && sceneTexture_[0] != 0) return true;

    deleteSceneResources();

    sceneSize_ = sceneSize;
    historyValid_ = false;
    writeIndex_ = 0;

    functions_->glGenTextures(2, sceneTexture_);
    functions_->glGenFramebuffers(2, sceneFbo_);

    for (int i = 0; i < 2; ++i) {
        functions_->glBindTexture(GL_TEXTURE_2D, sceneTexture_[i]);
        functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        functions_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        functions_->glTexImage2D(GL_TEXTURE_2D,
                                 0,
                                 GL_RGBA8,
                                 sceneSize.width(),
                                 sceneSize.height(),
                                 0,
                                 GL_RGBA,
                                 GL_UNSIGNED_BYTE,
                                 nullptr);

        functions_->glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_[i]);
        functions_->glFramebufferTexture2D(GL_FRAMEBUFFER,
                                           GL_COLOR_ATTACHMENT0,
                                           GL_TEXTURE_2D,
                                           sceneTexture_[i],
                                           0);
        if (functions_->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            deleteSceneResources();
            return false;
        }
    }

    functions_->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    functions_->glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void DisplayEmulationPass::deleteSceneResources() {
    if (sceneFbo_[0] != 0 || sceneFbo_[1] != 0) {
        functions_->glDeleteFramebuffers(2, sceneFbo_);
        sceneFbo_[0] = 0;
        sceneFbo_[1] = 0;
    }

    if (sceneTexture_[0] != 0 || sceneTexture_[1] != 0) {
        functions_->glDeleteTextures(2, sceneTexture_);
        sceneTexture_[0] = 0;
        sceneTexture_[1] = 0;
    }

    sceneSize_ = QSize();
    historyValid_ = false;
    writeIndex_ = 0;
}

}  // namespace renderer
