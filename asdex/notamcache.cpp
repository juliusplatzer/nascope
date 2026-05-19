#include "asdex/notamcache.h"

#include "math/geom.h"
#include "math/latlong.h"

#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPolygonF>
#include <QRectF>
#include <QStringList>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace asdex {
namespace {

constexpr int kRefreshIntervalMs = 10 * 60 * 1000;
constexpr int kScraperTimeoutMs = 90 * 1000;
constexpr double kTaxiwayAdjacencyToleranceM = 2.0;
constexpr double kRunwayEndpointToleranceM = 10.0;

struct TaxiwayClosureNode {
    int nodeIndex = -1;
    int originalIndex = -1;
    QString id;
    QSet<QString> tokens;
    QPolygonF geomM;
    QPointF centroidM;
    QRectF boundsM;
};

enum class EndpointKind {
    Taxiway,
    Runway,
    Unsupported,
};

struct EndpointSpec {
    EndpointKind kind = EndpointKind::Unsupported;
    QString token;
};

QSet<QString> runwaySetFromJson(const QJsonArray& values) {
    QSet<QString> out;
    for (const QJsonValue& value : values) {
        const QString runway = value.toString().trimmed().toUpper();
        if (!runway.isEmpty()) out.insert(runway);
    }
    return out;
}

QString normalizedTaxiwayToken(QString value) {
    value = value.trimmed().toUpper();
    if (value.startsWith(QStringLiteral("TWY "))) value = value.mid(4).trimmed();
    return value;
}

QString normalizedSurfaceId(QString value) {
    return value.trimmed().toUpper();
}

QSet<QString> idTokens(const QString& id) {
    QSet<QString> tokens;
    const QStringList parts = id.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString token = part.trimmed().toUpper();
        if (!token.isEmpty()) tokens.insert(token);
    }
    return tokens;
}

QString normalizeRunwayId(QString id) {
    id = id.trimmed().toUpper();
    id.replace(QStringLiteral("-"), QStringLiteral("/"));

    const QStringList parts = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return id;

    QStringList normalizedParts;
    for (QString part : parts) {
        part = part.trimmed();
        if (part.size() == 2 && part.at(0).isDigit() && part.at(1).isLetter()) {
            part.prepend(QLatin1Char('0'));
        } else if (part.size() == 1 && part.at(0).isDigit()) {
            part.prepend(QLatin1Char('0'));
        }
        normalizedParts << part;
    }

    return normalizedParts.join(QLatin1Char('/'));
}

QSet<QString> runwayTokens(QString id) {
    id = normalizeRunwayId(id);

    QSet<QString> out;
    out.insert(id);

    const QStringList parts = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        out.insert(part);
        if (part.size() >= 2 && part.at(0) == QLatin1Char('0')) out.insert(part.mid(1));
    }

    return out;
}

EndpointSpec endpointSpec(QString value) {
    value = value.trimmed().toUpper();
    if (value.startsWith(QStringLiteral("TWY "))) {
        return EndpointSpec{EndpointKind::Taxiway, normalizedTaxiwayToken(value)};
    }
    if (value.startsWith(QStringLiteral("RWY "))) {
        return EndpointSpec{EndpointKind::Runway, normalizeRunwayId(value.mid(4))};
    }
    return EndpointSpec{};
}

bool runwayMatches(QString lhs, QString rhs) {
    const QSet<QString> lhsTokens = runwayTokens(lhs);
    const QSet<QString> rhsTokens = runwayTokens(rhs);
    for (const QString& token : lhsTokens) {
        if (rhsTokens.contains(token)) return true;
    }
    return false;
}

bool chooseTaxiwayProjector(const QJsonArray& twys, math::LocalMeterProjector* projector) {
    double lonSum = 0.0;
    double latSum = 0.0;
    int count = 0;

    for (const QJsonValue& value : twys) {
        const QJsonArray points = value.toObject().value(QStringLiteral("polygon")).toArray();
        for (const QJsonValue& pointValue : points) {
            const QJsonArray point = pointValue.toArray();
            if (point.size() < 2) continue;
            lonSum += point.at(0).toDouble();
            latSum += point.at(1).toDouble();
            ++count;
        }
    }

    if (count == 0) return false;

    *projector = math::LocalMeterProjector(lonSum / count, latSum / count);
    return true;
}

