#include "asdex/scope.h"

#include "asdex/datablocks.h"
#include "asdex/tempdata.h"
#include "asdex/targets.h"
#include "asdex/videomaps.h"
#include "utils/math.h"
#include "utils/resources.h"
#include "renderer/builders.h"
#include "renderer/cmdbuffer.h"
#include "renderer/renderlayers.h"
#include "renderer/renderer.h"

#include <QCursor>
#include <QDebug>
#include <QSurfaceFormat>
#include <QUuid>
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
constexpr int DbAreas = -600;

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

int leaderDirectionDcbValue(LeaderDirection direction) {
    switch (direction) {
        case LeaderDirection::N:
            return 8;
        case LeaderDirection::NE:
            return 9;
        case LeaderDirection::E:
            return 6;
        case LeaderDirection::SE:
            return 3;
        case LeaderDirection::S:
            return 2;
        case LeaderDirection::SW:
            return 1;
        case LeaderDirection::W:
            return 4;
        case LeaderDirection::NW:
            return 7;
    }

    return 9;
}

LeaderDirection leaderDirectionFromDcbValueRaw(int value) {
    switch (value) {
        case 1:
            return LeaderDirection::SW;
        case 2:
            return LeaderDirection::S;
        case 3:
            return LeaderDirection::SE;
        case 4:
            return LeaderDirection::W;
        case 6:
            return LeaderDirection::E;
        case 7:
            return LeaderDirection::NW;
        case 8:
            return LeaderDirection::N;
        case 9:
            return LeaderDirection::NE;
        default:
            return LeaderDirection::NE;
    }
}

bool isTraitAreaValueCommand(CommandType type) {
    switch (type) {
        case CommandType::DefineTraitAreaDbCharSize:
        case CommandType::DefineTraitAreaDbBrightness:
        case CommandType::DefineTraitAreaLeaderLength:
        case CommandType::DefineTraitAreaLeaderDirection:
            return true;
        default:
            return false;
    }
}

