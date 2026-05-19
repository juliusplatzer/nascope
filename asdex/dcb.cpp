#include "asdex/dcb.h"

#include "asdex/colors.h"
#include "renderer/builders.h"
#include "renderer/cmdbuffer.h"

#include <algorithm>
#include <utility>

namespace asdex {
namespace {

constexpr int kButtonSpacing = 3;
constexpr int kColumnCount = 14;
constexpr int kLineSpacing = 4;
constexpr int kMinBrightness = 20;

const QColor kDcbBackground(56, 56, 56);
const QColor kDcbButtonBorder(100, 100, 100);
const QColor kDcbButton(56, 56, 56);
const QColor kDcbMenuButton(80, 80, 80);
const QColor kDcbDepressed(45, 45, 45);
const QColor kDcbText(Qt::white);
const QColor kDcbTextHover(Qt::green);
const QColor kDcbHighlight(255, 220, 40);

struct DcbTextFragment {
    QString text;
    bool active = false;
};

using DcbTextLine = QVector<DcbTextFragment>;

void addRect(renderer::ColoredTrianglesBuilder* builder, const QRectF& rect, const QColor& color) {
    if (!builder || rect.isEmpty()) return;

    builder->addQuad(rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft(), color);
}

DcbTextLine normalLine(const QString& text) {
    return DcbTextLine{DcbTextFragment{text, false}};
}

QVector<DcbTextLine> displayTextLinesForButton(const DcbButtonSpec& spec) {
    QVector<DcbTextLine> lines;
    lines.reserve(spec.lines.size() + 1);

    for (const QString& line : spec.lines) {
        lines.push_back(normalLine(line));
    }

    if (spec.kind == DcbButtonKind::Value && spec.showValue) {
        lines.push_back(normalLine(QString::number(spec.value)));
    }

    if (spec.kind == DcbButtonKind::Toggle) {
        DcbTextLine toggleLine;
        toggleLine.push_back(DcbTextFragment{spec.onLabel, spec.toggleOn});
        toggleLine.push_back(DcbTextFragment{QStringLiteral("/"), false});
        toggleLine.push_back(DcbTextFragment{spec.offLabel, !spec.toggleOn});
        lines.push_back(std::move(toggleLine));
    }

    return lines;
}

int measureFragmentLine(const renderer::BitmapFont& font, const DcbTextLine& line, int fontSize) {
    int width = 0;
    for (const DcbTextFragment& fragment : line) {
        width += font.measureText(QStringView(fragment.text), fontSize).width();
    }
    return width;
}

}  // namespace

Dcb::Dcb() = default;

void Dcb::setBrightness(int brightness) {
    brightness_ = std::clamp(brightness, 0, 100);
}

void Dcb::setCharSize(int charSize) {
    dcbCharSize_ = std::clamp(charSize, 1, 3);
}

QColor Dcb::applyDcbBrightness(QColor color, int brightness) {
    return applyBrightness(color, brightness, kMinBrightness);
}

QColor Dcb::backgroundColor() const {
    return applyDcbBrightness(kDcbBackground, brightness_);
}

QColor Dcb::menuSlabColor() const {
    return applyDcbBrightness(kDcbButtonBorder, brightness_);
}

QColor Dcb::normalButtonColor(bool depressed) const {
    return applyDcbBrightness(depressed ? kDcbDepressed : kDcbButton, brightness_);
}

QColor Dcb::menuButtonColor(bool depressed) const {
    return applyDcbBrightness(depressed ? kDcbDepressed : kDcbMenuButton, brightness_);
}

QColor Dcb::textColor(bool active, bool hover) const {
    if (active) return applyDcbBrightness(kDcbHighlight, brightness_);
    if (hover) return applyDcbBrightness(kDcbTextHover, brightness_);
    return applyDcbBrightness(kDcbText, brightness_);
}

bool Dcb::isHorizontal(DcbPosition position) {
    return position == DcbPosition::Top || position == DcbPosition::Bottom;
}

bool Dcb::isLargeFunction(DcbFunction function) {
    switch (function) {
    case DcbFunction::Range:
    case DcbFunction::SafetyLogic:
    case DcbFunction::Tools:
    case DcbFunction::HoldBarsBrightness:
    case DcbFunction::MovementAreasBrightness:
    case DcbFunction::BackgroundBrightness:
    case DcbFunction::TrackBrightness:
    case DcbFunction::DataBlocksBrightness:
    case DcbFunction::ListsBrightness:
    case DcbFunction::TempMapAreasBrightness:
    case DcbFunction::TempMapTextBrightness:
    case DcbFunction::DcbBrightness:
    case DcbFunction::DataBlockCharSize:
    case DcbFunction::DcbCharSize:
    case DcbFunction::CoastSuspendCharSize:
    case DcbFunction::TempDataCharSize:
    case DcbFunction::PreviewAreaCharSize:
    case DcbFunction::ClosedRunway:
    case DcbFunction::StoredGlobalTempData:
    case DcbFunction::DefineClosedArea:
    case DcbFunction::DefineRestrictedArea:
    case DcbFunction::DefineTempText:
    case DcbFunction::ShowHiddenTempData:
    case DcbFunction::HideTempData:
    case DcbFunction::DeleteGlobalTempData:
    case DcbFunction::DefineDbTraitArea:
    case DcbFunction::DefineDbOffArea:
    case DcbFunction::ModifyDbTraitArea:
    case DcbFunction::DeleteAllDbAreas:
    case DcbFunction::DeleteOneDbArea:
    case DcbFunction::DbFullPart:
    case DcbFunction::DbScratchpadOnOff:
    case DcbFunction::DbAreaVectorOnOff:
    case DcbFunction::DbAreaLeaderLength:
    case DcbFunction::DbAreaLeaderDirection:
    case DcbFunction::Done:
    case DcbFunction::Vacant:
        return true;
    default:
        return false;
    }
}

QVector<DcbButtonSpec> Dcb::mainButtonSpecs(const DcbState& state) {
    QVector<DcbButtonSpec> out;
    out.reserve(27);

    auto normal = [&out](DcbFunction function, QStringList lines) {
        out.push_back(DcbButtonSpec{function,
                                    DcbButtonKind::Normal,
                                    std::move(lines),
                                    isLargeFunction(function)});
    };

    auto menu = [&out](DcbFunction function, QStringList lines) {
        out.push_back(DcbButtonSpec{function,
                                    DcbButtonKind::Menu,
                                    std::move(lines),
                                    isLargeFunction(function)});
    };

    auto value = [&out](DcbFunction function, QStringList lines, int value, bool showValue = true) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Value;
        button.lines = std::move(lines);
        button.large = isLargeFunction(function);
        button.value = value;
        button.showValue = showValue;
        out.push_back(std::move(button));
    };

