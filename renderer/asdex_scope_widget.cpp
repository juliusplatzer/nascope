#include "renderer/asdex_scope_widget.h"

#include "renderer/asdex_math.h"
#include "renderer/asdex_resources.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QDebug>
#include <QSurfaceFormat>
#include <QVector4D>
#include <QtGlobal>

#include <algorithm>
#include <cstdint>

namespace renderer {
namespace {

constexpr double kMinHalfRangeFeet = 600.0;
constexpr double kMaxHalfRangeFeet = 30000.0;
constexpr double kWheelStepFeet = 400.0;
constexpr double kCtrlWheelStepFeet = 1600.0;
constexpr double kMaxHoverRangeFeet = 150.0;

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

bool isHeavyWake(QStringView wake) {
    if (wake.size() != 1) return false;

    const QChar c = wake.at(0).toUpper();
    return c == QLatin1Char('A') || c == QLatin1Char('B') || c == QLatin1Char('C')
        || c == QLatin1Char('D') || c == QLatin1Char('E') || c == QLatin1Char('H')
        || c == QLatin1Char('J');
}

} // namespace

AsdexScopeWidget::AsdexScopeWidget(QString airport, QWidget* parent)
    : QOpenGLWidget(parent),
      airport_(std::move(airport)),
      map_(asdex::VideoMap::load(airport_)),
      targetCache_(airport_, this) {
    QSurfaceFormat fmt = format();
    fmt.setSamples(0);
    setFormat(fmt);

    setMinimumSize(640, 480);
    setMouseTracking(true);

    const QString assetsDir = asdex::findProjectRelativeDir(QStringLiteral("asdex/assets"));
    QString cursorError;
    if (cursors_.loadFromAssetsDir(assetsDir, &cursorError)) {
        setAsdexCursor(CursorMode::Scope);
    } else {
        qWarning().noquote() << "[renderer] cursor load failed:" << cursorError;
    }

    QString fontError;
    fontLoaded_ =
        asdexFont_.loadFromFile(asdex::findProjectRelativeFile(QStringLiteral("asdex/assets/font.bin")),
                                &fontError);
    if (!fontLoaded_) {
        qWarning().noquote() << "[renderer] font load failed:" << fontError;
    }

    QString listError;
    if (!previewArea_.loadDefaultStateFromConfigFile(
            asdex::findProjectRelativeFile(
                QStringLiteral("resources/configs/asdex/%1.json").arg(airport_.toUpper())),
            &listError)) {
        qWarning().noquote() << "[renderer] preview area config load failed:" << listError;
    }

    connect(&targetCache_, &::asdex::TargetCache::changed, this, [this] {
        updateTargetsFromCache();
        update();
    });

    fitMapToView();
}

AsdexScopeWidget::~AsdexScopeWidget() {
    if (context()) {
        makeCurrent();
        targetRenderer_.deinitialize();
        datablockRenderer_.deinitialize();
        textRenderer_.deinitialize();
        doneCurrent();
    }
}

void AsdexScopeWidget::initializeGL() {
    QOpenGLFunctions* functions = context()->functions();
    functions->initializeOpenGLFunctions();
    functions->glDisable(GL_DEPTH_TEST);
    functions->glDisable(GL_STENCIL_TEST);
    functions->glDisable(GL_MULTISAMPLE);
    functions->glDisable(GL_LINE_SMOOTH);
    functions->glDisable(GL_POLYGON_SMOOTH);
    functions->glDisable(GL_DITHER);
    functions->glDisable(GL_CULL_FACE);

    functions->glEnable(GL_BLEND);
    functions->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    initializeShaders();
    uploadMapGeometry();
    targetRenderer_.initialize();
    datablockRenderer_.initialize();

    if (fontLoaded_) {
        QString fontError;
        textRendererReady_ = textRenderer_.initialize(asdexFont_, &fontError);
        if (!textRendererReady_)
            qWarning().noquote() << "[renderer] font renderer init failed:" << fontError;
    }
}

void AsdexScopeWidget::resizeGL(int width, int height) {
    Q_UNUSED(width);
    Q_UNUSED(height);
}

void AsdexScopeWidget::paintGL() {
    QOpenGLFunctions* functions = context()->functions();
    const QSize renderSize = framebufferRenderSize();
    functions->glViewport(0, 0, renderSize.width(), renderSize.height());

    const QColor background = asdex::backgroundColor(mode_);
    functions->glClearColor(background.redF(), background.greenF(), background.blueF(), 1.0f);
    functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderVideoMap(renderSize);
    renderTargets(renderSize);
    renderScreenOverlays(renderSize);
}

void AsdexScopeWidget::fitMapToView() {
    if (!map_.isValid()) return;

    const QRectF bounds = map_.boundsFeet();
    centerFeet_ = bounds.center();
    halfRangeFeet_ = 0.5 * std::max(bounds.width(), bounds.height());
    halfRangeFeet_ = std::clamp(halfRangeFeet_, kMinHalfRangeFeet, kMaxHalfRangeFeet);
    if (halfRangeFeet_ <= 0.0) halfRangeFeet_ = 1.0;
}

void AsdexScopeWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        panning_ = true;
        panStartMouseFramebuffer_ = framebufferPoint(event->position());
        panStartCenterFeet_ = centerFeet_;
        setAsdexCursor(CursorMode::Hidden);
        grabMouse();
        event->accept();
        return;
    }

    QOpenGLWidget::mousePressEvent(event);
}

