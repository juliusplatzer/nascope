#ifndef IO_ATIS_H_
#define IO_ATIS_H_

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace io {

struct AtisRunwayState {
    QString airport;
    QStringList landingRunways;
    QStringList departureRunways;
    QString rawText;
    QDateTime updatedAtUtc;
    bool valid = false;
};

class AtisFeed : public QObject {
    Q_OBJECT

public:
    explicit AtisFeed(QString airport, QObject* parent = nullptr);

    const AtisRunwayState& state() const { return state_; }
    QString airport() const { return airport_; }

    void setAirport(const QString& airport);
    void refreshNow();

signals:
    void changed();

private:
    void handlePayload(const QByteArray& bytes);

    QString airport_;
    QNetworkAccessManager network_;
    QTimer refreshTimer_;
    AtisRunwayState state_;
    bool inFlight_ = false;
};

} // namespace io

#endif  // IO_ATIS_H_