QPolygonF projectedPolygon(const QJsonArray& points,
                           const math::LocalMeterProjector& projector) {
    QPolygonF polygon;
    polygon.reserve(points.size());

    for (const QJsonValue& pointValue : points) {
        const QJsonArray point = pointValue.toArray();
        if (point.size() < 2) continue;
        polygon << projector.map(point.at(0).toDouble(), point.at(1).toDouble());
    }

    return polygon;
}

QVector<int> taxiwayEndpointNodeIndices(const QVector<TaxiwayClosureNode>& nodes,
                                        const QString& closedTwy,
                                        const QString& endpointTwy,
                                        const QString& otherEndpointTwy) {
    QVector<int> preferred;
    for (const TaxiwayClosureNode& node : nodes) {
        if (node.tokens.contains(closedTwy)
            && node.tokens.contains(endpointTwy)
            && !node.tokens.contains(otherEndpointTwy)) {
            preferred << node.nodeIndex;
        }
    }
    if (!preferred.isEmpty()) return preferred;

    QVector<int> fallback;
    for (const TaxiwayClosureNode& node : nodes) {
        if (node.tokens.contains(closedTwy) && node.tokens.contains(endpointTwy)) {
            fallback << node.nodeIndex;
        }
    }
    return fallback;
}

QVector<int> runwayEndpointNodeIndices(const QVector<TaxiwayClosureNode>& nodes,
                                       const QJsonObject& airportJson,
                                       const QString& closedTwy,
                                       const QString& runway,
                                       const math::LocalMeterProjector& projector) {
    QVector<QPolygonF> holdbars;
    const QJsonArray hbs = airportJson.value(QStringLiteral("hbs")).toArray();

    for (const QJsonValue& value : hbs) {
        const QJsonObject hb = value.toObject();
        const QString hbRunway = hb.value(QStringLiteral("runway")).toString();
        const QString hbId = hb.value(QStringLiteral("id")).toString();
        if (!runwayMatches(hbRunway, runway) || !idTokens(hbId).contains(closedTwy)) continue;

        const QPolygonF geom = projectedPolygon(hb.value(QStringLiteral("polygon")).toArray(),
                                                projector);
        if (geom.size() >= 2) holdbars << geom;
    }

    if (holdbars.isEmpty()) return {};

    QVector<int> matches;
    for (const TaxiwayClosureNode& node : nodes) {
        if (!node.tokens.contains(closedTwy)) continue;

        for (const QPolygonF& holdbar : holdbars) {
            if (!math::rectsNear(node.boundsM, holdbar.boundingRect(), kRunwayEndpointToleranceM)) {
                continue;
            }
            if (math::polygonDistance(node.geomM, holdbar) <= kRunwayEndpointToleranceM) {
                matches << node.nodeIndex;
                break;
            }
        }
    }

    return matches;
}

QVector<int> endpointNodeIndices(const QVector<TaxiwayClosureNode>& nodes,
                                 const QJsonObject& airportJson,
                                 const QString& closedTwy,
                                 const EndpointSpec& endpoint,
                                 const EndpointSpec& otherEndpoint,
                                 const math::LocalMeterProjector& projector) {
    if (endpoint.kind == EndpointKind::Taxiway) {
        return taxiwayEndpointNodeIndices(nodes,
                                          closedTwy,
                                          endpoint.token,
                                          otherEndpoint.kind == EndpointKind::Taxiway
                                              ? otherEndpoint.token
                                              : QString());
    }

    if (endpoint.kind == EndpointKind::Runway) {
        return runwayEndpointNodeIndices(nodes, airportJson, closedTwy, endpoint.token, projector);
    }

    return {};
}

bool isSupportedClosedArea(const QJsonObject& object) {
    const QString btnFrom = object.value(QStringLiteral("btnFrom")).toString().trimmed();
    const QString btnTo = object.value(QStringLiteral("btnTo")).toString().trimmed();

    if (btnFrom.isEmpty() && btnTo.isEmpty()) return true;
    if (btnFrom.isEmpty() || btnTo.isEmpty()) return false;

    const EndpointSpec from = endpointSpec(btnFrom);
    const EndpointSpec to = endpointSpec(btnTo);
    return from.kind != EndpointKind::Unsupported && to.kind != EndpointKind::Unsupported;
}

QVector<QPointF> polygonFeetFromJson(const QJsonArray& points, const QTransform& lonLatToFeetT) {
    QVector<QPointF> polygon;
    polygon.reserve(points.size());
    for (const QJsonValue& pointValue : points) {
        const QJsonArray point = pointValue.toArray();
        if (point.size() < 2) continue;
        polygon.push_back(lonLatToFeetT.map(QPointF(point.at(0).toDouble(),
                                                    point.at(1).toDouble())));
    }
    return polygon;
}

