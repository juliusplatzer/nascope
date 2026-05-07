#include "asdex/asdex_shell.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QSurfaceFormat>

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(0);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("nascope-asdex"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("nascope ASDE-X shell"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("airport"),
                                 QStringLiteral("ICAO airport code to load."));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) return 2;

    asdex::AsdexShell shell(args.first().toUpper());
    shell.show();
    return app.exec();
}