    auto toggle = [&out](DcbFunction function,
                         QStringList lines,
                         bool on,
                         QString onLabel = QStringLiteral("ON"),
                         QString offLabel = QStringLiteral("OFF")) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Toggle;
        button.lines = std::move(lines);
        button.large = isLargeFunction(function);
        button.toggleOn = on;
        button.onLabel = std::move(onLabel);
        button.offLabel = std::move(offLabel);
        out.push_back(std::move(button));
    };

    auto error = [&out](DcbFunction function, QStringList lines) {
        out.push_back(DcbButtonSpec{function,
                                    DcbButtonKind::Error,
                                    std::move(lines),
                                    isLargeFunction(function)});
    };

    value(DcbFunction::Range, {"RANGE"}, state.range);
    normal(DcbFunction::MapReposition, {"MAP", "RPOS"});
    value(DcbFunction::Rotate, {"ROTATE"}, state.rotation, false);
    normal(DcbFunction::Undo, {"UNDO"});
    normal(DcbFunction::Default, {"DEFAULT"});
    menu(DcbFunction::Prefs, {"PREF"});
    toggle(DcbFunction::DayNite, {}, !state.nightMode, "DAY", "NITE");
    menu(DcbFunction::Brightness, {"BRITE"});
    menu(DcbFunction::CharSize, {"CHAR", "SIZE"});
    menu(DcbFunction::SafetyLogic, {"SAFETY", "LOGIC", "LIMITED"});
    menu(DcbFunction::Tools, {"TOOLS"});
    toggle(DcbFunction::VectorOnOff, {"VECTOR"}, state.showVectorLine);
    value(DcbFunction::VectorLength, {"VECTOR"}, state.vectorLength);
    menu(DcbFunction::TempData, {"TEMP", "DATA"});
    value(DcbFunction::LeaderLength, {"LDR LNG"}, state.leaderLength);
    menu(DcbFunction::Local1, {"LOCAL", "101-188"});
    menu(DcbFunction::Local2, {"LOCAL", "189-276"});
    menu(DcbFunction::DataBlockArea, {"DB", "AREA"});
    menu(DcbFunction::DataBlockEdit, {"DB EDIT"});
    toggle(DcbFunction::DataBlocksOnOff, {"DB"}, state.showDataBlocks);
    normal(DcbFunction::InitControl, {"INIT", "CNTL"});
    normal(DcbFunction::TrackSuspend, {"TRK", "SUSP"});
    normal(DcbFunction::TermControl, {"TERM", "CNTL"});
    toggle(DcbFunction::DcbOnOff, {"DCB"}, state.dcbOn);

    if (!state.networkConnected) {
        error(DcbFunction::MlatOff, {"MLAT", "OFF"});
        error(DcbFunction::AsrOff, {"ASR", "OFF"});
    }

    menu(DcbFunction::OperationalMode, {"OPER", "MODE"});

    return out;
}

