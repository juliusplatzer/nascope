#include "asdex/scope.h"

#include "asdex/datablocks.h"
#include "asdex/tempdata.h"
#include "asdex/targets.h"
#include "asdex/videomaps.h"
#include "utils/math.h"
#include "utils/resources.h"
#include "renderer/builders.h"
#include "renderer/command_buffer.h"
#include "renderer/renderer.h"

#include <QDebug>
#include <QSurfaceFormat>
#include <QtGlobal>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <utility>

namespace asdex {
namespace {

constexpr double kMinHalfRangeFeet = 600.0;
constexpr double kMaxHalfRangeFeet = 30000.0;
constexpr double kWheelStepFeet = 400.0;
constexpr double kCtrlWheelStepFeet = 1600.0;
constexpr double kMaxHoverRangeFeet = 150.0;
constexpr double kRightClickDragTolerancePx = 3.0;

bool isHeavyWake(QStringView wake) {
    if (wake.size() != 1) return false;

    const QChar c = wake.at(0).toUpper();
    return c == QLatin1Char('A') || c == QLatin1Char('B') || c == QLatin1Char('C')
        || c == QLatin1Char('D') || c == QLatin1Char('E');
}

} // namespace

AsdexScopeWidget::AsdexScopeWidget(QString airport, QWidget* parent)
    : QOpenGLWidget(parent),
      airport_(std::move(airport)),
      map_(asdex::VideoMap::load(airport_)),
      targetCache_(airport_),
      atisCache_(airport_),
      runwayClosureCache_(airport_,
                           utils::findProjectRelativeFile(QStringLiteral("asdex/notams.py"))) {
    QSurfaceFormat fmt = format();
    fmt.setSamples(0);
    setFormat(fmt);

    setMinimumSize(640, 480);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    datablockTimeshareTimer_.setInterval(2000);
    connect(&datablockTimeshareTimer_, &QTimer::timeout, this, [this] {
        timesharePrimary_ = !timesharePrimary_;
        update();
    });
    datablockTimeshareTimer_.start();

    coastClockTimer_.setInterval(1000);
    connect(&coastClockTimer_, &QTimer::timeout, this, [this] {
        update();
    });
    coastClockTimer_.start();

    const QString assetsDir = utils::findProjectRelativeDir(QStringLiteral("asdex/assets"));
    QString cursorError;
    if (cursors_.loadFromAssetsDir(assetsDir, &cursorError)) {
        setAsdexCursor(CursorMode::Scope);
    } else {
        qWarning().noquote() << "[renderer] cursor load failed:" << cursorError;
    }

    QString fontError;
    fontLoaded_ =
        asdexFont_.loadFromFile(utils::findProjectRelativeFile(QStringLiteral("asdex/assets/font.bin")),
                                &fontError);
    if (!fontLoaded_) {
        qWarning().noquote() << "[renderer] font load failed:" << fontError;
    }

    QString listError;
    if (!previewArea_.loadDefaultStateFromConfigFile(
            utils::findProjectRelativeFile(
                QStringLiteral("resources/configs/asdex/%1.json").arg(airport_.toUpper())),
            &listError)) {
        qWarning().noquote() << "[renderer] preview area config load failed:" << listError;
    }

    QString runwayClosureError;
    const QString surfacePath = utils::findProjectRelativeFile(
        QStringLiteral("asdex/surface/%1.json").arg(airport_.toUpper()));
    if (!runwayClosureGeometry_.loadSurfaceFile(surfacePath,
                                                map_.anchorLonLat(),
                                                &runwayClosureError)) {
        qWarning().noquote() << "[asdex] runway closure surface load failed:"
                             << runwayClosureError;
    }
    QString closedAreaError;
    if (!runwayClosureCache_.loadSurfaceFile(surfacePath, map_.anchorLonLat(), &closedAreaError)) {
        qWarning().noquote() << "[asdex] closed temp area surface load failed:"
                             << closedAreaError;
    }

    connect(&targetCache_, &::asdex::TargetCache::changed, this, [this] {
        updateTargetsFromCache();
        update();
    });
    connect(&atisCache_, &::asdex::AtisCache::changed, this, [this] {
        const ::asdex::AtisRunwayState& atis = atisCache_.state();
        previewArea_.updateRunwayConfigFromRunways(atis.landingRunways, atis.departureRunways);
        update();
    });
    connect(&runwayClosureCache_, &::asdex::RunwayClosureCache::changed, this, [this] {
        runwayClosureGeometry_.setClosedRunways(runwayClosureCache_.closedRunways());

        QVector<TempArea> areas;
        areas.reserve(runwayClosureCache_.closedTempAreas().size()
                      + runwayClosureCache_.restrictedTempAreas().size()
                      + restrictedTempAreas_.size());

        for (TempArea area : runwayClosureCache_.closedTempAreas()) {
            area.type = TempAreaType::ClosedArea;
            areas.push_back(area);
        }

        for (TempArea area : runwayClosureCache_.restrictedTempAreas()) {
            area.type = TempAreaType::RestrictedArea;
            areas.push_back(area);
        }

        for (TempArea area : restrictedTempAreas_) {
            area.type = TempAreaType::RestrictedArea;
            areas.push_back(area);
        }

        tempAreaGeometry_.setAreas(std::move(areas));
        update();
    });

    fitMapToView();
}

AsdexScopeWidget::~AsdexScopeWidget() {
    if (context()) {
        makeCurrent();
        if (renderer_) {
            for (const std::uint32_t textureId : fontTextureIds_) {
                renderer_->destroyTexture(textureId);
            }
            fontTextureIds_.clear();
            renderer_->deinitialize();
        }
        doneCurrent();
    }
}

void AsdexScopeWidget::initializeGL() {
    renderer_ = renderer::makeOpenGLRenderer();
    QString rendererError;
    if (!renderer_->initialize(&rendererError)) {
        qWarning().noquote() << "[renderer] OpenGL renderer init failed:" << rendererError;
        renderer_.reset();
        return;
    }

    if (fontLoaded_) {
        const int fontSizes[] = {1, 2, 3};
        for (const int fontSize : fontSizes) {
            const renderer::BitmapFontSize* fontSizeData = asdexFont_.fontSize(fontSize);
            if (!fontSizeData) continue;

            const std::uint32_t textureId =
                renderer_->createTextureR8(fontSizeData->atlasWidth,
                                           fontSizeData->atlasHeight,
                                           fontSizeData->atlasR8,
                                           true);
            if (textureId != 0) fontTextureIds_.insert(fontSize, textureId);
        }
        fontTexturesReady_ = !fontTextureIds_.isEmpty();
    }
}

void AsdexScopeWidget::resizeGL(int width, int height) {
    Q_UNUSED(width);
    Q_UNUSED(height);
}

void AsdexScopeWidget::paintGL() {
    const QSize renderSize = framebufferRenderSize();
    renderScene(renderSize);
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
    setFocus(Qt::MouseFocusReason);

    if (datablockEdit_) {
        event->accept();
        return;
    }

    if (isPointOverDcb(event->position())) {
        clearHighlightedTarget();
        setAsdexCursor(CursorMode::Dcb);
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton) {
        panning_ = true;
        rightDragMoved_ = false;
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
    if (datablockEdit_) {
        clearHighlightedTarget();
        setAsdexCursor(CursorMode::Hidden);
        update();
        event->accept();
        return;
    }

    if (panning_) {
        const QSize renderSize = framebufferRenderSize();
        const double ppf = pixelsPerFoot(renderSize);

        if (ppf > 0.0) {
            const QPointF current = framebufferPoint(event->position());
            const QPointF delta = current - panStartMouseFramebuffer_;
            const double tolerance = kRightClickDragTolerancePx * devicePixelRatioF();
            if (delta.x() * delta.x() + delta.y() * delta.y() > tolerance * tolerance)
                rightDragMoved_ = true;
            centerFeet_ = QPointF(panStartCenterFeet_.x() - delta.x() / ppf,
                                  panStartCenterFeet_.y() + delta.y() / ppf);
            update();
        }

        setAsdexCursor(CursorMode::Hidden);
        event->accept();
        return;
    }

    if (isPointOverDcb(event->position())) {
        clearHighlightedTarget();
        setAsdexCursor(CursorMode::Dcb);
        update();
        event->accept();
        return;
    }

    setAsdexCursor(CursorMode::Scope);
    updateHighlightedTarget(event->position());
    update();
    QOpenGLWidget::mouseMoveEvent(event);
}

void AsdexScopeWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton && panning_) {
        const bool rightClick = !rightDragMoved_;
        panning_ = false;
        rightDragMoved_ = false;
        releaseMouse();

        if (rightClick) {
            if (isPointOverDcb(event->position())) {
                clearHighlightedTarget();
                updateHoverCursor(event->position());
            } else {
                updateHighlightedTarget(event->position());
                if (AsdexTarget* target = highlightedTarget()) {
                    startDatablockEdit(*target);
                } else {
                    updateHoverCursor(event->position());
                }
            }
        } else {
            updateHoverCursor(event->position());
        }

        event->accept();
        return;
    }

