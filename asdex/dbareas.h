#ifndef ASDEX_DBAREAS_H_
#define ASDEX_DBAREAS_H_

#include "asdex/datablock.h"

#include <QPointF>
#include <QString>
#include <QVector>

#include <optional>

namespace renderer {
class CommandBuffer;
}

namespace asdex {

enum class DbAreaKind {
    Trait,
    Off,
};

struct DbAreaTraits {
    bool dataBlocksOff = false;

    bool fullDataBlocks = true;
    bool showAltitude = false;
    bool showAircraftType = true;
    bool showSensors = false;
    bool showAircraftCategory = false;
    bool showFix = true;
    bool showVelocity = false;
    bool showScratchpads = true;

    int dataBlockCharSize = 2;
    int dataBlockBrightness = 95;
    bool showVector = true;
    int leaderLength = 2;
    LeaderDirection leaderDirection = LeaderDirection::NE;
};

struct DbArea {
    QString id;
    DbAreaKind kind = DbAreaKind::Off;
    DbAreaTraits traits;
    QVector<QPointF> polygonFeet;
};

class DbAreaStore {
public:
    const QVector<DbArea>& areas() const { return areas_; }

    void add(DbArea area);
    void clear();

    bool pointInsideOffArea(const QPointF& pointFeet) const;
    const DbArea* firstAreaContaining(const QPointF& pointFeet) const;
    DbArea* areaById(const QString& id);
    const DbArea* areaById(const QString& id) const;
    DbArea* areaAt(int index);
    const DbArea* areaAt(int index) const;
    int indexOfAreaContaining(const QPointF& pointFeet,
                              bool includeOffAreas = true) const;
    bool removeAreaContaining(const QPointF& pointFeet,
                              bool includeOffAreas = true);

private:
    QVector<DbArea> areas_;
};

void drawDbAreas(const DbAreaStore& store, renderer::CommandBuffer* commandBuffer);

void drawDbAreaDraft(const QVector<QPointF>& committedPoints,
                     std::optional<QPointF> mousePoint,
                     renderer::CommandBuffer* commandBuffer);

}  // namespace asdex

#endif  // ASDEX_DBAREAS_H_
