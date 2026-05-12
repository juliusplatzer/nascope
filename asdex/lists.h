#ifndef ASDEX_LISTS_H_
#define ASDEX_LISTS_H_

#include <QColor>
#include <QDateTime>
#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <utility>
#include <vector>

namespace renderer {
class BitmapFont;
class LinesBuilder;
class TextBuilder;
}

namespace asdex {

class DatablockEditCommand;

struct TextFragment {
    QString text;
    QColor foreground;
    QColor background = Qt::transparent;
    bool underlined = false;
    bool newLine = true;
};

struct TextBlock {
    std::vector<TextFragment> fragments;
    int lineSpacing = 0;
};

struct ScreenListStyle {
    QPointF location = QPointF(50.0, 150.0);
    QSizeF repositionSize = QSizeF(300.0, 500.0);

    int fontSize = 2;
    int brightness = 95;
    int minBrightness = 20;
    int lineSpacing = 3;

    QColor baseTextColor = QColor(0, 248, 0);
};

class ScreenList {
public:
    explicit ScreenList(ScreenListStyle style);

    void setLocation(QPointF location) { style_.location = location; }
    QPointF location() const { return style_.location; }

    const ScreenListStyle& style() const { return style_; }

    void render(renderer::TextBuilder& textBuilder,
                const renderer::BitmapFont& font,
                std::uint32_t fontTextureId,
                const TextBlock& block) const;

private:
    ScreenListStyle style_;
};

QColor applyCrcBrightness(QColor color, int brightness, int minBrightness = 20);

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

struct PreviewAreaState {
    QString runwayConfigName = QStringLiteral("WEST");
    QStringList towerPositions = {QStringLiteral("LC"), QStringLiteral("GC")};
    QString systemResponse;
};

class PreviewArea {
public:
    PreviewArea();

    void setState(PreviewAreaState state) { state_ = std::move(state); }
    const PreviewAreaState& state() const { return state_; }
    bool setRunwayConfigName(QString name);
    bool updateRunwayConfigFromRunways(const QStringList& landingRunways,
                                       const QStringList& departureRunways);
    void setSystemResponse(QString response);

    bool loadDefaultStateFromConfigFile(const QString& path, QString* error = nullptr);

    void render(renderer::TextBuilder& textBuilder,
                const renderer::BitmapFont& font,
                std::uint32_t fontTextureId,
                const QStringList& commandLines = {}) const;
    void renderCommandCursor(renderer::LinesBuilder& lineBuilder,
                             const renderer::BitmapFont& font,
                             const DatablockEditCommand& command,
                             const QMatrix4x4& screenProjection) const;

private:
    struct RunwayConfiguration {
        QString name;
        QStringList arrivalRunwayIds;
        QStringList departureRunwayIds;
    };

    TextBlock buildTextBlock(const QStringList& commandLines) const;
    QString matchedRunwayConfigName(const QStringList& landingRunways,
                                    const QStringList& departureRunways) const;
    int baseLineCount() const;
    QColor textColor() const;

    ScreenList list_;
    PreviewAreaState state_;
    QString defaultRunwayConfigName_;
    QVector<RunwayConfiguration> runwayConfigurations_;
};

} // namespace asdex

#endif  // ASDEX_LISTS_H_