void AsdexScopeWidget::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const QSize renderSize = framebufferRenderSize();
        const double ppf = pixelsPerFoot(renderSize);

        if (ppf > 0.0) {
            const QPointF current = framebufferPoint(event->position());
            const QPointF delta = current - panStartMouseFramebuffer_;
            centerFeet_ = QPointF(panStartCenterFeet_.x() - delta.x() / ppf,
                                  panStartCenterFeet_.y() + delta.y() / ppf);
            update();
        }

        event->accept();
        return;
    }

    updateHighlightedTarget(event->position());
    update();
    QOpenGLWidget::mouseMoveEvent(event);
}

void AsdexScopeWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton && panning_) {
        panning_ = false;
        releaseMouse();
        setAsdexCursor(CursorMode::Scope);
        event->accept();
        return;
    }

    QOpenGLWidget::mouseReleaseEvent(event);
}

void AsdexScopeWidget::wheelEvent(QWheelEvent* event) {
    const QPoint angleDelta = event->angleDelta();
    const QPoint pixelDelta = event->pixelDelta();

    int wheelY = angleDelta.y();
    if (wheelY == 0) wheelY = pixelDelta.y();

    if (wheelY == 0) {
        QOpenGLWidget::wheelEvent(event);
        return;
    }

    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    const bool alt = event->modifiers().testFlag(Qt::AltModifier);
    const double step = ctrl ? kCtrlWheelStepFeet : kWheelStepFeet;
    const double deltaFeet = (wheelY > 0) ? -step : step;

    if (alt)
        zoomToCursorByFeet(deltaFeet, event->position());
    else
        zoomByFeet(deltaFeet);

    event->accept();
}

void AsdexScopeWidget::initializeShaders() {
    if (!shader_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
        || !shader_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)
        || !shader_.link()) {
        qWarning().noquote() << "[renderer] shader setup failed:" << shader_.log();
        shaderReady_ = false;
        return;
    }

    shaderReady_ = true;
}