    if (datablockEdit_) {
        event->accept();
        return;
    }

    if (dcbMouseCaptured_ && event->button() == Qt::LeftButton) {
        dcbMouseCaptured_ = false;
        clearHighlightedTarget();
        updateHoverCursor(event->position());
        update();
        event->accept();
        return;
    }

    if (isPointOverDcb(event->position())) {
        clearHighlightedTarget();
        setAsdexCursor(CursorMode::Dcb);
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier) {
        updateHighlightedTarget(event->position());
        if (AsdexTarget* target = highlightedTarget()) {
            toggleDataBlockForTarget(*target);
            update();
            event->accept();
            return;
        }
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

    if (datablockEdit_) {
        if (wheelY > 0)
            datablockEdit_->moveUp();
        else
            datablockEdit_->moveDown();
        update();
        event->accept();
        return;
    }

    if (isPointOverDcb(event->position())) {
        setAsdexCursor(CursorMode::Dcb);
        event->accept();
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

void AsdexScopeWidget::keyPressEvent(QKeyEvent* event) {
    if (datablockEdit_ && handleDatablockEditKey(event)) return;

    if (event->key() == Qt::Key_F6 && event->modifiers() == Qt::NoModifier) {
        showDataBlocks_ = !showDataBlocks_;
        datablockVisibility_.clear();
        update();
        event->accept();
        return;
    }

    QOpenGLWidget::keyPressEvent(event);
}

void AsdexScopeWidget::leaveEvent(QEvent* event) {
    if (!datablockEdit_ && !panning_) {
        dcbMouseCaptured_ = false;
        clearHighlightedTarget();
        setAsdexCursor(CursorMode::Scope);
        update();
    }

    QOpenGLWidget::leaveEvent(event);
}

bool AsdexScopeWidget::handleDatablockEditKey(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Escape:
            cancelCommand();
            update();
            event->accept();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (datablockEdit_->enter())
                submitDatablockEdit();
            update();
            event->accept();
            return true;
        case Qt::Key_Left:
            datablockEdit_->moveLeft();
            update();
            event->accept();
            return true;
        case Qt::Key_Right:
            datablockEdit_->moveRight();
            update();
            event->accept();
            return true;
        case Qt::Key_Up:
            datablockEdit_->moveUp();
            update();
            event->accept();
            return true;
        case Qt::Key_Down:
            datablockEdit_->moveDown();
            update();
            event->accept();
            return true;
        case Qt::Key_Backspace:
            datablockEdit_->backspace();
            update();
            event->accept();
            return true;
        case Qt::Key_Delete:
            datablockEdit_->deleteForward();
            update();
            event->accept();
            return true;
        default:
            break;
    }

    if (event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::MetaModifier)) {
        event->accept();
        return true;
    }

    const QString text = event->text();
    if (!text.isEmpty()) {
        for (const QChar c : text) datablockEdit_->insert(c);
        update();
        event->accept();
        return true;
    }

    event->accept();
    return true;
}

void AsdexScopeWidget::updateTargetsFromCache() {
    targets_.clear();
    if (!map_.isValid()) return;

    const QTransform toFeet = utils::lonLatToFeet(map_.anchorLonLat());
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
        target.scratchpad1 = cached.scratchpad1;
        target.scratchpad2 = cached.scratchpad2;
        target.positionFeet = toFeet.map(QPointF(*cached.lon, *cached.lat));

        const double heading = cached.heading.value_or(0.0);
        target.headingDegrees = heading;
        target.groundTrackDegrees = heading;
        target.groundSpeedKnots = (cached.heading && cached.speed) ? *cached.speed : 0.0;

        target.correlated = cached.tgtType == QLatin1String("aircraft") && !cached.callsign.isEmpty();
        target.heavy = target.correlated && isHeavyWake(cached.wake);
        target.duplicateBeaconCode = false;
        target.coasting = false;
        target.highlighted = target.id == highlightedTargetId_;

        if (const auto pending = pendingDatablockEdits_.constFind(target.id);
            pending != pendingDatablockEdits_.constEnd()) {
            applyEditedFields(target, pending.value());
        }

        target.history.reserve(cached.positionHistoryLonLat.size());
        for (const QPointF& lonLat : cached.positionHistoryLonLat) {
            target.history.push_back(asdex::TargetHistoryPoint{toFeet.map(lonLat)});
        }

        targets_.push_back(std::move(target));
    }

