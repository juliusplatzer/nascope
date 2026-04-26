#include "tgtcache.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>

namespace asdex {

namespace {

constexpr char   kWsUrl[]           = "ws://localhost:8080/ws";
constexpr int    kReconnectDelayMs  = 2000;
constexpr int    kHistMaxLen        = 7;
constexpr qint64 kHistMinSpaceMs    = 5000;

// Apply a JSON value to a target field. We deliberately treat JSON null as
// "no update" (regardless of the server's full/partial flag): identity fields
// like tgtType/callsign/wake aren't always re-asserted on every full report
// (e.g. a position-only positionReport with full=true), and dropping them
// would flip an aircraft to Unknown for a frame. Cache is cumulative —
// removals come via the explicit `removed: true` frame.
void setString(QString& dst, const QJsonValue& v) {
    if (v.isNull()) return;
    dst = v.toString();
}
void setOptDouble(std::optional<double>& dst, const QJsonValue& v) {
    if (v.isNull()) return;
    dst = v.toDouble();
}

// SMES position-only positionReports default tgtType to "unknown" (the literal
// string) when no flightInfo is present, even with full=true — so we'd lose a
// previously-identified aircraft on every plain position update. Never
// downgrade aircraft → unknown; only ever upgrade.
void setTgtType(QString& dst, const QJsonValue& v) {
    if (v.isNull()) return;
    const QString next = v.toString();
    if (next == QLatin1String("aircraft") || dst.isEmpty()) dst = next;
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

    const QString airport = msg.value("airport").toString();

    Target& t = targets_[key];
    if (t.airport.isEmpty()) t.airport = airport;

    // Snapshot the pre-diff position into history (≥ 5 s spacing). Done
    // before applying field updates so the trail captures *previous*
    // positions and the live `lat`/`lon` always remain disjoint from history.
    if (t.lat && t.lon) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - t.historyMs >= kHistMinSpaceMs) {
            t.posHistory.append(QPointF(*t.lon, *t.lat));
            while (t.posHistory.size() > kHistMaxLen) t.posHistory.removeFirst();
            t.historyMs = nowMs;
        }
    }

    for (auto it = changedObj.constBegin(); it != changedObj.constEnd(); ++it) {
        const QString& k = it.key();
        const QJsonValue& v = it.value();
        if      (k == QLatin1String("tgtType"))  setTgtType(t.tgtType, v);
        else if (k == QLatin1String("callsign")) setString(t.callsign, v);
        else if (k == QLatin1String("acType"))   setString(t.acType,   v);
        else if (k == QLatin1String("squawk"))   setString(t.squawk,   v);
        else if (k == QLatin1String("exitFix"))  setString(t.exitFix,  v);
        else if (k == QLatin1String("wake"))     setString(t.wake,     v);
        else if (k == QLatin1String("lat"))      setOptDouble(t.lat,      v);
        else if (k == QLatin1String("lon"))      setOptDouble(t.lon,      v);
        else if (k == QLatin1String("altitude")) setOptDouble(t.altitude, v);
        else if (k == QLatin1String("speed"))    setOptDouble(t.speed,    v);
        else if (k == QLatin1String("heading"))  setOptDouble(t.heading,  v);
    }

    emit changed();
}

} // namespace asdex