void AsdexScopeWidget::uploadMapGeometry() {
    if (!shaderReady_ || !map_.isValid() || geometryUploaded_) return;

    QVector<asdex::VideoMap::Vertex> vertices;
    QVector<std::uint32_t> indices;
    drawBatches_.clear();

    for (const asdex::VideoMap::Mesh& mesh : map_.meshes()) {
        if (mesh.indices.isEmpty()) continue;

        const qsizetype batchStart = indices.size();
        const std::uint32_t vertexBase = static_cast<std::uint32_t>(vertices.size());
        vertices += mesh.vertices;
        for (const std::uint32_t index : mesh.indices) indices.append(vertexBase + index);
        const qsizetype batchCount = indices.size() - batchStart;

        if (batchCount > 0) {
            drawBatches_.append(DrawBatch{mesh.kind,
                                          static_cast<int>(batchCount),
                                          static_cast<std::size_t>(batchStart)
                                              * sizeof(std::uint32_t)});
        }
    }

    if (vertices.isEmpty() || indices.isEmpty()) return;

    vertexArray_.create();
    QOpenGLVertexArrayObject::Binder vaoBinder(&vertexArray_);

    vertexBuffer_.create();
    vertexBuffer_.bind();
    vertexBuffer_.allocate(vertices.constData(),
                           static_cast<int>(vertices.size() * sizeof(asdex::VideoMap::Vertex)));

    indexBuffer_.create();
    indexBuffer_.bind();
    indexBuffer_.allocate(indices.constData(),
                          static_cast<int>(indices.size() * sizeof(std::uint32_t)));

    shader_.bind();
    shader_.enableAttributeArray(0);
    shader_.setAttributeBuffer(0,
                               GL_FLOAT,
                               0,
                               2,
                               sizeof(asdex::VideoMap::Vertex));
    shader_.release();

    geometryUploaded_ = true;
}

void AsdexScopeWidget::updateTargetsFromCache() {
    targets_.clear();
    if (!map_.isValid()) return;

    const QTransform toFeet = asdex::lonLatToFeet(map_.anchorLonLat());
    const QHash<QString, ::asdex::TargetCache::Target>& cachedTargets = targetCache_.targets();
    targets_.reserve(cachedTargets.size());

    for (auto it = cachedTargets.constBegin(); it != cachedTargets.constEnd(); ++it) {
        const ::asdex::TargetCache::Target& cached = it.value();
        if (!cached.lat || !cached.lon) continue;

        asdex::AsdexTarget target;
        target.id = it.key();
        target.callsign = cached.callsign;
        target.aircraftType = cached.acType;
        target.category = cached.wake;
        target.beaconCode = cached.squawk;
        target.fix = cached.exitFix;
        target.altitudeTrue = cached.altitude;
        target.scratchpad1.clear();
        target.scratchpad2.clear();
        target.positionFeet = toFeet.map(QPointF(*cached.lon, *cached.lat));

        const double heading = cached.heading.value_or(0.0);
        target.headingDegrees = heading;
        target.groundTrackDegrees = heading;
        target.groundSpeedKnots = (cached.heading && cached.speed) ? *cached.speed : 0.0;

        target.correlated = cached.tgtType == QLatin1String("aircraft") && !cached.callsign.isEmpty();
        target.heavy = target.correlated && isHeavyWake(cached.wake);
        target.duplicateBeaconCode = false;
        target.coasting = false;

        target.history.reserve(cached.positionHistoryLonLat.size());
        for (const QPointF& lonLat : cached.positionHistoryLonLat) {
            target.history.push_back(asdex::TargetHistoryPoint{toFeet.map(lonLat)});
        }

        targets_.push_back(std::move(target));
    }
}

void AsdexScopeWidget::updateHighlightedTarget(const QPointF& mouseLogical) {
    const QSize renderSize = framebufferRenderSize();
    const QPointF mouseWorld = screenToWorldFeet(mouseLogical, renderSize);
    const double maxDistance2 = kMaxHoverRangeFeet * kMaxHoverRangeFeet;

    int bestIndex = -1;
    double bestDistance2 = maxDistance2;

    for (qsizetype i = 0; i < targets_.size(); ++i) {
        targets_[i].highlighted = false;

        const QPointF delta = targets_[i].positionFeet - mouseWorld;
        const double distance2 = delta.x() * delta.x() + delta.y() * delta.y();
        if (distance2 <= bestDistance2) {
            bestDistance2 = distance2;
            bestIndex = int(i);
        }
    }

    if (bestIndex >= 0) targets_[bestIndex].highlighted = true;
}

