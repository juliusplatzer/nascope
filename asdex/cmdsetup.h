#ifndef ASDEX_CMDSETUP_H_
#define ASDEX_CMDSETUP_H_

#include "asdex/commands.h"

#include <QChar>
#include <QString>
#include <QStringList>

#include <functional>

namespace asdex {

bool isBrightnessValueCommand(CommandType type);
bool isCharSizeValueCommand(CommandType type);
bool isDbAreaCommand(CommandType type);

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
        QString entryPrefix;
        int entryColumnOffset = 0;
        std::function<void(int)> apply;
        std::function<bool(int, QString*)> validate;
        CommandType nextCommandType = CommandType::None;
    };

    static DcbEntryCommand range(int currentRange,
                                 std::function<void(int)> apply,
                                 CommandType nextCommandType);
    static DcbEntryCommand rotate(int currentRotation,
                                  std::function<void(int)> apply,
                                  CommandType nextCommandType);
    static DcbEntryCommand vectorLength(int currentVectorLength,
                                        std::function<void(int)> apply,
                                        CommandType nextCommandType);
    static DcbEntryCommand leaderLength(int currentLeaderLength,
                                        std::function<void(int)> apply,
                                        CommandType nextCommandType);
    static DcbEntryCommand brightness(CommandType type,
                                      QString label,
                                      int currentValue,
                                      std::function<void(int)> apply,
                                      CommandType nextCommandType);
    static DcbEntryCommand charSize(CommandType type,
                                    QString label,
                                    int currentValue,
                                    std::function<void(int)> apply,
                                    CommandType nextCommandType);
    static DcbEntryCommand deleteAllDbAreas(std::function<void(int)> apply,
                                            CommandType nextCommandType);
    static DcbEntryCommand traitAreaDbCharSize(int currentValue,
                                               std::function<void(int)> apply,
                                               CommandType nextCommandType);
    static DcbEntryCommand traitAreaDbBrightness(int currentValue,
                                                 std::function<void(int)> apply,
                                                 CommandType nextCommandType);
    static DcbEntryCommand traitAreaLeaderLength(int currentValue,
                                                 std::function<void(int)> apply,
                                                 CommandType nextCommandType);
    static DcbEntryCommand traitAreaLeaderDirection(int currentValue,
                                                    std::function<void(int)> apply,
                                                    CommandType nextCommandType);
    static DcbEntryCommand modifyTraitAreaDbCharSize(int currentValue,
                                                     std::function<void(int)> apply,
                                                     CommandType nextCommandType);
    static DcbEntryCommand modifyTraitAreaDbBrightness(int currentValue,
                                                       std::function<void(int)> apply,
                                                       CommandType nextCommandType);
    static DcbEntryCommand modifyTraitAreaLeaderLength(int currentValue,
                                                       std::function<void(int)> apply,
                                                       CommandType nextCommandType);
    static DcbEntryCommand modifyTraitAreaLeaderDirection(int currentValue,
                                                          std::function<void(int)> apply,
                                                          CommandType nextCommandType);

    QStringList displayLines() const;
    int cursorLine() const;
    int cursorColumn() const;
    CommandType type() const;
    CommandType nextCommandType() const;
    QString invalidMessage() const;
    void apply(int value) const;

    void insert(QChar c);
    void backspace();
    void deleteForward();
    void moveLeft();
    void moveRight();
    void wheelDelta(int steps);
    void setEntryValue(int value);

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
