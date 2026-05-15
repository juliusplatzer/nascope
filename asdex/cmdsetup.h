#ifndef ASDEX_CMDSETUP_H_
#define ASDEX_CMDSETUP_H_

#include "asdex/commands.h"

#include <QChar>
#include <QString>
#include <QStringList>

namespace asdex {

bool isBrightnessValueCommand(CommandType type);

class DcbEntryCommand {
public:
    struct Spec {
        CommandType type = CommandType::None;
        QStringList headingLines;

        int minValue = 0;
        int maxValue = 0;
        int wheelStep = 1;

        QString invalidMessage = QStringLiteral("INVALID ENTRY");
        bool numericOnly = true;
        bool wrapWheel = false;
        QString initialEntry;
        int wheelBaseValue = 0;
    };

    static DcbEntryCommand range(int currentRange);
    static DcbEntryCommand rotate(int currentRotation);
    static DcbEntryCommand vectorLength(int currentVectorLength);
    static DcbEntryCommand leaderLength(int currentLeaderLength);
    static DcbEntryCommand brightness(CommandType type, QString label, int currentValue);

    QStringList displayLines() const;
    int cursorLine() const;
    int cursorColumn() const;
    CommandType type() const;
    QString invalidMessage() const;

    void insert(QChar c);
    void backspace();
    void deleteForward();
    void moveLeft();
    void moveRight();
    void wheelDelta(int steps);

    bool valueInt(int* out) const;

private:
    explicit DcbEntryCommand(Spec spec);

    int currentOrMinimum() const;

    Spec spec_;
    QString value_;
    int cursor_ = 0;
    bool resetOnFirstType_ = true;
};

}  // namespace asdex

#endif  // ASDEX_CMDSETUP_H_