QJsonArray getClosedTwysFromNotam(const QJsonObject& airportJson,
                                  const QString& closedTwyId,
                                  const QString& fromId,
                                  const QString& toId) {
    const QString closedTwy = normalizedTaxiwayToken(closedTwyId);
    const EndpointSpec from = endpointSpec(fromId);
    const EndpointSpec to = endpointSpec(toId);
    const QJsonArray twys = airportJson.value(QStringLiteral("twys")).toArray();

    if (twys.isEmpty()
        || closedTwy.isEmpty()
        || from.kind == EndpointKind::Unsupported
        || to.kind == EndpointKind::Unsupported) {
        return {};
    }

    math::LocalMeterProjector projector;
    if (!chooseTaxiwayProjector(twys, &projector)) return {};

    QVector<TaxiwayClosureNode> nodes;
    nodes.reserve(twys.size());

    for (int originalIndex = 0; originalIndex < twys.size(); ++originalIndex) {
        const QJsonObject twy = twys.at(originalIndex).toObject();
        const QString id = twy.value(QStringLiteral("id")).toString().trimmed().toUpper();
        const QSet<QString> tokens = idTokens(id);
        if (!tokens.contains(closedTwy)) continue;

        const QPolygonF geom = projectedPolygon(twy.value(QStringLiteral("polygon")).toArray(),
                                                projector);
        if (geom.size() < 3) continue;

        TaxiwayClosureNode node;
        node.nodeIndex = nodes.size();
        node.originalIndex = originalIndex;
        node.id = id;
        node.tokens = tokens;
        node.geomM = geom;
        node.centroidM = math::polygonCentroid(geom);
        node.boundsM = geom.boundingRect();
        nodes << node;
    }

    if (nodes.isEmpty()) return {};

    const QVector<int> startNodes =
        endpointNodeIndices(nodes, airportJson, closedTwy, from, to, projector);
    const QVector<int> endNodeList =
        endpointNodeIndices(nodes, airportJson, closedTwy, to, from, projector);

    QSet<int> endNodes;
    for (const int nodeIndex : endNodeList) endNodes.insert(nodeIndex);
    if (startNodes.isEmpty() || endNodes.isEmpty()) return {};

    QVector<QVector<std::pair<int, double>>> graph(nodes.size());
    for (int i = 0; i < nodes.size(); ++i) {
        for (int j = i + 1; j < nodes.size(); ++j) {
            if (!math::rectsNear(nodes.at(i).boundsM,
                                 nodes.at(j).boundsM,
                                 kTaxiwayAdjacencyToleranceM))
                continue;
            if (math::polygonDistance(nodes.at(i).geomM, nodes.at(j).geomM)
                > kTaxiwayAdjacencyToleranceM)
                continue;

            const QPointF dc = nodes.at(i).centroidM - nodes.at(j).centroidM;
            const double weight = std::hypot(dc.x(), dc.y());
            graph[i].append({j, weight});
            graph[j].append({i, weight});
        }
    }

    QVector<double> dist(nodes.size(), std::numeric_limits<double>::infinity());
    QVector<int> prev(nodes.size(), -1);
    using QueueItem = std::pair<double, int>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> heap;

    for (const int nodeIndex : startNodes) {
        dist[nodeIndex] = 0.0;
        heap.push({0.0, nodeIndex});
    }

    int target = -1;
    while (!heap.empty()) {
        const auto [curDist, u] = heap.top();
        heap.pop();
        if (curDist != dist.at(u)) continue;

        if (endNodes.contains(u)) {
            target = u;
            break;
        }

        for (const auto& [v, weight] : graph.at(u)) {
            const double newDist = curDist + weight;
            if (newDist < dist.at(v)) {
                dist[v] = newDist;
                prev[v] = u;
                heap.push({newDist, v});
            }
        }
    }

    if (target < 0) return {};

    QVector<int> pathNodes;
    for (int u = target; u >= 0; u = prev.at(u)) {
        pathNodes.prepend(u);
        if (startNodes.contains(u)) break;
    }

    QJsonArray result;
    if (pathNodes.size() <= 2) return result;

    for (int i = 1; i < pathNodes.size() - 1; ++i) {
        const TaxiwayClosureNode& node = nodes.at(pathNodes.at(i));
        QJsonObject item;
        item.insert(QStringLiteral("index"), node.originalIndex);
        item.insert(QStringLiteral("id"), node.id);
        result.append(item);
    }

    return result;
}

} // namespace