    if (!highlightedTargetId_.isEmpty() && !highlightedTarget())
        highlightedTargetId_.clear();
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

    highlightedTargetId_.clear();
    if (bestIndex >= 0) {
        targets_[bestIndex].highlighted = true;
        highlightedTargetId_ = targets_[bestIndex].id;
    }
}

void AsdexScopeWidget::clearHighlightedTarget() {
    highlightedTargetId_.clear();
    for (AsdexTarget& target : targets_) target.highlighted = false;
}

AsdexTarget* AsdexScopeWidget::highlightedTarget() {
    if (highlightedTargetId_.isEmpty()) return nullptr;

    return targetById(highlightedTargetId_);
}

AsdexTarget* AsdexScopeWidget::targetById(const QString& targetId) {
    if (targetId.isEmpty()) return nullptr;

    for (AsdexTarget& target : targets_) {
        if (target.id == targetId) return &target;
    }

    return nullptr;
}

void AsdexScopeWidget::startDatablockEdit(const AsdexTarget& target) {
    if (commandType_ != CommandType::None) return;
    if (!target.correlated || target.coasting) {
        setAsdexCursor(CursorMode::Scope);
        return;
    }

    commandType_ = CommandType::EditDatablockFields;
    datablockEdit_ = DatablockEditCommand::fromTarget(target);
    editingTrackId_ = target.id;
    clearHighlightedTarget();
    previewArea_.setSystemResponse({});
    setAsdexCursor(CursorMode::Hidden);
    update();
}

