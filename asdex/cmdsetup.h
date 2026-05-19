#ifndef ASDEX_CMDSETUP_H_
#define ASDEX_CMDSETUP_H_

#include "asdex/commands.h"

#include <QChar>
#include <QString>
#include <QStringList>

#include <algorithm>
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

class TempTextEntryCommand {
public:
    QString line1;
    QString line2;
    int activeLine = 0;
    int cursor = 0;

    QStringList displayLines() const {
        return {
            QStringLiteral("TEMP DATA"),
            QStringLiteral("DEFINE TEXT"),
            QStringLiteral(">: ") + line1,
            QStringLiteral(">: ") + line2,
        };
    }

    int cursorLine() const { return activeLine == 0 ? 3 : 4; }
    int cursorColumn() const { return 3 + cursor; }

    QString& activeText() { return activeLine == 0 ? line1 : line2; }
    const QString& activeText() const { return activeLine == 0 ? line1 : line2; }

    void insert(QChar c) {
        c = c.toUpper();
        const bool allowed = c.isLetterOrNumber()
                          || c == QLatin1Char(' ')
                          || c == QLatin1Char('.')
                          || c == QLatin1Char('/');
        if (!allowed) return;

        QString& text = activeText();
        text.insert(cursor, c);
        ++cursor;
    }

    void backspace() {
        QString& text = activeText();
        if (cursor <= 0) return;
        text.remove(cursor - 1, 1);
        --cursor;
    }

    void deleteForward() {
        QString& text = activeText();
        if (cursor < 0 || cursor >= text.size()) return;
        text.remove(cursor, 1);
    }

    void moveLeft() { cursor = std::max(0, cursor - 1); }
    void moveRight() { cursor = std::min(cursor + 1, int(activeText().size())); }

    void setActiveLine(int line) {
        activeLine = std::clamp(line, 0, 1);
        cursor = activeText().size();
    }

    void cycleActiveLine(int delta) { setActiveLine(activeLine + delta); }

    bool handleEnter() {
        if (activeLine == 0) {
            setActiveLine(1);
            return false;
        }

        return true;
    }
};

}  // namespace asdex

#endif  // ASDEX_CMDSETUP_H_
