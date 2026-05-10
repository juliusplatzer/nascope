#pragma once

#include <QObject>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QTimer>

namespace asdex {

class RunwayClosureCache : public QObject {
    Q_OBJECT

public:
    RunwayClosureCache(QString icao, QString scraperPath, QObject* parent = nullptr);

    const QSet<QString>& closedRunways() const { return closedRunways_; }
    QString airport() const { return icao_; }

    void setAirport(const QString& icao);
    void refreshNow();

signals:
    void changed();

private:
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handlePayload(const QByteArray& bytes);

    QString icao_;
    QString scraperPath_;
    QTimer refreshTimer_;
    QProcess process_;
    QSet<QString> closedRunways_;
};

} // namespace asdex
