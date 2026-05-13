#include "asdex/datablocks.h"

#include "asdex/colors.h"
#include "renderer/builders.h"
#include "renderer/command_buffer.h"
#include "renderer/font.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace asdex {
namespace {

constexpr int kDatablockLineSpacing = 2;
constexpr int kLeaderStartOffsetPx = 7;
constexpr int kLeaderStepPx = 15;
constexpr int kZeroLengthAnchorPx = 10;

struct BuiltDataBlock {
    QStringList lines;
    int maxLineWidth = 0;
    int longestHighestLineNumber = 0;
    int longestLowestLineNumber = 0;
};

int leaderHeadingDegrees(LeaderDirection direction) {
    switch (direction) {
    case LeaderDirection::N:
        return 360;
    case LeaderDirection::E:
        return 90;
    case LeaderDirection::SE:
        return 135;
    case LeaderDirection::S:
        return 180;
    case LeaderDirection::SW:
        return 225;
    case LeaderDirection::W:
        return 270;
    case LeaderDirection::NW:
        return 315;
    case LeaderDirection::NE:
        return 45;
    }
    return 45;
}

bool isLeftDatablock(LeaderDirection direction) {
    return direction == LeaderDirection::SW
        || direction == LeaderDirection::W
        || direction == LeaderDirection::NW;
}

QPointF leaderDelta(double distancePx, int headingDegrees) {
    const double rad = headingDegrees * M_PI / 180.0;
    const double dx = distancePx * std::sin(rad);
    const double dy = distancePx * std::cos(rad);
    return QPointF(static_cast<int>(dx), -static_cast<int>(dy));
}

QString altitudeHundreds(std::optional<double> altitudeTrue) {
    if (!altitudeTrue) return QStringLiteral("XXX");
    const int hundreds = int(std::round(*altitudeTrue / 100.0));
    return QStringLiteral("%1").arg(std::clamp(hundreds, 0, 999), 3, 10, QLatin1Char('0'));
}

QString velocityTens(double groundSpeedKnots) {
    const int tens = int(std::round(groundSpeedKnots / 10.0));
    return QStringLiteral("%1").arg(std::clamp(tens, 0, 99), 2, 10, QLatin1Char('0'));
}

QString beaconOrNoBeacon(const QString& beaconCode) {
    const QString trimmed = beaconCode.trimmed();
    if (trimmed.isEmpty()) return QStringLiteral("NO BCN");
    return trimmed.rightJustified(4, QLatin1Char('0'));
}

void updateMeasuredWidth(BuiltDataBlock& block,
                         const QString& text,
                         int lineNumber,
                         int fontSize,
                         const renderer::BitmapFont& font) {
    const int width = font.measureText(QStringView(text), fontSize).width();

    if (width > block.maxLineWidth) {
        block.maxLineWidth = width;
        block.longestHighestLineNumber = lineNumber;
        block.longestLowestLineNumber = lineNumber;
    } else if (width == block.maxLineWidth) {
        block.longestLowestLineNumber = lineNumber;
    }
}

QString buildPrimaryLine2(const AsdexTarget& target, const DataBlockSettings& settings) {
    QString primaryLine2;
    if (settings.showAircraftType && !target.aircraftType.trimmed().isEmpty()) {
        primaryLine2 += target.aircraftType.trimmed();
    }

    if (settings.fullDataBlocks
        && settings.showAircraftCategory
        && !target.category.trimmed().isEmpty()) {
        if (!primaryLine2.isEmpty()) primaryLine2 += QLatin1Char(' ');
        primaryLine2 += target.category.trimmed();
    }

    if (settings.showFix && !target.fix.trimmed().isEmpty()) {
        if (!primaryLine2.isEmpty()) primaryLine2 += QLatin1Char(' ');
        primaryLine2 += target.fix.trimmed();
    }

    if (settings.fullDataBlocks && settings.showVelocity) {
        if (!primaryLine2.isEmpty()) primaryLine2 += QLatin1Char(' ');
        primaryLine2 += velocityTens(target.groundSpeedKnots);
    }

    return primaryLine2.trimmed();
}

QString buildScratchpadLine2(const AsdexTarget& target, const DataBlockSettings& settings) {
    if (!settings.showScratchpads) return {};
    return QStringLiteral("%1 %2").arg(target.scratchpad1, target.scratchpad2).trimmed();
}

QString chooseLine2(const QString& primary,
                    const QString& secondary,
                    const DataBlockSettings& settings) {
    const bool hasPrimary = !primary.isEmpty();
    const bool hasSecondary = !secondary.isEmpty();

    if (settings.alertInProgress) return primary;
    if (hasPrimary && hasSecondary) return settings.timesharePrimary ? primary : secondary;
    if (hasPrimary) return primary;
    if (hasSecondary) return secondary;
    return {};
}

BuiltDataBlock buildDataBlock(const AsdexTarget& target,
                              const DataBlockSettings& settings,
                              const renderer::BitmapFont& font) {
    BuiltDataBlock out;
    out.lines << (target.duplicateBeaconCode ? QStringLiteral("DUP BCN") : QString());

    QString line1;
    if (!target.callsign.trimmed().isEmpty())
        line1 = target.callsign.trimmed();
    else
        line1 = beaconOrNoBeacon(target.beaconCode);

    if (settings.fullDataBlocks && settings.showAltitude) {
        line1 += QLatin1Char(' ');
        line1 += altitudeHundreds(target.altitudeTrue);
    }

    if (settings.fullDataBlocks) {
        if (target.coasting) {
            line1 += QStringLiteral(" CST");
        } else if (settings.showSensors) {
            line1 += QStringLiteral(" FUS");
        }
    }

    out.lines << line1.trimmed();

    const QString primaryLine2 = buildPrimaryLine2(target, settings);
    const QString scratchLine2 = buildScratchpadLine2(target, settings);
    out.lines << chooseLine2(primaryLine2, scratchLine2, settings);

    updateMeasuredWidth(out, out.lines.value(0), 0, settings.fontSize, font);
    updateMeasuredWidth(out, out.lines.value(1), 1, settings.fontSize, font);
    updateMeasuredWidth(out, primaryLine2, 2, settings.fontSize, font);
    updateMeasuredWidth(out, scratchLine2, 2, settings.fontSize, font);
    return out;
}

void drawOneDataBlock(const AsdexTarget& target,
                      const QPointF& targetScreen,
                      renderer::LinesBuilder& lineBuilder,
                      renderer::TextBuilder& textBuilder,
                      const renderer::BitmapFont& font,
                      std::uint32_t fontTextureId,
                      const DataBlockSettings& settings) {
    const int fontSize = settings.fontSize;
    const int height = font.lineHeight(fontSize);
    if (height <= 0) return;

    const QColor color = applyBrightness(QColor(0, 208, 0), settings.brightness, 20);
    const BuiltDataBlock block = buildDataBlock(target, settings, font);
    if (block.maxLineWidth <= 0) return;

    const LeaderDirection direction = settings.leaderDirection;
    const int heading = leaderHeadingDegrees(direction);
    const bool left = isLeftDatablock(direction);
    const int leaderLengthPx = std::max(0, settings.leaderLength) * kLeaderStepPx;

    const QPointF leaderStart = targetScreen + leaderDelta(kLeaderStartOffsetPx, heading);
    const QPointF leaderEnd =
        targetScreen + leaderDelta(leaderLengthPx == 0 ? kZeroLengthAnchorPx : leaderLengthPx,
                                   heading);

    if (leaderLengthPx > 0) lineBuilder.addLine(leaderStart, leaderEnd);

    int verticalLeftOffset = 0;
    if (left) {
        const int selectedLine = direction != LeaderDirection::NW
            ? block.longestHighestLineNumber
            : block.longestLowestLineNumber;
        verticalLeftOffset = (height + kDatablockLineSpacing) * (-1 + selectedLine);
    }

    const int textX = int(leaderEnd.x()) + (left ? (-2 - block.maxLineWidth) : 2);
    const int textY = int(leaderEnd.y())
                    - height * 3 / 2
                    - kDatablockLineSpacing
                    - verticalLeftOffset;

    renderer::TextStyle style;
    style.size = fontSize;
    style.color = color;
    style.background = Qt::transparent;

    QPointF linePosition(textX, textY);
    for (const QString& line : block.lines) {
        if (!line.isEmpty()) textBuilder.addText(QStringView(line), linePosition, style, fontTextureId);
        linePosition.ry() += height + kDatablockLineSpacing;
    }
}

} // namespace

