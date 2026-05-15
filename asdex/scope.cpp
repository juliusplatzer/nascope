#include "asdex/scope.h"

#include "asdex/datablocks.h"
#include "asdex/tempdata.h"
#include "asdex/targets.h"
#include "asdex/videomaps.h"
#include "utils/math.h"
#include "utils/resources.h"
#include "renderer/builders.h"
#include "renderer/command_buffer.h"
#include "renderer/render_layers.h"
#include "renderer/renderer.h"

#include <QCursor>
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
constexpr double kPi = 3.14159265358979323846;

namespace z {

constexpr int VideoMap = -900;
constexpr int RunwayClosures = -800;

constexpr int RestrictedArea = -700;
constexpr int ClosedArea = -690;

constexpr int Targets = -500;
constexpr int Datablocks = -480;

constexpr int PreviewArea = -200;
constexpr int PreviewCursor = -190;

constexpr int DcbBackground = -100;
constexpr int DcbButtons = -99;
constexpr int DcbText = -98;

}  // namespace z

int normalizedDegrees(int degrees) {
    return ((degrees % 360) + 360) % 360;
}

double radiansFromDegrees(int degrees) {
    return double(normalizedDegrees(degrees)) * kPi / 180.0;
}

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
    renderScene(sceneDisplaySize(), sceneFramebufferSize());
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

    if (isMapRepositionCommandActive()) {
        if (event->button() == Qt::LeftButton) {
            suppressNextMapRepositionRelease_ = true;
            commitMapRepositionCommand();
        }

        event->accept();
        return;
    }

    if (commandActive()) {
        event->accept();
        return;
    }

    const std::optional<QPointF> displayPoint = widgetToDisplayPoint(event->position());
    if (!displayPoint) {
        clearHighlightedTarget();
        clearDcbHover();
        setAsdexCursor(CursorMode::Scope);
        update();
        event->accept();
        return;
    }

    if (isPointOverDcb(*displayPoint)) {
        clearHighlightedTarget();
        updateDcbHover(*displayPoint);
        if (event->button() == Qt::LeftButton) {
            dcbMouseCaptured_ = true;
            setAsdexCursor(CursorMode::Captured);
        } else {
            setAsdexCursor(CursorMode::Dcb);
        }
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton) {
        clearDcbHover();
        panning_ = true;
        rightDragMoved_ = false;
        panStartMouseFramebuffer_ =
            displayToFramebufferPoint(*displayPoint, sceneDisplaySize(), sceneFramebufferSize());
        panStartCenterFeet_ = centerFeet_;
        setAsdexCursor(CursorMode::Hidden);
        grabMouse();
        event->accept();
        return;
    }

    QOpenGLWidget::mousePressEvent(event);
}

void AsdexScopeWidget::mouseMoveEvent(QMouseEvent* event) {
    const std::optional<QPointF> displayPoint = widgetToDisplayPoint(event->position());

    if (isMapRepositionCommandActive()) {
        clearHighlightedTarget();
        clearDcbHover();
        setAsdexCursor(CursorMode::Hidden);
        if (displayPoint) handleMapRepositionMouseMove(*displayPoint);
        event->accept();
        return;
    }

    if (commandActive()) {
        clearHighlightedTarget();
        clearDcbHover();
        setAsdexCursor(CursorMode::Hidden);
        update();
        event->accept();
        return;
    }

    if (!displayPoint) {
        clearHighlightedTarget();
        clearDcbHover();
        setAsdexCursor(CursorMode::Scope);
        update();
        event->accept();
        return;
    }

    if (panning_) {
        const QSize displaySize = sceneDisplaySize();
        const QSize renderSize = sceneFramebufferSize();
        const double ppf = pixelsPerFoot(renderSize);

        if (ppf > 0.0) {
            const QPointF current =
                displayToFramebufferPoint(*displayPoint, displaySize, renderSize);
            const QPointF delta = current - panStartMouseFramebuffer_;
            const double tolerance =
                kRightClickDragTolerancePx * double(renderSize.width()) / double(displaySize.width());
            if (delta.x() * delta.x() + delta.y() * delta.y() > tolerance * tolerance)
                rightDragMoved_ = true;
            const QPointF worldDelta = screenDeltaToWorldDelta(delta, renderSize);
            centerFeet_ = panStartCenterFeet_ - worldDelta;
            update();
        }

        clearDcbHover();
        setAsdexCursor(CursorMode::Hidden);
        event->accept();
        return;
    }

    if (dcbMouseCaptured_) {
        clearHighlightedTarget();
        if (isPointOverDcb(*displayPoint))
            updateDcbHover(*displayPoint);
        else
            clearDcbHover();
        setAsdexCursor(CursorMode::Captured);
        event->accept();
        return;
    }

    if (isPointOverDcb(*displayPoint)) {
        clearHighlightedTarget();
        updateDcbHover(*displayPoint);
        setAsdexCursor(CursorMode::Dcb);
        event->accept();
        return;
    }

    clearDcbHover();
    setAsdexCursor(CursorMode::Scope);
    updateHighlightedTarget(*displayPoint);
    update();
    QOpenGLWidget::mouseMoveEvent(event);
}

void AsdexScopeWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (suppressNextMapRepositionRelease_ && event->button() == Qt::LeftButton) {
        suppressNextMapRepositionRelease_ = false;
        event->accept();
        return;
    }

    if (isMapRepositionCommandActive()) {
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton && panning_) {
        const std::optional<QPointF> displayPoint = widgetToDisplayPoint(event->position());
        const bool rightClick = !rightDragMoved_;
        panning_ = false;
        rightDragMoved_ = false;
        releaseMouse();

        if (!displayPoint) {
            clearHighlightedTarget();
            clearDcbHover();
            setAsdexCursor(CursorMode::Scope);
            update();
        } else if (rightClick) {
            if (isPointOverDcb(*displayPoint)) {
                clearHighlightedTarget();
                updateDcbHover(*displayPoint);
                updateHoverCursor(*displayPoint);
            } else {
                clearDcbHover();
                updateHighlightedTarget(*displayPoint);
                if (AsdexTarget* target = highlightedTarget()) {
                    startDatablockEdit(*target);
                } else {
                    updateHoverCursor(*displayPoint);
                }
            }
        } else {
            updateHoverCursor(*displayPoint);
        }

        event->accept();
        return;
    }

    if (datablockEdit_) {
        clearDcbHover();
        event->accept();
        return;
    }

    if (dcbEntryCommand_) {
        if (event->button() == Qt::LeftButton) submitDcbEntryCommand();

        event->accept();
        return;
    }

    if (dcbMouseCaptured_ && event->button() == Qt::LeftButton) {
        dcbMouseCaptured_ = false;
        clearHighlightedTarget();

        const std::optional<QPointF> displayPoint = widgetToDisplayPoint(event->position());
        const DcbHit hit = displayPoint
            ? dcb_.hitTest(*displayPoint, sceneDisplaySize(), asdexFont_, makeDcbState())
            : DcbHit{};
        if (hit.overDcb && hit.buttonIndex >= 0 && hit.function.has_value()) {
            handleDcbButtonClicked(*hit.function);
        }

        if (commandActive()) {
            clearDcbHover();
            setAsdexCursor(CursorMode::Hidden);
            update();
            event->accept();
            return;
        }

        if (displayPoint && isPointOverDcb(*displayPoint)) {
            updateDcbHover(*displayPoint);
            setAsdexCursor(CursorMode::Dcb);
        } else {
            clearDcbHover();
            setAsdexCursor(CursorMode::Scope);
        }
        update();
        event->accept();
        return;
    }

    const std::optional<QPointF> displayPoint = widgetToDisplayPoint(event->position());
    if (!displayPoint) {
        clearHighlightedTarget();
        clearDcbHover();
        setAsdexCursor(CursorMode::Scope);
        update();
        event->accept();
        return;
    }

    if (isPointOverDcb(*displayPoint)) {
        clearHighlightedTarget();
        updateDcbHover(*displayPoint);
        setAsdexCursor(CursorMode::Dcb);
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier) {
        clearDcbHover();
        updateHighlightedTarget(*displayPoint);
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

    if (isMapRepositionCommandActive()) {
        event->accept();
        return;
    }

    if (datablockEdit_) {
        clearDcbHover();
        if (wheelY > 0)
            datablockEdit_->moveUp();
        else
            datablockEdit_->moveDown();
        update();
        event->accept();
        return;
    }

    if (dcbEntryCommand_) {
        int steps = 0;
        switch (dcbEntryCommand_->type()) {
            case CommandType::Rotate:
            case CommandType::VectorLength:
            case CommandType::LeaderLength:
            case CommandType::HoldBarsBrightness:
            case CommandType::MovementAreasBrightness:
            case CommandType::BackgroundBrightness:
            case CommandType::TrackBrightness:
            case CommandType::DataBlocksBrightness:
            case CommandType::ListsBrightness:
            case CommandType::TempMapAreasBrightness:
            case CommandType::TempMapTextBrightness:
            case CommandType::DcbBrightness:
                steps = wheelY > 0 ? 1 : -1;
                break;
            case CommandType::Range:
            case CommandType::Brightness:
            case CommandType::None:
            case CommandType::EditDatablockFields:
            case CommandType::MapReposition:
            default:
                steps = wheelY > 0 ? -1 : 1;
                break;
        }

        dcbEntryCommand_->wheelDelta(steps);

        int value = 0;
        if (dcbEntryCommand_->valueInt(&value)) {
            switch (dcbEntryCommand_->type()) {
                case CommandType::Rotate:
                    setRotationValue(value);
                    break;
                case CommandType::VectorLength:
                    setVectorLengthValue(value);
                    break;
                case CommandType::LeaderLength:
                    setLeaderLengthValue(value);
                    break;
                case CommandType::HoldBarsBrightness:
                case CommandType::MovementAreasBrightness:
                case CommandType::BackgroundBrightness:
                case CommandType::TrackBrightness:
                case CommandType::DataBlocksBrightness:
                case CommandType::ListsBrightness:
                case CommandType::TempMapAreasBrightness:
                case CommandType::TempMapTextBrightness:
                case CommandType::DcbBrightness:
                    setBrightnessValue(dcbEntryCommand_->type(), value);
                    break;
                default:
                    break;
            }
        }

        clearDcbHover();
        setAsdexCursor(CursorMode::Hidden);
        update();
        event->accept();
        return;
    }

    const std::optional<QPointF> displayPoint = widgetToDisplayPoint(event->position());
    if (!displayPoint) {
        clearHighlightedTarget();
        clearDcbHover();
        setAsdexCursor(CursorMode::Scope);
        update();
        event->accept();
        return;
    }

    if (isPointOverDcb(*displayPoint)) {
        const DcbHit hit = dcb_.hitTest(*displayPoint,
                                        sceneDisplaySize(),
                                        asdexFont_,
                                        makeDcbState());
        if (hit.function && handleDcbWheel(*hit.function, wheelY)) {
            updateDcbHover(*displayPoint);
            setAsdexCursor(CursorMode::Dcb);
            update();
            event->accept();
            return;
        }

        updateDcbHover(*displayPoint);
        setAsdexCursor(CursorMode::Dcb);
        event->accept();
        return;
    }

    clearDcbHover();

    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        rotateByDegrees(wheelY > 0 ? 1 : -1);
        event->accept();
        return;
    }

    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    const bool alt = event->modifiers().testFlag(Qt::AltModifier);
    const double step = ctrl ? kCtrlWheelStepFeet : kWheelStepFeet;
    const double deltaFeet = (wheelY > 0) ? -step : step;

    if (alt)
        zoomToCursorByFeet(deltaFeet, *displayPoint);
    else
        zoomByFeet(deltaFeet);

    event->accept();
}

void AsdexScopeWidget::keyPressEvent(QKeyEvent* event) {
    if (isMapRepositionCommandActive()) {
        if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Backspace) {
            cancelCommand();
            event->accept();
            return;
        }

        event->accept();
        return;
    }

    if (datablockEdit_ && handleDatablockEditKey(event)) return;
    if (dcbEntryCommand_ && handleDcbEntryCommandKey(event)) return;

    if (commandType_ == CommandType::Brightness && event->key() == Qt::Key_Escape) {
        cancelCommand();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_F6 && event->modifiers() == Qt::NoModifier) {
        toggleAllDataBlocks();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_F10 && event->modifiers() == Qt::NoModifier) {
        toggleDayNite();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_F8 && event->modifiers() == Qt::NoModifier) {
        startMapRepositionCommand();
        event->accept();
        return;
    }

    QOpenGLWidget::keyPressEvent(event);
}