void AsdexScopeWidget::renderVideoMap(const QSize& renderSize) {
    if (!shaderReady_ || !geometryUploaded_ || renderSize.isEmpty()) return;

    shader_.bind();
    QMatrix4x4 model;
    model.setToIdentity();
    shader_.setUniformValue("u_projection", viewProjection(renderSize));
    shader_.setUniformValue("u_model", model);

    QOpenGLVertexArrayObject::Binder vaoBinder(&vertexArray_);
    indexBuffer_.bind();

    QOpenGLFunctions* functions = context()->functions();
    for (const DrawBatch& batch : drawBatches_) {
        const QColor color = colorFor(batch.kind);
        shader_.setUniformValue("u_color",
                                QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF()));
        functions->glDrawElements(GL_TRIANGLES,
                                  batch.indexCount,
                                  GL_UNSIGNED_INT,
                                  reinterpret_cast<const void*>(batch.indexOffsetBytes));
    }

    shader_.release();
}

void AsdexScopeWidget::renderTargets(const QSize& renderSize) {
    if (renderSize.isEmpty()) return;
    targetRenderer_.render(targets_, viewProjection(renderSize), mode_);
}

void AsdexScopeWidget::renderScreenOverlays(const QSize& renderSize) {
    if (!textRendererReady_) return;
    if (renderSize.isEmpty()) return;

    const QMatrix4x4 projection = screenProjection();
    textRenderer_.beginFrame(projection);

    DataBlockSettings datablockSettings;
    datablockSettings.fontSize = 2;
    datablockSettings.brightness = 95;
    datablockSettings.leaderLength = 2;
    datablockSettings.leaderDirection = LeaderDirection::NE;

    datablockRenderer_.render(targets_,
                              projection,
                              [this, &renderSize](QPointF worldFeet) {
                                  return worldToScreenLogical(worldFeet, renderSize);
                              },
                              textRenderer_,
                              datablockSettings);

    previewArea_.render(textRenderer_);
    textRenderer_.flush();
}

QSize AsdexScopeWidget::framebufferRenderSize() const {
    const qreal ratio = devicePixelRatioF();
    return QSize(qRound(width() * ratio), qRound(height() * ratio));
}

QMatrix4x4 AsdexScopeWidget::screenProjection() const {
    QMatrix4x4 projection;
    projection.setToIdentity();
    projection.ortho(0.0f,
                     static_cast<float>(width()),
                     static_cast<float>(height()),
                     0.0f,
                     -1.0f,
                     1.0f);
    return projection;
}

QPointF AsdexScopeWidget::worldToScreenLogical(const QPointF& worldFeet,
                                               const QSize& renderSize) const {
    const double ppf = pixelsPerFoot(renderSize);
    const double framebufferX = renderSize.width() * 0.5
                              + (worldFeet.x() - centerFeet_.x()) * ppf;
    const double framebufferY = renderSize.height() * 0.5
                              - (worldFeet.y() - centerFeet_.y()) * ppf;

    const qreal dpr = devicePixelRatioF();
    return QPointF(framebufferX / dpr, framebufferY / dpr);
}

QPointF AsdexScopeWidget::framebufferPoint(const QPointF& logicalPoint) const {
    const qreal ratio = devicePixelRatioF();
    return QPointF(logicalPoint.x() * ratio, logicalPoint.y() * ratio);
}

double AsdexScopeWidget::pixelsPerFoot(const QSize& renderSize) const {
    if (renderSize.isEmpty() || halfRangeFeet_ <= 0.0) return 1.0;

    const double availW = renderSize.width() * (1.0 - 2.0 * asdex::kViewportMargin);
    const double availH = renderSize.height() * (1.0 - 2.0 * asdex::kViewportMargin);
    const double radiusPx = 0.5 * std::min(availW, availH);
    return radiusPx / halfRangeFeet_;
}

