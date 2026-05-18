#include "asdex/atiscache.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QtGlobal>

#include <algorithm>
#include <utility>

namespace asdex {
namespace {

constexpr int kRefreshIntervalMs = 60 * 1000;

QString endpointForAirport(const QString& airport) {
    const QString icao = airport.toUpper();
    const QByteArray customTemplate = qgetenv("NASCOPE_ATIS_URL_TEMPLATE");
    if (!customTemplate.isEmpty()) {
        QString value = QString::fromUtf8(customTemplate);
        if (value.contains(QStringLiteral("%1"))) return value.arg(icao);
        if (value.contains(QStringLiteral("{ICAO}")) || value.contains(QStringLiteral("{icao}"))) {
            value.replace(QStringLiteral("{ICAO}"), icao);
            value.replace(QStringLiteral("{icao}"), icao);
            return value;
        }
        return value + icao;
    }

    return QStringLiteral("https://atis.info/api/%1").arg(icao);
}

QString normalizeRunway(QString number, QString suffix) {
    number = number.trimmed();
    suffix = suffix.trimmed().toUpper();

    bool ok = false;
    const int runway = number.toInt(&ok);
    if (!ok || runway < 1 || runway > 36) return {};

    QString out = QString::number(runway);
    if (!suffix.isEmpty()) out += suffix.left(1);
    return out;
}

void appendUnique(QStringList& out, const QString& value) {
    const QString normalized = value.trimmed().toUpper();
    if (!normalized.isEmpty() && !out.contains(normalized)) out << normalized;
}

void appendRunways(QStringList& out, const QStringList& values) {
    for (const QString& value : values) appendUnique(out, value);
}

QString normalizeAtisText(QString text) {
    text = text.toUpper();
    text.replace(QRegularExpression(QStringLiteral("\\bRUNWAYS\\b")), QStringLiteral("RWYS"));
    text.replace(QRegularExpression(QStringLiteral("\\bRUNWAY\\b")), QStringLiteral("RWY"));
    text.replace(QRegularExpression(QStringLiteral("\\bRY\\b")), QStringLiteral("RWY"));
    text.replace(QRegularExpression(QStringLiteral("\\bDEPARTING\\b")), QStringLiteral("DEPG"));
    text.replace(QRegularExpression(QStringLiteral("\\bDEPARTURES\\b")), QStringLiteral("DEPS"));
    text.replace(QRegularExpression(QStringLiteral("\\bDEPARTURE\\b")), QStringLiteral("DEP"));
    text.replace(QRegularExpression(QStringLiteral("\\bDEPTG\\b")), QStringLiteral("DEPG"));
    text.replace(QRegularExpression(QStringLiteral("\\bLNDG\\b")), QStringLiteral("LDG"));
    text.replace(QRegularExpression(QStringLiteral("\\bLANDING\\b")), QStringLiteral("LDG"));
    text.replace(QRegularExpression(QStringLiteral("\\bAPPROACHES\\b")), QStringLiteral("APCHS"));
    text.replace(QRegularExpression(QStringLiteral("\\bAPPROACH\\b")), QStringLiteral("APCH"));
    return text;
}

QString expandSpokenRunways(QString text) {
    static const QRegularExpression twoDigitSide(
        QStringLiteral("\\b([0-3])\\s+([0-9])\\s+(LEFT|RIGHT|CENTER|CENTRE)\\b"));
    static const QRegularExpression oneOrTwoDigitSide(
        QStringLiteral("\\b([0-3]?\\d)\\s+(LEFT|RIGHT|CENTER|CENTRE)\\b"));

    auto sideLetter = [](const QString& side) {
        if (side.startsWith(QLatin1String("LEFT"))) return QStringLiteral("L");
        if (side.startsWith(QLatin1String("RIGHT"))) return QStringLiteral("R");
        return QStringLiteral("C");
    };

    QRegularExpressionMatchIterator twoDigitMatches = twoDigitSide.globalMatch(text);
    QString expanded;
    qsizetype last = 0;
    while (twoDigitMatches.hasNext()) {
        const QRegularExpressionMatch match = twoDigitMatches.next();
        expanded += text.mid(last, match.capturedStart() - last);
        expanded += match.captured(1) + match.captured(2) + sideLetter(match.captured(3));
        last = match.capturedEnd();
    }
    expanded += text.mid(last);

    QRegularExpressionMatchIterator oneDigitMatches = oneOrTwoDigitSide.globalMatch(expanded);
    text.clear();
    last = 0;
    while (oneDigitMatches.hasNext()) {
        const QRegularExpressionMatch match = oneDigitMatches.next();
        text += expanded.mid(last, match.capturedStart() - last);
        text += match.captured(1) + sideLetter(match.captured(2));
        last = match.capturedEnd();
    }
    text += expanded.mid(last);
    return text;
}

QStringList extractRunways(QString text) {
    text = expandSpokenRunways(normalizeAtisText(text));

    static const QRegularExpression runwayRe(
        QStringLiteral("(?<![A-Z0-9.])(?:RWYS?\\s*)?([0-3]?\\d)\\s*([LRC])?(?![A-Z0-9.])"));

    QStringList runways;
    QRegularExpressionMatchIterator matches = runwayRe.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        appendUnique(runways, normalizeRunway(match.captured(1), match.captured(2)));
    }
    return runways;
}

bool containsAny(const QString& text, const QStringList& needles) {
    for (const QString& needle : needles) {
        if (text.contains(needle)) return true;
    }
    return false;
}

bool isOutOfServiceOnly(const QString& sentence) {
    return containsAny(sentence, {QStringLiteral(" CLSD"),
                                  QStringLiteral(" CLOSED"),
                                  QStringLiteral(" OTS"),
                                  QStringLiteral(" OUT OF SERVICE")})
        && !containsAny(sentence, {QStringLiteral(" IN USE"),
                                   QStringLiteral(" EXPECT"),
                                   QStringLiteral(" LDG"),
                                   QStringLiteral(" DEPG"),
                                   QStringLiteral(" DEP ")});
}

void parseRunwaysFromText(const QString& rawText, QStringList& landing, QStringList& departure) {
    const QString text = normalizeAtisText(rawText);
    const QStringList sentences =
        text.split(QRegularExpression(QStringLiteral("[.;]")), Qt::SkipEmptyParts);

    for (QString sentence : sentences) {
        sentence = sentence.trimmed();
        if (sentence.isEmpty() || isOutOfServiceOnly(sentence)) continue;

        const bool hasBothPhrase =
            sentence.contains(QStringLiteral("LDG AND DEPG"))
            || sentence.contains(QStringLiteral("LND AND DEPG"));
        const bool hasDeparture = containsAny(sentence, {QStringLiteral("DEPG"),
                                                         QStringLiteral("DEP "),
                                                         QStringLiteral("DEPS")});
        const bool hasLanding = containsAny(sentence, {QStringLiteral("LDG"),
                                                       QStringLiteral("ARRIVAL"),
                                                       QStringLiteral("ARRIVALS"),
                                                       QStringLiteral("APCH"),
                                                       QStringLiteral("ILS"),
                                                       QStringLiteral("VISUAL")});
        const bool genericInUse = sentence.startsWith(QStringLiteral("RWYS"))
            && sentence.contains(QStringLiteral(" IN USE"))
            && !hasDeparture;

        if (hasBothPhrase || genericInUse) {
            const QStringList runways = extractRunways(sentence);
            appendRunways(landing, runways);
            appendRunways(departure, runways);
            continue;
        }

        if (hasDeparture && hasLanding) {
            const qsizetype depIndex = std::min({
                sentence.indexOf(QStringLiteral("DEPG")) >= 0 ? sentence.indexOf(QStringLiteral("DEPG")) : sentence.size(),
                sentence.indexOf(QStringLiteral("DEPS")) >= 0 ? sentence.indexOf(QStringLiteral("DEPS")) : sentence.size(),
                sentence.indexOf(QStringLiteral("DEP ")) >= 0 ? sentence.indexOf(QStringLiteral("DEP ")) : sentence.size(),
            });

            appendRunways(landing, extractRunways(sentence.left(depIndex)));
            appendRunways(departure, extractRunways(sentence.mid(depIndex)));
            continue;
        }

        if (hasDeparture) {
            appendRunways(departure, extractRunways(sentence));
        } else if (hasLanding) {
            appendRunways(landing, extractRunways(sentence));
        }
    }
}

QStringList runwaysFromJsonValue(const QJsonValue& value) {
    QStringList runways;

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue& item : array) appendRunways(runways, runwaysFromJsonValue(item));
    } else if (value.isString()) {
        appendRunways(runways, extractRunways(value.toString()));
    } else if (value.isDouble()) {
        appendRunways(runways, extractRunways(QString::number(value.toInt())));
    }

    return runways;
}

