#ifndef ASDEX_DCB_H_
#define ASDEX_DCB_H_

#include "renderer/font.h"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <optional>

namespace renderer {
class CommandBuffer;
class TextBuilder;
}  // namespace renderer

namespace asdex {

enum class DcbPosition {
    Top,
    Bottom,
    Left,
    Right,
    Off,
};

enum class DcbMenu {
    Main,
    Brightness,
    Off,
};

enum class DcbButtonKind {
    Normal,
    Menu,
    Toggle,
    Value,
    Error,
    Vacant,
};

enum class DcbFunction {
    Range,
    MapReposition,
    Rotate,
    Undo,
    Default,
    Prefs,
    DayNite,
    Brightness,
    CharSize,
    SafetyLogic,
    Tools,
    VectorOnOff,
    VectorLength,
    TempData,
    LeaderLength,
    Local1,
    Local2,
    DataBlockArea,
    DataBlockEdit,
    DataBlocksOnOff,
    InitControl,
    TrackSuspend,
    TermControl,
    DcbOnOff,
    MlatOff,
    AsrOff,
    OperationalMode,
    HoldBarsBrightness,
    MovementAreasBrightness,
    BackgroundBrightness,
    TrackBrightness,
    DataBlocksBrightness,
    ListsBrightness,
    TempMapAreasBrightness,
    TempMapTextBrightness,
    DcbBrightness,
    Done,
    Vacant,
};

struct DcbButtonSpec {
    DcbFunction function = DcbFunction::Vacant;
    DcbButtonKind kind = DcbButtonKind::Normal;
    QStringList lines;
    bool large = false;

    int value = 0;
    bool showValue = false;

    bool toggleOn = false;
    QString onLabel = QStringLiteral("ON");
    QString offLabel = QStringLiteral("OFF");
};

struct DcbButtonLayout {
    DcbButtonSpec spec;
    QRectF bounds;
};

struct DcbLayout {
    QSize menuSize;
    QSize buttonSize;

    QRectF dcbBounds;
    QRectF menuBounds;

    int autoSize = 1;
    int renderFontSize = 1;

    QVector<DcbButtonLayout> buttons;
};

struct DcbHit {
    bool overDcb = false;
    int buttonIndex = -1;
    std::optional<DcbFunction> function;
};

struct DcbState {
    int range = 100;
    int rotation = 0;
    int vectorLength = 5;
    int leaderLength = 2;

    bool nightMode = false;
    bool showVectorLine = true;
    bool showDataBlocks = true;
    bool dcbOn = true;

    bool networkConnected = true;

    int holdBarsBrightness = 95;
    int movementAreasBrightness = 95;
    int backgroundBrightness = 95;
    int trackBrightness = 95;
    int dataBlocksBrightness = 95;
    int listsBrightness = 95;
    int tempMapAreasBrightness = 95;
    int tempMapTextBrightness = 95;
    int dcbBrightness = 95;
};

class Dcb {
public:
    Dcb();

    void setVisible(bool visible) { visible_ = visible; }
    bool visible() const { return visible_; }

    void setPosition(DcbPosition position) { position_ = position; }
    DcbPosition position() const { return position_; }

    void setBrightness(int brightness);
    int brightness() const { return brightness_; }

    void setCharSize(int charSize);
    int charSize() const { return dcbCharSize_; }

    void setMenu(DcbMenu menu) { menu_ = menu; }
    DcbMenu menu() const { return menu_; }

    DcbLayout layout(QSize displaySize,
                     const renderer::BitmapFont& font,
                     const DcbState& state) const;

    DcbHit hitTest(QPointF displayPoint,
                   QSize displaySize,
                   const renderer::BitmapFont& font,
                   const DcbState& state) const;

    bool contains(QPointF displayPoint,
                  QSize displaySize,
                  const renderer::BitmapFont& font,
                  const DcbState& state) const;

    void drawQuads(renderer::CommandBuffer* commandBuffer, const DcbLayout& layout) const;
    void drawBackground(renderer::CommandBuffer* commandBuffer, const DcbLayout& layout) const;
    void drawButtons(renderer::CommandBuffer* commandBuffer, const DcbLayout& layout) const;

    void drawText(renderer::TextBuilder& textBuilder,
                  const renderer::BitmapFont& font,
                  std::uint32_t fontTextureId,
                  const DcbLayout& layout,
                  int hoveredButtonIndex = -1) const;

    int reservedTopHeight(QSize displaySize,
                          const renderer::BitmapFont& font,
                          const DcbState& state) const;

private:
    static QVector<DcbButtonSpec> mainButtonSpecs(const DcbState& state);
    static QVector<DcbButtonSpec> brightnessButtonSpecs(const DcbState& state);
    static QVector<DcbButtonSpec> offButtonSpecs(const DcbState& state);
    static bool isHorizontal(DcbPosition position);
    static bool isLargeFunction(DcbFunction function);

    static QSize buttonSizeForFont(const renderer::BitmapFont& font, int autoSize);
    static QSize horizontalMenuSize(QSize buttonSize);
    static QSize offMenuSize(QSize buttonSize);
    static QColor applyDcbBrightness(QColor color, int brightness);

    QColor backgroundColor() const;
    QColor menuSlabColor() const;
    QColor normalButtonColor(bool depressed = false) const;
    QColor menuButtonColor(bool depressed = false) const;
    QColor textColor(bool active = false, bool hover = false) const;

    bool visible_ = true;
    DcbPosition position_ = DcbPosition::Top;
    DcbMenu menu_ = DcbMenu::Main;

    int brightness_ = 95;
    int dcbCharSize_ = 2;
};

}  // namespace asdex

#endif  // ASDEX_DCB_H_
