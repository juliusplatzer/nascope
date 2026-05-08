#include "renderer/datablocks.h"

#include <QDateTime>
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QVector4D>

#include <algorithm>
#include <cmath>

namespace renderer {
namespace {

constexpr int kDatablockLineSpacing = 2;
constexpr int kLeaderStartOffsetPx = 7;
constexpr int kLeaderStepPx = 15;
constexpr int kZeroLengthAnchorPx = 10;

constexpr char kVertexShader[] = R"(
#version 330 core
layout(location = 0) in vec2 a_position;

uniform mat4 u_projection;
uniform mat4 u_model;

void main() {
    gl_Position = u_projection * u_model * vec4(a_position, 0.0, 1.0);
}
)";

constexpr char kFragmentShader[] = R"(
#version 330 core

uniform vec4 u_color;

out vec4 fragColor;

void main() {
    fragColor = u_color;
}
)";

struct BuiltDataBlock {
    QStringList lines;
    int maxLineWidth = 0;
    int longestHighestLineNumber = 0;
    int longestLowestLineNumber = 0;
};

QVector4D colorVector(const QColor& color) {
    return QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF());
}

int leaderHeadingDegrees(LeaderDirection direction) {
    switch (direction) {
        case LeaderDirection::N:
            return 360;
        case LeaderDirection::E:
            return 90;
        case LeaderDirection::SE:
            return 135;
        case LeaderDirection::S:
            return 180;
        case LeaderDirection::SW:
            return 225;
        case LeaderDirection::W:
            return 270;
        case LeaderDirection::NW:
            return 315;
        case LeaderDirection::NE:
            return 45;
    }

    return 45;
}

bool isLeftDatablock(LeaderDirection direction) {
    return direction == LeaderDirection::SW
        || direction == LeaderDirection::W
        || direction == LeaderDirection::NW;
}

QPointF leaderDelta(double distancePx, int headingDegrees) {
    const double rad = headingDegrees * M_PI / 180.0;
    const double dx = distancePx * std::sin(rad);
    const double dy = distancePx * std::cos(rad);
    return QPointF(static_cast<int>(dx), -static_cast<int>(dy));
}

QString altitudeHundreds(std::optional<double> altitudeTrue) {
    if (!altitudeTrue) return QStringLiteral("XXX");

    const int hundreds = int(std::round(*altitudeTrue / 100.0));
    return QStringLiteral("%1").arg(std::clamp(hundreds, 0, 999), 3, 10, QLatin1Char('0'));
}

QString velocityTens(double groundSpeedKnots) {
    const int tens = int(std::round(groundSpeedKnots / 10.0));
    return QStringLiteral("%1").arg(std::clamp(tens, 0, 99), 2, 10, QLatin1Char('0'));
}

QString beaconOrNoBeacon(const QString& beaconCode) {
    const QString trimmed = beaconCode.trimmed();
    if (trimmed.isEmpty()) return QStringLiteral("NO BCN");
    return trimmed.rightJustified(4, QLatin1Char('0'));
}

bool scratchpadPhase() {
    constexpr qint64 kTimeShareCycleMs = 8000;
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    return (ms % kTimeShareCycleMs) >= (kTimeShareCycleMs / 2);
}

BuiltDataBlock buildDataBlock(const asdex::AsdexTarget& target,
                              const DataBlockSettings& settings,
                              const BitmapFontRenderer& textRenderer) {
    BuiltDataBlock out;

    out.lines << (target.duplicateBeaconCode ? QStringLiteral("DUP BCN") : QString());

    QString line1;
    if (!target.callsign.trimmed().isEmpty())
        line1 = target.callsign.trimmed();
    else
        line1 = beaconOrNoBeacon(target.beaconCode);

    if (settings.fullDataBlocks && settings.showAltitude) {
        line1 += QLatin1Char(' ');
        line1 += altitudeHundreds(target.altitudeTrue);
    }

    if (settings.fullDataBlocks) {
        if (target.coasting) {
            line1 += QStringLiteral(" CST");
        } else if (settings.showSensors) {
            line1 += QStringLiteral(" FUS");
        }
    }

    out.lines << line1.trimmed();

    QString primaryLine2;
    if (settings.showAircraftType && !target.aircraftType.trimmed().isEmpty()) {
        primaryLine2 += target.aircraftType.trimmed();
    }

    if (settings.fullDataBlocks
        && settings.showAircraftCategory
        && !target.category.trimmed().isEmpty()) {
        if (!primaryLine2.isEmpty()) primaryLine2 += QLatin1Char(' ');
        primaryLine2 += target.category.trimmed();
    }

    if (settings.showFix && !target.fix.trimmed().isEmpty()) {
        if (!primaryLine2.isEmpty()) primaryLine2 += QLatin1Char(' ');
        primaryLine2 += target.fix.trimmed();
    }

    if (settings.fullDataBlocks && settings.showVelocity) {
        if (!primaryLine2.isEmpty()) primaryLine2 += QLatin1Char(' ');
        primaryLine2 += velocityTens(target.groundSpeedKnots);
    }

    QString scratchLine2;
    if (settings.showScratchpads) {
        scratchLine2 = QStringLiteral("%1 %2")
                           .arg(target.scratchpad1, target.scratchpad2)
                           .trimmed();
    }

    QString line2;
    if (scratchLine2.isEmpty())
        line2 = primaryLine2.trimmed();
    else if (primaryLine2.trimmed().isEmpty())
        line2 = scratchLine2;
    else
        line2 = scratchpadPhase() ? scratchLine2 : primaryLine2.trimmed();

    out.lines << line2;

    for (int i = 0; i < out.lines.size(); ++i) {
        const int width = textRenderer.measureText(QStringView(out.lines.at(i)), settings.fontSize).width();

        if (width > out.maxLineWidth) {
            out.maxLineWidth = width;
            out.longestHighestLineNumber = i;
            out.longestLowestLineNumber = i;
        } else if (width == out.maxLineWidth) {
            out.longestLowestLineNumber = i;
        }
    }

    return out;
}

} // namespace

