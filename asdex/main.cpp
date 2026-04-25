#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>

#include "scope.h"
#include "tgtcache.h"
#include "videomaps.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("nascope ASDE-X scope");
    parser.addHelpOption();
    parser.addPositionalArgument("icao", "ICAO code of the airport videomap to render.");
    parser.process(app);

    const QStringList pos = parser.positionalArguments();
    if (pos.isEmpty()) {
        qCritical().noquote() << "usage: scope <ICAO>";
        return 2;
    }

    const QString icao = pos.first();
    asdex::VideoMap map = asdex::VideoMap::load(icao);
    if (!map.isValid()) {
        qCritical().noquote() << "no videomap for" << icao;
        return 3;
    }

    asdex::TgtCache cache(icao);
    asdex::Scope    scope(std::move(map), &cache);
    scope.show();
    return app.exec();
}
