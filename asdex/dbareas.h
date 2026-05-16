#ifndef ASDEX_DBAREAS_H_
#define ASDEX_DBAREAS_H_

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

    int dataBlockCharSize = 2;
    int dataBlockBrightness = 95;
    int leaderLength = 2;
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

private:
    QVector<DbArea> areas_;
};

bool pointInPolygon(const QVector<QPointF>& polygon, const QPointF& point);

void drawDbAreas(const DbAreaStore& store, renderer::CommandBuffer* commandBuffer);

void drawDbAreaDraft(const QVector<QPointF>& committedPoints,
                     std::optional<QPointF> mousePoint,
                     renderer::CommandBuffer* commandBuffer);

}  // namespace asdex

#endif  // ASDEX_DBAREAS_H_