QPointF AsdexScopeWidget::screenToWorldFeet(const QPointF& logicalPoint,
                                            const QSize& renderSize) const {
    const QPointF point = framebufferPoint(logicalPoint);
    const double ppf = pixelsPerFoot(renderSize);

    if (ppf <= 0.0) return centerFeet_;

    return QPointF(centerFeet_.x() + (point.x() - renderSize.width() * 0.5) / ppf,
                   centerFeet_.y() + (renderSize.height() * 0.5 - point.y()) / ppf);
}

void AsdexScopeWidget::zoomByFeet(double deltaFeet) {
    const double nextRange = std::clamp(halfRangeFeet_ + deltaFeet,
                                        kMinHalfRangeFeet,
                                        kMaxHalfRangeFeet);
    if (qFuzzyCompare(halfRangeFeet_, nextRange)) return;

    halfRangeFeet_ = nextRange;
    update();
}

void AsdexScopeWidget::zoomToCursorByFeet(double deltaFeet, const QPointF& cursorLogicalPoint) {
    const QSize renderSize = framebufferRenderSize();
    const QPointF worldBefore = screenToWorldFeet(cursorLogicalPoint, renderSize);
    const double oldRange = halfRangeFeet_;
    const double newRange = std::clamp(oldRange + deltaFeet,
                                       kMinHalfRangeFeet,
                                       kMaxHalfRangeFeet);

    if (qFuzzyCompare(oldRange, newRange)) return;

    halfRangeFeet_ = newRange;
    const QPointF worldAfter = screenToWorldFeet(cursorLogicalPoint, renderSize);
    centerFeet_ += worldBefore - worldAfter;
    update();
}

void AsdexScopeWidget::setAsdexCursor(asdex::CursorType type) {
    if (cursors_.has(type)) setCursor(cursors_.cursor(type));
}

void AsdexScopeWidget::setAsdexCursor(CursorMode mode) {
    switch (mode) {
        case CursorMode::Scope:
            if (cursors_.has(asdex::CursorType::Scope))
                setCursor(cursors_.cursor(asdex::CursorType::Scope));
            else
                unsetCursor();
            break;
        case CursorMode::Hidden:
            setCursor(Qt::BlankCursor);
            break;
    }
}

QMatrix4x4 AsdexScopeWidget::viewProjection(const QSize& renderSize) const {
    QMatrix4x4 matrix;
    matrix.setToIdentity();
    if (renderSize.isEmpty() || halfRangeFeet_ <= 0.0) return matrix;

    const double availW = renderSize.width() * (1.0 - 2.0 * asdex::kViewportMargin);
    const double availH = renderSize.height() * (1.0 - 2.0 * asdex::kViewportMargin);
    const double radiusPx = 0.5 * std::min(availW, availH);
    const double pxPerFoot = radiusPx / halfRangeFeet_;
    const double sx = 2.0 * pxPerFoot / renderSize.width();
    const double sy = 2.0 * pxPerFoot / renderSize.height();

    matrix(0, 0) = static_cast<float>(sx);
    matrix(0, 3) = static_cast<float>(-centerFeet_.x() * sx);
    matrix(1, 1) = static_cast<float>(sy);
    matrix(1, 3) = static_cast<float>(-centerFeet_.y() * sy);
    return matrix;
}

QColor AsdexScopeWidget::colorFor(asdex::VideoMap::Kind kind) const {
    const bool day = mode_ == asdex::Mode::Day;
    QColor base;

    switch (kind) {
        case asdex::VideoMap::Kind::Runway:
            base = QColor(0, 0, 0);
            break;
        case asdex::VideoMap::Kind::Taxiway:
            base = day ? QColor(47, 47, 47) : QColor(17, 39, 80);
            break;
        case asdex::VideoMap::Kind::Apron:
            base = day ? QColor(73, 73, 73) : QColor(18, 55, 97);
            break;
        case asdex::VideoMap::Kind::Structure:
            base = day ? QColor(100, 100, 100) : QColor(34, 63, 103);
            break;
    }

    return asdex::applyBrightness(base);
}

} // namespace renderer
