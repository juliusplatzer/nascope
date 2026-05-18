#include "asdex/window.h"

#include "asdex/asdex.h"

namespace asdex {

AsdexShell::AsdexShell(const QString& airport, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("nascope - ASDE-X - %1").arg(airport));
    resize(1280, 800);
    setCentralWidget(new Asdex(airport, this));
}

} // namespace asdex
