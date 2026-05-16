#include "asdex/cmdsetup.h"

#include <algorithm>
#include <utility>

namespace asdex {

bool isBrightnessValueCommand(CommandType type) {
    switch (type) {
    case CommandType::HoldBarsBrightness:
    case CommandType::MovementAreasBrightness:
    case CommandType::BackgroundBrightness:
    case CommandType::TrackBrightness:
    case CommandType::DataBlocksBrightness:
    case CommandType::ListsBrightness:
    case CommandType::TempMapAreasBrightness:
    case CommandType::TempMapTextBrightness:
    case CommandType::DcbBrightness:
        return true;
    default:
        return false;
    }
}

bool isCharSizeValueCommand(CommandType type) {
    switch (type) {
    case CommandType::DataBlockCharSize:
    case CommandType::DcbCharSize:
    case CommandType::CoastSuspendCharSize:
    case CommandType::TempDataCharSize:
    case CommandType::PreviewAreaCharSize:
        return true;
    default:
        return false;
    }
}

bool isDbAreaCommand(CommandType type) {
    switch (type) {
    case CommandType::DbArea:
    case CommandType::DefineTraitArea:
    case CommandType::DefineOffArea:
    case CommandType::ModifyTraitArea:
    case CommandType::DeleteAllDbAreas:
    case CommandType::DeleteOneDbArea:
        return true;
    default:
        return false;
    }
}

DcbEntryCommand DcbEntryCommand::range(int currentRange) {
    Spec spec;
    spec.type = CommandType::Range;
    spec.headingLines = {QStringLiteral("RANGE")};
    spec.minValue = 6;
    spec.maxValue = 300;
    spec.wheelStep = 1;
    spec.invalidMessage = QStringLiteral("INVALID RANGE");
    spec.numericOnly = true;
    spec.initialEntry = QString();
    spec.wheelBaseValue = currentRange;
    return DcbEntryCommand(spec);
}

DcbEntryCommand DcbEntryCommand::rotate(int currentRotation) {
    Spec spec;
    spec.type = CommandType::Rotate;
    spec.headingLines = {QStringLiteral("ROTATE")};
    spec.minValue = 0;
    spec.maxValue = 359;
    spec.wheelStep = 1;
    spec.invalidMessage = QStringLiteral("INVALID ENTRY");
    spec.numericOnly = true;
    spec.wrapWheel = true;
    spec.initialEntry = QString();
    spec.wheelBaseValue = currentRotation;
    return DcbEntryCommand(spec);
}

DcbEntryCommand DcbEntryCommand::vectorLength(int currentVectorLength) {
    Spec spec;
    spec.type = CommandType::VectorLength;
    spec.headingLines = {QStringLiteral("VECTOR LENGTH")};
    spec.minValue = 1;
    spec.maxValue = 20;
    spec.wheelStep = 1;
    spec.invalidMessage = QStringLiteral("INVALID ENTRY");
    spec.numericOnly = true;
    spec.initialEntry = QString();
    spec.wheelBaseValue = currentVectorLength;
    return DcbEntryCommand(spec);
}

DcbEntryCommand DcbEntryCommand::leaderLength(int currentLeaderLength) {
    Spec spec;
    spec.type = CommandType::LeaderLength;
    spec.headingLines = {QStringLiteral("LDR LNG")};
    spec.minValue = 0;
    spec.maxValue = 15;
    spec.wheelStep = 1;
    spec.invalidMessage = QStringLiteral("INVALID LNG");
    spec.numericOnly = true;
    spec.initialEntry = QString();
    spec.wheelBaseValue = currentLeaderLength;
    return DcbEntryCommand(spec);
}

DcbEntryCommand DcbEntryCommand::brightness(CommandType type,
                                            QString label,
                                            int currentValue) {
    Spec spec;
    spec.type = type;
    spec.headingLines = {QStringLiteral("BRITE"), std::move(label)};
    spec.minValue = 1;
    spec.maxValue = 99;
    spec.wheelStep = 1;
    spec.invalidMessage = QStringLiteral("INVALID ENTRY");
    spec.numericOnly = true;
    spec.initialEntry = QString();
    spec.wheelBaseValue = currentValue;
    return DcbEntryCommand(spec);
}

DcbEntryCommand DcbEntryCommand::charSize(CommandType type,
                                          QString label,
                                          int currentValue) {
    Spec spec;
    spec.type = type;
    spec.headingLines = {QStringLiteral("CHAR SIZE"), std::move(label)};
    spec.minValue = 1;
    spec.maxValue = type == CommandType::DcbCharSize ? 3 : 6;
    spec.wheelStep = 1;
    spec.invalidMessage = QStringLiteral("INVALID SIZE");
    spec.numericOnly = true;
    spec.initialEntry = QString();
    spec.wheelBaseValue = currentValue;
    return DcbEntryCommand(spec);
}

DcbEntryCommand DcbEntryCommand::deleteAllDbAreas() {
    Spec spec;
    spec.type = CommandType::DeleteAllDbAreas;
    spec.headingLines = {
        QStringLiteral("DB AREA"),
        QStringLiteral("DELETE ALL AREAS?"),
        QStringLiteral("1 = NO"),
        QStringLiteral("2 = YES"),
    };
    spec.minValue = 1;
    spec.maxValue = 2;
    spec.wheelStep = 1;
    spec.invalidMessage = QStringLiteral("INVALID ENTRY");
    spec.numericOnly = true;
    spec.initialEntry = QString();
    spec.wheelBaseValue = 1;
    spec.entryPrefix = QStringLiteral("(1 OR 2):");
    spec.entryColumnOffset = 10;
    return DcbEntryCommand(spec);
}

QStringList DcbEntryCommand::displayLines() const {
    QStringList lines = spec_.headingLines;
    if (spec_.entryPrefix.isEmpty())
        lines << value_;
    else
        lines << spec_.entryPrefix + QStringLiteral(" ") + value_;
    return lines;
}

int DcbEntryCommand::cursorLine() const {
    return spec_.headingLines.size() + 1;
}

int DcbEntryCommand::cursorColumn() const {
    if (spec_.entryColumnOffset > 0) return spec_.entryColumnOffset + cursor_;
    return cursor_;
}

CommandType DcbEntryCommand::type() const {
    return spec_.type;
}

QString DcbEntryCommand::invalidMessage() const {
    return spec_.invalidMessage;
}

void DcbEntryCommand::insert(QChar c) {
    if (spec_.numericOnly && !c.isDigit()) return;

    if (resetOnFirstType_) {
        value_.clear();
        cursor_ = 0;
        resetOnFirstType_ = false;
    }

    value_.insert(cursor_, c);
    ++cursor_;
}

void DcbEntryCommand::backspace() {
    resetOnFirstType_ = false;
    if (cursor_ <= 0) return;

    value_.remove(cursor_ - 1, 1);
    --cursor_;
}

void DcbEntryCommand::deleteForward() {
    resetOnFirstType_ = false;
    if (cursor_ < 0 || cursor_ >= value_.size()) return;

    value_.remove(cursor_, 1);
}

void DcbEntryCommand::moveLeft() {
    resetOnFirstType_ = false;
    cursor_ = std::max(0, cursor_ - 1);
}

void DcbEntryCommand::moveRight() {
    resetOnFirstType_ = false;
    cursor_ = std::min(cursor_ + 1, static_cast<int>(value_.size()));
}

void DcbEntryCommand::wheelDelta(int steps) {
    int value = currentOrMinimum();
    value += steps * spec_.wheelStep;
    if (spec_.wrapWheel && spec_.maxValue >= spec_.minValue) {
        const int span = spec_.maxValue - spec_.minValue + 1;
        value = ((value - spec_.minValue) % span + span) % span + spec_.minValue;
    } else {
        value = std::clamp(value, spec_.minValue, spec_.maxValue);
    }

    value_ = QString::number(value);
    cursor_ = value_.size();
    resetOnFirstType_ = false;
}

bool DcbEntryCommand::valueInt(int* out) const {
    bool ok = false;
    const int value = value_.trimmed().toInt(&ok);
    if (!ok) return false;
    if (value < spec_.minValue || value > spec_.maxValue) return false;

    if (out) *out = value;
    return true;
}

DcbEntryCommand::DcbEntryCommand(Spec spec)
    : spec_(std::move(spec)),
      value_(spec_.initialEntry),
      cursor_(value_.size()) {}

int DcbEntryCommand::currentOrMinimum() const {
    bool ok = false;
    const int value = value_.trimmed().toInt(&ok);
    if (ok) return std::clamp(value, spec_.minValue, spec_.maxValue);
    return std::clamp(spec_.wheelBaseValue, spec_.minValue, spec_.maxValue);
}

}  // namespace asdex
