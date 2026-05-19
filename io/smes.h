#ifndef IO_SMES_H_
#define IO_SMES_H_

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QtWebSockets/QWebSocket>

#include <optional>

namespace io {

struct SmesTarget {
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

class SmesClient : public QObject {
    Q_OBJECT
public:
    explicit SmesClient(QString icao, QObject* parent = nullptr);
    ~SmesClient() override;

    const QHash<QString, SmesTarget>& targets() const { return targets_; }
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
    QHash<QString, SmesTarget> targets_;
};

} // namespace io

#endif  // IO_SMES_H_
