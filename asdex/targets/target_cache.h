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

class TargetCache : public QObject {
    Q_OBJECT

public:
    struct Target {
        QString airport;
        QString tgtType;
        QString callsign;
        QString acType;
        QString squawk;
        QString exitFix;
        QString wake;

        std::optional<double> lat;
        std::optional<double> lon;
        std::optional<double> altitude;
        std::optional<double> speed;
        std::optional<double> heading;

        QList<QPointF> positionHistoryLonLat;
        qint64 historyMs = 0;
    };

    explicit TargetCache(QString icao, QObject* parent = nullptr);
    ~TargetCache() override;

    const QHash<QString, Target>& targets() const { return targets_; }
    QString airport() const { return icao_; }

    void setAirport(const QString& icao);
    void sendDatablockEdit(const QString& facilityId,
                           const QString& trackId,
                           const QString& callsign,
                           const QString& beaconCode,
                           const QString& category,
                           const QString& aircraftType,
                           const QString& fix,
                           const QString& scratchpad1,
                           const QString& scratchpad2);

signals:
    void changed();

private:
    void openSocket();
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString& text);
    void sendAirportFilter();

    QString icao_;
    QWebSocket socket_;
    QTimer reconnect_;
    QHash<QString, Target> targets_;
};

} // namespace asdex
