#include "tgtcache.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>

namespace asdex {

namespace {

constexpr char kWsUrl[]            = "ws://localhost:8080/ws";
constexpr int  kReconnectDelayMs   = 2000;

// Apply a JSON value to a target field. For full reports an explicit JSON null
// clears the field; for partial reports the server omits unchanged fields, so
// null in `changed` shouldn't occur — guarded anyway.
void setString(QString& dst, const QJsonValue& v, bool isFull) {
    if (v.isNull()) { if (isFull) dst.clear(); return; }
    dst = v.toString();
}
void setOptDouble(std::optional<double>& dst, const QJsonValue& v, bool isFull) {
    if (v.isNull()) { if (isFull) dst.reset(); return; }
    dst = v.toDouble();
}

} // namespace

TgtCache::TgtCache(QString icao, QObject* parent)
    : QObject(parent), icao_(std::move(icao)) {
    reconnect_.setSingleShot(true);
    reconnect_.setInterval(kReconnectDelayMs);
    connect(&reconnect_, &QTimer::timeout, this, &TgtCache::openSocket);

    connect(&ws_, &QWebSocket::connected,    this, &TgtCache::onConnected);
    connect(&ws_, &QWebSocket::disconnected, this, &TgtCache::onDisconnected);
    connect(&ws_, &QWebSocket::textMessageReceived, this, &TgtCache::onTextMessage);

    openSocket();
}

void TgtCache::openSocket() {
    ws_.open(QUrl(QString::fromLatin1(kWsUrl)));
}

void TgtCache::onConnected() {
    QJsonObject msg;
    msg.insert("type", "setAirports");
    QJsonArray airports;
    airports.append(icao_);
    msg.insert("airports", airports);
    ws_.sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void TgtCache::onDisconnected() {
    if (!targets_.isEmpty()) {
        targets_.clear();
        emit changed();
    }
    reconnect_.start();
}

void TgtCache::onTextMessage(const QString& text) {
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject()) return;
    const QJsonObject msg = doc.object();

    // Hello frame — ignore (connection just confirms the socket is live).
    if (msg.value("type").toString() == QLatin1String("connected")) return;

    const QString key = msg.value("key").toString();
    if (key.isEmpty()) return;

    if (msg.value("removed").toBool(false)) {
        if (targets_.remove(key) > 0) emit changed();
        return;
    }

    const QJsonObject changedObj = msg.value("changed").toObject();
    if (changedObj.isEmpty()) return;

    const bool   isFull  = msg.value("isFull").toBool(false);
    const QString airport = msg.value("airport").toString();

    Target& t = targets_[key];
    if (t.airport.isEmpty()) t.airport = airport;

    for (auto it = changedObj.constBegin(); it != changedObj.constEnd(); ++it) {
        const QString& k = it.key();
        const QJsonValue& v = it.value();
        if      (k == QLatin1String("tgtType"))  setString(t.tgtType,  v, isFull);
        else if (k == QLatin1String("callsign")) setString(t.callsign, v, isFull);
        else if (k == QLatin1String("acType"))   setString(t.acType,   v, isFull);
        else if (k == QLatin1String("squawk"))   setString(t.squawk,   v, isFull);
        else if (k == QLatin1String("exitFix"))  setString(t.exitFix,  v, isFull);
        else if (k == QLatin1String("wake"))     setString(t.wake,     v, isFull);
        else if (k == QLatin1String("lat"))      setOptDouble(t.lat,      v, isFull);
        else if (k == QLatin1String("lon"))      setOptDouble(t.lon,      v, isFull);
        else if (k == QLatin1String("altitude")) setOptDouble(t.altitude, v, isFull);
        else if (k == QLatin1String("speed"))    setOptDouble(t.speed,    v, isFull);
        else if (k == QLatin1String("heading"))  setOptDouble(t.heading,  v, isFull);
    }

    emit changed();
}

} // namespace asdex
