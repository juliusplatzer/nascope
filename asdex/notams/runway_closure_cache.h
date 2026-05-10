#pragma once

#include "asdex/render/temp_areas.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

namespace asdex {

class RunwayClosureCache : public QObject {
    Q_OBJECT

public:
    RunwayClosureCache(QString icao, QString scraperPath, QObject* parent = nullptr);

    const QSet<QString>& closedRunways() const { return closedRunways_; }
    const QVector<TempArea>& closedTempAreas() const { return closedTempAreas_; }
    QString airport() const { return icao_; }

    bool loadSurfaceFile(const QString& path,
                         const QPointF& anchorLonLat,
                         QString* error = nullptr);

    void setAirport(const QString& icao);
    void refreshNow();

signals:
    void changed();

private:
    struct ClosedAreaClosure {
        QString id;
        QString btnFrom;
        QString btnTo;
    };

    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handlePayload(const QByteArray& bytes);
    void rebuildClosedTempAreas();

    QString icao_;
    QString scraperPath_;
    QTimer refreshTimer_;
    QProcess process_;
    QSet<QString> closedRunways_;
    QList<ClosedAreaClosure> closedAreaClosures_;
    QJsonObject surfaceJson_;
    QVector<QVector<QPointF>> twysByIndexFeet_;
    QVector<QString> twyIdsByIndex_;
    QHash<QString, QList<int>> exactTwyIndices_;
    QVector<TempArea> closedTempAreas_;
};

} // namespace asdex
