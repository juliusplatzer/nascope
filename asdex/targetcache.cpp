#include "asdex/targetcache.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>
#include <QtGlobal>

#include <utility>

namespace asdex {
namespace {

constexpr int kReconnectDelayMs = 2000;
constexpr int kHistoryMaxLen = 7;
constexpr qint64 kHistoryMinSpaceMs = 5000;

QString websocketUrl() {
    const QByteArray explicitUrl = qgetenv("NASCOPE_TARGET_WS_URL");
    if (!explicitUrl.isEmpty()) return QString::fromUtf8(explicitUrl);

    bool ok = false;
    const int port = qEnvironmentVariableIntValue("WS_PORT", &ok);
    return QStringLiteral("ws://localhost:%1/ws").arg(ok ? port : 8080);
}

void setString(QString& dst, const QJsonValue& value) {
    if (value.isNull()) return;
    dst = value.toString();
}

void setScratchpad(QString& dst, const QJsonValue& value) {
    if (value.isNull()) return;

    const QString next = value.toString();
    dst = next.trimmed().compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0
        ? QString()
        : next;
}

void setOptionalDouble(std::optional<double>& dst, const QJsonValue& value) {
    if (value.isNull()) return;
    dst = value.toDouble();
}

void setTargetType(QString& dst, const QJsonValue& value) {
    if (value.isNull()) return;

    const QString next = value.toString();
    if (next == QLatin1String("aircraft") || dst.isEmpty()) dst = next;
}

} // namespace

TargetCache::TargetCache(QString icao, QObject* parent)
    : QObject(parent),
      icao_(std::move(icao)) {
    reconnect_.setSingleShot(true);
    reconnect_.setInterval(kReconnectDelayMs);

    connect(&reconnect_, &QTimer::timeout, this, &TargetCache::openSocket);
    connect(&socket_, &QWebSocket::connected, this, &TargetCache::onConnected);
    connect(&socket_, &QWebSocket::disconnected, this, &TargetCache::onDisconnected);
    connect(&socket_, &QWebSocket::textMessageReceived, this, &TargetCache::onTextMessage);
    connect(&socket_, &QWebSocket::errorOccurred, this, [](QAbstractSocket::SocketError) {
        qWarning().noquote() << "[asdex] target websocket error";
    });

    openSocket();
}

TargetCache::~TargetCache() {
    reconnect_.stop();
    socket_.close();
}

void TargetCache::openSocket() {
    if (socket_.state() != QAbstractSocket::UnconnectedState) return;
    socket_.open(QUrl(websocketUrl()));
}

void TargetCache::onConnected() {
    sendAirportFilter();
}

void TargetCache::onDisconnected() {
    if (!targets_.isEmpty()) {
        targets_.clear();
        emit changed();
    }
    reconnect_.start();
}

void TargetCache::sendAirportFilter() {
    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("setAirports"));

    QJsonArray airports;
    airports.append(icao_.toUpper());
    message.insert(QStringLiteral("airports"), airports);

    socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void TargetCache::onTextMessage(const QString& text) {
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isObject()) return;

    const QJsonObject message = document.object();
    if (message.value(QStringLiteral("type")).toString() == QLatin1String("connected")) return;

    const QString key = message.value(QStringLiteral("key")).toString();
    if (key.isEmpty()) return;

    if (message.value(QStringLiteral("removed")).toBool(false)) {
        if (targets_.remove(key) > 0) emit changed();
        return;
    }

    const QJsonObject changedObject = message.value(QStringLiteral("changed")).toObject();
    if (changedObject.isEmpty()) return;

    Target& target = targets_[key];
    if (target.airport.isEmpty())
        target.airport = message.value(QStringLiteral("airport")).toString();

    if (target.lat && target.lon) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - target.historyMs >= kHistoryMinSpaceMs) {
            target.positionHistoryLonLat.append(QPointF(*target.lon, *target.lat));
            while (target.positionHistoryLonLat.size() > kHistoryMaxLen)
                target.positionHistoryLonLat.removeFirst();
            target.historyMs = nowMs;
        }
    }

    for (auto it = changedObject.constBegin(); it != changedObject.constEnd(); ++it) {
        const QString& keyName = it.key();
        const QJsonValue& value = it.value();

        if (keyName == QLatin1String("tgtType"))
            setTargetType(target.tgtType, value);
        else if (keyName == QLatin1String("callsign"))
            setString(target.callsign, value);
        else if (keyName == QLatin1String("acType"))
            setString(target.acType, value);
        else if (keyName == QLatin1String("squawk"))
            setString(target.squawk, value);
        else if (keyName == QLatin1String("exitFix"))
            setString(target.exitFix, value);
        else if (keyName == QLatin1String("wake"))
            setString(target.wake, value);
        else if (keyName == QLatin1String("scratchpad1"))
            setScratchpad(target.scratchpad1, value);
        else if (keyName == QLatin1String("scratchpad2"))
            setScratchpad(target.scratchpad2, value);
        else if (keyName == QLatin1String("lat"))
            setOptionalDouble(target.lat, value);
        else if (keyName == QLatin1String("lon"))
            setOptionalDouble(target.lon, value);
        else if (keyName == QLatin1String("altitude"))
            setOptionalDouble(target.altitude, value);
        else if (keyName == QLatin1String("speed"))
            setOptionalDouble(target.speed, value);
        else if (keyName == QLatin1String("heading"))
            setOptionalDouble(target.heading, value);
    }

    emit changed();
}

void TargetCache::setAirport(const QString& icao) {
    const QString next = icao.toUpper();
    if (next.isEmpty() || next == icao_) return;

    icao_ = next;
    if (!targets_.isEmpty()) {
        targets_.clear();
        emit changed();
    }

    if (socket_.state() == QAbstractSocket::ConnectedState) sendAirportFilter();
}

void TargetCache::sendDatablockEdit(const QString& facilityId,
                                    const QString& trackId,
                                    const QString& callsign,
                                    const QString& beaconCode,
                                    const QString& category,
                                    const QString& aircraftType,
                                    const QString& fix,
                                    const QString& scratchpad1,
                                    const QString& scratchpad2) {
    if (socket_.state() != QAbstractSocket::ConnectedState) {
        qWarning().noquote() << "[asdex] cannot send datablock edit: websocket disconnected";
        return;
    }

    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("edit_asdex_db_fields"));
    message.insert(QStringLiteral("facilityId"), facilityId.toUpper());
    message.insert(QStringLiteral("trackId"), trackId);
    message.insert(QStringLiteral("callsign"), callsign);
    message.insert(QStringLiteral("beaconCode"), beaconCode);
    message.insert(QStringLiteral("category"), category);
    message.insert(QStringLiteral("aircraftType"), aircraftType);
    message.insert(QStringLiteral("fix"), fix);
    message.insert(QStringLiteral("scratchpad1"), scratchpad1);
    message.insert(QStringLiteral("scratchpad2"), scratchpad2);

    socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

} // namespace asdex