QVector<DcbButtonSpec> Dcb::offButtonSpecs(const DcbState& state) {
    QVector<DcbButtonSpec> out;
    out.reserve(2);

    DcbButtonSpec dcb;
    dcb.function = DcbFunction::DcbOnOff;
    dcb.kind = DcbButtonKind::Toggle;
    dcb.lines = {QStringLiteral("DCB")};
    dcb.toggleOn = state.dcbOn;
    dcb.onLabel = QStringLiteral("ON");
    dcb.offLabel = QStringLiteral("OFF");
    out.push_back(std::move(dcb));

    DcbButtonSpec oper;
    oper.function = DcbFunction::OperationalMode;
    oper.kind = DcbButtonKind::Menu;
    oper.lines = {QStringLiteral("OPER"), QStringLiteral("MODE")};
    out.push_back(std::move(oper));

    return out;
}

QVector<DcbButtonSpec> Dcb::brightnessButtonSpecs(const DcbState& state) {
    QVector<DcbButtonSpec> out;
    out.reserve(14);

    auto vacant = [&out]() {
        DcbButtonSpec button;
        button.function = DcbFunction::Vacant;
        button.kind = DcbButtonKind::Vacant;
        button.large = true;
        out.push_back(std::move(button));
    };

    auto value = [&out](DcbFunction function, QStringList lines, int value) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Value;
        button.lines = std::move(lines);
        button.large = true;
        button.value = value;
        button.showValue = true;
        out.push_back(std::move(button));
    };

    auto normal = [&out](DcbFunction function, QStringList lines) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Normal;
        button.lines = std::move(lines);
        button.large = true;
        out.push_back(std::move(button));
    };

    vacant();
    vacant();
    value(DcbFunction::HoldBarsBrightness, {"HOLD BARS"}, state.holdBarsBrightness);
    value(DcbFunction::MovementAreasBrightness, {"MVMENT", "AREA"}, state.movementAreasBrightness);
    value(DcbFunction::BackgroundBrightness, {"BAKGND"}, state.backgroundBrightness);
    value(DcbFunction::TrackBrightness, {"TRACK"}, state.trackBrightness);
    value(DcbFunction::DataBlocksBrightness, {"DATA", "BLOCKS"}, state.dataBlocksBrightness);
    value(DcbFunction::ListsBrightness, {"LISTS"}, state.listsBrightness);
    value(DcbFunction::TempMapAreasBrightness, {"TEMP MAP", "AREAS"}, state.tempMapAreasBrightness);
    value(DcbFunction::TempMapTextBrightness, {"TEMP MAP", "TEXT"}, state.tempMapTextBrightness);
    value(DcbFunction::DcbBrightness, {"DCB"}, state.dcbBrightness);
    normal(DcbFunction::Done, {"DONE"});
    vacant();
    vacant();

    return out;
}

