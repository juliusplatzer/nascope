#pragma once

#include "asdex/render/targets.h"
#include "renderer/text/bitmap_font_renderer.h"

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPointF>
#include <QStringList>
#include <QVector>

#include <functional>

namespace asdex {

enum class LeaderDirection {
    NE,
    N,
    E,
    SE,
    S,
    SW,
    W,
    NW,
};

enum class DataBlockVisibility {
    Inherit,
    ForceOn,
    ForceOff,
};

struct DataBlockSettings {
    bool showDataBlocks = true;
    bool fullDataBlocks = true;

    int fontSize = 2;
    int brightness = 95;
    int leaderLength = 2;
    LeaderDirection leaderDirection = LeaderDirection::NE;
    bool timesharePrimary = true;
    bool alertInProgress = false;

    bool showAltitude = false;
    bool showAircraftType = true;
    bool showSensors = false;
    bool showAircraftCategory = false;
    bool showFix = true;
    bool showVelocity = false;
    bool showScratchpads = true;
};

class DataBlockRenderer {
public:
    DataBlockRenderer();
    ~DataBlockRenderer();

    DataBlockRenderer(const DataBlockRenderer&) = delete;
    DataBlockRenderer& operator=(const DataBlockRenderer&) = delete;

    void initialize();
    void deinitialize();

    void render(const QVector<AsdexTarget>& targets,
                const QMatrix4x4& screenProjection,
                const std::function<QPointF(QPointF)>& worldToScreen,
                const std::function<bool(const AsdexTarget&)>& isVisible,
                renderer::BitmapFontRenderer& textRenderer,
                const DataBlockSettings& settings);

private:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
    };

    void initializeShaders();
    void drawScreenLine(const QPointF& a,
                        const QPointF& b,
                        const QColor& color,
                        const QMatrix4x4& screenProjection);
    void renderOneDataBlock(const AsdexTarget& target,
                            const QPointF& targetScreen,
                            renderer::BitmapFontRenderer& textRenderer,
                            const DataBlockSettings& settings,
                            const QMatrix4x4& screenProjection);

    QOpenGLShaderProgram shader_;
    QOpenGLVertexArrayObject lineVao_;
    QOpenGLBuffer lineVbo_{QOpenGLBuffer::VertexBuffer};
    bool ready_ = false;
};

} // namespace asdex
