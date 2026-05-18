#ifndef ASDEX_TARGETCACHE_H_
#define ASDEX_TARGETCACHE_H_

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QtWebSockets/QWebSocket>

#include <optional>

namespace asdex {

struct TargetHistoryPoint {
    QPointF positionFeet;
};

struct AsdexTarget {
    QString id;
    QString callsign;
    QString aircraftType;
    QString category;
    QString beaconCode;
    QString fix;
    QString scratchpad1;
    QString scratchpad2;

    QPointF positionFeet;

    // Aviation convention: 0 = north, 90 = east, 180 = south, 270 = west.
    double headingDegrees = 0.0;
    double groundTrackDegrees = 0.0;
    double groundSpeedKnots = 0.0;

    std::optional<double> altitudeTrue;

    bool correlated = true;
    bool heavy = false;
    bool duplicateBeaconCode = false;
    bool coasting = false;
    bool highlighted = false;

    QVector<TargetHistoryPoint> history;
};

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
        QString scratchpad1;
        QString scratchpad2;

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

#endif  // ASDEX_TARGETCACHE_H_