QVector<DcbButtonSpec> Dcb::charSizeButtonSpecs(const DcbState& state) {
    QVector<DcbButtonSpec> out;
    out.reserve(14);

    auto vacant = [&out]() {
        DcbButtonSpec button;
        button.function = DcbFunction::Vacant;
        button.kind = DcbButtonKind::Vacant;
        button.large = true;
        out.push_back(std::move(button));
    };

    auto value = [&out](DcbFunction function, QStringList lines, int value) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Value;
        button.lines = std::move(lines);
        button.large = true;
        button.value = value;
        button.showValue = true;
        out.push_back(std::move(button));
    };

    auto normal = [&out](DcbFunction function, QStringList lines) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Normal;
        button.lines = std::move(lines);
        button.large = true;
        out.push_back(std::move(button));
    };

    vacant();
    vacant();
    vacant();
    vacant();
    value(DcbFunction::DataBlockCharSize, {"DATA", "BLOCK"}, state.dataBlockCharSize);
    value(DcbFunction::DcbCharSize, {"DCB"}, state.dcbCharSize);
    value(DcbFunction::CoastSuspendCharSize,
          {"COAST", "SUSPEND"},
          state.coastSuspendCharSize);
    value(DcbFunction::TempDataCharSize, {"TEMP DATA"}, state.tempDataCharSize);
    value(DcbFunction::PreviewAreaCharSize, {"PREVIEW", "AREA"}, state.previewAreaCharSize);
    normal(DcbFunction::Done, {"DONE"});
    vacant();
    vacant();
    vacant();
    vacant();

    return out;
}

QVector<DcbButtonSpec> Dcb::dbAreaButtonSpecs() {
    QVector<DcbButtonSpec> out;
    out.reserve(14);

    auto vacant = [&out]() {
        DcbButtonSpec button;
        button.function = DcbFunction::Vacant;
        button.kind = DcbButtonKind::Vacant;
        button.large = true;
        out.push_back(std::move(button));
    };

    auto normal = [&out](DcbFunction function, QStringList lines) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Normal;
        button.lines = std::move(lines);
        button.large = true;
        out.push_back(std::move(button));
    };

    vacant();
    vacant();
    vacant();
    vacant();
    normal(DcbFunction::DefineDbTraitArea, {"DEFINE", "TRAIT", "AREA"});
    normal(DcbFunction::DefineDbOffArea, {"DEFINE", "OFF", "AREA"});
    normal(DcbFunction::ModifyDbTraitArea, {"MODIFY", "TRAIT", "AREA"});
    normal(DcbFunction::DeleteAllDbAreas, {"DELETE", "ALL", "AREAS"});
    normal(DcbFunction::DeleteOneDbArea, {"DELETE", "ONE", "AREA"});
    normal(DcbFunction::Done, {"DONE"});
    vacant();
    vacant();
    vacant();
    vacant();

    return out;
}

