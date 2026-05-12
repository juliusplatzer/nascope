#include "asdex/cmdslew.h"

#include <QRegularExpression>

#include <algorithm>

namespace asdex {
namespace {

QString normalized(QString value) {
    return value.trimmed().toUpper();
}

bool matches(const QRegularExpression& expression, const QString& value) {
    const QRegularExpressionMatch match = expression.match(value);
    return match.hasMatch() && match.capturedLength() == value.size();
}

} // namespace

QString DatablockEditCommand::EditField::displayLine() const {
    return label + QLatin1Char(' ') + value;
}

int DatablockEditCommand::EditField::cursorColumn() const {
    return columnOffset + cursor;
}

void DatablockEditCommand::EditField::insert(QChar c) {
    if (resetOnFirstType) {
        value.clear();
        cursor = 0;
        resetOnFirstType = false;
    }

    value.insert(cursor, c);
    ++cursor;
}

void DatablockEditCommand::EditField::backspace() {
    resetOnFirstType = false;
    if (cursor <= 0) return;

    value.remove(cursor - 1, 1);
    --cursor;
}

void DatablockEditCommand::EditField::deleteForward() {
    resetOnFirstType = false;
    if (cursor < 0 || cursor >= value.size()) return;

    value.remove(cursor, 1);
}

DatablockEditCommand DatablockEditCommand::fromTarget(const AsdexTarget& target) {
    DatablockEditCommand command;
    command.fields_ = {
        EditField{QStringLiteral("A/C:"), normalized(target.callsign)},
        EditField{QStringLiteral("BCN:"), normalized(target.beaconCode)},
        EditField{QStringLiteral("CAT:"), normalized(target.category)},
        EditField{QStringLiteral("TYP:"), normalized(target.aircraftType)},
        EditField{QStringLiteral("FIX:"), normalized(target.fix)},
        EditField{QStringLiteral("SP1:"), normalized(target.scratchpad1)},
        EditField{QStringLiteral("SP2:"), normalized(target.scratchpad2)},
    };
    command.activateField(0);
    return command;
}

QStringList DatablockEditCommand::displayLines() const {
    QStringList lines;
    lines.reserve(fields_.size());
    for (const EditField& field : fields_) lines << field.displayLine();
    return lines;
}

int DatablockEditCommand::cursorLine() const {
    return active_ + 1;
}

int DatablockEditCommand::cursorColumn() const {
    return activeField().cursorColumn();
}

void DatablockEditCommand::insert(QChar c) {
    if (!isAllowedInputChar(c)) return;
    activeField().insert(c.toUpper());
}

void DatablockEditCommand::backspace() {
    activeField().backspace();
}

void DatablockEditCommand::deleteForward() {
    activeField().deleteForward();
}

void DatablockEditCommand::moveLeft() {
    EditField& field = activeField();
    field.resetOnFirstType = false;
    field.cursor = std::max(0, field.cursor - 1);
}

void DatablockEditCommand::moveRight() {
    EditField& field = activeField();
    field.resetOnFirstType = false;
    field.cursor = std::min(static_cast<int>(field.value.size()), field.cursor + 1);
}

void DatablockEditCommand::moveUp() {
    activateField(active_ - 1);
}

void DatablockEditCommand::moveDown() {
    activateField(active_ + 1);
}

bool DatablockEditCommand::enter() {
    if (active_ >= static_cast<int>(fields_.size()) - 1) return true;

    activateField(active_ + 1);
    return false;
}

EditedDbFields DatablockEditCommand::values() const {
    EditedDbFields values;
    if (fields_.size() < 7) return values;

    values.callsign = normalized(fields_.at(0).value);
    values.beaconCode = normalized(fields_.at(1).value);
    values.category = normalized(fields_.at(2).value);
    values.aircraftType = normalized(fields_.at(3).value);
    values.fix = normalized(fields_.at(4).value);
    values.scratchpad1 = normalized(fields_.at(5).value);
    values.scratchpad2 = normalized(fields_.at(6).value);
    return values;
}

bool DatablockEditCommand::validateForTarget(const AsdexTarget& target, QString* error) const {
    static const QRegularExpression kBeaconRe(QStringLiteral("^[0-7]{4}$|^$"));
    static const QRegularExpression kCategoryRe(QStringLiteral("^[A-Z]$|^$"));
    static const QRegularExpression kAircraftTypeRe(QStringLiteral("^[A-Z\\d]{0,4}$"));
    static const QRegularExpression kFixRe(QStringLiteral("^[A-Z\\d]{3}$|^$"));
    static const QRegularExpression kScratchpadRe(QStringLiteral("^[A-Z\\d]{0,7}$"));

    const EditedDbFields edited = values();
    const QString targetCallsign = normalized(target.callsign);

    if (edited.callsign != targetCallsign
        && (!targetCallsign.isEmpty() || !edited.callsign.isEmpty())) {
        if (error) *error = QStringLiteral("INVALID ENTRY");
        return false;
    }

    const bool valid = matches(kBeaconRe, edited.beaconCode)
        && matches(kCategoryRe, edited.category)
        && matches(kAircraftTypeRe, edited.aircraftType)
        && matches(kFixRe, edited.fix)
        && matches(kScratchpadRe, edited.scratchpad1)
        && matches(kScratchpadRe, edited.scratchpad2);

    if (!valid && error) *error = QStringLiteral("INVALID ENTRY");
    return valid;
}

bool DatablockEditCommand::isAllowedInputChar(QChar c) {
    return c.isLetterOrNumber()
        || c == QLatin1Char(' ')
        || c == QLatin1Char('.')
        || c == QLatin1Char('/');
}

void DatablockEditCommand::activateField(int index) {
    if (fields_.isEmpty()) {
        active_ = 0;
        return;
    }

    active_ = std::clamp(index, 0, static_cast<int>(fields_.size()) - 1);
    EditField& field = fields_[active_];
    field.cursor = field.value.size();
    field.resetOnFirstType = true;
}

DatablockEditCommand::EditField& DatablockEditCommand::activeField() {
    return fields_[active_];
}

const DatablockEditCommand::EditField& DatablockEditCommand::activeField() const {
    return fields_.at(active_);
}

} // namespace asdex