void AsdexScopeWidget::cancelCommand() {
    commandType_ = CommandType::None;
    datablockEdit_.reset();
    editingTrackId_.clear();
    previewArea_.setSystemResponse({});
    setAsdexCursor(CursorMode::Scope);
    clearHighlightedTarget();
    update();
}

void AsdexScopeWidget::submitDatablockEdit() {
    if (!datablockEdit_) return;

    AsdexTarget* target = targetById(editingTrackId_);
    if (!target) {
        cancelCommand();
        return;
    }

    QString error;
    if (!datablockEdit_->validateForTarget(*target, &error)) {
        previewArea_.setSystemResponse(error.isEmpty() ? QStringLiteral("INVALID ENTRY") : error);
        return;
    }

    const EditedDbFields fields = datablockEdit_->values();
    targetCache_.sendDatablockEdit(airport_,
                                   editingTrackId_,
                                   fields.callsign,
                                   fields.beaconCode,
                                   fields.category,
                                   fields.aircraftType,
                                   fields.fix,
                                   fields.scratchpad1,
                                   fields.scratchpad2);

    pendingDatablockEdits_.insert(editingTrackId_, fields);
    applyEditedFields(*target, fields);
    cancelCommand();
}

void AsdexScopeWidget::applyEditedFields(AsdexTarget& target,
                                         const EditedDbFields& fields) const {
    target.callsign = fields.callsign;
    target.beaconCode = fields.beaconCode;
    target.category = fields.category;
    target.aircraftType = fields.aircraftType;
    target.fix = fields.fix;
    target.scratchpad1 = fields.scratchpad1;
    target.scratchpad2 = fields.scratchpad2;
}

bool AsdexScopeWidget::defaultDataBlockVisibleForTarget(const AsdexTarget& target) const {
    Q_UNUSED(target);
    return showDataBlocks_;
}

bool AsdexScopeWidget::isDataBlockVisible(const AsdexTarget& target) const {
    const DataBlockVisibility visibility =
        datablockVisibility_.value(target.id, DataBlockVisibility::Inherit);

    switch (visibility) {
        case DataBlockVisibility::ForceOn:
            return true;
        case DataBlockVisibility::ForceOff:
            return false;
        case DataBlockVisibility::Inherit:
            return defaultDataBlockVisibleForTarget(target);
    }

    return defaultDataBlockVisibleForTarget(target);
}