QVector<DcbButtonSpec> Dcb::dbEditButtonSpecs(const DcbState& state) {
    QVector<DcbButtonSpec> out;
    out.reserve(17);

    auto vacant = [&out]() {
        DcbButtonSpec button;
        button.function = DcbFunction::Vacant;
        button.kind = DcbButtonKind::Vacant;
        button.large = true;
        out.push_back(std::move(button));
    };

    auto toggle = [&out](DcbFunction function,
                         QStringList lines,
                         bool on,
                         QString onLabel = QStringLiteral("ON"),
                         QString offLabel = QStringLiteral("OFF")) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Toggle;
        button.lines = std::move(lines);
        button.large = isLargeFunction(function);
        button.toggleOn = on;
        button.onLabel = std::move(onLabel);
        button.offLabel = std::move(offLabel);
        out.push_back(std::move(button));
    };

    auto normal = [&out](DcbFunction function, QStringList lines) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Normal;
        button.lines = std::move(lines);
        button.large = isLargeFunction(function);
        out.push_back(std::move(button));
    };

    vacant();
    vacant();
    vacant();
    vacant();
    toggle(DcbFunction::DbFullPart, {}, state.fullDataBlocks, "FULL", "PART");
    toggle(DcbFunction::DbAltitudeOnOff, {"ALTITUDE"}, state.showAltitudeInDb);
    toggle(DcbFunction::DbTypeOnOff, {"TYPE"}, state.showAircraftTypeInDb);
    toggle(DcbFunction::DbSensorsOnOff, {"SENSORS"}, state.showSensorsInDb);
    toggle(DcbFunction::DbCategoryOnOff, {"CAT"}, state.showAircraftCategoryInDb);
    toggle(DcbFunction::DbFixOnOff, {"FIX"}, state.showFixInDb);
    toggle(DcbFunction::DbVelocityOnOff, {"VELOCITY"}, state.showVelocityInDb);
    toggle(DcbFunction::DbScratchpadOnOff, {"SCRATCH", "PAD"}, state.showScratchpadsInDb);
    normal(DcbFunction::Done, {"DONE"});
    vacant();
    vacant();
    vacant();
    vacant();

    return out;
}

QVector<DcbButtonSpec> Dcb::traitAreaButtonSpecs(const DcbState& state) {
    QVector<DcbButtonSpec> out;
    out.reserve(17);

    auto vacant = [&out]() {
        DcbButtonSpec button;
        button.function = DcbFunction::Vacant;
        button.kind = DcbButtonKind::Vacant;
        button.large = true;
        out.push_back(std::move(button));
    };

    auto toggle = [&out](DcbFunction function,
                         QStringList lines,
                         bool on,
                         QString onLabel = QStringLiteral("ON"),
                         QString offLabel = QStringLiteral("OFF")) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Toggle;
        button.lines = std::move(lines);
        button.large = isLargeFunction(function);
        button.toggleOn = on;
        button.onLabel = std::move(onLabel);
        button.offLabel = std::move(offLabel);
        out.push_back(std::move(button));
    };

    auto value = [&out](DcbFunction function, QStringList lines, int value) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Value;
        button.lines = std::move(lines);
        button.large = isLargeFunction(function);
        button.value = value;
        button.showValue = true;
        out.push_back(std::move(button));
    };

    auto normal = [&out](DcbFunction function, QStringList lines) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Normal;
        button.lines = std::move(lines);
        button.large = isLargeFunction(function);
        out.push_back(std::move(button));
    };

    vacant();
    vacant();
    toggle(DcbFunction::DbFullPart, {}, state.selectedTraitFullDataBlocks, "FULL", "PART");
    toggle(DcbFunction::DbAltitudeOnOff, {"ALTITUDE"}, state.selectedTraitShowAltitude);
    toggle(DcbFunction::DbTypeOnOff, {"TYPE"}, state.selectedTraitShowAircraftType);
    toggle(DcbFunction::DbSensorsOnOff, {"SENSORS"}, state.selectedTraitShowSensors);
    toggle(DcbFunction::DbCategoryOnOff, {"CAT"}, state.selectedTraitShowAircraftCategory);
    toggle(DcbFunction::DbFixOnOff, {"FIX"}, state.selectedTraitShowFix);
    toggle(DcbFunction::DbVelocityOnOff, {"VELOCITY"}, state.selectedTraitShowVelocity);
    toggle(DcbFunction::DbScratchpadOnOff, {"SCRATCH", "PAD"},
           state.selectedTraitShowScratchpads);
    value(DcbFunction::DataBlockCharSize, {"DB", "SIZE"},
          state.selectedTraitDataBlockCharSize);
    value(DcbFunction::DataBlocksBrightness, {"DB", "BRITE"},
          state.selectedTraitDataBlockBrightness);
    toggle(DcbFunction::DbAreaVectorOnOff, {"VECTOR"}, state.selectedTraitShowVector);
    value(DcbFunction::DbAreaLeaderLength, {"LDR", "LNG"}, state.selectedTraitLeaderLength);
    value(DcbFunction::DbAreaLeaderDirection, {"LDR", "DIR"},
          state.selectedTraitLeaderDirection);
    normal(DcbFunction::Done, {"DONE"});
    vacant();

    return out;
}