bool isModifyTraitAreaValueCommand(CommandType type) {
    switch (type) {
        case CommandType::ModifyTraitAreaDbCharSize:
        case CommandType::ModifyTraitAreaDbBrightness:
        case CommandType::ModifyTraitAreaLeaderLength:
        case CommandType::ModifyTraitAreaLeaderDirection:
            return true;
        default:
            return false;
    }
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
        const int fontSizes[] = {1, 2, 3, 4, 5, 6};
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

    if (leaderDirectionCommand_ && event->button() == Qt::LeftButton) {
        event->accept();
        return;
    }

    if (isPointOverDcb(event->position())) {
        clearHighlightedTarget();
        updateDcbHover(event->position());
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

    if (isSelectingDbArea()) {
        if (event->button() != Qt::RightButton) {
            suppressNextDbAreaSelectionRelease_ = true;
            if (commandType_ == CommandType::DeleteOneDbArea)
                deleteDbAreaAt(event->position());
            else if (commandType_ == CommandType::ModifyTraitArea)
                selectTraitAreaAt(event->position());
            event->accept();
            return;
        }
    }

    if (isDrawingDbArea()) {
        if (event->button() == Qt::LeftButton) {
            addDbAreaPoint(screenToWorldFeet(event->position(), framebufferRenderSize()));
            event->accept();
            return;
        }

        if (event->button() == Qt::MiddleButton) {
            completeDbAreaPolygon();
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::RightButton) {
        clearDcbHover();
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
    if (isMapRepositionCommandActive()) {
        clearHighlightedTarget();
        clearDcbHover();
        setAsdexCursor(CursorMode::Hidden);
        handleMapRepositionMouseMove(event->position());
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

    if (panning_) {
        const QSize renderSize = framebufferRenderSize();
        const double ppf = pixelsPerFoot(renderSize);

        if (ppf > 0.0) {
            const QPointF current = framebufferPoint(event->position());
            const QPointF delta = current - panStartMouseFramebuffer_;
            const double tolerance = kRightClickDragTolerancePx * devicePixelRatioF();
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

    if (leaderDirectionCommand_) {
        clearDcbHover();
        updateHighlightedTarget(event->position());
        setAsdexCursor(CursorMode::Scope);
        update();
        event->accept();
        return;
    }

    if (dcbMouseCaptured_) {
        clearHighlightedTarget();
        if (isPointOverDcb(event->position()))
            updateDcbHover(event->position());
        else
            clearDcbHover();
        setAsdexCursor(CursorMode::Captured);
        event->accept();
        return;
    }

    if (isPointOverDcb(event->position())) {
        clearHighlightedTarget();
        updateDcbHover(event->position());
        setAsdexCursor(CursorMode::Dcb);
        event->accept();
        return;
    }

    if (isSelectingDbArea()) {
        clearHighlightedTarget();
        clearDcbHover();
        setAsdexCursor(dbAreaSelectableAt(event->position()) ? CursorMode::Select
                                                             : CursorMode::Scope);
        update();
        event->accept();
        return;
    }

    if (isDrawingDbArea()) {
        dbAreaDraftMouse_ = screenToWorldFeet(event->position(), framebufferRenderSize());
        clearHighlightedTarget();
        clearDcbHover();
        setAsdexCursor(CursorMode::Scope);
        update();
        event->accept();
        return;
    }

    clearDcbHover();
    setAsdexCursor(CursorMode::Scope);
    updateHighlightedTarget(event->position());
    update();
    QOpenGLWidget::mouseMoveEvent(event);
}

void AsdexScopeWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (suppressNextDbAreaSelectionRelease_ && event->button() != Qt::RightButton) {
        suppressNextDbAreaSelectionRelease_ = false;
        event->accept();
        return;
    }

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
        const bool rightClick = !rightDragMoved_;
        panning_ = false;
        rightDragMoved_ = false;
        releaseMouse();

        if (rightClick) {
            if (leaderDirectionCommand_) {
                clearDcbHover();
                updateHighlightedTarget(event->position());
                setAsdexCursor(CursorMode::Scope);
            } else if (isPointOverDcb(event->position())) {
                clearHighlightedTarget();
                updateDcbHover(event->position());
                updateHoverCursor(event->position());
            } else {
                clearDcbHover();
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
        clearDcbHover();
        event->accept();
        return;
    }

    if (dcbEntryCommand_) {
        if (event->button() == Qt::LeftButton
            && dcbEntryCommand_->type() != CommandType::DeleteAllDbAreas) {
            submitDcbEntryCommand();
        }

        event->accept();
        return;
    }

    if (leaderDirectionCommand_ && event->button() == Qt::LeftButton) {
        submitLeaderDirectionForTargetAt(event->position());
        event->accept();
        return;
    }

    if (dcbMouseCaptured_ && event->button() == Qt::LeftButton) {
        dcbMouseCaptured_ = false;
        clearHighlightedTarget();

        const DcbHit hit = dcb_.hitTest(event->position(), size(), asdexFont_, makeDcbState());
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

        if (isPointOverDcb(event->position())) {
            updateDcbHover(event->position());
            setAsdexCursor(CursorMode::Dcb);
        } else {
            clearDcbHover();
            setAsdexCursor(CursorMode::Scope);
        }
        update();
        event->accept();
        return;
    }

    if (isPointOverDcb(event->position())) {
        clearHighlightedTarget();
        updateDcbHover(event->position());
        setAsdexCursor(CursorMode::Dcb);
        update();
        event->accept();
        return;
    }

    if (isSelectingDbArea() && event->button() != Qt::RightButton) {
        event->accept();
        return;
    }

    if (isDrawingDbArea()
        && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier) {
        clearDcbHover();
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
        if (dcbEntryCommand_->type() == CommandType::DeleteAllDbAreas) {
            event->accept();
            return;
        }

        if (dcbEntryCommand_->type() == CommandType::DefineTraitAreaLeaderDirection
            || dcbEntryCommand_->type() == CommandType::ModifyTraitAreaLeaderDirection) {
            int value = 0;
            const int current = dcbEntryCommand_->valueInt(&value)
                ? value
                : selectedTraitLeaderDirectionValue();
            const int next = nextLeaderDirectionValue(current, wheelY > 0 ? 1 : -1);

            dcbEntryCommand_->setEntryValue(next);
            setSelectedTraitLeaderDirectionValue(next);

            clearDcbHover();
            setAsdexCursor(CursorMode::Hidden);
            update();
            event->accept();
            return;
        }

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
            case CommandType::DataBlockCharSize:
            case CommandType::DcbCharSize:
            case CommandType::CoastSuspendCharSize:
            case CommandType::TempDataCharSize:
            case CommandType::PreviewAreaCharSize:
            case CommandType::DefineTraitAreaDbCharSize:
            case CommandType::ModifyTraitAreaDbCharSize:
            case CommandType::DefineTraitAreaDbBrightness:
            case CommandType::ModifyTraitAreaDbBrightness:
            case CommandType::DefineTraitAreaLeaderLength:
            case CommandType::ModifyTraitAreaLeaderLength:
                steps = wheelY > 0 ? 1 : -1;
                break;
            case CommandType::Range:
            case CommandType::Brightness:
            case CommandType::CharSize:
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
                case CommandType::DataBlockCharSize:
                case CommandType::DcbCharSize:
                case CommandType::CoastSuspendCharSize:
                case CommandType::TempDataCharSize:
                case CommandType::PreviewAreaCharSize:
                    setCharSizeValue(dcbEntryCommand_->type(), value);
                    break;
                case CommandType::DefineTraitAreaDbCharSize:
                case CommandType::ModifyTraitAreaDbCharSize:
                    setSelectedTraitDbCharSizeValue(value);
                    break;
                case CommandType::DefineTraitAreaDbBrightness:
                case CommandType::ModifyTraitAreaDbBrightness:
                    setSelectedTraitDbBrightnessValue(value);
                    break;
                case CommandType::DefineTraitAreaLeaderLength:
                case CommandType::ModifyTraitAreaLeaderLength:
                    setSelectedTraitLeaderLengthValue(value);
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

    if (isPointOverDcb(event->position())) {
        const DcbHit hit = dcb_.hitTest(event->position(), size(), asdexFont_, makeDcbState());
        if (hit.function && handleDcbWheel(*hit.function, wheelY)) {
            updateDcbHover(event->position());
            setAsdexCursor(CursorMode::Dcb);
            update();
            event->accept();
            return;
        }

        updateDcbHover(event->position());
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
        zoomToCursorByFeet(deltaFeet, event->position());
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

    if (leaderDirectionCommand_) {
        switch (event->key()) {
            case Qt::Key_Escape:
                cancelLeaderDirectionKeyboardCommand();
                event->accept();
                return;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                submitLeaderDirectionForAll();
                event->accept();
                return;
            case Qt::Key_Backspace:
            case Qt::Key_Delete:
                leaderDirectionCommand_->value.clear();
                update();
                event->accept();
                return;
            default:
                break;
        }

        if (event->modifiers() == Qt::NoModifier && event->text().size() == 1) {
            const QChar c = event->text().at(0);
            if (c.isDigit()) {
                const int value = c.digitValue();
                if (value >= 1 && value <= 9) {
                    leaderDirectionCommand_->value = QString::number(value);
                    previewArea_.setSystemResponse(QString());
                    update();
                    event->accept();
                    return;
                }
            }
        }

        event->accept();
        return;
    }

    if ((commandType_ == CommandType::Brightness
         || commandType_ == CommandType::CharSize
         || commandType_ == CommandType::DbEdit
         || isDbAreaCommand(commandType_))
        && event->key() == Qt::Key_Escape) {
        cancelCommand();
        event->accept();
        return;
    }

    if (commandType_ == CommandType::None
        && event->modifiers() == Qt::NoModifier
        && event->text().size() == 1) {
        const QChar c = event->text().at(0);
        if (c.isDigit()) {
            const int value = c.digitValue();
            if (value >= 1 && value <= 9) {
                startLeaderDirectionKeyboardCommand(value);
                event->accept();
                return;
            }
        }
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

    if (leaderDirectionCommand_) {
        cancelLeaderDirectionKeyboardCommand();
        return;
    }

    if (isDbAreaCommand(commandType_)) {
        clearDbAreaDraft();
        suppressNextDbAreaSelectionRelease_ = false;

        if (commandType_ != CommandType::DbArea) {
            commandType_ = CommandType::DbArea;
            dcb_.setMenu(DcbMenu::DbArea);
        } else {
            commandType_ = CommandType::None;
            dcb_.setMenu(currentDcbMenu());
        }

        dcbEntryCommand_.reset();
        editingTrackId_.clear();
        previewArea_.setSystemResponse({});
        clearDcbHover();
        setAsdexCursor(CursorMode::Scope);
        clearHighlightedTarget();
        update();
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

    if (isCharSizeValueCommand(commandType_)
        || (dcbEntryCommand_ && isCharSizeValueCommand(dcbEntryCommand_->type()))) {
        commandType_ = CommandType::CharSize;
        dcbEntryCommand_.reset();
        editingTrackId_.clear();
        previewArea_.setSystemResponse({});
        dcb_.setMenu(DcbMenu::CharSize);
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
    suppressNextDbAreaSelectionRelease_ = false;
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
        if (isBrightnessValueCommand(dcbEntryCommand_->type())) {
            commandType_ = CommandType::Brightness;
        } else if (isCharSizeValueCommand(dcbEntryCommand_->type())) {
            commandType_ = CommandType::CharSize;
        } else if (dcbEntryCommand_->type() == CommandType::DeleteAllDbAreas) {
            commandType_ = CommandType::DbArea;
        } else if (isTraitAreaValueCommand(dcbEntryCommand_->type())) {
            commandType_ = CommandType::DefineTraitAreaTraits;
        } else if (isModifyTraitAreaValueCommand(dcbEntryCommand_->type())) {
            commandType_ = CommandType::ModifyTraitAreaTraits;
        } else {
            commandType_ = CommandType::None;
        }
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
        case CommandType::DataBlockCharSize:
        case CommandType::DcbCharSize:
        case CommandType::CoastSuspendCharSize:
        case CommandType::TempDataCharSize:
        case CommandType::PreviewAreaCharSize:
            setCharSizeValue(dcbEntryCommand_->type(), value);
            commandType_ = CommandType::CharSize;
            break;
        case CommandType::DeleteAllDbAreas:
            if (value == 2) {
                dbAreaStore_.clear();
                dbOffAreaDatablockOverride_.clear();
                selectedDbAreaId_.clear();
                clearDbAreaDraft();
            }
            commandType_ = CommandType::DbArea;
            break;
        case CommandType::DefineTraitAreaDbCharSize:
            setSelectedTraitDbCharSizeValue(value);
            commandType_ = CommandType::DefineTraitAreaTraits;
            break;
        case CommandType::ModifyTraitAreaDbCharSize:
            setSelectedTraitDbCharSizeValue(value);
            commandType_ = CommandType::ModifyTraitAreaTraits;
            break;
        case CommandType::DefineTraitAreaDbBrightness:
            setSelectedTraitDbBrightnessValue(value);
            commandType_ = CommandType::DefineTraitAreaTraits;
            break;
        case CommandType::ModifyTraitAreaDbBrightness:
            setSelectedTraitDbBrightnessValue(value);
            commandType_ = CommandType::ModifyTraitAreaTraits;
            break;
        case CommandType::DefineTraitAreaLeaderLength:
            setSelectedTraitLeaderLengthValue(value);
            commandType_ = CommandType::DefineTraitAreaTraits;
            break;
        case CommandType::ModifyTraitAreaLeaderLength:
            setSelectedTraitLeaderLengthValue(value);
            commandType_ = CommandType::ModifyTraitAreaTraits;
            break;
        case CommandType::DefineTraitAreaLeaderDirection:
            if (value == 5) {
                previewArea_.setSystemResponse(QStringLiteral("INVALID ENTRY"));
                commandType_ = CommandType::DefineTraitAreaTraits;
                dcb_.setMenu(currentDcbMenu());
                dcbEntryCommand_.reset();
                clearDcbHover();
                setAsdexCursor(CursorMode::Scope);
                update();
                return;
            }
            setSelectedTraitLeaderDirectionValue(value);
            commandType_ = CommandType::DefineTraitAreaTraits;
            break;
        case CommandType::ModifyTraitAreaLeaderDirection:
            if (value == 5) {
                previewArea_.setSystemResponse(QStringLiteral("INVALID ENTRY"));
                commandType_ = CommandType::ModifyTraitAreaTraits;
                dcb_.setMenu(currentDcbMenu());
                dcbEntryCommand_.reset();
                clearDcbHover();
                setAsdexCursor(CursorMode::Scope);
                update();
                return;
            }
            setSelectedTraitLeaderDirectionValue(value);
            commandType_ = CommandType::ModifyTraitAreaTraits;
            break;
        case CommandType::None:
        case CommandType::EditDatablockFields:
        case CommandType::MapReposition:
        case CommandType::Brightness:
        case CommandType::CharSize:
        case CommandType::LeaderDirection:
        case CommandType::DbArea:
        case CommandType::DefineTraitArea:
        case CommandType::DefineTraitAreaTraits:
        case CommandType::DefineOffArea:
        case CommandType::ModifyTraitArea:
        case CommandType::ModifyTraitAreaTraits:
        case CommandType::DeleteOneDbArea:
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

    if (commandType_ == CommandType::CharSize || isCharSizeValueCommand(commandType_))
        return DcbMenu::CharSize;

    if (commandType_ == CommandType::DefineTraitAreaTraits
        || commandType_ == CommandType::DefineTraitAreaDbCharSize
        || commandType_ == CommandType::DefineTraitAreaDbBrightness
        || commandType_ == CommandType::DefineTraitAreaLeaderLength
        || commandType_ == CommandType::DefineTraitAreaLeaderDirection) {
        return DcbMenu::DefineTraitArea;
    }

    if (commandType_ == CommandType::ModifyTraitAreaTraits
        || commandType_ == CommandType::ModifyTraitAreaDbCharSize
        || commandType_ == CommandType::ModifyTraitAreaDbBrightness
        || commandType_ == CommandType::ModifyTraitAreaLeaderLength
        || commandType_ == CommandType::ModifyTraitAreaLeaderDirection) {
        return DcbMenu::ModifyTraitArea;
    }

    if (isDbAreaCommand(commandType_)) return DcbMenu::DbArea;

    if (commandType_ == CommandType::DbEdit) return DcbMenu::DbEdit;

    return DcbMenu::Main;
}

bool AsdexScopeWidget::defaultDataBlockVisibleForTarget(const AsdexTarget& target) const {
    Q_UNUSED(target);
    return showDataBlocks_;
}

bool AsdexScopeWidget::targetInsideDbOffArea(const AsdexTarget& target) const {
    return dbAreaStore_.pointInsideOffArea(target.positionFeet);
}

bool AsdexScopeWidget::isDataBlockVisible(const AsdexTarget& target) const {
    if (targetInsideDbOffArea(target)) {
        return dbOffAreaDatablockOverride_.value(target.id, false);
    }

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
    if (targetInsideDbOffArea(target)) {
        const bool current = dbOffAreaDatablockOverride_.value(target.id, false);
        dbOffAreaDatablockOverride_[target.id] = !current;
        return;
    }

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
    if (currentDcbMenu() == DcbMenu::DefineTraitArea
        || currentDcbMenu() == DcbMenu::ModifyTraitArea) {
        switch (function) {
            case DcbFunction::DbFullPart:
            case DcbFunction::DbAltitudeOnOff:
            case DcbFunction::DbTypeOnOff:
            case DcbFunction::DbSensorsOnOff:
            case DcbFunction::DbCategoryOnOff:
            case DcbFunction::DbFixOnOff:
            case DcbFunction::DbVelocityOnOff:
            case DcbFunction::DbScratchpadOnOff:
                toggleSelectedTraitDbField(function);
                return;
            case DcbFunction::DbAreaVectorOnOff:
                toggleSelectedTraitVector();
                return;
            case DcbFunction::DataBlockCharSize:
                startTraitAreaDbCharSizeCommand();
                return;
            case DcbFunction::DataBlocksBrightness:
                startTraitAreaDbBrightnessCommand();
                return;
            case DcbFunction::DbAreaLeaderLength:
                startTraitAreaLeaderLengthCommand();
                return;
            case DcbFunction::DbAreaLeaderDirection:
                startTraitAreaLeaderDirectionCommand();
                return;
            case DcbFunction::Done:
                handleDcbDone();
                return;
            default:
                break;
        }
    }

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
        case DcbFunction::DataBlockArea:
            startDbAreaMenu();
            return;
        case DcbFunction::DataBlockEdit:
            startDbEditMenu();
            return;
        case DcbFunction::Brightness:
            startBrightnessMenu();
            return;
        case DcbFunction::CharSize:
            startCharSizeMenu();
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
        case DcbFunction::DataBlockCharSize:
        case DcbFunction::DcbCharSize:
        case DcbFunction::CoastSuspendCharSize:
        case DcbFunction::TempDataCharSize:
        case DcbFunction::PreviewAreaCharSize:
            startCharSizeValueCommand(function);
            return;
        case DcbFunction::DefineDbTraitArea:
            startDefineTraitAreaCommand();
            return;
        case DcbFunction::DefineDbOffArea:
            startDefineOffAreaCommand();
            return;
        case DcbFunction::ModifyDbTraitArea:
            startModifyTraitAreaCommand();
            return;
        case DcbFunction::DeleteAllDbAreas:
            startDeleteAllDbAreasCommand();
            return;
        case DcbFunction::DeleteOneDbArea:
            startDeleteOneDbAreaCommand();
            return;
        case DcbFunction::DbFullPart:
        case DcbFunction::DbAltitudeOnOff:
        case DcbFunction::DbTypeOnOff:
        case DcbFunction::DbSensorsOnOff:
        case DcbFunction::DbCategoryOnOff:
        case DcbFunction::DbFixOnOff:
        case DcbFunction::DbVelocityOnOff:
        case DcbFunction::DbScratchpadOnOff:
            toggleGlobalDbEditField(function);
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

void AsdexScopeWidget::startCharSizeMenu() {
    if (commandType_ != CommandType::None) return;

    commandType_ = CommandType::CharSize;
    dcb_.setMenu(DcbMenu::CharSize);
    datablockEdit_.reset();
    dcbEntryCommand_.reset();
    editingTrackId_.clear();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
}

void AsdexScopeWidget::startCharSizeValueCommand(DcbFunction function) {
    const CommandType type = commandForCharSizeFunction(function);
    if (type == CommandType::None) return;

    commandType_ = type;
    dcb_.setMenu(DcbMenu::CharSize);
    dcbEntryCommand_ =
        DcbEntryCommand::charSize(type, charSizeCommandLabel(type), charSizeValue(type));
    datablockEdit_.reset();
    editingTrackId_.clear();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Hidden);
    update();
}

void AsdexScopeWidget::startDbAreaMenu() {
    if (commandType_ != CommandType::None) return;

    commandType_ = CommandType::DbArea;
    dcb_.setMenu(DcbMenu::DbArea);
    datablockEdit_.reset();
    dcbEntryCommand_.reset();
    editingTrackId_.clear();
    clearDbAreaDraft();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
}

void AsdexScopeWidget::startDbEditMenu() {
    if (commandType_ != CommandType::None) return;

    commandType_ = CommandType::DbEdit;
    dcb_.setMenu(DcbMenu::DbEdit);
    datablockEdit_.reset();
    dcbEntryCommand_.reset();
    editingTrackId_.clear();
    clearDbAreaDraft();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
}

void AsdexScopeWidget::startDefineTraitAreaCommand() {
    commandType_ = CommandType::DefineTraitArea;
    dcb_.setMenu(DcbMenu::DbArea);
    selectedDbAreaId_.clear();
    clearDbAreaDraft();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
}

void AsdexScopeWidget::startTraitAreaDbCharSizeCommand() {
    const DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return;

    const bool modify = currentDcbMenu() == DcbMenu::ModifyTraitArea;
    commandType_ = modify ? CommandType::ModifyTraitAreaDbCharSize
                          : CommandType::DefineTraitAreaDbCharSize;
    dcb_.setMenu(modify ? DcbMenu::ModifyTraitArea : DcbMenu::DefineTraitArea);
    dcbEntryCommand_ = modify
        ? DcbEntryCommand::modifyTraitAreaDbCharSize(selectedTraitDbCharSizeValue())
        : DcbEntryCommand::traitAreaDbCharSize(selectedTraitDbCharSizeValue());
    datablockEdit_.reset();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Hidden);
    update();
}

void AsdexScopeWidget::startTraitAreaDbBrightnessCommand() {
    const DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return;

    const bool modify = currentDcbMenu() == DcbMenu::ModifyTraitArea;
    commandType_ = modify ? CommandType::ModifyTraitAreaDbBrightness
                          : CommandType::DefineTraitAreaDbBrightness;
    dcb_.setMenu(modify ? DcbMenu::ModifyTraitArea : DcbMenu::DefineTraitArea);
    dcbEntryCommand_ = modify
        ? DcbEntryCommand::modifyTraitAreaDbBrightness(selectedTraitDbBrightnessValue())
        : DcbEntryCommand::traitAreaDbBrightness(selectedTraitDbBrightnessValue());
    datablockEdit_.reset();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Hidden);
    update();
}

void AsdexScopeWidget::startTraitAreaLeaderLengthCommand() {
    const DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return;

    const bool modify = currentDcbMenu() == DcbMenu::ModifyTraitArea;
    commandType_ = modify ? CommandType::ModifyTraitAreaLeaderLength
                          : CommandType::DefineTraitAreaLeaderLength;
    dcb_.setMenu(modify ? DcbMenu::ModifyTraitArea : DcbMenu::DefineTraitArea);
    dcbEntryCommand_ = modify
        ? DcbEntryCommand::modifyTraitAreaLeaderLength(selectedTraitLeaderLengthValue())
        : DcbEntryCommand::traitAreaLeaderLength(selectedTraitLeaderLengthValue());
    datablockEdit_.reset();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Hidden);
    update();
}

void AsdexScopeWidget::startTraitAreaLeaderDirectionCommand() {
    const DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return;

    const bool modify = currentDcbMenu() == DcbMenu::ModifyTraitArea;
    commandType_ = modify ? CommandType::ModifyTraitAreaLeaderDirection
                          : CommandType::DefineTraitAreaLeaderDirection;
    dcb_.setMenu(modify ? DcbMenu::ModifyTraitArea : DcbMenu::DefineTraitArea);
    dcbEntryCommand_ = modify
        ? DcbEntryCommand::modifyTraitAreaLeaderDirection(selectedTraitLeaderDirectionValue())
        : DcbEntryCommand::traitAreaLeaderDirection(selectedTraitLeaderDirectionValue());
    datablockEdit_.reset();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Hidden);
    update();
}

void AsdexScopeWidget::startDefineOffAreaCommand() {
    commandType_ = CommandType::DefineOffArea;
    dcb_.setMenu(DcbMenu::DbArea);
    clearDbAreaDraft();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
}

void AsdexScopeWidget::startModifyTraitAreaCommand() {
    commandType_ = CommandType::ModifyTraitArea;
    dcb_.setMenu(DcbMenu::DbArea);
    selectedDbAreaId_.clear();
    clearDbAreaDraft();
    datablockEdit_.reset();
    dcbEntryCommand_.reset();
    editingTrackId_.clear();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
}

void AsdexScopeWidget::startDeleteAllDbAreasCommand() {
    commandType_ = CommandType::DeleteAllDbAreas;
    dcb_.setMenu(DcbMenu::DbArea);
    dcbEntryCommand_ = DcbEntryCommand::deleteAllDbAreas();
    datablockEdit_.reset();
    clearDbAreaDraft();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Hidden);
    update();
}

void AsdexScopeWidget::startDeleteOneDbAreaCommand() {
    commandType_ = CommandType::DeleteOneDbArea;
    dcb_.setMenu(DcbMenu::DbArea);
    clearDbAreaDraft();
    clearHighlightedTarget();
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
}

bool AsdexScopeWidget::isDrawingDbArea() const {
    return commandType_ == CommandType::DefineOffArea
        || commandType_ == CommandType::DefineTraitArea;
}

bool AsdexScopeWidget::isSelectingDbArea() const {
    return commandType_ == CommandType::DeleteOneDbArea
        || commandType_ == CommandType::ModifyTraitArea;
}

bool AsdexScopeWidget::dbAreaSelectableAt(const QPointF& logicalPoint) const {
    if (!isSelectingDbArea()) return false;

    const QPointF world = screenToWorldFeet(logicalPoint, framebufferRenderSize());
    if (commandType_ == CommandType::DeleteOneDbArea) {
        return dbAreaStore_.indexOfAreaContaining(world, true) >= 0;
    }

    if (commandType_ == CommandType::ModifyTraitArea) {
        return dbAreaStore_.indexOfAreaContaining(world, false) >= 0;
    }

    return false;
}

bool AsdexScopeWidget::deleteDbAreaAt(const QPointF& logicalPoint) {
    if (commandType_ != CommandType::DeleteOneDbArea) return false;

    const QPointF world = screenToWorldFeet(logicalPoint, framebufferRenderSize());
    if (!dbAreaStore_.removeAreaContaining(world, true)) {
        update();
        return false;
    }

    dbOffAreaDatablockOverride_.clear();
    selectedDbAreaId_.clear();
    commandType_ = CommandType::DbArea;
    dcb_.setMenu(DcbMenu::DbArea);
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
    return true;
}

bool AsdexScopeWidget::selectTraitAreaAt(const QPointF& logicalPoint) {
    if (commandType_ != CommandType::ModifyTraitArea) return false;

    const QPointF world = screenToWorldFeet(logicalPoint, framebufferRenderSize());
    const int index = dbAreaStore_.indexOfAreaContaining(world, false);
    if (index < 0) {
        update();
        return false;
    }

    DbArea* area = dbAreaStore_.areaAt(index);
    if (!area || area->kind != DbAreaKind::Trait) {
        update();
        return false;
    }

    selectedDbAreaId_ = area->id;
    commandType_ = CommandType::ModifyTraitAreaTraits;
    dcb_.setMenu(DcbMenu::ModifyTraitArea);
    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
    return true;
}

bool AsdexScopeWidget::showsDbAreas() const {
    return isDbAreaCommand(commandType_);
}

DbArea* AsdexScopeWidget::selectedDbArea() {
    if (selectedDbAreaId_.isEmpty()) return nullptr;
    return dbAreaStore_.areaById(selectedDbAreaId_);
}

const DbArea* AsdexScopeWidget::selectedDbArea() const {
    if (selectedDbAreaId_.isEmpty()) return nullptr;
    return dbAreaStore_.areaById(selectedDbAreaId_);
}

void AsdexScopeWidget::selectDbArea(const QString& id) {
    selectedDbAreaId_ = id;
}

const DbArea* AsdexScopeWidget::traitAreaForTarget(const AsdexTarget& target) const {
    for (const DbArea& area : dbAreaStore_.areas()) {
        if (area.kind == DbAreaKind::Trait
            && pointInPolygon(area.polygonFeet, target.positionFeet)) {
            return &area;
        }
    }

    return nullptr;
}

bool AsdexScopeWidget::vectorVisibleForTarget(const AsdexTarget& target) const {
    if (const DbArea* area = traitAreaForTarget(target)) {
        return area->traits.showVector;
    }

    return showVectorLine_;
}

DataBlockSettings AsdexScopeWidget::dataBlockSettingsForTarget(
    const AsdexTarget& target) const {
    DataBlockSettings settings;
    settings.showDataBlocks = showDataBlocks_;
    settings.fullDataBlocks = fullDataBlocks_;
    settings.fontSize = dataBlockCharSize_;
    settings.brightness = dataBlocksBrightness_;
    settings.leaderLength = currentLeaderLengthValue();
    settings.leaderDirection = leaderDirection_;
    settings.timesharePrimary = timesharePrimary_;
    settings.alertInProgress = false;
    settings.showAltitude = showAltitudeInDb_;
    settings.showAircraftType = showAircraftTypeInDb_;
    settings.showSensors = showSensorsInDb_;
    settings.showAircraftCategory = showAircraftCategoryInDb_;
    settings.showFix = showFixInDb_;
    settings.showVelocity = showVelocityInDb_;
    settings.showScratchpads = showScratchpadsInDb_;

    if (const DbArea* area = traitAreaForTarget(target)) {
        settings.fullDataBlocks = area->traits.fullDataBlocks;
        settings.fontSize = area->traits.dataBlockCharSize;
        settings.brightness = area->traits.dataBlockBrightness;
        settings.leaderLength = area->traits.leaderLength;
        settings.leaderDirection = area->traits.leaderDirection;
        settings.showAltitude = area->traits.showAltitude;
        settings.showAircraftType = area->traits.showAircraftType;
        settings.showSensors = area->traits.showSensors;
        settings.showAircraftCategory = area->traits.showAircraftCategory;
        settings.showFix = area->traits.showFix;
        settings.showVelocity = area->traits.showVelocity;
        settings.showScratchpads = area->traits.showScratchpads;
    }

    const int overrideValue = targetLeaderDirectionOverrides_.value(target.id, -1);
    if (isValidLeaderDirectionValue(overrideValue)) {
        settings.leaderDirection = leaderDirectionFromDcbValue(overrideValue);
    }

    return settings;
}

int AsdexScopeWidget::selectedTraitDbCharSizeValue() const {
    const DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return 2;
    return std::clamp(area->traits.dataBlockCharSize, 1, 6);
}

int AsdexScopeWidget::selectedTraitDbBrightnessValue() const {
    const DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return 95;
    return std::clamp(area->traits.dataBlockBrightness, 1, 99);
}

int AsdexScopeWidget::selectedTraitLeaderLengthValue() const {
    const DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return 2;
    return std::clamp(area->traits.leaderLength, 0, 15);
}

int AsdexScopeWidget::selectedTraitLeaderDirectionValue() const {
    const DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return 9;
    return leaderDirectionDcbValue(area->traits.leaderDirection);
}

void AsdexScopeWidget::setSelectedTraitDbCharSizeValue(int value) {
    DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return;

    area->traits.dataBlockCharSize = std::clamp(value, 1, 6);
    update();
}

void AsdexScopeWidget::setSelectedTraitDbBrightnessValue(int value) {
    DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return;

    area->traits.dataBlockBrightness = std::clamp(value, 1, 99);
    update();
}

void AsdexScopeWidget::setSelectedTraitLeaderLengthValue(int value) {
    DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return;

    area->traits.leaderLength = std::clamp(value, 0, 15);
    update();
}

void AsdexScopeWidget::setSelectedTraitLeaderDirectionValue(int value) {
    if (value < 1 || value > 9 || value == 5) return;

    DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return;

    area->traits.leaderDirection = leaderDirectionFromDcbValue(value);
    update();
}

LeaderDirection AsdexScopeWidget::leaderDirectionFromDcbValue(int value) const {
    return leaderDirectionFromDcbValueRaw(value);
}

int AsdexScopeWidget::nextLeaderDirectionValue(int current, int step) const {
    static constexpr int values[] = {1, 2, 3, 4, 6, 7, 8, 9};
    constexpr int n = sizeof(values) / sizeof(values[0]);
    int index = n - 1;

    for (int i = 0; i < n; ++i) {
        if (values[i] == current) {
            index = i;
            break;
        }
    }

    index = (index + step) % n;
    if (index < 0) index += n;
    return values[index];
}

void AsdexScopeWidget::toggleGlobalDbEditField(DcbFunction function) {
    switch (function) {
        case DcbFunction::DbFullPart:
            fullDataBlocks_ = !fullDataBlocks_;
            break;
        case DcbFunction::DbAltitudeOnOff:
            showAltitudeInDb_ = !showAltitudeInDb_;
            break;
        case DcbFunction::DbTypeOnOff:
            showAircraftTypeInDb_ = !showAircraftTypeInDb_;
            break;
        case DcbFunction::DbSensorsOnOff:
            showSensorsInDb_ = !showSensorsInDb_;
            break;
        case DcbFunction::DbCategoryOnOff:
            showAircraftCategoryInDb_ = !showAircraftCategoryInDb_;
            break;
        case DcbFunction::DbFixOnOff:
            showFixInDb_ = !showFixInDb_;
            break;
        case DcbFunction::DbVelocityOnOff:
            showVelocityInDb_ = !showVelocityInDb_;
            break;
        case DcbFunction::DbScratchpadOnOff:
            showScratchpadsInDb_ = !showScratchpadsInDb_;
            break;
        default:
            return;
    }

    clearDcbHover();
    update();
}

void AsdexScopeWidget::toggleSelectedTraitDbField(DcbFunction function) {
    DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return;

    switch (function) {
        case DcbFunction::DbFullPart:
            area->traits.fullDataBlocks = !area->traits.fullDataBlocks;
            break;
        case DcbFunction::DbAltitudeOnOff:
            area->traits.showAltitude = !area->traits.showAltitude;
            break;
        case DcbFunction::DbTypeOnOff:
            area->traits.showAircraftType = !area->traits.showAircraftType;
            break;
        case DcbFunction::DbSensorsOnOff:
            area->traits.showSensors = !area->traits.showSensors;
            break;
        case DcbFunction::DbCategoryOnOff:
            area->traits.showAircraftCategory = !area->traits.showAircraftCategory;
            break;
        case DcbFunction::DbFixOnOff:
            area->traits.showFix = !area->traits.showFix;
            break;
        case DcbFunction::DbVelocityOnOff:
            area->traits.showVelocity = !area->traits.showVelocity;
            break;
        case DcbFunction::DbScratchpadOnOff:
            area->traits.showScratchpads = !area->traits.showScratchpads;
            break;
        default:
            return;
    }

    clearDcbHover();
    update();
}

void AsdexScopeWidget::toggleSelectedTraitVector() {
    DbArea* area = selectedDbArea();
    if (!area || area->kind != DbAreaKind::Trait) return;

    area->traits.showVector = !area->traits.showVector;
    clearDcbHover();
    update();
}

std::optional<DcbFunction> AsdexScopeWidget::activeDcbFunctionForCommand() const {
    switch (commandType_) {
        case CommandType::Brightness:
            return DcbFunction::Brightness;
        case CommandType::CharSize:
            return DcbFunction::CharSize;
        case CommandType::DbArea:
            return DcbFunction::DataBlockArea;
        case CommandType::DefineTraitArea:
            return DcbFunction::DefineDbTraitArea;
        case CommandType::DefineOffArea:
            return DcbFunction::DefineDbOffArea;
        case CommandType::DeleteOneDbArea:
            return DcbFunction::DeleteOneDbArea;
        case CommandType::DbEdit:
            return DcbFunction::DataBlockEdit;
        default:
            return std::nullopt;
    }
}

void AsdexScopeWidget::addDbAreaPoint(const QPointF& worldFeet) {
    if (dbAreaDraftWouldSelfIntersect(worldFeet)
        || dbAreaDraftWouldOverlapExisting(worldFeet)) {
        update();
        return;
    }

    dbAreaDraftPoints_.push_back(worldFeet);
    dbAreaDraftMouse_.reset();

    if (dbAreaDraftPoints_.size() >= 20) {
        completeDbAreaPolygon();
        return;
    }

    update();
}

void AsdexScopeWidget::completeDbAreaPolygon() {
    if (dbAreaDraftPoints_.size() < 3) {
        clearDbAreaDraft();
        previewArea_.setSystemResponse(QStringLiteral("BAD POLYGON,REDRAW POINT"));
        update();
        return;
    }

    if (!dbAreaPolygonIsValidOnClose()) {
        update();
        return;
    }

    QVector<QPointF> closed = dbAreaDraftPoints_;
    closed.push_back(closed.first());

    const bool isTrait = commandType_ == CommandType::DefineTraitArea;

    DbArea area;
    area.id = QStringLiteral("%1-%2")
                  .arg(isTrait ? QStringLiteral("db-trait") : QStringLiteral("db-off"),
                       QUuid::createUuid().toString(QUuid::WithoutBraces));
    area.kind = isTrait ? DbAreaKind::Trait : DbAreaKind::Off;
    area.polygonFeet = std::move(closed);

    if (isTrait) {
        area.traits.dataBlocksOff = false;
        area.traits.fullDataBlocks = true;
        area.traits.showAltitude = false;
        area.traits.showAircraftType = true;
        area.traits.showSensors = false;
        area.traits.showAircraftCategory = false;
        area.traits.showFix = true;
        area.traits.showVelocity = false;
        area.traits.showScratchpads = true;
        area.traits.dataBlockCharSize = 2;
        area.traits.dataBlockBrightness = 95;
        area.traits.showVector = true;
        area.traits.leaderLength = 2;
        area.traits.leaderDirection = LeaderDirection::NE;
    } else {
        area.traits.dataBlocksOff = true;
    }

    const QString newId = area.id;
    dbAreaStore_.add(std::move(area));
    selectDbArea(newId);
    clearDbAreaDraft();

    if (isTrait) {
        commandType_ = CommandType::DefineTraitAreaTraits;
        dcb_.setMenu(DcbMenu::DefineTraitArea);
    } else {
        commandType_ = CommandType::DbArea;
        dcb_.setMenu(DcbMenu::DbArea);
    }

    previewArea_.setSystemResponse(QString());
    update();
}

void AsdexScopeWidget::clearDbAreaDraft() {
    dbAreaDraftPoints_.clear();
    dbAreaDraftMouse_.reset();
}

bool AsdexScopeWidget::dbAreaDraftWouldSelfIntersect(const QPointF& nextPoint) const {
    if (dbAreaDraftPoints_.size() < 2) return false;

    const QPointF a = dbAreaDraftPoints_.last();
    const QPointF b = nextPoint;

    for (int i = 0; i + 1 < dbAreaDraftPoints_.size() - 1; ++i) {
        if (utils::lineSegmentsIntersect(a, b, dbAreaDraftPoints_[i], dbAreaDraftPoints_[i + 1])) {
            return true;
        }
    }

    return false;
}

bool AsdexScopeWidget::dbAreaDraftWouldOverlapExisting(const QPointF& nextPoint) const {
    if (dbAreaStore_.firstAreaContaining(nextPoint)) return true;
    if (dbAreaDraftPoints_.isEmpty()) return false;

    const QPointF a = dbAreaDraftPoints_.last();
    const QPointF b = nextPoint;

    for (const DbArea& area : dbAreaStore_.areas()) {
        const QVector<QPointF>& polygon = area.polygonFeet;
        for (int i = 0; i + 1 < polygon.size(); ++i) {
            if (utils::lineSegmentsIntersect(a, b, polygon[i], polygon[i + 1])) {
                return true;
            }
        }
    }

    return false;
}

bool AsdexScopeWidget::dbAreaPolygonIsValidOnClose() const {
    if (dbAreaDraftPoints_.size() < 3) return false;

    const QPointF a = dbAreaDraftPoints_.last();
    const QPointF b = dbAreaDraftPoints_.first();

    for (int i = 0; i + 1 < dbAreaDraftPoints_.size() - 1; ++i) {
        if (i == 0) continue;
        if (utils::lineSegmentsIntersect(a, b, dbAreaDraftPoints_[i], dbAreaDraftPoints_[i + 1])) {
            return false;
        }
    }

    for (const DbArea& area : dbAreaStore_.areas()) {
        const QVector<QPointF>& polygon = area.polygonFeet;
        for (int i = 0; i + 1 < polygon.size(); ++i) {
            if (utils::lineSegmentsIntersect(a, b, polygon[i], polygon[i + 1])) {
                return false;
            }
        }
    }

    QVector<QPointF> closed = dbAreaDraftPoints_;
    closed.push_back(closed.first());

    for (const DbArea& area : dbAreaStore_.areas()) {
        for (const QPointF& point : area.polygonFeet) {
            if (pointInPolygon(closed, point)) return false;
        }
    }

    return true;
}

void AsdexScopeWidget::handleDcbDone() {
    if (commandType_ == CommandType::DefineTraitAreaTraits
        || commandType_ == CommandType::DefineTraitAreaDbCharSize
        || commandType_ == CommandType::DefineTraitAreaDbBrightness
        || commandType_ == CommandType::DefineTraitAreaLeaderLength
        || commandType_ == CommandType::DefineTraitAreaLeaderDirection
        || commandType_ == CommandType::ModifyTraitAreaTraits
        || commandType_ == CommandType::ModifyTraitAreaDbCharSize
        || commandType_ == CommandType::ModifyTraitAreaDbBrightness
        || commandType_ == CommandType::ModifyTraitAreaLeaderLength
        || commandType_ == CommandType::ModifyTraitAreaLeaderDirection) {
        commandType_ = CommandType::DbArea;
        dcb_.setMenu(DcbMenu::DbArea);
        dcbEntryCommand_.reset();
        datablockEdit_.reset();
        editingTrackId_.clear();
        clearDbAreaDraft();
        clearDcbHover();
        previewArea_.setSystemResponse(QString());
        setAsdexCursor(CursorMode::Scope);
        update();
        return;
    }

    commandType_ = CommandType::None;
    dcb_.setMenu(currentDcbMenu());
    dcbEntryCommand_.reset();
    datablockEdit_.reset();
    editingTrackId_.clear();
    clearDbAreaDraft();
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

CommandType AsdexScopeWidget::commandForCharSizeFunction(DcbFunction function) const {
    switch (function) {
        case DcbFunction::DataBlockCharSize:
            return CommandType::DataBlockCharSize;
        case DcbFunction::DcbCharSize:
            return CommandType::DcbCharSize;
        case DcbFunction::CoastSuspendCharSize:
            return CommandType::CoastSuspendCharSize;
        case DcbFunction::TempDataCharSize:
            return CommandType::TempDataCharSize;
        case DcbFunction::PreviewAreaCharSize:
            return CommandType::PreviewAreaCharSize;
        default:
            return CommandType::None;
    }
}

QString AsdexScopeWidget::charSizeCommandLabel(CommandType type) const {
    switch (type) {
        case CommandType::DataBlockCharSize:
            return QStringLiteral("DATA BLOCK");
        case CommandType::DcbCharSize:
            return QStringLiteral("DCB");
        case CommandType::CoastSuspendCharSize:
            return QStringLiteral("CS LIST");
        case CommandType::TempDataCharSize:
            return QStringLiteral("TEMP DATA");
        case CommandType::PreviewAreaCharSize:
            return QStringLiteral("PREVIEW");
        default:
            return {};
    }
}

int AsdexScopeWidget::charSizeValue(CommandType type) const {
    switch (type) {
        case CommandType::DataBlockCharSize:
            return dataBlockCharSize_;
        case CommandType::DcbCharSize:
            return dcbCharSize_;
        case CommandType::CoastSuspendCharSize:
            return coastSuspendCharSize_;
        case CommandType::TempDataCharSize:
            return tempDataCharSize_;
        case CommandType::PreviewAreaCharSize:
            return previewAreaCharSize_;
        default:
            return 2;
    }
}

void AsdexScopeWidget::setCharSizeValue(CommandType type, int value) {
    const int maxValue = type == CommandType::DcbCharSize ? 3 : 6;
    value = std::clamp(value, 1, maxValue);

    switch (type) {
        case CommandType::DataBlockCharSize:
            dataBlockCharSize_ = value;
            break;
        case CommandType::DcbCharSize:
            dcbCharSize_ = value;
            dcb_.setCharSize(value);
            break;
        case CommandType::CoastSuspendCharSize:
            coastSuspendCharSize_ = value;
            break;
        case CommandType::TempDataCharSize:
            tempDataCharSize_ = value;
            break;
        case CommandType::PreviewAreaCharSize:
            previewAreaCharSize_ = value;
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

int AsdexScopeWidget::currentLeaderDirectionValue() const {
    return leaderDirectionDcbValue(leaderDirection_);
}

void AsdexScopeWidget::setLeaderDirectionValue(int value) {
    if (!isValidLeaderDirectionValue(value)) return;

    leaderDirection_ = leaderDirectionFromDcbValue(value);
    update();
}

bool AsdexScopeWidget::leaderDirectionCommandActive() const {
    return leaderDirectionCommand_.has_value();
}

bool AsdexScopeWidget::isValidLeaderDirectionValue(int value) const {
    return value >= 1 && value <= 9 && value != 5;
}

void AsdexScopeWidget::startLeaderDirectionKeyboardCommand(int value) {
    if (datablockEdit_ || dcbEntryCommand_ || isMapRepositionCommandActive()) return;
    if (commandType_ != CommandType::None) return;

    LeaderDirectionKeyboardCommand command;
    command.value = QString::number(value);
    leaderDirectionCommand_ = std::move(command);
    commandType_ = CommandType::LeaderDirection;

    clearDcbHover();
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
}

void AsdexScopeWidget::cancelLeaderDirectionKeyboardCommand() {
    leaderDirectionCommand_.reset();
    commandType_ = CommandType::None;
    clearDcbHover();
    setAsdexCursor(CursorMode::Scope);
    update();
}

void AsdexScopeWidget::submitLeaderDirectionForAll() {
    if (!leaderDirectionCommand_) return;

    int value = 0;
    if (!leaderDirectionCommand_->valueInt(&value) || !isValidLeaderDirectionValue(value)) {
        previewArea_.setSystemResponse(QStringLiteral("INVALID ENTRY"));
        leaderDirectionCommand_.reset();
        commandType_ = CommandType::None;
        setAsdexCursor(CursorMode::Scope);
        update();
        return;
    }

    leaderDirection_ = leaderDirectionFromDcbValue(value);
    targetLeaderDirectionOverrides_.clear();
    leaderDirectionCommand_.reset();
    commandType_ = CommandType::None;
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
}

void AsdexScopeWidget::submitLeaderDirectionForTargetAt(const QPointF& logicalPoint) {
    if (!leaderDirectionCommand_) return;

    int value = 0;
    if (!leaderDirectionCommand_->valueInt(&value) || !isValidLeaderDirectionValue(value)) {
        previewArea_.setSystemResponse(QStringLiteral("INVALID ENTRY"));
        leaderDirectionCommand_.reset();
        commandType_ = CommandType::None;
        setAsdexCursor(CursorMode::Scope);
        update();
        return;
    }

    updateHighlightedTarget(logicalPoint);
    AsdexTarget* target = highlightedTarget();
    if (!target) {
        setAsdexCursor(CursorMode::Scope);
        update();
        return;
    }

    targetLeaderDirectionOverrides_.insert(target->id, value);
    leaderDirectionCommand_.reset();
    commandType_ = CommandType::None;
    previewArea_.setSystemResponse(QString());
    setAsdexCursor(CursorMode::Scope);
    update();
}

void AsdexScopeWidget::startMapRepositionCommand() {
    if (commandType_ != CommandType::None) return;

    commandType_ = CommandType::MapReposition;
    datablockEdit_.reset();
    dcbEntryCommand_.reset();
    editingTrackId_.clear();
    mapRepositionOriginalCenter_ = centerFeet_;
    mapRepositionLastMouseFramebuffer_ = framebufferPoint(mapRepositionBoxCenterLogical());
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

QPointF AsdexScopeWidget::mapRepositionBoxCenterLogical() const {
    return QPointF(std::min(50.0, std::max(0.0, width() * 0.5)),
                   std::min(50.0, std::max(0.0, height() * 0.5)));
}

void AsdexScopeWidget::moveMapRepositionCursorToBoxCenter() {
    const QPointF point = mapRepositionBoxCenterLogical();
    QCursor::setPos(mapToGlobal(QPoint(int(std::round(point.x())),
                                      int(std::round(point.y())))));
}

void AsdexScopeWidget::handleMapRepositionMouseMove(const QPointF& logicalPoint) {
    const QSize renderSize = framebufferRenderSize();
    if (renderSize.isEmpty()) return;

    const QPointF currentFramebuffer = framebufferPoint(logicalPoint);
    if (suppressNextMapRepositionMove_) {
        suppressNextMapRepositionMove_ = false;
        mapRepositionLastMouseFramebuffer_ = currentFramebuffer;
        return;
    }

    const QPointF boxCenterFramebuffer = framebufferPoint(mapRepositionBoxCenterLogical());
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
    if (leaderDirectionCommand_) return leaderDirectionCommand_->displayLines();
    if (isMapRepositionCommandActive()) return {QStringLiteral("MAP RPOS")};
    if (commandType_ == CommandType::Brightness) return {QStringLiteral("BRITE")};
    if (commandType_ == CommandType::CharSize) return {QStringLiteral("CHAR SIZE")};
    if (commandType_ == CommandType::DbEdit) return {QStringLiteral("DB EDIT")};

    switch (commandType_) {
        case CommandType::DbArea:
            return {QStringLiteral("DB AREA")};
        case CommandType::LeaderDirection:
            return {QStringLiteral("LDR DIR")};
        case CommandType::DefineTraitArea:
        case CommandType::DefineTraitAreaTraits:
            return {QStringLiteral("DB AREA"), QStringLiteral("DEFINE TRAIT AREA")};
        case CommandType::ModifyTraitArea:
        case CommandType::ModifyTraitAreaTraits:
            return {QStringLiteral("DB AREA"), QStringLiteral("MODIFY TRAIT AREA")};
        case CommandType::DefineTraitAreaDbCharSize:
            return {QStringLiteral("DB AREA"),
                    QStringLiteral("DEFINE TRAIT AREA"),
                    QStringLiteral("CHAR SIZE"),
                    QStringLiteral("DATA BLOCK")};
        case CommandType::ModifyTraitAreaDbCharSize:
            return {QStringLiteral("DB AREA"),
                    QStringLiteral("MODIFY TRAIT AREA"),
                    QStringLiteral("CHAR SIZE"),
                    QStringLiteral("DATA BLOCK")};
        case CommandType::DefineTraitAreaDbBrightness:
            return {QStringLiteral("DB AREA"),
                    QStringLiteral("DEFINE TRAIT AREA"),
                    QStringLiteral("BRITE"),
                    QStringLiteral("DATA BLOCK")};
        case CommandType::ModifyTraitAreaDbBrightness:
            return {QStringLiteral("DB AREA"),
                    QStringLiteral("MODIFY TRAIT AREA"),
                    QStringLiteral("BRITE"),
                    QStringLiteral("DATA BLOCK")};
        case CommandType::DefineTraitAreaLeaderLength:
            return {QStringLiteral("DB AREA"),
                    QStringLiteral("DEFINE TRAIT AREA"),
                    QStringLiteral("LDR LNG")};
        case CommandType::ModifyTraitAreaLeaderLength:
            return {QStringLiteral("DB AREA"),
                    QStringLiteral("MODIFY TRAIT AREA"),
                    QStringLiteral("LDR LNG")};
        case CommandType::DefineTraitAreaLeaderDirection:
            return {QStringLiteral("DB AREA"),
                    QStringLiteral("DEFINE TRAIT AREA"),
                    QStringLiteral("LDR DIR")};
        case CommandType::ModifyTraitAreaLeaderDirection:
            return {QStringLiteral("DB AREA"),
                    QStringLiteral("MODIFY TRAIT AREA"),
                    QStringLiteral("LDR DIR")};
        case CommandType::DefineOffArea:
            return {QStringLiteral("DB AREA"), QStringLiteral("DEFINE OFF AREA")};
        case CommandType::DeleteAllDbAreas:
            return {QStringLiteral("DB AREA"),
                    QStringLiteral("DELETE ALL AREAS?"),
                    QStringLiteral("1 = NO"),
                    QStringLiteral("2 = YES"),
                    QStringLiteral("(1 OR 2):")};
        case CommandType::DeleteOneDbArea:
            return {QStringLiteral("DB AREA"), QStringLiteral("DELETE ONE AREA")};
        default:
            break;
    }

    return {};
}

void AsdexScopeWidget::renderScene(const QSize& renderSize) {
    if (!renderer_ || renderSize.isEmpty()) return;

    renderer::LayeredCommandBuffer layers;
    const QMatrix4x4 worldProjection = viewProjection(renderSize);
    const QMatrix4x4 projection = screenProjection();

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

    if (showsDbAreas()) {
        renderer::CommandBuffer& dbAreaBuffer = layers.layer(z::DbAreas);
        prepareLayer(dbAreaBuffer, worldProjection);
        drawDbAreas(dbAreaStore_, &dbAreaBuffer);

        if (isDrawingDbArea()) {
            drawDbAreaDraft(dbAreaDraftPoints_, dbAreaDraftMouse_, &dbAreaBuffer);
        }
    }

    renderer::CommandBuffer& targetBuffer = layers.layer(z::Targets);
    prepareLayer(targetBuffer, worldProjection);
    drawTargets(targets_,
                &targetBuffer,
                worldProjection,
                mode_,
                targetVectorSeconds_,
                [this](const AsdexTarget& target) {
                    return vectorVisibleForTarget(target);
                },
                trackBrightness_);

    const DcbState dcbState = makeDcbState();
    dcb_.setBrightness(dcbBrightness_);
    dcb_.setCharSize(dcbCharSize_);
    dcb_.setMenu(currentDcbMenu());
    const DcbLayout dcbLayout = dcb_.layout(size(), asdexFont_, dcbState);

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

    if (fontTexturesReady_) {
        renderer::CommandBuffer& datablockBuffer = layers.layer(z::Datablocks);
        prepareLayer(datablockBuffer, projection);
        drawDatablocks(targets_,
                       &datablockBuffer,
                       projection,
                       [this, &renderSize](QPointF worldFeet) {
                           return worldToScreenLogical(worldFeet, renderSize);
                       },
                       [this](const AsdexTarget& target) {
                           return isDataBlockVisible(target);
                       },
                       [this](const AsdexTarget& target) {
                           return dataBlockSettingsForTarget(target);
                       },
                       [this](int fontSize) {
                           return fontTextureId(fontSize);
                       },
                       asdexFont_);
    }

    renderer::CommandBuffer& listBuffer = layers.layer(z::PreviewArea);
    prepareLayer(listBuffer, projection);

    const std::uint32_t coastFontTexture = fontTextureId(coastSuspendCharSize_);
    if (fontTexturesReady_ && coastFontTexture != 0) {
        renderer::TextBuilder* textBuilder = renderer::getTextBuilder();
        textBuilder->setFont(&asdexFont_);

        coastList_.setBrightness(listsBrightness_);
        coastList_.setFontSize(coastSuspendCharSize_);
        coastList_.render(*textBuilder, asdexFont_, coastFontTexture, size());
        textBuilder->generateCommands(&listBuffer);
        renderer::returnTextBuilder(textBuilder);
    }

    const std::uint32_t previewFontTexture = fontTextureId(previewAreaCharSize_);
    if (fontTexturesReady_ && previewFontTexture != 0) {
        renderer::TextBuilder* textBuilder = renderer::getTextBuilder();
        textBuilder->setFont(&asdexFont_);

        previewArea_.setBrightness(listsBrightness_);
        previewArea_.setFontSize(previewAreaCharSize_);
        const QStringList commandLines = activeCommandLines();
        previewArea_.render(*textBuilder, asdexFont_, previewFontTexture, commandLines);
        textBuilder->generateCommands(&listBuffer);
        renderer::returnTextBuilder(textBuilder);

        if (datablockEdit_ || dcbEntryCommand_ || leaderDirectionCommand_) {
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
            } else if (leaderDirectionCommand_) {
                previewArea_.renderCommandCursor(*lineBuilder,
                                                 asdexFont_,
                                                 leaderDirectionCommand_->cursorLine(),
                                                 leaderDirectionCommand_->cursorColumn(),
                                                 projection);
            }
            lineBuilder->generateCommands(&cursorBuffer);
            renderer::returnLinesBuilder(lineBuilder);
        }
    }

    layers.flushTo(renderer_.get());
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
    state.dataBlockCharSize = dataBlockCharSize_;
    state.dcbCharSize = dcbCharSize_;
    state.coastSuspendCharSize = coastSuspendCharSize_;
    state.tempDataCharSize = tempDataCharSize_;
    state.previewAreaCharSize = previewAreaCharSize_;
    state.fullDataBlocks = fullDataBlocks_;
    state.showAltitudeInDb = showAltitudeInDb_;
    state.showAircraftTypeInDb = showAircraftTypeInDb_;
    state.showSensorsInDb = showSensorsInDb_;
    state.showAircraftCategoryInDb = showAircraftCategoryInDb_;
    state.showFixInDb = showFixInDb_;
    state.showVelocityInDb = showVelocityInDb_;
    state.showScratchpadsInDb = showScratchpadsInDb_;
    if (const DbArea* area = selectedDbArea()) {
        if (area->kind == DbAreaKind::Trait) {
            state.selectedTraitFullDataBlocks = area->traits.fullDataBlocks;
            state.selectedTraitShowAltitude = area->traits.showAltitude;
            state.selectedTraitShowAircraftType = area->traits.showAircraftType;
            state.selectedTraitShowSensors = area->traits.showSensors;
            state.selectedTraitShowAircraftCategory = area->traits.showAircraftCategory;
            state.selectedTraitShowFix = area->traits.showFix;
            state.selectedTraitShowVelocity = area->traits.showVelocity;
            state.selectedTraitShowScratchpads = area->traits.showScratchpads;
            state.selectedTraitDataBlockCharSize = area->traits.dataBlockCharSize;
            state.selectedTraitDataBlockBrightness = area->traits.dataBlockBrightness;
            state.selectedTraitShowVector = area->traits.showVector;
            state.selectedTraitLeaderLength = area->traits.leaderLength;
            state.selectedTraitLeaderDirection = leaderDirectionDcbValue(area->traits.leaderDirection);
        }
    }
    state.activeFunction = activeDcbFunctionForCommand();
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
    const double theta = radiansFromDegrees(currentRotationValue());
    const double c = std::cos(theta);
    const double s = std::sin(theta);

    const double dx = worldFeet.x() - centerFeet_.x();
    const double dy = worldFeet.y() - centerFeet_.y();
    const double rx = c * dx - s * dy;
    const double ry = s * dx + c * dy;

    const double framebufferX = renderSize.width() * 0.5 + rx * ppf;
    const double framebufferY = renderSize.height() * 0.5 - ry * ppf;

    const qreal dpr = devicePixelRatioF();
    return QPointF(framebufferX / dpr, framebufferY / dpr);
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

bool AsdexScopeWidget::handleDcbWheel(DcbFunction function, int wheelY) {
    const int step = wheelY > 0 ? 1 : -1;

    if (currentDcbMenu() == DcbMenu::DefineTraitArea
        || currentDcbMenu() == DcbMenu::ModifyTraitArea) {
        switch (function) {
            case DcbFunction::DataBlockCharSize:
                setSelectedTraitDbCharSizeValue(selectedTraitDbCharSizeValue() + step);
                return true;
            case DcbFunction::DataBlocksBrightness:
                setSelectedTraitDbBrightnessValue(selectedTraitDbBrightnessValue() + step);
                return true;
            case DcbFunction::DbAreaLeaderLength:
                setSelectedTraitLeaderLengthValue(selectedTraitLeaderLengthValue() + step);
                return true;
            case DcbFunction::DbAreaLeaderDirection:
                setSelectedTraitLeaderDirectionValue(
                    nextLeaderDirectionValue(selectedTraitLeaderDirectionValue(), step));
                return true;
            default:
                return false;
        }
    }

    const CommandType brightnessType = commandForBrightnessFunction(function);

    if (brightnessType != CommandType::None) {
        setBrightnessValue(brightnessType, brightnessValue(brightnessType) + step);
        return true;
    }

    const CommandType charSizeType = commandForCharSizeFunction(function);
    if (charSizeType != CommandType::None) {
        setCharSizeValue(charSizeType, charSizeValue(charSizeType) + step);
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

    const DcbHit hit = dcb_.hitTest(logicalPoint, size(), asdexFont_, makeDcbState());
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

    if (leaderDirectionCommand_) {
        setAsdexCursor(CursorMode::Scope);
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

    if (isSelectingDbArea()) {
        setAsdexCursor(dbAreaSelectableAt(logicalPoint) ? CursorMode::Select
                                                        : CursorMode::Scope);
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
        case CursorMode::Select:
            if (cursors_.has(asdex::CursorType::Select))
                setCursor(cursors_.cursor(asdex::CursorType::Select));
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