RunwayClosureCache::RunwayClosureCache(QString icao, QString scraperPath, QObject* parent)
    : QObject(parent),
      icao_(std::move(icao)),
      scraperPath_(std::move(scraperPath)) {
    icao_ = icao_.toUpper();

    refreshTimer_.setInterval(kRefreshIntervalMs);
    processTimeout_.setSingleShot(true);
    processTimeout_.setInterval(kScraperTimeoutMs);

    connect(&refreshTimer_, &QTimer::timeout, this, &RunwayClosureCache::refreshNow);
    connect(&processTimeout_, &QTimer::timeout, this, [this] {
        if (process_.state() == QProcess::NotRunning) return;

        qWarning().noquote() << "[asdex] NOTAM scraper timed out:" << icao_;
        process_.kill();
    });
    connect(&process_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &RunwayClosureCache::handleFinished);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (stoppingProcess_) return;
        qWarning().noquote() << "[asdex] NOTAM scraper process error:" << icao_ << error;
    });

    refreshTimer_.start();
    QTimer::singleShot(0, this, &RunwayClosureCache::refreshNow);
}

RunwayClosureCache::~RunwayClosureCache() {
    refreshTimer_.stop();
    processTimeout_.stop();

    if (process_.state() == QProcess::NotRunning) return;

    stoppingProcess_ = true;
    process_.terminate();
    if (!process_.waitForFinished(1500)) {
        process_.kill();
        process_.waitForFinished(1500);
    }
}

void RunwayClosureCache::setAirport(const QString& icao) {
    const QString next = icao.toUpper();
    if (next.isEmpty() || next == icao_) return;

    icao_ = next;
    closedRunways_.clear();
    closedAreaClosures_.clear();
    restrictedAreaClosures_.clear();
    closedTempAreas_.clear();
    restrictedTempAreas_.clear();
    emit changed();
    refreshNow();
}

bool RunwayClosureCache::loadSurfaceFile(const QString& path,
                                          const QPointF& anchorLonLat,
                                          QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("invalid surface JSON %1: %2")
                         .arg(path, parseError.errorString());
        }
        return false;
    }

    surfaceJson_ = document.object();
    twysByIndexFeet_.clear();
    twyIdsByIndex_.clear();
    exactTwyIndices_.clear();

    const QTransform toFeet = math::lonLatToFeet(anchorLonLat);
    const QJsonArray twys = surfaceJson_.value(QStringLiteral("twys")).toArray();
    twysByIndexFeet_.resize(twys.size());
    twyIdsByIndex_.resize(twys.size());

    for (int i = 0; i < twys.size(); ++i) {
        const QJsonObject twy = twys.at(i).toObject();
        const QString id = normalizedSurfaceId(twy.value(QStringLiteral("id")).toString());
        const QVector<QPointF> polygon =
            polygonFeetFromJson(twy.value(QStringLiteral("polygon")).toArray(), toFeet);
        if (id.isEmpty() || polygon.size() < 3) continue;

        twyIdsByIndex_[i] = id;
        twysByIndexFeet_[i] = polygon;
        exactTwyIndices_[id].append(i);
    }

    rebuildTempAreas();
    return true;
}

void RunwayClosureCache::refreshNow() {
    if (icao_.isEmpty() || process_.state() != QProcess::NotRunning) return;

    if (!QFileInfo::exists(scraperPath_)) {
        qWarning().noquote() << "[asdex] NOTAM scraper not found:" << scraperPath_;
        return;
    }

    process_.setProgram(QStringLiteral("python3"));
    process_.setArguments({scraperPath_, icao_, QStringLiteral("--output"), QStringLiteral("-")});
    stoppingProcess_ = false;
    process_.start();
    processTimeout_.start();
}

void RunwayClosureCache::handleFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    processTimeout_.stop();

    const QByteArray stdoutBytes = process_.readAllStandardOutput();
    const QByteArray stderrBytes = process_.readAllStandardError();
    if (!stderrBytes.trimmed().isEmpty()) {
        qInfo().noquote() << QString::fromUtf8(stderrBytes).trimmed();
    }

    if (stoppingProcess_) {
        stoppingProcess_ = false;
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        qWarning().noquote() << "[asdex] NOTAM scraper failed:" << icao_ << "exit" << exitCode;
        return;
    }

    handlePayload(stdoutBytes);
}