QVector<DcbButtonSpec> Dcb::tempDataButtonSpecs() {
    QVector<DcbButtonSpec> out;
    out.reserve(14);

    auto vacant = [&out]() {
        DcbButtonSpec button;
        button.function = DcbFunction::Vacant;
        button.kind = DcbButtonKind::Vacant;
        button.large = true;
        out.push_back(std::move(button));
    };

    auto menu = [&out](DcbFunction function, QStringList lines) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Menu;
        button.lines = std::move(lines);
        button.large = true;
        out.push_back(std::move(button));
    };

    auto normal = [&out](DcbFunction function, QStringList lines) {
        DcbButtonSpec button;
        button.function = function;
        button.kind = DcbButtonKind::Normal;
        button.lines = std::move(lines);
        button.large = true;
        out.push_back(std::move(button));
    };

    vacant();
    vacant();
    vacant();

    menu(DcbFunction::ClosedRunway, {"CLOSED", "RWY"});
    menu(DcbFunction::StoredGlobalTempData, {"STORED", "GLOBAL", "TEMP", "DATA"});
    normal(DcbFunction::DefineClosedArea, {"DEFINE", "CLOSED", "AREA"});
    normal(DcbFunction::DefineRestrictedArea, {"DEFINE", "RESTR", "AREA"});
    normal(DcbFunction::DefineTempText, {"DEFINE", "TEXT"});
    normal(DcbFunction::ShowHiddenTempData, {"SHOW", "HIDDEN", "DATA"});
    normal(DcbFunction::HideTempData, {"HIDE", "DATA"});
    normal(DcbFunction::DeleteGlobalTempData, {"DELETE", "GLOBAL"});
    normal(DcbFunction::Done, {"DONE"});

    vacant();
    vacant();

    return out;
}

QSize Dcb::buttonSizeForFont(const renderer::BitmapFont& font, int autoSize) {
    const QSize charSize = font.charSize(autoSize);
    const int charHeight = std::max(1, charSize.height());
    const int buttonHeight = charHeight * 2 + 9;
    const int buttonWidth = buttonHeight * 3;
    return QSize(buttonWidth, buttonHeight);
}

QSize Dcb::horizontalMenuSize(QSize buttonSize) {
    return QSize((buttonSize.width() + kButtonSpacing) * kColumnCount + kButtonSpacing,
                 buttonSize.height() * 2 + 9);
}

QSize Dcb::offMenuSize(QSize buttonSize) {
    return QSize(buttonSize.width() + 6, buttonSize.height() * 2 + 9);
}

