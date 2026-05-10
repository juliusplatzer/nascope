#pragma once

#include "asdex/input/asdex_command.h"
#include "asdex/input/datablock_edit_command.h"
#include "asdex/lists/preview_area.h"
#include "asdex/targets/target_cache.h"
#include "asdex/render/colors.h"
#include "asdex/render/cursors.h"
#include "asdex/render/datablocks.h"
#include "asdex/render/screen_line_renderer.h"
#include "renderer/text/bitmap_font.h"
#include "renderer/text/bitmap_font_renderer.h"
#include "asdex/render/targets.h"
#include "asdex/render/videomap.h"

#include <QMatrix4x4>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPointF>
#include <QTimer>
#include <QVector>
#include <QWheelEvent>

#include <cstddef>
#include <optional>

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

    struct DrawBatch {
        asdex::VideoMap::Kind kind = asdex::VideoMap::Kind::Apron;
        int indexCount = 0;
        std::size_t indexOffsetBytes = 0;
    };

    void fitMapToView();
    void initializeShaders();
    void uploadMapGeometry();
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
    void renderVideoMap(const QSize& renderSize);
    void renderTargets(const QSize& renderSize);
    void renderScreenOverlays(const QSize& renderSize);
    QSize framebufferRenderSize() const;
    QMatrix4x4 screenProjection() const;
    QMatrix4x4 viewProjection(const QSize& renderSize) const;
    QColor colorFor(asdex::VideoMap::Kind kind) const;
    QPointF worldToScreenLogical(const QPointF& worldFeet, const QSize& renderSize) const;
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
    asdex::CursorSet cursors_;
    ::asdex::PreviewArea previewArea_;
    renderer::BitmapFont asdexFont_;
    renderer::BitmapFontRenderer textRenderer_;
    asdex::TargetRenderer targetRenderer_;
    DataBlockRenderer datablockRenderer_;
    ScreenLineRenderer screenLineRenderer_;
    QVector<asdex::AsdexTarget> targets_;
    QHash<QString, DataBlockVisibility> datablockVisibility_;
    QHash<QString, EditedDbFields> pendingDatablockEdits_;
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
    bool panning_ = false;
    bool rightDragMoved_ = false;
    QPointF panStartMouseFramebuffer_;
    QPointF panStartCenterFeet_;

    QOpenGLShaderProgram shader_;
    QOpenGLVertexArrayObject vertexArray_;
    QOpenGLBuffer vertexBuffer_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer indexBuffer_{QOpenGLBuffer::IndexBuffer};
    QVector<DrawBatch> drawBatches_;
    bool shaderReady_ = false;
    bool geometryUploaded_ = false;
    bool fontLoaded_ = false;
    bool textRendererReady_ = false;
};

} // namespace asdex
