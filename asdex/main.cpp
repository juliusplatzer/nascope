#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>

#include "scope.h"
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

    asdex::VideoMap map = asdex::VideoMap::load(pos.first());
    if (!map.isValid()) {
        qCritical().noquote() << "no videomap for" << pos.first();
        return 3;
    }

    asdex::Scope scope(std::move(map));
    scope.show();
    return app.exec();
}