void AsdexScopeWidget::toggleDataBlockForTarget(const AsdexTarget& target) {
    const DataBlockVisibility current =
        datablockVisibility_.value(target.id, DataBlockVisibility::Inherit);

    if (current == DataBlockVisibility::Inherit) {
        datablockVisibility_[target.id] = defaultDataBlockVisibleForTarget(target)
            ? DataBlockVisibility::ForceOff
            : DataBlockVisibility::ForceOn;
        return;
    }

    datablockVisibility_[target.id] = current == DataBlockVisibility::ForceOn
        ? DataBlockVisibility::ForceOff
        : DataBlockVisibility::ForceOn;
}

void AsdexScopeWidget::renderScene(const QSize& renderSize) {
    if (!renderer_ || renderSize.isEmpty()) return;

    renderer::CommandBuffer* commandBuffer = renderer::getCommandBuffer();
    commandBuffer->resetState();
    commandBuffer->viewport(0, 0, renderSize.width(), renderSize.height());
    commandBuffer->clear(renderer::RGBA::fromQColor(backgroundColor(mode_)));

    const QMatrix4x4 worldProjection = viewProjection(renderSize);
    commandBuffer->loadProjectionMatrix(worldProjection);
    drawVideoMap(map_, commandBuffer, mode_);
    drawRunwayClosures(runwayClosureGeometry_, commandBuffer, worldProjection);
    drawTempAreas(tempAreaGeometry_,
                  commandBuffer,
                  worldProjection,
                  [this, &renderSize](QPointF worldFeet) {
                      return worldToFramebufferTopLeft(worldFeet, renderSize);
                  });
    drawTargets(targets_, commandBuffer, worldProjection, mode_, targetVectorSeconds_);

    const QMatrix4x4 projection = screenProjection();
    commandBuffer->loadProjectionMatrix(projection);

    const DcbState dcbState = makeDcbState();
    const DcbLayout dcbLayout = dcb_.layout(size(), asdexFont_, dcbState);
    dcb_.drawQuads(commandBuffer, dcbLayout);

    const std::uint32_t dcbFontTexture = fontTextureId(dcbLayout.renderFontSize);
    if (fontTexturesReady_ && dcbFontTexture != 0) {
        renderer::TextBuilder* dcbTextBuilder = renderer::getTextBuilder();
        dcbTextBuilder->setFont(&asdexFont_);
        dcb_.drawText(*dcbTextBuilder, asdexFont_, dcbFontTexture, dcbLayout);
        dcbTextBuilder->generateCommands(commandBuffer);
        renderer::returnTextBuilder(dcbTextBuilder);
    }

    const std::uint32_t listFontTexture = fontTextureId(2);
    if (fontTexturesReady_ && listFontTexture != 0) {
        DataBlockSettings datablockSettings;
        datablockSettings.fontSize = 2;
        datablockSettings.brightness = 95;
        datablockSettings.leaderLength = 2;
        datablockSettings.leaderDirection = LeaderDirection::NE;
        datablockSettings.timesharePrimary = timesharePrimary_;
        datablockSettings.alertInProgress = false;

        drawDatablocks(targets_,
                       commandBuffer,
                       projection,
                       [this, &renderSize](QPointF worldFeet) {
                           return worldToScreenLogical(worldFeet, renderSize);
                       },
                       [this](const AsdexTarget& target) {
                           return isDataBlockVisible(target);
                       },
                       asdexFont_,
                       listFontTexture,
                       datablockSettings);

        commandBuffer->loadProjectionMatrix(projection);

        renderer::TextBuilder* textBuilder = renderer::getTextBuilder();
        textBuilder->setFont(&asdexFont_);

        coastList_.render(*textBuilder, asdexFont_, listFontTexture, size());

        const QStringList commandLines =
            datablockEdit_ ? datablockEdit_->displayLines() : QStringList();
        previewArea_.render(*textBuilder, asdexFont_, listFontTexture, commandLines);
        textBuilder->generateCommands(commandBuffer);
        renderer::returnTextBuilder(textBuilder);

        if (datablockEdit_) {
            commandBuffer->setRgba(renderer::RGBA::fromQColor(
                applyBrightness(QColor(0, 248, 0), 95, 20)));
            commandBuffer->lineWidth(1.0f);

            renderer::LinesBuilder* lineBuilder = renderer::getLinesBuilder();
            previewArea_.renderCommandCursor(*lineBuilder,
                                             asdexFont_,
                                             *datablockEdit_,
                                             projection);
            lineBuilder->generateCommands(commandBuffer);
            renderer::returnLinesBuilder(lineBuilder);
        }
    }

    renderer_->renderCommandBuffer(commandBuffer);
    renderer::returnCommandBuffer(commandBuffer);
}

