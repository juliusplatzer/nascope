#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QtWebSockets/QWebSocket>

#include <optional>

namespace asdex {

/**
 * Client-side mirror of the Java TargetStore. Owns a single QWebSocket
 * connection to the backend, sends `setAirports` to scope the feed, and applies
 * incoming diff/removal frames into a hash keyed by `airport:track:stid`.
 *
 * On connect (and every reconnect), the server replies with a one-shot
 * snapshot — a full-state diff per in-scope target — so a fresh client doesn't
 * need to wait for the natural AT-full cycle to populate.
 *
 * Emits `changed()` after every applied frame; consumers should drive their
 * own repaint from that signal (Qt coalesces).
 */
class TgtCache : public QObject {
    Q_OBJECT
public:
    struct Target {
        QString airport;
        QString tgtType;     // "aircraft" | "unknown"
        QString callsign;    // empty if UNKN/missing
        QString acType;
        QString squawk;
        QString exitFix;
        QString wake;        // CWT A-E (and legacy H/J) = heavy
        // Scratchpads — client-only free-form alphanumeric strings (≤ 7 chars
        // each). Set by the user via the right-click datablock editor; not
        // sent over the WebSocket today, so they survive server diffs but
        // are dropped along with the target on `removed: true` frames.
        QString sp1;
        QString sp2;
        std::optional<double> lat, lon, altitude, speed, heading;

        // Position trail — up to 7 past `(lon, lat)` snapshots, oldest first,
        // each separated by ≥ 5 s wall-clock. Excludes the current position
        // (which lives in `lat`/`lon` above), so a renderer can iterate this
        // list directly to draw a fading tail behind the live symbol.
        QList<QPointF> posHistory;
        qint64         historyMs = 0;  // ms-since-epoch of the most recent push
    };

    explicit TgtCache(QString icao, QObject* parent = nullptr);

    const QHash<QString, Target>& targets() const { return targets_; }

    /** ICAO the cache is currently scoped to (the last setAirport sent). */
    QString airport() const { return icao_; }

    /**
     * Re-targets the cache at a different airport. Drops every target we
     * know about (so the scope doesn't briefly render stale aircraft from
     * the previous facility) and, if the socket is connected, sends a fresh
     * setAirports — the server replies with the snapshot for the new ICAO.
     * If we're disconnected, the next reconnect's onConnected uses the new
     * `icao_` automatically. No-op when `icao` matches the current airport.
     */
    void setAirport(const QString& icao);

    /**
     * Writes the editable subset of fields (the ones surfaced by the
     * datablock editor) back into the cache for `key`. No-op if the key is
     * absent. Emits `changed()` so the scope repaints with the new values
     * on the next event-loop pass. Not persisted — the next server diff
     * for this target may overwrite these fields.
     */
    void applyDatablockEdit(const QString& key,
                            const QString& callsign, const QString& squawk,
                            const QString& wake,     const QString& acType,
                            const QString& exitFix,
                            const QString& sp1,      const QString& sp2);

signals:
    void changed();

private:
    void openSocket();
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString& text);

    QString    icao_;
    QWebSocket ws_;
    QTimer     reconnect_;
    QHash<QString, Target> targets_;
};

} // namespace asdex
