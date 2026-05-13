#include "asdex/dcb.h"

#include "asdex/colors.h"
#include "renderer/builders.h"
#include "renderer/command_buffer.h"

#include <algorithm>

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

void addRect(renderer::ColoredTrianglesBuilder* builder, const QRectF& rect, const QColor& color) {
    if (!builder || rect.isEmpty()) return;

    builder->addQuad(rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft(), color);
}

QStringList displayLinesForButton(const DcbButtonSpec& spec) {
    QStringList lines = spec.lines;

    if (spec.kind == DcbButtonKind::Value && spec.showValue) {
        if (lines.size() > 1) {
            bool ok = false;
            lines.at(1).toInt(&ok);
            if (ok) {
                lines[1] = QString::number(spec.value);
            } else if (lines.size() < 3) {
                lines << QString::number(spec.value);
            } else {
                lines[2] = QString::number(spec.value);
            }
        } else {
            lines << QString::number(spec.value);
        }
    }

    if (spec.kind == DcbButtonKind::Toggle) {
        lines << (spec.onLabel + QLatin1Char('/') + spec.offLabel);
    }

    return lines;
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
    toggle(DcbFunction::DcbOnOff, {"DCB"}, state.showDcb);

    if (!state.networkConnected) {
        error(DcbFunction::MlatOff, {"MLAT", "OFF"});
        error(DcbFunction::AsrOff, {"ASR", "OFF"});
    }

    menu(DcbFunction::OperationalMode, {"OPER", "MODE"});

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

DcbLayout Dcb::layout(QSize displaySize,
                      const renderer::BitmapFont& font,
                      const DcbState& state) const {
    DcbLayout out;
    if (!visible_ || position_ == DcbPosition::Off || displaySize.isEmpty()) return out;

    int autoSize = 3;
    QSize buttonSize;
    QSize menuSize;

    while (autoSize >= 1) {
        buttonSize = buttonSizeForFont(font, autoSize);
        menuSize = horizontalMenuSize(buttonSize);
        if (autoSize == 1 || displaySize.width() >= menuSize.width()) break;
        --autoSize;
    }

    out.autoSize = autoSize;
    out.renderFontSize = std::min(autoSize, dcbCharSize_);
    out.buttonSize = buttonSize;
    out.menuSize = menuSize;

    if (!isHorizontal(position_)) return out;

    const int menuX = displaySize.width() > menuSize.width()
        ? (displaySize.width() - menuSize.width()) / 2
        : 0;
    const int menuY = position_ == DcbPosition::Top ? 0 : displaySize.height() - menuSize.height();

    out.dcbBounds = QRectF(0, menuY, displaySize.width(), menuSize.height());
    out.menuBounds = QRectF(menuX, menuY, menuSize.width(), menuSize.height());

    int row = 1;
    int column = 1;

    for (const DcbButtonSpec& spec : mainButtonSpecs(state)) {
        const int x = menuX + column * kButtonSpacing + (column - 1) * buttonSize.width();
        const int y = menuY + (row == 1 ? kButtonSpacing
                                        : (2 * kButtonSpacing + buttonSize.height()));
        const int height =
            spec.large ? (buttonSize.height() * 2 + kButtonSpacing) : buttonSize.height();

        out.buttons.push_back(DcbButtonLayout{spec, QRectF(x, y, buttonSize.width(), height)});

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
        if (dcbLayout.buttons[i].bounds.contains(displayPoint)) {
            hit.buttonIndex = i;
            hit.function = dcbLayout.buttons[i].spec.function;
            break;
        }
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
    if (!commandBuffer || layout.dcbBounds.isEmpty()) return;

    renderer::ColoredTrianglesBuilder* builder = renderer::getColoredTrianglesBuilder();

    addRect(builder, layout.dcbBounds, backgroundColor());
    addRect(builder, layout.menuBounds, menuSlabColor());

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

        renderer::TextStyle style;
        style.size = layout.renderFontSize;
        style.color = textColor(false, hovered);
        style.background = Qt::transparent;

        const QStringList lines = displayLinesForButton(button.spec);
        if (lines.isEmpty()) continue;

        const int blockHeight =
            lines.size() * lineHeight + (lines.size() - 1) * kLineSpacing;
        int y = int(button.bounds.y()) + (int(button.bounds.height()) - blockHeight) / 2;

        for (const QString& line : lines) {
            const QSize measured = font.measureText(QStringView(line), layout.renderFontSize);
            const int x = int(button.bounds.x()) + (int(button.bounds.width()) - measured.width()) / 2;

            textBuilder.addText(QStringView(line), QPointF(x, y), style, fontTextureId);
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
