#pragma once

#include <QHash>
#include <QObject>
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
        std::optional<double> lat, lon, altitude, speed, heading;
    };

    explicit TgtCache(QString icao, QObject* parent = nullptr);

    const QHash<QString, Target>& targets() const { return targets_; }

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