DcbLayout Dcb::layout(QSize displaySize,
                      const renderer::BitmapFont& font,
                      const DcbState& state) const {
    DcbLayout out;
    if (!visible_ || position_ == DcbPosition::Off || displaySize.isEmpty()) return out;

    int autoSize = 3;
    QSize buttonSize;
    QSize menuSize;
    const bool offMenu = menu_ == DcbMenu::Off;

    while (autoSize >= 1) {
        buttonSize = buttonSizeForFont(font, autoSize);
        menuSize = offMenu ? offMenuSize(buttonSize) : horizontalMenuSize(buttonSize);
        if (autoSize == 1 || displaySize.width() >= menuSize.width()) break;
        --autoSize;
    }

    out.autoSize = autoSize;
    out.renderFontSize = std::min(autoSize, dcbCharSize_);
    out.buttonSize = buttonSize;
    out.menuSize = menuSize;

    if (!isHorizontal(position_)) return out;

    int menuX = 0;
    int menuY = position_ == DcbPosition::Top ? 0 : displaySize.height() - menuSize.height();

    if (offMenu) {
        menuX = std::max(0, displaySize.width() - menuSize.width());
        out.dcbBounds = QRectF(menuX, menuY, menuSize.width(), menuSize.height());
        out.menuBounds = out.dcbBounds;
    } else {
        menuX = displaySize.width() > menuSize.width()
            ? (displaySize.width() - menuSize.width()) / 2
            : 0;
        out.dcbBounds = QRectF(0, menuY, displaySize.width(), menuSize.height());
        out.menuBounds = QRectF(menuX, menuY, menuSize.width(), menuSize.height());
    }

    const QVector<DcbButtonSpec> specs =
        menu_ == DcbMenu::Brightness ? brightnessButtonSpecs(state)
        : menu_ == DcbMenu::CharSize ? charSizeButtonSpecs(state)
        : menu_ == DcbMenu::DbArea ? dbAreaButtonSpecs()
        : menu_ == DcbMenu::DbEdit ? dbEditButtonSpecs(state)
        : menu_ == DcbMenu::DefineTraitArea ? traitAreaButtonSpecs(state)
        : menu_ == DcbMenu::ModifyTraitArea ? traitAreaButtonSpecs(state)
        : menu_ == DcbMenu::TempData ? tempDataButtonSpecs()
                                     : offMenu ? offButtonSpecs(state) : mainButtonSpecs(state);

    int row = 1;
    int column = 1;

    for (const DcbButtonSpec& spec : specs) {
        const int x = menuX + column * kButtonSpacing + (column - 1) * buttonSize.width();
        const int y = menuY + (row == 1 ? kButtonSpacing
                                        : (2 * kButtonSpacing + buttonSize.height()));
        const int height =
            spec.large ? (buttonSize.height() * 2 + kButtonSpacing) : buttonSize.height();
        const bool active =
            state.activeFunction.has_value() && *state.activeFunction == spec.function;

        out.buttons.push_back(
            DcbButtonLayout{spec, QRectF(x, y, buttonSize.width(), height), active});

        if (row == 2 || (row == 1 && spec.large)) {
            ++column;
            row = 1;
        } else {
            row = 2;
        }

        if (column > kColumnCount) break;
    }

    return out;
}

DcbHit Dcb::hitTest(QPointF displayPoint,
                    QSize displaySize,
                    const renderer::BitmapFont& font,
                    const DcbState& state) const {
    DcbHit hit;

    const DcbLayout dcbLayout = layout(displaySize, font, state);
    if (dcbLayout.dcbBounds.isEmpty() || !dcbLayout.dcbBounds.contains(displayPoint)) {
        return hit;
    }

    hit.overDcb = true;

    for (int i = 0; i < dcbLayout.buttons.size(); ++i) {
        const DcbButtonLayout& button = dcbLayout.buttons[i];
        if (!button.bounds.contains(displayPoint)) continue;

        hit.buttonIndex = i;
        if (button.spec.kind != DcbButtonKind::Vacant) hit.function = button.spec.function;
        break;
    }

    return hit;
}