void RunwayClosureCache::handlePayload(const QByteArray& bytes) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning().noquote() << "[asdex] NOTAM closure JSON parse failed:"
                             << parseError.errorString();
        return;
    }

    const QJsonObject root = document.object();
    const QSet<QString> nextRunways =
        runwaySetFromJson(root.value(QStringLiteral("rwyClosures")).toArray());

    auto parseAreaClosures = [](const QJsonArray& values) {
        QList<ClosedAreaClosure> closures;

        for (const QJsonValue& value : values) {
            const QJsonObject object = value.toObject();
            const QString id =
                normalizedTaxiwayToken(object.value(QStringLiteral("id")).toString());
            if (id.isEmpty() || !isSupportedClosedArea(object)) continue;

            ClosedAreaClosure closure;
            closure.id = id;
            closure.btnFrom = object.value(QStringLiteral("btnFrom")).toString().trimmed();
            closure.btnTo = object.value(QStringLiteral("btnTo")).toString().trimmed();
            closures.append(closure);
        }

        return closures;
    };

    QJsonArray closedAreas = root.value(QStringLiteral("closedAreas")).toArray();
    if (closedAreas.isEmpty() && root.contains(QStringLiteral("twyClosures"))) {
        closedAreas = root.value(QStringLiteral("twyClosures")).toArray();
    }

    const QList<ClosedAreaClosure> nextClosedAreas = parseAreaClosures(closedAreas);
    const QList<ClosedAreaClosure> nextRestrictedAreas =
        parseAreaClosures(root.value(QStringLiteral("restrictionAreas")).toArray());

    closedRunways_ = nextRunways;
    closedAreaClosures_ = nextClosedAreas;
    restrictedAreaClosures_ = nextRestrictedAreas;
    rebuildTempAreas();
    qInfo().noquote() << "[asdex] runway closures" << icao_
                      << QStringList(closedRunways_.values()).join(",");
    qInfo().noquote() << "[asdex] closed temp areas" << icao_ << closedTempAreas_.size();
    qInfo().noquote() << "[asdex] restricted temp areas" << icao_
                      << restrictedTempAreas_.size();
    emit changed();
}

void RunwayClosureCache::rebuildTempAreas() {
    closedTempAreas_ = buildTempAreas(closedAreaClosures_,
                                      TempAreaType::ClosedArea,
                                      QStringLiteral("closed area"));
    restrictedTempAreas_ = buildTempAreas(restrictedAreaClosures_,
                                          TempAreaType::RestrictedArea,
                                          QStringLiteral("restricted area"));
}

QVector<TempArea> RunwayClosureCache::buildTempAreas(
    const QList<ClosedAreaClosure>& closures,
    TempAreaType type,
    const QString& logLabel) const {
    QSet<int> closedTwyIndices;

    for (const ClosedAreaClosure& closure : closures) {
        if (closure.btnFrom.isEmpty() && closure.btnTo.isEmpty()) {
            const auto it = exactTwyIndices_.constFind(closure.id);
            if (it == exactTwyIndices_.constEnd()) {
                qDebug().noquote() << "[asdex] no exact surface match for" << logLabel
                                   << closure.id;
                continue;
            }
            for (const int index : it.value()) closedTwyIndices.insert(index);
            continue;
        }

        const QJsonArray hits =
            getClosedTwysFromNotam(surfaceJson_, closure.id, closure.btnFrom, closure.btnTo);
        if (hits.isEmpty()) {
            qDebug().noquote() << "[asdex] no surface path for" << logLabel
                               << closure.id << "between" << closure.btnFrom
                               << "and" << closure.btnTo;
            continue;
        }

        for (const QJsonValue& value : hits) {
            const int index = value.toObject().value(QStringLiteral("index")).toInt(-1);
            if (index >= 0) closedTwyIndices.insert(index);
        }
    }

    QList<int> sortedIndices = closedTwyIndices.values();
    std::sort(sortedIndices.begin(), sortedIndices.end());

    QVector<TempArea> next;
    next.reserve(sortedIndices.size());
    for (const int index : sortedIndices) {
        if (index < 0
            || index >= twysByIndexFeet_.size()
            || twysByIndexFeet_.at(index).size() < 3) {
            continue;
        }

        TempArea area;
        area.id = index < twyIdsByIndex_.size() ? twyIdsByIndex_.at(index) : QString();
        area.type = type;
        area.polygonFeet = twysByIndexFeet_.at(index);
        next.push_back(std::move(area));
    }

    return next;
}

} // namespace asdex
