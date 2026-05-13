#ifndef ASDEX_SCOPE_H_
#define ASDEX_SCOPE_H_

#include "asdex/atiscache.h"
#include "asdex/cmdslew.h"
#include "asdex/lists.h"
#include "asdex/notamcache.h"
#include "asdex/targetcache.h"
#include "asdex/colors.h"
#include "asdex/cursors.h"
#include "asdex/dcb.h"
#include "asdex/datablocks.h"
#include "asdex/tempdata.h"
#include "asdex/targets.h"
#include "renderer/font.h"
#include "asdex/videomaps.h"

#include <QEvent>
#include <QHash>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPointF>
#include <QTimer>
#include <QVector>
#include <QWheelEvent>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace renderer {
class Renderer;
}

namespace asdex {

class AsdexScopeWidget : public QOpenGLWidget {
public:
    explicit AsdexScopeWidget(QString airport, QWidget* parent = nullptr);
    ~AsdexScopeWidget() override;

    QString airport() const { return airport_; }

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class CursorMode {
        Scope,
        Dcb,
        Captured,
        Hidden,
    };

    void fitMapToView();
    void updateTargetsFromCache();
    void updateHighlightedTarget(const QPointF& mouseLogical);
    void clearHighlightedTarget();
    bool handleDatablockEditKey(QKeyEvent* event);
    AsdexTarget* highlightedTarget();
    AsdexTarget* targetById(const QString& targetId);
    void startDatablockEdit(const AsdexTarget& target);
    void cancelCommand();
    void submitDatablockEdit();
    void submitDcbEntryCommand();
    void applyEditedFields(AsdexTarget& target, const EditedDbFields& fields) const;
    bool commandActive() const;
    bool defaultDataBlockVisibleForTarget(const AsdexTarget& target) const;
    bool isDataBlockVisible(const AsdexTarget& target) const;
    void toggleDataBlockForTarget(const AsdexTarget& target);
    void handleDcbButtonClicked(DcbFunction function);
    void toggleDcbOnOff();
    void toggleDayNite();
    int currentRangeValue() const;
    void setRangeValue(int range);
    void startRangeCommand();
    int currentRotationValue() const;
    void setRotationValue(int degrees);
    void rotateByDegrees(int deltaDegrees);
    void startRotateCommand();
    bool handleDcbEntryCommandKey(QKeyEvent* event);
    QStringList activeCommandLines() const;
    void renderScene(const QSize& renderSize);
    DcbState makeDcbState() const;
    QSize framebufferRenderSize() const;
    std::uint32_t fontTextureId(int fontSize) const;
    QMatrix4x4 screenProjection() const;
    QMatrix4x4 viewProjection(const QSize& renderSize) const;
    QPointF worldToScreenLogical(const QPointF& worldFeet, const QSize& renderSize) const;
    QPointF worldToFramebufferTopLeft(const QPointF& worldFeet, const QSize& renderSize) const;
    QPointF framebufferPoint(const QPointF& logicalPoint) const;
    double pixelsPerFoot(const QSize& renderSize) const;
    QPointF screenToWorldFeet(const QPointF& logicalPoint, const QSize& renderSize) const;
    QPointF screenDeltaToWorldDelta(const QPointF& framebufferDelta,
                                    const QSize& renderSize) const;
    void zoomByFeet(double deltaFeet);
    void zoomToCursorByFeet(double deltaFeet, const QPointF& cursorLogicalPoint);
    bool isPointOverDcb(const QPointF& logicalPoint) const;
    void clearDcbHover();
    void updateDcbHover(const QPointF& logicalPoint);
    void updateHoverCursor(const QPointF& logicalPoint);
    void setAsdexCursor(CursorMode mode);
    void setAsdexCursor(asdex::CursorType type);

    QString airport_;
    asdex::VideoMap map_;
    ::asdex::TargetCache targetCache_;
    ::asdex::AtisCache atisCache_;
    ::asdex::RunwayClosureCache runwayClosureCache_;
    asdex::CursorSet cursors_;
    Dcb dcb_;
    ::asdex::PreviewArea previewArea_;
    renderer::BitmapFont asdexFont_;
    std::unique_ptr<renderer::Renderer> renderer_;
    QHash<int, std::uint32_t> fontTextureIds_;
    RunwayClosureGeometry runwayClosureGeometry_;
    TempAreaGeometry tempAreaGeometry_;
    QVector<asdex::AsdexTarget> targets_;
    QHash<QString, DataBlockVisibility> datablockVisibility_;
    QHash<QString, EditedDbFields> pendingDatablockEdits_;
    QVector<TempArea> restrictedTempAreas_;
    CoastList coastList_;
    QString highlightedTargetId_;
    CommandType commandType_ = CommandType::None;
    std::optional<DatablockEditCommand> datablockEdit_;
    std::optional<DcbEntryCommand> dcbEntryCommand_;
    QString editingTrackId_;
    QPointF centerFeet_;
    double halfRangeFeet_ = 1.0;
    int rotationDegrees_ = 0;
    asdex::Mode mode_ = asdex::Mode::Day;
    bool showDataBlocks_ = true;
    bool timesharePrimary_ = true;
    QTimer datablockTimeshareTimer_;
    QTimer coastClockTimer_;
    bool panning_ = false;
    bool rightDragMoved_ = false;
    bool dcbMouseCaptured_ = false;
    bool dcbOff_ = false;
    int hoveredDcbButtonIndex_ = -1;
    std::optional<DcbFunction> hoveredDcbFunction_;
    QPointF panStartMouseFramebuffer_;
    QPointF panStartCenterFeet_;

    int targetVectorSeconds_ = 5;
    CursorMode currentCursorMode_ = CursorMode::Hidden;
    bool fontLoaded_ = false;
    bool fontTexturesReady_ = false;
};

} // namespace asdex

#endif  // ASDEX_SCOPE_H_