DcbState AsdexScopeWidget::makeDcbState() const {
    DcbState state;
    state.range = int(std::round(halfRangeFeet_ / 100.0));
    state.rotation = 0;
    state.vectorLength = targetVectorSeconds_;
    state.leaderLength = 2;
    state.nightMode = mode_ == Mode::Night;
    state.showVectorLine = true;
    state.showDataBlocks = showDataBlocks_;
    state.showDcb = true;
    state.networkConnected = true;
    return state;
}

QSize AsdexScopeWidget::framebufferRenderSize() const {
    const qreal ratio = devicePixelRatioF();
    return QSize(qRound(width() * ratio), qRound(height() * ratio));
}

std::uint32_t AsdexScopeWidget::fontTextureId(int fontSize) const {
    return fontTextureIds_.value(fontSize, 0);
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

QPointF AsdexScopeWidget::worldToFramebufferTopLeft(const QPointF& worldFeet,
                                                    const QSize& renderSize) const {
    const double ppf = pixelsPerFoot(renderSize);
    return QPointF(renderSize.width() * 0.5 + (worldFeet.x() - centerFeet_.x()) * ppf,
                   renderSize.height() * 0.5 - (worldFeet.y() - centerFeet_.y()) * ppf);
}

QPointF AsdexScopeWidget::framebufferPoint(const QPointF& logicalPoint) const {
    const qreal ratio = devicePixelRatioF();
    return QPointF(logicalPoint.x() * ratio, logicalPoint.y() * ratio);
}

double AsdexScopeWidget::pixelsPerFoot(const QSize& renderSize) const {
    if (renderSize.isEmpty() || halfRangeFeet_ <= 0.0) return 1.0;

    const double availW = renderSize.width() * (1.0 - 2.0 * utils::kViewportMargin);
    const double availH = renderSize.height() * (1.0 - 2.0 * utils::kViewportMargin);
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

bool AsdexScopeWidget::isPointOverDcb(const QPointF& logicalPoint) const {
    if (!fontLoaded_) return false;

    return dcb_.contains(logicalPoint, size(), asdexFont_, makeDcbState());
}

void AsdexScopeWidget::updateHoverCursor(const QPointF& logicalPoint) {
    if (datablockEdit_ || panning_) {
        setAsdexCursor(CursorMode::Hidden);
        return;
    }

    if (dcbMouseCaptured_) {
        setAsdexCursor(CursorMode::Captured);
        return;
    }

    if (isPointOverDcb(logicalPoint)) {
        setAsdexCursor(CursorMode::Dcb);
        return;
    }

    setAsdexCursor(CursorMode::Scope);
}

void AsdexScopeWidget::setAsdexCursor(asdex::CursorType type) {
    if (cursors_.has(type)) setCursor(cursors_.cursor(type));
}

void AsdexScopeWidget::setAsdexCursor(CursorMode mode) {
    if (currentCursorMode_ == mode) return;

    currentCursorMode_ = mode;

    switch (mode) {
        case CursorMode::Scope:
            if (cursors_.has(asdex::CursorType::Scope))
                setCursor(cursors_.cursor(asdex::CursorType::Scope));
            else
                unsetCursor();
            break;
        case CursorMode::Dcb:
            if (cursors_.has(asdex::CursorType::Dcb))
                setCursor(cursors_.cursor(asdex::CursorType::Dcb));
            else if (cursors_.has(asdex::CursorType::Scope))
                setCursor(cursors_.cursor(asdex::CursorType::Scope));
            else
                unsetCursor();
            break;
        case CursorMode::Captured:
            if (cursors_.has(asdex::CursorType::Captured))
                setCursor(cursors_.cursor(asdex::CursorType::Captured));
            else if (cursors_.has(asdex::CursorType::Dcb))
                setCursor(cursors_.cursor(asdex::CursorType::Dcb));
            else if (cursors_.has(asdex::CursorType::Scope))
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

    const double availW = renderSize.width() * (1.0 - 2.0 * utils::kViewportMargin);
    const double availH = renderSize.height() * (1.0 - 2.0 * utils::kViewportMargin);
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

} // namespace asdex