void drawDatablocks(const QVector<AsdexTarget>& targets,
                    renderer::CommandBuffer* commandBuffer,
                    const QMatrix4x4& screenProjection,
                    const std::function<QPointF(QPointF)>& worldToScreen,
                    const std::function<bool(const AsdexTarget&)>& isVisible,
                    const renderer::BitmapFont& font,
                    std::uint32_t fontTextureId,
                    const DataBlockSettings& settings) {
    if (!commandBuffer || !settings.showDataBlocks || fontTextureId == 0) return;

    renderer::LinesBuilder* lineBuilder = renderer::getLinesBuilder();
    renderer::TextBuilder* textBuilder = renderer::getTextBuilder();
    textBuilder->setFont(&font);

    for (const AsdexTarget& target : targets) {
        if (!target.correlated) continue;
        if (isVisible && !isVisible(target)) continue;
        drawOneDataBlock(target,
                         worldToScreen(target.positionFeet),
                         *lineBuilder,
                         *textBuilder,
                         font,
                         fontTextureId,
                         settings);
    }

    commandBuffer->loadProjectionMatrix(screenProjection);
    commandBuffer->setRgba(renderer::RGBA::fromQColor(applyBrightness(QColor(0, 208, 0),
                                                                      settings.brightness,
                                                                      20)));
    lineBuilder->generateCommands(commandBuffer);
    textBuilder->generateCommands(commandBuffer);

    renderer::returnTextBuilder(textBuilder);
    renderer::returnLinesBuilder(lineBuilder);
}

} // namespace asdex