bool Dcb::contains(QPointF displayPoint,
                   QSize displaySize,
                   const renderer::BitmapFont& font,
                   const DcbState& state) const {
    return hitTest(displayPoint, displaySize, font, state).overDcb;
}

void Dcb::drawQuads(renderer::CommandBuffer* commandBuffer, const DcbLayout& layout) const {
    drawBackground(commandBuffer, layout);
    drawButtons(commandBuffer, layout);
}

void Dcb::drawBackground(renderer::CommandBuffer* commandBuffer, const DcbLayout& layout) const {
    if (!commandBuffer || layout.dcbBounds.isEmpty()) return;

    renderer::ColoredTrianglesBuilder* builder = renderer::getColoredTrianglesBuilder();

    addRect(builder, layout.dcbBounds, backgroundColor());
    addRect(builder, layout.menuBounds, menuSlabColor());

    builder->generateCommands(commandBuffer);
    renderer::returnColoredTrianglesBuilder(builder);
}

void Dcb::drawButtons(renderer::CommandBuffer* commandBuffer, const DcbLayout& layout) const {
    if (!commandBuffer || layout.dcbBounds.isEmpty()) return;

    renderer::ColoredTrianglesBuilder* builder = renderer::getColoredTrianglesBuilder();

    for (const DcbButtonLayout& button : layout.buttons) {
        QColor color;
        switch (button.spec.kind) {
        case DcbButtonKind::Menu:
            color = menuButtonColor(false);
            break;
        case DcbButtonKind::Error:
        case DcbButtonKind::Normal:
        case DcbButtonKind::Toggle:
        case DcbButtonKind::Value:
        case DcbButtonKind::Vacant:
        default:
            color = normalButtonColor(false);
            break;
        }

        addRect(builder, button.bounds, color);
    }

    builder->generateCommands(commandBuffer);
    renderer::returnColoredTrianglesBuilder(builder);
}

void Dcb::drawText(renderer::TextBuilder& textBuilder,
                   const renderer::BitmapFont& font,
                   std::uint32_t fontTextureId,
                   const DcbLayout& layout,
                   int hoveredButtonIndex) const {
    if (layout.buttons.isEmpty() || fontTextureId == 0) return;

    const int lineHeight = font.lineHeight(layout.renderFontSize);
    if (lineHeight <= 0) return;

    for (int buttonIndex = 0; buttonIndex < layout.buttons.size(); ++buttonIndex) {
        const DcbButtonLayout& button = layout.buttons[buttonIndex];
        const bool hovered = buttonIndex == hoveredButtonIndex;
        const bool active = button.active;

        const QVector<DcbTextLine> lines = displayTextLinesForButton(button.spec);
        if (lines.isEmpty()) continue;

        const int blockHeight =
            lines.size() * lineHeight + (lines.size() - 1) * kLineSpacing;
        int y = int(button.bounds.y()) + (int(button.bounds.height()) - blockHeight) / 2;

        for (const DcbTextLine& line : lines) {
            const int lineWidth = measureFragmentLine(font, line, layout.renderFontSize);
            int x = int(button.bounds.x()) + (int(button.bounds.width()) - lineWidth) / 2;

            for (const DcbTextFragment& fragment : line) {
                renderer::TextStyle style;
                style.size = layout.renderFontSize;
                style.background = Qt::transparent;
                style.color = fragment.active || active ? textColor(true, false)
                                                        : textColor(false, hovered);

                textBuilder.addText(QStringView(fragment.text), QPointF(x, y), style, fontTextureId);
                x += font.measureText(QStringView(fragment.text), layout.renderFontSize).width();
            }

            y += lineHeight + kLineSpacing;
        }
    }
}

int Dcb::reservedTopHeight(QSize displaySize,
                           const renderer::BitmapFont& font,
                           const DcbState& state) const {
    if (!visible_ || position_ != DcbPosition::Top) return 0;
    return layout(displaySize, font, state).menuSize.height();
}

}  // namespace asdex
