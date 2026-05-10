#include "asdex/notams/runway_closure_cache.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

#include <utility>

namespace asdex {
namespace {

constexpr int kRefreshIntervalMs = 10 * 60 * 1000;

QSet<QString> runwaySetFromJson(const QJsonArray& values) {
    QSet<QString> out;
    for (const QJsonValue& value : values) {
        const QString runway = value.toString().trimmed().toUpper();
        if (!runway.isEmpty()) out.insert(runway);
    }
    return out;
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
    emit changed();
    refreshNow();
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

    const QSet<QString> next =
        runwaySetFromJson(document.object().value(QStringLiteral("rwyClosures")).toArray());
    if (next == closedRunways_) return;

    closedRunways_ = next;
    qInfo().noquote() << "[asdex] runway closures" << icao_
                      << QStringList(closedRunways_.values()).join(",");
    emit changed();
}

} // namespace asdex
