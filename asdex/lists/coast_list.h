#pragma once

#include "asdex/lists/screen_list.h"

#include <QDateTime>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector>

#include <cstdint>
#include <utility>

namespace renderer {
class BitmapFont;
class TextBuilder;
}

namespace asdex {

enum class CoastListEntryStatus {
    Coasting,
    Suspended,
    Dropped,
};

struct CoastListEntry {
    CoastListEntryStatus status = CoastListEntryStatus::Coasting;

    QString trackId;
    QString callsign;
    QString beaconCode;

    double timeoutSeconds = 0.0;
    bool selected = false;
};

class CoastList {
public:
    CoastList();

    void setVisible(bool visible) { visible_ = visible; }
    bool visible() const { return visible_; }

    void setEntries(QVector<CoastListEntry> entries) { entries_ = std::move(entries); }

    void render(renderer::TextBuilder& textBuilder,
                const renderer::BitmapFont& font,
                std::uint32_t fontTextureId,
                QSize displaySize) const;

private:
    QPointF locationForDisplay(QSize displaySize) const;
    TextBlock buildHeaderBlock(QDateTime utcNow) const;

    TextBlock buildFullBlock(QDateTime utcNow) const;
    QString entryLine(const CoastListEntry& entry) const;
    QChar entryChar(CoastListEntryStatus status) const;

    bool visible_ = true;
    bool expanded_ = false;
    int offset_ = 0;

    ScreenListStyle style_;
    QVector<CoastListEntry> entries_;
};

} // namespace asdex
