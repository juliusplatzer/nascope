#ifndef ASDEX_ASDEX_H_
#define ASDEX_ASDEX_H_

#include "asdex/cmdsetup.h"
#include "asdex/cmdslew.h"
#include "asdex/lists.h"
#include "asdex/notamcache.h"
#include "asdex/colors.h"
#include "asdex/cursors.h"
#include "asdex/dcb.h"
#include "asdex/datablock.h"
#include "asdex/dbareas.h"
#include "asdex/tempdata.h"
#include "asdex/target.h"
#include "io/smes.h"
#include "io/atis.h"
#include "renderer/font.h"
#include "panes/display.h"
#include "asdex/videomaps.h"

#include <QEvent>
#include <QHash>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPointF>
#include <QStringList>
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

class Asdex : public panes::Display {
public:
    explicit Asdex(QString airport, QWidget* parent = nullptr);
    ~Asdex() override;

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
    struct LeaderDirectionKeyboardCommand {
        QString value;

        QStringList displayLines() const {
            return {QStringLiteral("LDR DIR"), value};
        }

        int cursorLine() const { return 2; }
        int cursorColumn() const { return value.size(); }

        bool valueInt(int* out) const {
            bool ok = false;
            const int parsed = value.trimmed().toInt(&ok);
            if (!ok || parsed < 1 || parsed > 9) return false;
            if (out) *out = parsed;
            return true;
        }
    };

    enum class CursorMode {
        Scope,
        Dcb,
        Captured,
        Select,
        Hidden,
    };

    struct BrightnessProperty {
        CommandType command;
        DcbFunction function;
        const char* label;
        int Asdex::*field;
        bool affectsDcb;
    };

    struct CharSizeProperty {
        CommandType command;
        DcbFunction function;
        const char* label;
        int Asdex::*field;
        int maxValue;
        bool affectsDcb;
    };

    struct TraitAreaProperty {
        CommandType defineCommand;
        CommandType modifyCommand;
        DcbFunction function;
        int defaultValue;
        int (*read)(const DbArea&);
        void (*write)(DbArea&, int);
    };

    void fitMapToView();
    void updateTargetsFromSmes();
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
    DcbMenu currentDcbMenu() const;
    bool defaultDataBlockVisibleForTarget(const AsdexTarget& target) const;
    bool isDataBlockVisible(const AsdexTarget& target) const;
    void toggleDataBlockForTarget(const AsdexTarget& target);
    void handleDcbButtonClicked(DcbFunction function);
    void toggleDcbOnOff();
    void toggleDayNite();
    void toggleAllDataBlocks();
    void startDcbSubmenu(CommandType command, DcbMenu menu, bool clearDraft);
    void startBrightnessMenu();
    void startBrightnessValueCommand(DcbFunction function);
    void startCharSizeMenu();
    void startCharSizeValueCommand(DcbFunction function);
    void startDbAreaMenu();
    void startDbEditMenu();
    void startDefineTraitAreaCommand();
    void startTraitAreaValueCommand(DcbFunction function);
    void startDefineOffAreaCommand();
    void startModifyTraitAreaCommand();
    void startDeleteAllDbAreasCommand();
    void startDeleteOneDbAreaCommand();
    void handleDcbDone();
    bool isDrawingDbArea() const;
    bool isSelectingDbArea() const;
    bool dbAreaSelectableAt(const QPointF& logicalPoint) const;
    bool deleteDbAreaAt(const QPointF& logicalPoint);
    bool selectTraitAreaAt(const QPointF& logicalPoint);
    bool showsDbAreas() const;
    std::optional<DcbFunction> activeDcbFunctionForCommand() const;
    DbArea* selectedDbArea();
    const DbArea* selectedDbArea() const;
    void selectDbArea(const QString& id);
    void toggleGlobalDbEditField(DcbFunction function);
    void toggleSelectedTraitDbField(DcbFunction function);
    void toggleSelectedTraitVector();
    const DbArea* traitAreaForTarget(const AsdexTarget& target) const;
    bool vectorVisibleForTarget(const AsdexTarget& target) const;
    DataBlockSettings dataBlockSettingsForTarget(const AsdexTarget& target) const;
    int selectedTraitValue(CommandType type) const;
    void setSelectedTraitValue(CommandType type, int value);
    LeaderDirection leaderDirectionFromDcbValue(int value) const;
    int nextLeaderDirectionValue(int current, int step) const;
    bool targetInsideDbOffArea(const AsdexTarget& target) const;
    void addDbAreaPoint(const QPointF& worldFeet);
    void completeDbAreaPolygon();
    void clearDbAreaDraft();
    bool dbAreaDraftWouldSelfIntersect(const QPointF& nextPoint) const;
    bool dbAreaDraftWouldOverlapExisting(const QPointF& nextPoint) const;
    bool dbAreaPolygonIsValidOnClose() const;
    void setBrightnessValue(CommandType type, int value);
    void setCharSizeValue(CommandType type, int value);
    int currentRangeValue() const;
    void setRangeValue(int range);
    void startRangeCommand();
    int currentRotationValue() const;
    void setRotationValue(int degrees);
    void rotateByDegrees(int deltaDegrees);
    void startRotateCommand();
    void toggleVectorLine();
    int currentVectorLengthValue() const;
    void setVectorLengthValue(int seconds);
    void startVectorLengthCommand();
    int currentLeaderLengthValue() const;
    void setLeaderLengthValue(int leaderLength);
    void startLeaderLengthCommand();
    int currentLeaderDirectionValue() const;
    void setLeaderDirectionValue(int value);
    bool leaderDirectionCommandActive() const;
    bool isValidLeaderDirectionValue(int value) const;
    void startLeaderDirectionKeyboardCommand(int value);
    void cancelLeaderDirectionKeyboardCommand();
    void submitLeaderDirectionForAll();
    void submitLeaderDirectionForTargetAt(const QPointF& logicalPoint);
    void beginDcbEntryCommand(DcbEntryCommand command);
    void finalizeDcbEntryCommand();
    static const BrightnessProperty* brightnessProperties(std::size_t* count);
    static const BrightnessProperty* brightnessPropertyFor(CommandType type);
    static const BrightnessProperty* brightnessPropertyFor(DcbFunction function);
    static const CharSizeProperty* charSizeProperties(std::size_t* count);
    static const CharSizeProperty* charSizePropertyFor(CommandType type);
    static const CharSizeProperty* charSizePropertyFor(DcbFunction function);
    static const TraitAreaProperty* traitAreaProperties(std::size_t* count);
    static const TraitAreaProperty* traitAreaPropertyFor(CommandType type);
    static const TraitAreaProperty* traitAreaPropertyFor(DcbFunction function);
    void startMapRepositionCommand();
    void commitMapRepositionCommand();
    void cancelMapRepositionCommand();
    bool isMapRepositionCommandActive() const;
    void moveMapRepositionCursorToBoxCenter();
    QPointF mapRepositionBoxCenterLogical() const;
    void handleMapRepositionMouseMove(const QPointF& logicalPoint);
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
    bool handleDcbWheel(DcbFunction function, int wheelY);
    void clearDcbHover();
    void updateDcbHover(const QPointF& logicalPoint);
    void updateHoverCursor(const QPointF& logicalPoint);
    void setAsdexCursor(CursorMode mode);
    void setAsdexCursor(asdex::CursorType type);