bool isLandingKey(const QString& key) {
    const QString k = key.toLower();
    return k == QLatin1String("ldg_rwys")
        || k == QLatin1String("landing_runways")
        || k == QLatin1String("landingrunways");
}

bool isDepartureKey(const QString& key) {
    const QString k = key.toLower();
    return k == QLatin1String("dep_rwys")
        || k == QLatin1String("departure_runways")
        || k == QLatin1String("departurerunways");
}

bool isTextKey(const QString& key) {
    const QString k = key.toLower();
    return k == QLatin1String("datis")
        || k == QLatin1String("raw")
        || k == QLatin1String("text")
        || k == QLatin1String("message")
        || k == QLatin1String("atis")
        || k == QLatin1String("transcript");
}

void collectAtisData(const QJsonValue& value,
                     QStringList& landing,
                     QStringList& departure,
                     QStringList& rawTexts) {
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue& item : array) collectAtisData(item, landing, departure, rawTexts);
        return;
    }

    if (!value.isObject()) return;

    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (isLandingKey(it.key())) {
            appendRunways(landing, runwaysFromJsonValue(it.value()));
        } else if (isDepartureKey(it.key())) {
            appendRunways(departure, runwaysFromJsonValue(it.value()));
        } else if (isTextKey(it.key()) && it.value().isString()) {
            rawTexts << it.value().toString();
        } else if (it.value().isObject() || it.value().isArray()) {
            collectAtisData(it.value(), landing, departure, rawTexts);
        }
    }
}

} // namespace

