#include "asdex/notams/runway_closure_cache.h"

#include "asdex/math.h"

#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLineF>
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
constexpr double kTaxiwayAdjacencyToleranceM = 2.0;
constexpr double kRunwayEndpointToleranceM = 10.0;
constexpr double kEarthRadiusM = 6371008.8;

struct LocalMeterProjector {
    double lon0 = 0.0;
    double lat0 = 0.0;
    double cosLat0 = 1.0;

    QPointF map(double lon, double lat) const {
        const double degToRad = M_PI / 180.0;
        return QPointF((lon - lon0) * degToRad * kEarthRadiusM * cosLat0,
                       (lat - lat0) * degToRad * kEarthRadiusM);
    }
};

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

bool chooseTaxiwayProjector(const QJsonArray& twys, LocalMeterProjector* projector) {
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

    projector->lon0 = lonSum / count;
    projector->lat0 = latSum / count;
    projector->cosLat0 = std::cos(projector->lat0 * M_PI / 180.0);
    return true;
}

QPolygonF projectedPolygon(const QJsonArray& points, const LocalMeterProjector& projector) {
    QPolygonF polygon;
    polygon.reserve(points.size());

    for (const QJsonValue& pointValue : points) {
        const QJsonArray point = pointValue.toArray();
        if (point.size() < 2) continue;
        polygon << projector.map(point.at(0).toDouble(), point.at(1).toDouble());
    }

    return polygon;
}

QPointF polygonCentroid(const QPolygonF& polygon) {
    double twiceArea = 0.0;
    double cx = 0.0;
    double cy = 0.0;

    for (int i = 0; i < polygon.size(); ++i) {
        const QPointF a = polygon.at(i);
        const QPointF b = polygon.at((i + 1) % polygon.size());
        const double cross = a.x() * b.y() - b.x() * a.y();
        twiceArea += cross;
        cx += (a.x() + b.x()) * cross;
        cy += (a.y() + b.y()) * cross;
    }

    if (std::abs(twiceArea) > 1e-9) {
        return QPointF(cx / (3.0 * twiceArea), cy / (3.0 * twiceArea));
    }

    QPointF average;
    for (const QPointF& point : polygon) average += point;
    return polygon.isEmpty() ? QPointF() : average / polygon.size();
}

bool bboxesNear(const QRectF& a, const QRectF& b, double toleranceM) {
    return !(a.right() + toleranceM < b.left()
          || b.right() + toleranceM < a.left()
          || a.bottom() + toleranceM < b.top()
          || b.bottom() + toleranceM < a.top());
}

double pointSegmentDistance(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF ab = b - a;
    const double lenSq = QPointF::dotProduct(ab, ab);
    if (lenSq <= 0.0) return std::hypot(p.x() - a.x(), p.y() - a.y());

    const double t = std::clamp(QPointF::dotProduct(p - a, ab) / lenSq, 0.0, 1.0);
    const QPointF q = a + ab * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

double segmentDistance(const QPointF& a1,
                       const QPointF& a2,
                       const QPointF& b1,
                       const QPointF& b2) {
    QPointF ignored;
    if (QLineF(a1, a2).intersects(QLineF(b1, b2), &ignored) == QLineF::BoundedIntersection) {
        return 0.0;
    }

    return std::min({pointSegmentDistance(a1, b1, b2),
                     pointSegmentDistance(a2, b1, b2),
                     pointSegmentDistance(b1, a1, a2),
                     pointSegmentDistance(b2, a1, a2)});
}

double polygonDistance(const QPolygonF& a, const QPolygonF& b) {
    if (a.isEmpty() || b.isEmpty()) return std::numeric_limits<double>::infinity();
    if (a.containsPoint(b.first(), Qt::OddEvenFill)
        || b.containsPoint(a.first(), Qt::OddEvenFill)) {
        return 0.0;
    }

    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < a.size(); ++i) {
        const QPointF a1 = a.at(i);
        const QPointF a2 = a.at((i + 1) % a.size());
        for (int j = 0; j < b.size(); ++j) {
            const double d = segmentDistance(a1, a2, b.at(j), b.at((j + 1) % b.size()));
            best = std::min(best, d);
            if (best <= 0.0) return 0.0;
        }
    }
    return best;
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
                                       const LocalMeterProjector& projector) {
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
            if (!bboxesNear(node.boundsM, holdbar.boundingRect(), kRunwayEndpointToleranceM)) {
                continue;
            }
            if (polygonDistance(node.geomM, holdbar) <= kRunwayEndpointToleranceM) {
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
                                 const LocalMeterProjector& projector) {
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

    LocalMeterProjector projector;
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
        node.centroidM = polygonCentroid(geom);
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
            if (!bboxesNear(nodes.at(i).boundsM, nodes.at(j).boundsM, kTaxiwayAdjacencyToleranceM))
                continue;
            if (polygonDistance(nodes.at(i).geomM, nodes.at(j).geomM) > kTaxiwayAdjacencyToleranceM)
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
    connect(&refreshTimer_, &QTimer::timeout, this, &RunwayClosureCache::refreshNow);
    connect(&process_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &RunwayClosureCache::handleFinished);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        qWarning().noquote() << "[asdex] NOTAM scraper process error:" << icao_ << error;
    });

    refreshTimer_.start();
    QTimer::singleShot(0, this, &RunwayClosureCache::refreshNow);
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

    const QTransform toFeet = lonLatToFeet(anchorLonLat);
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
    process_.start();
}

void RunwayClosureCache::handleFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    const QByteArray stdoutBytes = process_.readAllStandardOutput();
    const QByteArray stderrBytes = process_.readAllStandardError();
    if (!stderrBytes.trimmed().isEmpty()) {
        qInfo().noquote() << QString::fromUtf8(stderrBytes).trimmed();
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