void AsdexScopeWidget::leaveEvent(QEvent* event) {
    if (!commandActive() && !panning_) {
        dcbMouseCaptured_ = false;
        clearHighlightedTarget();
        clearDcbHover();
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

bool AsdexScopeWidget::handleDcbEntryCommandKey(QKeyEvent* event) {
    if (!dcbEntryCommand_) return false;

    switch (event->key()) {
        case Qt::Key_Escape:
            cancelCommand();
            event->accept();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            submitDcbEntryCommand();
            event->accept();
            return true;
        case Qt::Key_Backspace:
            dcbEntryCommand_->backspace();
            update();
            event->accept();
            return true;
        case Qt::Key_Delete:
            dcbEntryCommand_->deleteForward();
            update();
            event->accept();
            return true;
        case Qt::Key_Left:
            dcbEntryCommand_->moveLeft();
            update();
            event->accept();
            return true;
        case Qt::Key_Right:
            dcbEntryCommand_->moveRight();
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
    if (text.size() == 1) {
        dcbEntryCommand_->insert(text.at(0));
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
    const QSize displaySize = sceneDisplaySize();
    const QSize renderSize = sceneFramebufferSize();
    const QPointF mouseWorld = screenToWorldFeet(mouseLogical, displaySize, renderSize);
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
    clearDcbHover();
    previewArea_.setSystemResponse({});
    setAsdexCursor(CursorMode::Hidden);
    update();
}

void AsdexScopeWidget::cancelCommand() {
    if (commandType_ == CommandType::MapReposition) {
        cancelMapRepositionCommand();
        return;
    }

    if (isBrightnessValueCommand(commandType_)
        || (dcbEntryCommand_ && isBrightnessValueCommand(dcbEntryCommand_->type()))) {
        commandType_ = CommandType::Brightness;
        dcbEntryCommand_.reset();
        editingTrackId_.clear();
        previewArea_.setSystemResponse({});
        dcb_.setMenu(DcbMenu::Brightness);
        clearDcbHover();
        setAsdexCursor(CursorMode::Scope);
        clearHighlightedTarget();
        update();
        return;
    }

    commandType_ = CommandType::None;
    datablockEdit_.reset();
    dcbEntryCommand_.reset();
    mapRepositionOriginalCenter_.reset();
    suppressNextMapRepositionMove_ = false;
    suppressNextMapRepositionRelease_ = false;
    editingTrackId_.clear();
    previewArea_.setSystemResponse({});
    dcb_.setMenu(currentDcbMenu());
    clearDcbHover();
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

void AsdexScopeWidget::submitDcbEntryCommand() {
    if (!dcbEntryCommand_) return;

    int value = 0;
    if (!dcbEntryCommand_->valueInt(&value)) {
        previewArea_.setSystemResponse(dcbEntryCommand_->invalidMessage());
        commandType_ = isBrightnessValueCommand(dcbEntryCommand_->type())
            ? CommandType::Brightness
            : CommandType::None;
        dcb_.setMenu(currentDcbMenu());
        dcbEntryCommand_.reset();
        clearDcbHover();
        setAsdexCursor(CursorMode::Scope);
        update();
        return;
    }

    switch (dcbEntryCommand_->type()) {
        case CommandType::Range:
            setRangeValue(value);
            commandType_ = CommandType::None;
            break;
        case CommandType::Rotate:
            setRotationValue(value);
            commandType_ = CommandType::None;
            break;
        case CommandType::VectorLength:
            setVectorLengthValue(value);
            commandType_ = CommandType::None;
            break;
        case CommandType::LeaderLength:
            setLeaderLengthValue(value);
            commandType_ = CommandType::None;
            break;
        case CommandType::HoldBarsBrightness:
        case CommandType::MovementAreasBrightness:
        case CommandType::BackgroundBrightness:
        case CommandType::TrackBrightness:
        case CommandType::DataBlocksBrightness:
        case CommandType::ListsBrightness:
        case CommandType::TempMapAreasBrightness:
        case CommandType::TempMapTextBrightness:
        case CommandType::DcbBrightness:
            setBrightnessValue(dcbEntryCommand_->type(), value);
            commandType_ = CommandType::Brightness;
            break;
        case CommandType::None:
        case CommandType::EditDatablockFields:
        case CommandType::MapReposition:
        case CommandType::Brightness:
        default:
            commandType_ = CommandType::None;
            break;
    }

    previewArea_.setSystemResponse(QString());
    dcb_.setMenu(currentDcbMenu());
    dcbEntryCommand_.reset();
    clearDcbHover();
    setAsdexCursor(CursorMode::Scope);
    update();
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

bool AsdexScopeWidget::commandActive() const {
    return datablockEdit_.has_value()
        || dcbEntryCommand_.has_value()
        || isMapRepositionCommandActive();
}

DcbMenu AsdexScopeWidget::currentDcbMenu() const {
    if (dcbOff_) return DcbMenu::Off;

    if (commandType_ == CommandType::Brightness || isBrightnessValueCommand(commandType_))
        return DcbMenu::Brightness;

    return DcbMenu::Main;
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

void AsdexScopeWidget::handleDcbButtonClicked(DcbFunction function) {
    switch (function) {
        case DcbFunction::Range:
            startRangeCommand();
            return;
        case DcbFunction::MapReposition:
            startMapRepositionCommand();
            return;
        case DcbFunction::Rotate:
            startRotateCommand();
            return;
        case DcbFunction::VectorOnOff:
            toggleVectorLine();
            return;
        case DcbFunction::VectorLength:
            startVectorLengthCommand();
            return;
        case DcbFunction::LeaderLength:
            startLeaderLengthCommand();
            return;
        case DcbFunction::DataBlocksOnOff:
            toggleAllDataBlocks();
            return;
        case DcbFunction::Brightness:
            startBrightnessMenu();
            return;
        case DcbFunction::HoldBarsBrightness:
        case DcbFunction::MovementAreasBrightness:
        case DcbFunction::BackgroundBrightness:
        case DcbFunction::TrackBrightness:
        case DcbFunction::DataBlocksBrightness:
        case DcbFunction::ListsBrightness:
        case DcbFunction::TempMapAreasBrightness:
        case DcbFunction::TempMapTextBrightness:
        case DcbFunction::DcbBrightness:
            startBrightnessValueCommand(function);
            return;
        case DcbFunction::Done:
            handleDcbDone();
            return;
        case DcbFunction::DayNite:
            toggleDayNite();
            return;
        case DcbFunction::DcbOnOff:
            toggleDcbOnOff();
            return;
        default:
            return;
    }
}

void AsdexScopeWidget::toggleDcbOnOff() {
    dcbOff_ = !dcbOff_;
    dcb_.setMenu(dcbOff_ ? DcbMenu::Off : DcbMenu::Main);
    clearDcbHover();
    update();
}

void AsdexScopeWidget::toggleDayNite() {
    mode_ = (mode_ == Mode::Day) ? Mode::Night : Mode::Day;
    clearDcbHover();
    update();
}

void AsdexScopeWidget::toggleAllDataBlocks() {
    showDataBlocks_ = !showDataBlocks_;
    datablockVisibility_.clear();
    clearDcbHover();
    update();
}

void AsdexScopeWidget::startBrightnessMenu() {
    if (commandType_ != CommandType::None) return;

    commandType_ = CommandType::Brightness;
    dcb_.setMenu(DcbMenu::Brightness);
    datablockEdit_.reset();
    dcbEntryCommand_.reset();
    editingTrackId_.clear();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
}

void AsdexScopeWidget::startBrightnessValueCommand(DcbFunction function) {
    const CommandType type = commandForBrightnessFunction(function);
    if (type == CommandType::None) return;

    commandType_ = type;
    dcb_.setMenu(DcbMenu::Brightness);
    dcbEntryCommand_ =
        DcbEntryCommand::brightness(type, brightnessCommandLabel(type), brightnessValue(type));
    datablockEdit_.reset();
    editingTrackId_.clear();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Hidden);
    update();
}

void AsdexScopeWidget::handleDcbDone() {
    commandType_ = CommandType::None;
    dcb_.setMenu(currentDcbMenu());
    dcbEntryCommand_.reset();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
}

CommandType AsdexScopeWidget::commandForBrightnessFunction(DcbFunction function) const {
    switch (function) {
        case DcbFunction::HoldBarsBrightness:
            return CommandType::HoldBarsBrightness;
        case DcbFunction::MovementAreasBrightness:
            return CommandType::MovementAreasBrightness;
        case DcbFunction::BackgroundBrightness:
            return CommandType::BackgroundBrightness;
        case DcbFunction::TrackBrightness:
            return CommandType::TrackBrightness;
        case DcbFunction::DataBlocksBrightness:
            return CommandType::DataBlocksBrightness;
        case DcbFunction::ListsBrightness:
            return CommandType::ListsBrightness;
        case DcbFunction::TempMapAreasBrightness:
            return CommandType::TempMapAreasBrightness;
        case DcbFunction::TempMapTextBrightness:
            return CommandType::TempMapTextBrightness;
        case DcbFunction::DcbBrightness:
            return CommandType::DcbBrightness;
        default:
            return CommandType::None;
    }
}

QString AsdexScopeWidget::brightnessCommandLabel(CommandType type) const {
    switch (type) {
        case CommandType::HoldBarsBrightness:
            return QStringLiteral("HOLD BARS");
        case CommandType::MovementAreasBrightness:
            return QStringLiteral("MVMENT AREA");
        case CommandType::BackgroundBrightness:
            return QStringLiteral("BAKGND");
        case CommandType::TrackBrightness:
            return QStringLiteral("TRACK");
        case CommandType::DataBlocksBrightness:
            return QStringLiteral("DATA BLOCKS");
        case CommandType::ListsBrightness:
            return QStringLiteral("LISTS");
        case CommandType::TempMapAreasBrightness:
            return QStringLiteral("TEMP MAP AREAS");
        case CommandType::TempMapTextBrightness:
            return QStringLiteral("TEMP MAP TEXT");
        case CommandType::DcbBrightness:
            return QStringLiteral("DCB");
        default:
            return {};
    }
}

int AsdexScopeWidget::brightnessValue(CommandType type) const {
    switch (type) {
        case CommandType::HoldBarsBrightness:
            return holdBarsBrightness_;
        case CommandType::MovementAreasBrightness:
            return movementAreasBrightness_;
        case CommandType::BackgroundBrightness:
            return backgroundBrightness_;
        case CommandType::TrackBrightness:
            return trackBrightness_;
        case CommandType::DataBlocksBrightness:
            return dataBlocksBrightness_;
        case CommandType::ListsBrightness:
            return listsBrightness_;
        case CommandType::TempMapAreasBrightness:
            return tempMapAreasBrightness_;
        case CommandType::TempMapTextBrightness:
            return tempMapTextBrightness_;
        case CommandType::DcbBrightness:
            return dcbBrightness_;
        default:
            return 95;
    }
}

void AsdexScopeWidget::setBrightnessValue(CommandType type, int value) {
    value = std::clamp(value, 1, 99);

    switch (type) {
        case CommandType::HoldBarsBrightness:
            holdBarsBrightness_ = value;
            break;
        case CommandType::MovementAreasBrightness:
            movementAreasBrightness_ = value;
            break;
        case CommandType::BackgroundBrightness:
            backgroundBrightness_ = value;
            break;
        case CommandType::TrackBrightness:
            trackBrightness_ = value;
            break;
        case CommandType::DataBlocksBrightness:
            dataBlocksBrightness_ = value;
            break;
        case CommandType::ListsBrightness:
            listsBrightness_ = value;
            break;
        case CommandType::TempMapAreasBrightness:
            tempMapAreasBrightness_ = value;
            break;
        case CommandType::TempMapTextBrightness:
            tempMapTextBrightness_ = value;
            break;
        case CommandType::DcbBrightness:
            dcbBrightness_ = value;
            dcb_.setBrightness(value);
            break;
        default:
            return;
    }

    update();
}

int AsdexScopeWidget::currentRangeValue() const {
    return std::clamp(int(std::round(halfRangeFeet_ / 100.0)), 6, 300);
}

void AsdexScopeWidget::setRangeValue(int range) {
    range = std::clamp(range, 6, 300);
    halfRangeFeet_ = std::clamp(double(range) * 100.0,
                                kMinHalfRangeFeet,
                                kMaxHalfRangeFeet);
    update();
}

void AsdexScopeWidget::startRangeCommand() {
    if (commandType_ != CommandType::None) return;

    commandType_ = CommandType::Range;
    dcbEntryCommand_ = DcbEntryCommand::range(currentRangeValue());
    datablockEdit_.reset();
    editingTrackId_.clear();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Hidden);
    update();
}

int AsdexScopeWidget::currentRotationValue() const {
    return normalizedDegrees(rotationDegrees_);
}

void AsdexScopeWidget::setRotationValue(int degrees) {
    rotationDegrees_ = normalizedDegrees(degrees);
    update();
}

void AsdexScopeWidget::rotateByDegrees(int deltaDegrees) {
    setRotationValue(rotationDegrees_ + deltaDegrees);
}

void AsdexScopeWidget::startRotateCommand() {
    if (commandType_ != CommandType::None) return;

    commandType_ = CommandType::Rotate;
    dcbEntryCommand_ = DcbEntryCommand::rotate(currentRotationValue());
    datablockEdit_.reset();
    editingTrackId_.clear();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Hidden);
    update();
}

void AsdexScopeWidget::toggleVectorLine() {
    showVectorLine_ = !showVectorLine_;
    clearDcbHover();
    update();
}

int AsdexScopeWidget::currentVectorLengthValue() const {
    return clampedTargetVectorSeconds(targetVectorSeconds_);
}

void AsdexScopeWidget::setVectorLengthValue(int seconds) {
    targetVectorSeconds_ = clampedTargetVectorSeconds(seconds);
    update();
}

void AsdexScopeWidget::startVectorLengthCommand() {
    if (commandType_ != CommandType::None) return;

    commandType_ = CommandType::VectorLength;
    dcbEntryCommand_ = DcbEntryCommand::vectorLength(currentVectorLengthValue());
    datablockEdit_.reset();
    editingTrackId_.clear();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Hidden);
    update();
}

int AsdexScopeWidget::currentLeaderLengthValue() const {
    return std::clamp(leaderLength_, 0, 15);
}

void AsdexScopeWidget::setLeaderLengthValue(int leaderLength) {
    leaderLength_ = std::clamp(leaderLength, 0, 15);
    update();
}

void AsdexScopeWidget::startLeaderLengthCommand() {
    if (commandType_ != CommandType::None) return;

    commandType_ = CommandType::LeaderLength;
    dcbEntryCommand_ = DcbEntryCommand::leaderLength(currentLeaderLengthValue());
    datablockEdit_.reset();
    editingTrackId_.clear();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Hidden);
    update();
}

void AsdexScopeWidget::startMapRepositionCommand() {
    if (commandType_ != CommandType::None) return;

    commandType_ = CommandType::MapReposition;
    datablockEdit_.reset();
    dcbEntryCommand_.reset();
    editingTrackId_.clear();
    mapRepositionOriginalCenter_ = centerFeet_;
    mapRepositionLastMouseFramebuffer_ =
        displayToFramebufferPoint(mapRepositionBoxCenterDisplay(),
                                  sceneDisplaySize(),
                                  sceneFramebufferSize());
    suppressNextMapRepositionMove_ = true;
    suppressNextMapRepositionRelease_ = false;

    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Hidden);
    grabMouse();
    moveMapRepositionCursorToBoxCenter();
    update();
}

void AsdexScopeWidget::commitMapRepositionCommand() {
    commandType_ = CommandType::None;
    mapRepositionOriginalCenter_.reset();
    suppressNextMapRepositionMove_ = false;
    releaseMouse();
    clearHighlightedTarget();
    clearDcbHover();
    setAsdexCursor(CursorMode::Scope);
    update();
}

void AsdexScopeWidget::cancelMapRepositionCommand() {
    if (mapRepositionOriginalCenter_) centerFeet_ = *mapRepositionOriginalCenter_;

    commandType_ = CommandType::None;
    mapRepositionOriginalCenter_.reset();
    suppressNextMapRepositionMove_ = false;
    suppressNextMapRepositionRelease_ = false;
    releaseMouse();
    clearHighlightedTarget();
    clearDcbHover();
    setAsdexCursor(CursorMode::Scope);
    update();
}

bool AsdexScopeWidget::isMapRepositionCommandActive() const {
    return commandType_ == CommandType::MapReposition;
}

QPointF AsdexScopeWidget::mapRepositionBoxCenterDisplay() const {
    const QSize displaySize = sceneDisplaySize();
    return QPointF(std::min(50.0, std::max(0.0, displaySize.width() * 0.5)),
                   std::min(50.0, std::max(0.0, displaySize.height() * 0.5)));
}

void AsdexScopeWidget::moveMapRepositionCursorToBoxCenter() {
    const QPointF point = displayToWidgetPoint(mapRepositionBoxCenterDisplay());
    QCursor::setPos(mapToGlobal(QPoint(int(std::round(point.x())),
                                      int(std::round(point.y())))));
}

void AsdexScopeWidget::handleMapRepositionMouseMove(const QPointF& displayPoint) {
    const QSize displaySize = sceneDisplaySize();
    const QSize renderSize = sceneFramebufferSize();
    if (renderSize.isEmpty()) return;

    const QPointF currentFramebuffer =
        displayToFramebufferPoint(displayPoint, displaySize, renderSize);
    if (suppressNextMapRepositionMove_) {
        suppressNextMapRepositionMove_ = false;
        mapRepositionLastMouseFramebuffer_ = currentFramebuffer;
        return;
    }

    const QPointF boxCenterFramebuffer =
        displayToFramebufferPoint(mapRepositionBoxCenterDisplay(), displaySize, renderSize);
    QPointF delta = currentFramebuffer - boxCenterFramebuffer;
    if (std::abs(delta.x()) < 0.5 && std::abs(delta.y()) < 0.5) {
        delta = currentFramebuffer - mapRepositionLastMouseFramebuffer_;
    }

    if (std::abs(delta.x()) < 0.5 && std::abs(delta.y()) < 0.5) return;

    const QPointF worldDelta = screenDeltaToWorldDelta(delta, renderSize);
    centerFeet_ -= worldDelta;
    mapRepositionLastMouseFramebuffer_ = boxCenterFramebuffer;
    suppressNextMapRepositionMove_ = true;
    moveMapRepositionCursorToBoxCenter();
    update();
}

QStringList AsdexScopeWidget::activeCommandLines() const {
    if (datablockEdit_) return datablockEdit_->displayLines();
    if (dcbEntryCommand_) return dcbEntryCommand_->displayLines();
    if (isMapRepositionCommandActive()) return {QStringLiteral("MAP RPOS")};
    if (commandType_ == CommandType::Brightness) return {QStringLiteral("BRITE")};
    return {};
}

void AsdexScopeWidget::renderScene(const QSize& displaySize, const QSize& renderSize) {
    if (!renderer_ || renderSize.isEmpty()) return;

    renderer::LayeredCommandBuffer layers;
    const QMatrix4x4 worldProjection = viewProjection(renderSize);
    const QMatrix4x4 projection = screenProjection(displaySize);

    auto prepareLayer = [&renderSize](renderer::CommandBuffer& buffer,
                                      const QMatrix4x4& layerProjection) {
        buffer.viewport(0, 0, renderSize.width(), renderSize.height());
        buffer.loadProjectionMatrix(layerProjection);
    };

    renderer::CommandBuffer& mapBuffer = layers.layer(z::VideoMap);
    prepareLayer(mapBuffer, worldProjection);
    mapBuffer.clear(
        renderer::RGBA::fromQColor(applyBrightness(backgroundColor(mode_), backgroundBrightness_, 20)));
    drawVideoMap(map_, &mapBuffer, mode_);

    renderer::CommandBuffer& runwayBuffer = layers.layer(z::RunwayClosures);
    prepareLayer(runwayBuffer, worldProjection);
    drawRunwayClosures(runwayClosureGeometry_, &runwayBuffer, worldProjection);

    auto worldToFramebuffer = [this, &renderSize](QPointF worldFeet) {
        return worldToFramebufferTopLeft(worldFeet, renderSize);
    };

    renderer::CommandBuffer& restrictedAreaBuffer = layers.layer(z::RestrictedArea);
    prepareLayer(restrictedAreaBuffer, worldProjection);
    tempAreaGeometry_.drawType(&restrictedAreaBuffer,
                               worldProjection,
                               worldToFramebuffer,
                               TempAreaType::RestrictedArea,
                               tempMapAreasBrightness_);

    renderer::CommandBuffer& closedAreaBuffer = layers.layer(z::ClosedArea);
    prepareLayer(closedAreaBuffer, worldProjection);
    tempAreaGeometry_.drawType(&closedAreaBuffer,
                               worldProjection,
                               worldToFramebuffer,
                               TempAreaType::ClosedArea,
                               tempMapAreasBrightness_);

    renderer::CommandBuffer& targetBuffer = layers.layer(z::Targets);
    prepareLayer(targetBuffer, worldProjection);
    drawTargets(targets_,
                &targetBuffer,
                worldProjection,
                mode_,
                targetVectorSeconds_,
                showVectorLine_,
                trackBrightness_);

    const DcbState dcbState = makeDcbState();
    dcb_.setBrightness(dcbBrightness_);
    dcb_.setMenu(currentDcbMenu());
    const DcbLayout dcbLayout = dcb_.layout(displaySize, asdexFont_, dcbState);

    renderer::CommandBuffer& dcbBackgroundBuffer = layers.layer(z::DcbBackground);
    prepareLayer(dcbBackgroundBuffer, projection);
    dcb_.drawBackground(&dcbBackgroundBuffer, dcbLayout);

    renderer::CommandBuffer& dcbButtonBuffer = layers.layer(z::DcbButtons);
    prepareLayer(dcbButtonBuffer, projection);
    dcb_.drawButtons(&dcbButtonBuffer, dcbLayout);

    const std::uint32_t dcbFontTexture = fontTextureId(dcbLayout.renderFontSize);
    if (fontTexturesReady_ && dcbFontTexture != 0) {
        const int dcbHoverIndex =
            hoveredDcbButtonIndex_ >= 0 && hoveredDcbButtonIndex_ < dcbLayout.buttons.size()
                ? hoveredDcbButtonIndex_
                : -1;

        renderer::CommandBuffer& dcbTextBuffer = layers.layer(z::DcbText);
        prepareLayer(dcbTextBuffer, projection);

        renderer::TextBuilder* dcbTextBuilder = renderer::getTextBuilder();
        dcbTextBuilder->setFont(&asdexFont_);
        dcb_.drawText(*dcbTextBuilder,
                      asdexFont_,
                      dcbFontTexture,
                      dcbLayout,
                      dcbHoverIndex);
        dcbTextBuilder->generateCommands(&dcbTextBuffer);
        renderer::returnTextBuilder(dcbTextBuilder);
    }

    const std::uint32_t listFontTexture = fontTextureId(2);
    if (fontTexturesReady_ && listFontTexture != 0) {
        DataBlockSettings datablockSettings;
        datablockSettings.fontSize = 2;
        datablockSettings.brightness = dataBlocksBrightness_;
        datablockSettings.leaderLength = currentLeaderLengthValue();
        datablockSettings.leaderDirection = LeaderDirection::NE;
        datablockSettings.timesharePrimary = timesharePrimary_;
        datablockSettings.alertInProgress = false;

        renderer::CommandBuffer& datablockBuffer = layers.layer(z::Datablocks);
        prepareLayer(datablockBuffer, projection);
        drawDatablocks(targets_,
                       &datablockBuffer,
                       projection,
                       [this, &displaySize, &renderSize](QPointF worldFeet) {
                           return worldToScreenLogical(worldFeet, displaySize, renderSize);
                       },
                       [this](const AsdexTarget& target) {
                           return isDataBlockVisible(target);
                       },
                       asdexFont_,
                       listFontTexture,
                       datablockSettings);

        renderer::CommandBuffer& listBuffer = layers.layer(z::PreviewArea);
        prepareLayer(listBuffer, projection);
        renderer::TextBuilder* textBuilder = renderer::getTextBuilder();
        textBuilder->setFont(&asdexFont_);

        coastList_.setBrightness(listsBrightness_);
        previewArea_.setBrightness(listsBrightness_);
        coastList_.render(*textBuilder, asdexFont_, listFontTexture, displaySize);

        const QStringList commandLines = activeCommandLines();
        previewArea_.render(*textBuilder, asdexFont_, listFontTexture, commandLines);
        textBuilder->generateCommands(&listBuffer);
        renderer::returnTextBuilder(textBuilder);

        if (datablockEdit_ || dcbEntryCommand_) {
            renderer::CommandBuffer& cursorBuffer = layers.layer(z::PreviewCursor);
            prepareLayer(cursorBuffer, projection);
            cursorBuffer.setRgba(renderer::RGBA::fromQColor(previewArea_.textColor()));
            cursorBuffer.lineWidth(1.0f);

            renderer::LinesBuilder* lineBuilder = renderer::getLinesBuilder();
            if (datablockEdit_) {
                previewArea_.renderCommandCursor(*lineBuilder,
                                                 asdexFont_,
                                                 *datablockEdit_,
                                                 projection);
            } else if (dcbEntryCommand_) {
                previewArea_.renderCommandCursor(*lineBuilder,
                                                 asdexFont_,
                                                 dcbEntryCommand_->cursorLine(),
                                                 dcbEntryCommand_->cursorColumn(),
                                                 projection);
            }
            lineBuilder->generateCommands(&cursorBuffer);
            renderer::returnLinesBuilder(lineBuilder);
        }
    }

    renderer::FrameSpec frameSpec;
    frameSpec.sceneFramebufferSize = renderSize;
    frameSpec.outputFramebufferSize = nativeFramebufferSize();
    frameSpec.display = necDisplayEmulation_
        ? renderer::DisplayEmulationSettings::necLcd2190Uxp()
        : renderer::DisplayEmulationSettings{};
    if (!necDisplayEmulation_) frameSpec.display.mode = renderer::DisplayEmulationMode::Native;

    layers.flushTo(renderer_.get(), frameSpec);
}

DcbState AsdexScopeWidget::makeDcbState() const {
    DcbState state;
    state.range = currentRangeValue();
    state.rotation = currentRotationValue();
    state.vectorLength = currentVectorLengthValue();
    state.leaderLength = currentLeaderLengthValue();
    state.nightMode = mode_ == Mode::Night;
    state.showVectorLine = showVectorLine_;
    state.showDataBlocks = showDataBlocks_;
    state.dcbOn = !dcbOff_;
    state.networkConnected = true;
    state.holdBarsBrightness = holdBarsBrightness_;
    state.movementAreasBrightness = movementAreasBrightness_;
    state.backgroundBrightness = backgroundBrightness_;
    state.trackBrightness = trackBrightness_;
    state.dataBlocksBrightness = dataBlocksBrightness_;
    state.listsBrightness = listsBrightness_;
    state.tempMapAreasBrightness = tempMapAreasBrightness_;
    state.tempMapTextBrightness = tempMapTextBrightness_;
    state.dcbBrightness = dcbBrightness_;
    return state;
}

QSize AsdexScopeWidget::sceneDisplaySize() const {
    if (fixedVirtualPanel_) return QSize(1600, 1200);
    return size();
}

QSize AsdexScopeWidget::sceneFramebufferSize() const {
    if (fixedVirtualPanel_) return QSize(1600, 1200);
    return nativeFramebufferSize();
}

QSize AsdexScopeWidget::nativeFramebufferSize() const {
    const qreal ratio = devicePixelRatioF();
    return QSize(qRound(width() * ratio), qRound(height() * ratio));
}

QRectF AsdexScopeWidget::panelRectInWidgetLogical() const {
    const QSize displaySize = sceneDisplaySize();
    if (!fixedVirtualPanel_ || displaySize.isEmpty()) {
        return QRectF(0.0, 0.0, double(width()), double(height()));
    }

    const double sx = double(width()) / double(displaySize.width());
    const double sy = double(height()) / double(displaySize.height());
    const double scale = std::min(sx, sy);
    const double panelWidth = displaySize.width() * scale;
    const double panelHeight = displaySize.height() * scale;
    return QRectF((width() - panelWidth) * 0.5,
                  (height() - panelHeight) * 0.5,
                  panelWidth,
                  panelHeight);
}

std::optional<QPointF> AsdexScopeWidget::widgetToDisplayPoint(QPointF widgetPoint) const {
    if (!fixedVirtualPanel_) return widgetPoint;

    const QSize displaySize = sceneDisplaySize();
    const QRectF panelRect = panelRectInWidgetLogical();
    if (displaySize.isEmpty() || panelRect.isEmpty() || !panelRect.contains(widgetPoint)) {
        return std::nullopt;
    }

    return QPointF((widgetPoint.x() - panelRect.x()) * displaySize.width() / panelRect.width(),
                   (widgetPoint.y() - panelRect.y()) * displaySize.height() / panelRect.height());
}

QPointF AsdexScopeWidget::displayToWidgetPoint(QPointF displayPoint) const {
    if (!fixedVirtualPanel_) return displayPoint;

    const QSize displaySize = sceneDisplaySize();
    const QRectF panelRect = panelRectInWidgetLogical();
    if (displaySize.isEmpty() || panelRect.isEmpty()) return displayPoint;

    return QPointF(panelRect.x() + displayPoint.x() * panelRect.width() / displaySize.width(),
                   panelRect.y() + displayPoint.y() * panelRect.height() / displaySize.height());
}

std::uint32_t AsdexScopeWidget::fontTextureId(int fontSize) const {
    return fontTextureIds_.value(fontSize, 0);
}

QMatrix4x4 AsdexScopeWidget::screenProjection(const QSize& displaySize) const {
    QMatrix4x4 projection;
    projection.setToIdentity();
    projection.ortho(0.0f,
                     static_cast<float>(displaySize.width()),
                     static_cast<float>(displaySize.height()),
                     0.0f,
                     -1.0f,
                     1.0f);
    return projection;
}

QPointF AsdexScopeWidget::worldToScreenLogical(const QPointF& worldFeet,
                                               const QSize& displaySize,
                                               const QSize& renderSize) const {
    const double ppf = pixelsPerFoot(renderSize);
    const double theta = radiansFromDegrees(currentRotationValue());
    const double c = std::cos(theta);
    const double s = std::sin(theta);

    const double dx = worldFeet.x() - centerFeet_.x();
    const double dy = worldFeet.y() - centerFeet_.y();
    const double rx = c * dx - s * dy;
    const double ry = s * dx + c * dy;

    const double framebufferX = renderSize.width() * 0.5 + rx * ppf;
    const double framebufferY = renderSize.height() * 0.5 - ry * ppf;

    return QPointF(framebufferX * displaySize.width() / renderSize.width(),
                   framebufferY * displaySize.height() / renderSize.height());
}

QPointF AsdexScopeWidget::worldToFramebufferTopLeft(const QPointF& worldFeet,
                                                    const QSize& renderSize) const {
    const double ppf = pixelsPerFoot(renderSize);
    const double theta = radiansFromDegrees(currentRotationValue());
    const double c = std::cos(theta);
    const double s = std::sin(theta);

    const double dx = worldFeet.x() - centerFeet_.x();
    const double dy = worldFeet.y() - centerFeet_.y();
    const double rx = c * dx - s * dy;
    const double ry = s * dx + c * dy;

    return QPointF(renderSize.width() * 0.5 + rx * ppf,
                   renderSize.height() * 0.5 - ry * ppf);
}

QPointF AsdexScopeWidget::displayToFramebufferPoint(const QPointF& displayPoint,
                                                    const QSize& displaySize,
                                                    const QSize& framebufferSize) const {
    if (displaySize.isEmpty()) return displayPoint;

    return QPointF(displayPoint.x() * framebufferSize.width() / displaySize.width(),
                   displayPoint.y() * framebufferSize.height() / displaySize.height());
}

double AsdexScopeWidget::pixelsPerFoot(const QSize& renderSize) const {
    if (renderSize.isEmpty() || halfRangeFeet_ <= 0.0) return 1.0;

    const double availW = renderSize.width() * (1.0 - 2.0 * utils::kViewportMargin);
    const double availH = renderSize.height() * (1.0 - 2.0 * utils::kViewportMargin);
    const double radiusPx = 0.5 * std::min(availW, availH);
    return radiusPx / halfRangeFeet_;
}

QPointF AsdexScopeWidget::screenToWorldFeet(const QPointF& displayPoint,
                                            const QSize& displaySize,
                                            const QSize& renderSize) const {
    const QPointF point = displayToFramebufferPoint(displayPoint, displaySize, renderSize);
    const double ppf = pixelsPerFoot(renderSize);

    if (ppf <= 0.0) return centerFeet_;

    const double rx = (point.x() - renderSize.width() * 0.5) / ppf;
    const double ry = (renderSize.height() * 0.5 - point.y()) / ppf;

    const double theta = radiansFromDegrees(currentRotationValue());
    const double c = std::cos(theta);
    const double s = std::sin(theta);

    const double dx = c * rx + s * ry;
    const double dy = -s * rx + c * ry;

    return QPointF(centerFeet_.x() + dx, centerFeet_.y() + dy);
}

QPointF AsdexScopeWidget::screenDeltaToWorldDelta(const QPointF& framebufferDelta,
                                                  const QSize& renderSize) const {
    const double ppf = pixelsPerFoot(renderSize);
    if (ppf <= 0.0) return QPointF();

    const double rx = framebufferDelta.x() / ppf;
    const double ry = -framebufferDelta.y() / ppf;

    const double theta = radiansFromDegrees(currentRotationValue());
    const double c = std::cos(theta);
    const double s = std::sin(theta);

    const double dx = c * rx + s * ry;
    const double dy = -s * rx + c * ry;
    return QPointF(dx, dy);
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
    const QSize displaySize = sceneDisplaySize();
    const QSize renderSize = sceneFramebufferSize();
    const QPointF worldBefore = screenToWorldFeet(cursorLogicalPoint, displaySize, renderSize);
    const double oldRange = halfRangeFeet_;
    const double newRange = std::clamp(oldRange + deltaFeet,
                                       kMinHalfRangeFeet,
                                       kMaxHalfRangeFeet);

    if (qFuzzyCompare(oldRange, newRange)) return;

    halfRangeFeet_ = newRange;
    const QPointF worldAfter = screenToWorldFeet(cursorLogicalPoint, displaySize, renderSize);
    centerFeet_ += worldBefore - worldAfter;
    update();
}

bool AsdexScopeWidget::isPointOverDcb(const QPointF& logicalPoint) const {
    if (!fontLoaded_) return false;

    return dcb_.contains(logicalPoint, sceneDisplaySize(), asdexFont_, makeDcbState());
}

bool AsdexScopeWidget::handleDcbWheel(DcbFunction function, int wheelY) {
    const int step = wheelY > 0 ? 1 : -1;
    const CommandType brightnessType = commandForBrightnessFunction(function);

    if (brightnessType != CommandType::None) {
        setBrightnessValue(brightnessType, brightnessValue(brightnessType) + step);
        return true;
    }

    switch (function) {
        case DcbFunction::Range:
            setRangeValue(currentRangeValue() - step);
            return true;
        case DcbFunction::LeaderLength:
            setLeaderLengthValue(currentLeaderLengthValue() + step);
            return true;
        case DcbFunction::VectorLength:
            setVectorLengthValue(currentVectorLengthValue() + step);
            return true;
        default:
            return false;
    }
}

void AsdexScopeWidget::clearDcbHover() {
    if (hoveredDcbButtonIndex_ == -1 && !hoveredDcbFunction_.has_value()) return;

    hoveredDcbButtonIndex_ = -1;
    hoveredDcbFunction_.reset();
    update();
}

void AsdexScopeWidget::updateDcbHover(const QPointF& logicalPoint) {
    if (!fontLoaded_) {
        clearDcbHover();
        return;
    }

    const DcbHit hit =
        dcb_.hitTest(logicalPoint, sceneDisplaySize(), asdexFont_, makeDcbState());
    const int oldIndex = hoveredDcbButtonIndex_;
    const std::optional<DcbFunction> oldFunction = hoveredDcbFunction_;

    if (hit.overDcb && hit.buttonIndex >= 0) {
        hoveredDcbButtonIndex_ = hit.buttonIndex;
        hoveredDcbFunction_ = hit.function;
    } else {
        hoveredDcbButtonIndex_ = -1;
        hoveredDcbFunction_.reset();
    }

    if (hoveredDcbButtonIndex_ != oldIndex || hoveredDcbFunction_ != oldFunction) update();
}

void AsdexScopeWidget::updateHoverCursor(const QPointF& logicalPoint) {
    if (commandActive() || panning_) {
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

    const double theta = radiansFromDegrees(currentRotationValue());
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    const double cx = centerFeet_.x();
    const double cy = centerFeet_.y();

    matrix(0, 0) = static_cast<float>(sx * c);
    matrix(0, 1) = static_cast<float>(-sx * s);
    matrix(0, 3) = static_cast<float>(sx * (-c * cx + s * cy));
    matrix(1, 0) = static_cast<float>(sy * s);
    matrix(1, 1) = static_cast<float>(sy * c);
    matrix(1, 3) = static_cast<float>(sy * (-s * cx - c * cy));
    return matrix;
}

} // namespace asdex