DataBlockRenderer::DataBlockRenderer() = default;

DataBlockRenderer::~DataBlockRenderer() = default;

void DataBlockRenderer::initialize() {
    initializeShaders();
    if (!ready_) return;

    lineVao_.create();
    QOpenGLVertexArrayObject::Binder binder(&lineVao_);

    lineVbo_.create();
    lineVbo_.bind();

    shader_.bind();
    shader_.enableAttributeArray(0);
    shader_.setAttributeBuffer(0, GL_FLOAT, 0, 2, sizeof(Vertex));
    shader_.release();

    lineVbo_.release();
}

void DataBlockRenderer::deinitialize() {
    lineVao_.destroy();
    lineVbo_.destroy();
    shader_.removeAllShaders();
    ready_ = false;
}

void DataBlockRenderer::initializeShaders() {
    if (!shader_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
        || !shader_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)
        || !shader_.link()) {
        qWarning().noquote() << "[renderer] datablock shader setup failed:" << shader_.log();
        ready_ = false;
        return;
    }

    ready_ = true;
}

void DataBlockRenderer::render(const QVector<asdex::AsdexTarget>& targets,
                               const QMatrix4x4& screenProjection,
                               const std::function<QPointF(QPointF)>& worldToScreen,
                               BitmapFontRenderer& textRenderer,
                               const DataBlockSettings& settings) {
    if (!ready_ || !settings.showDataBlocks) return;

    for (const asdex::AsdexTarget& target : targets) {
        if (!target.correlated) continue;
        renderOneDataBlock(target,
                           worldToScreen(target.positionFeet),
                           textRenderer,
                           settings,
                           screenProjection);
    }
}

void DataBlockRenderer::drawScreenLine(const QPointF& a,
                                       const QPointF& b,
                                       const QColor& color,
                                       const QMatrix4x4& screenProjection) {
    const Vertex vertices[2] = {
        {float(a.x()), float(a.y())},
        {float(b.x()), float(b.y())},
    };

    lineVao_.bind();
    lineVbo_.bind();
    lineVbo_.allocate(vertices, int(sizeof(vertices)));

    QMatrix4x4 model;
    model.setToIdentity();

    shader_.bind();
    shader_.setUniformValue("u_projection", screenProjection);
    shader_.setUniformValue("u_model", model);
    shader_.setUniformValue("u_color", colorVector(color));

    QOpenGLFunctions* functions = QOpenGLContext::currentContext()->functions();
    functions->glLineWidth(1.0f);
    functions->glDrawArrays(GL_LINES, 0, 2);

    shader_.release();

    lineVbo_.release();
    lineVao_.release();
}

void DataBlockRenderer::renderOneDataBlock(const asdex::AsdexTarget& target,
                                           const QPointF& targetScreen,
                                           BitmapFontRenderer& textRenderer,
                                           const DataBlockSettings& settings,
                                           const QMatrix4x4& screenProjection) {
    const int fontSize = settings.fontSize;
    const int height = textRenderer.lineHeight(fontSize);
    if (height <= 0) return;

    const QColor color = asdex::applyBrightness(QColor(0, 208, 0), settings.brightness, 20);
    const BuiltDataBlock block = buildDataBlock(target, settings, textRenderer);
    if (block.maxLineWidth <= 0) return;

    const LeaderDirection direction = settings.leaderDirection;
    const int heading = leaderHeadingDegrees(direction);
    const bool left = isLeftDatablock(direction);
    const int leaderLengthPx = std::max(0, settings.leaderLength) * kLeaderStepPx;

    const QPointF leaderStart = targetScreen + leaderDelta(kLeaderStartOffsetPx, heading);
    const QPointF leaderEnd =
        targetScreen + leaderDelta(leaderLengthPx == 0 ? kZeroLengthAnchorPx : leaderLengthPx,
                                   heading);

    if (leaderLengthPx > 0) drawScreenLine(leaderStart, leaderEnd, color, screenProjection);

    int verticalLeftOffset = 0;
    if (left) {
        const int selectedLine = direction != LeaderDirection::NW
            ? block.longestHighestLineNumber
            : block.longestLowestLineNumber;
        verticalLeftOffset = (height + kDatablockLineSpacing) * (-1 + selectedLine);
    }

    const int textX = int(leaderEnd.x()) + (left ? (-2 - block.maxLineWidth) : 2);
    const int textY = int(leaderEnd.y())
                    - height * 3 / 2
                    - kDatablockLineSpacing
                    - verticalLeftOffset;

    TextStyle style;
    style.size = fontSize;
    style.color = color;
    style.background = Qt::transparent;

    QPointF linePosition(textX, textY);
    for (const QString& line : block.lines) {
        if (!line.isEmpty())
            textRenderer.drawTextTopLeft(QStringView(line), linePosition, style);
        linePosition.ry() += height + kDatablockLineSpacing;
    }
}

} // namespace renderer
