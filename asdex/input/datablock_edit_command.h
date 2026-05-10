#pragma once

#include "asdex/render/targets.h"

#include <QChar>
#include <QString>
#include <QStringList>
#include <QVector>

namespace asdex {

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

} // namespace asdex
