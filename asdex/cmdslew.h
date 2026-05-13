#ifndef ASDEX_CMDSLEW_H_
#define ASDEX_CMDSLEW_H_

#include "asdex/targetcache.h"

#include <QChar>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <utility>

namespace asdex {

enum class CommandType {
    None,
    EditDatablockFields,
    Range,
    Rotate,
    VectorLength,
};

struct EditedDbFields {
    QString callsign;
    QString beaconCode;
    QString category;
    QString aircraftType;
    QString fix;
    QString scratchpad1;
    QString scratchpad2;
};

class DatablockEditCommand {
public:
    static DatablockEditCommand fromTarget(const AsdexTarget& target);

    QStringList displayLines() const;

    int cursorLine() const;
    int cursorColumn() const;

    void insert(QChar c);
    void backspace();
    void deleteForward();
    void moveLeft();
    void moveRight();
    void moveUp();
    void moveDown();

    bool enter();

    EditedDbFields values() const;
    bool validateForTarget(const AsdexTarget& target, QString* error = nullptr) const;

private:
    struct EditField {
        QString label;
        QString value;
        int cursor = 0;
        int columnOffset = 5;
        bool resetOnFirstType = true;

        QString displayLine() const;
        int cursorColumn() const;
        void insert(QChar c);
        void backspace();
        void deleteForward();
    };

    static bool isAllowedInputChar(QChar c);

    void activateField(int index);
    EditField& activeField();
    const EditField& activeField() const;

    QVector<EditField> fields_;
    int active_ = 0;
};

class DcbEntryCommand {
public:
    struct Spec {
        CommandType type = CommandType::None;
        QString label;

        int minValue = 0;
        int maxValue = 0;
        int wheelStep = 1;

        QString invalidMessage = QStringLiteral("INVALID ENTRY");
        bool numericOnly = true;
        bool wrapWheel = false;
    };

    static DcbEntryCommand range(int currentRange) {
        Spec spec;
        spec.type = CommandType::Range;
        spec.label = QStringLiteral("RANGE");
        spec.minValue = 6;
        spec.maxValue = 300;
        spec.wheelStep = 1;
        spec.invalidMessage = QStringLiteral("INVALID RANGE");
        spec.numericOnly = true;
        return DcbEntryCommand(spec, QString::number(currentRange));
    }

    static DcbEntryCommand rotate(int currentRotation) {
        Spec spec;
        spec.type = CommandType::Rotate;
        spec.label = QStringLiteral("ROTATE");
        spec.minValue = 0;
        spec.maxValue = 359;
        spec.wheelStep = 1;
        spec.invalidMessage = QStringLiteral("INVALID ENTRY");
        spec.numericOnly = true;
        spec.wrapWheel = true;
        return DcbEntryCommand(spec, QString::number(currentRotation));
    }

    static DcbEntryCommand vectorLength(int currentVectorLength) {
        Spec spec;
        spec.type = CommandType::VectorLength;
        spec.label = QStringLiteral("VECTOR LENGTH");
        spec.minValue = 1;
        spec.maxValue = 20;
        spec.wheelStep = 1;
        spec.invalidMessage = QStringLiteral("INVALID ENTRY");
        spec.numericOnly = true;
        return DcbEntryCommand(spec, QString::number(currentVectorLength));
    }

    QStringList displayLines() const { return {spec_.label, value_}; }
    int cursorLine() const { return 2; }
    int cursorColumn() const { return cursor_; }
    CommandType type() const { return spec_.type; }
    QString invalidMessage() const { return spec_.invalidMessage; }

    void insert(QChar c) {
        if (spec_.numericOnly && !c.isDigit()) return;

        if (resetOnFirstType_) {
            value_.clear();
            cursor_ = 0;
            resetOnFirstType_ = false;
        }

        value_.insert(cursor_, c);
        ++cursor_;
    }

    void backspace() {
        resetOnFirstType_ = false;
        if (cursor_ <= 0) return;

        value_.remove(cursor_ - 1, 1);
        --cursor_;
    }

    void deleteForward() {
        resetOnFirstType_ = false;
        if (cursor_ < 0 || cursor_ >= value_.size()) return;

        value_.remove(cursor_, 1);
    }

    void moveLeft() {
        resetOnFirstType_ = false;
        cursor_ = std::max(0, cursor_ - 1);
    }

    void moveRight() {
        resetOnFirstType_ = false;
        cursor_ = std::min(cursor_ + 1, static_cast<int>(value_.size()));
    }

    void wheelDelta(int steps) {
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

    bool valueInt(int* out) const {
        bool ok = false;
        const int value = value_.trimmed().toInt(&ok);
        if (!ok) return false;
        if (value < spec_.minValue || value > spec_.maxValue) return false;

        if (out) *out = value;
        return true;
    }

private:
    DcbEntryCommand(Spec spec, QString initialValue)
        : spec_(std::move(spec)),
          value_(std::move(initialValue)),
          cursor_(value_.size()) {}

    int currentOrMinimum() const {
        bool ok = false;
        const int value = value_.trimmed().toInt(&ok);
        if (!ok) return spec_.minValue;
        return std::clamp(value, spec_.minValue, spec_.maxValue);
    }

    Spec spec_;
    QString value_;
    int cursor_ = 0;
    bool resetOnFirstType_ = true;
};

} // namespace asdex

#endif  // ASDEX_CMDSLEW_H_
