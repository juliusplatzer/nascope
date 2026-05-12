#pragma once

#include "asdex/atiscache.h"
#include "asdex/cmdslew.h"
#include "asdex/lists.h"
#include "asdex/notamcache.h"
#include "asdex/targetcache.h"
#include "asdex/colors.h"
#include "asdex/cursors.h"
#include "asdex/datablocks.h"
#include "asdex/tempdata.h"
#include "asdex/targets.h"
#include "renderer/font.h"
#include "asdex/videomaps.h"

#include <QMatrix4x4>
#include <QHash>
#include <QKeyEvent>
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

private:
    enum class CursorMode {
        Scope,
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
    void applyEditedFields(AsdexTarget& target, const EditedDbFields& fields) const;
    bool defaultDataBlockVisibleForTarget(const AsdexTarget& target) const;
    bool isDataBlockVisible(const AsdexTarget& target) const;
    void toggleDataBlockForTarget(const AsdexTarget& target);
    void renderScene(const QSize& renderSize);
    QSize framebufferRenderSize() const;
    std::uint32_t fontTextureId(int fontSize) const;
    QMatrix4x4 screenProjection() const;
    QMatrix4x4 viewProjection(const QSize& renderSize) const;
    QPointF worldToScreenLogical(const QPointF& worldFeet, const QSize& renderSize) const;
    QPointF worldToFramebufferTopLeft(const QPointF& worldFeet, const QSize& renderSize) const;
    QPointF framebufferPoint(const QPointF& logicalPoint) const;
    double pixelsPerFoot(const QSize& renderSize) const;
    QPointF screenToWorldFeet(const QPointF& logicalPoint, const QSize& renderSize) const;
    void zoomByFeet(double deltaFeet);
    void zoomToCursorByFeet(double deltaFeet, const QPointF& cursorLogicalPoint);
    void setAsdexCursor(CursorMode mode);
    void setAsdexCursor(asdex::CursorType type);

    QString airport_;
    asdex::VideoMap map_;
    ::asdex::TargetCache targetCache_;
    ::asdex::AtisCache atisCache_;
    ::asdex::RunwayClosureCache runwayClosureCache_;
    asdex::CursorSet cursors_;
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
    QString editingTrackId_;
    QPointF centerFeet_;
    double halfRangeFeet_ = 1.0;
    asdex::Mode mode_ = asdex::Mode::Day;
    bool showDataBlocks_ = true;
    bool timesharePrimary_ = true;
    QTimer datablockTimeshareTimer_;
    QTimer coastClockTimer_;
    bool panning_ = false;
    bool rightDragMoved_ = false;
    QPointF panStartMouseFramebuffer_;
    QPointF panStartCenterFeet_;

    int targetVectorSeconds_ = 5;
    bool fontLoaded_ = false;
    bool fontTexturesReady_ = false;
};

} // namespace asdex