    QString airport_;
    asdex::VideoMap map_;
    io::SmesClient smes_;
    io::AtisFeed atis_;
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
    QHash<QString, bool> dbOffAreaDatablockOverride_;
    QHash<QString, EditedDbFields> pendingDatablockEdits_;
    QVector<TempArea> restrictedTempAreas_;
    DbAreaStore dbAreaStore_;
    QVector<QPointF> dbAreaDraftPoints_;
    std::optional<QPointF> dbAreaDraftMouse_;
    CoastList coastList_;
    QString highlightedTargetId_;
    QString selectedDbAreaId_;
    CommandType commandType_ = CommandType::None;
    std::optional<DatablockEditCommand> datablockEdit_;
    std::optional<DcbEntryCommand> dcbEntryCommand_;
    std::optional<LeaderDirectionKeyboardCommand> leaderDirectionCommand_;
    std::optional<QPointF> mapRepositionOriginalCenter_;
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
    QPointF mapRepositionLastMouseFramebuffer_;
    bool suppressNextMapRepositionMove_ = false;
    bool suppressNextMapRepositionRelease_ = false;
    bool suppressNextDbAreaSelectionRelease_ = false;
    QPointF panStartMouseFramebuffer_;
    QPointF panStartCenterFeet_;

    int targetVectorSeconds_ = 5;
    bool showVectorLine_ = true;
    int leaderLength_ = 2;
    LeaderDirection leaderDirection_ = LeaderDirection::NE;
    QHash<QString, int> targetLeaderDirectionOverrides_;
    int holdBarsBrightness_ = 95;
    int movementAreasBrightness_ = 95;
    int backgroundBrightness_ = 95;
    int trackBrightness_ = 95;
    int dataBlocksBrightness_ = 95;
    int listsBrightness_ = 95;
    int tempMapAreasBrightness_ = 95;
    int tempMapTextBrightness_ = 95;
    int dcbBrightness_ = 95;
    int dataBlockCharSize_ = 2;
    int dcbCharSize_ = 2;
    int coastSuspendCharSize_ = 2;
    int tempDataCharSize_ = 2;
    int previewAreaCharSize_ = 2;
    bool fullDataBlocks_ = true;
    bool showAltitudeInDb_ = false;
    bool showAircraftTypeInDb_ = true;
    bool showSensorsInDb_ = false;
    bool showAircraftCategoryInDb_ = false;
    bool showFixInDb_ = true;
    bool showVelocityInDb_ = false;
    bool showScratchpadsInDb_ = true;
    CursorMode currentCursorMode_ = CursorMode::Hidden;
    bool fontLoaded_ = false;
    bool fontTexturesReady_ = false;
};

} // namespace asdex

#endif  // ASDEX_ASDEX_H_