AtisCache::AtisCache(QString airport, QObject* parent)
    : QObject(parent),
      airport_(airport.toUpper()),
      network_(this) {
    state_.airport = airport_;
    refreshTimer_.setInterval(kRefreshIntervalMs);
    connect(&refreshTimer_, &QTimer::timeout, this, &AtisCache::refreshNow);
    refreshTimer_.start();
    QTimer::singleShot(0, this, &AtisCache::refreshNow);
}

void AtisCache::setAirport(const QString& airport) {
    const QString next = airport.toUpper();
    if (next.isEmpty() || next == airport_) return;

    airport_ = next;
    state_ = {};
    state_.airport = airport_;
    emit changed();
    refreshNow();
}

void AtisCache::refreshNow() {
    if (airport_.isEmpty() || inFlight_) return;

    QNetworkRequest request(QUrl(endpointForAirport(airport_)));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("nascope/1.0"));
    request.setRawHeader("Accept", "application/json");

    inFlight_ = true;
    QNetworkReply* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        inFlight_ = false;
        const QByteArray bytes = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            qWarning().noquote() << "[asdex] ATIS fetch failed:" << airport_ << errorText;
            return;
        }

        handlePayload(bytes);
    });
}

void AtisCache::handlePayload(const QByteArray& bytes) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning().noquote() << "[asdex] ATIS JSON parse failed:" << parseError.errorString();
        return;
    }

    QStringList landing;
    QStringList departure;
    QStringList rawTexts;
    collectAtisData(document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object()),
                    landing,
                    departure,
                    rawTexts);

    if (landing.isEmpty() || departure.isEmpty()) {
        for (const QString& rawText : rawTexts)
            parseRunwaysFromText(rawText, landing, departure);
    }

    AtisRunwayState next;
    next.airport = airport_;
    next.landingRunways = landing;
    next.departureRunways = departure;
    next.rawText = rawTexts.join(QStringLiteral("\n"));
    next.updatedAtUtc = QDateTime::currentDateTimeUtc();
    next.valid = !landing.isEmpty() || !departure.isEmpty() || !rawTexts.isEmpty();

    if (next.landingRunways == state_.landingRunways
        && next.departureRunways == state_.departureRunways
        && next.rawText == state_.rawText
        && next.valid == state_.valid) {
        state_.updatedAtUtc = next.updatedAtUtc;
        return;
    }

    state_ = std::move(next);
    qInfo().noquote() << "[asdex] ATIS" << airport_
                      << "LDG" << state_.landingRunways.join(",")
                      << "DEP" << state_.departureRunways.join(",");
    emit changed();
}

} // namespace asdex
