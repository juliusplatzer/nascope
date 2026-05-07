#include "asdex/asdex_shell.h"

#include "renderer/asdex_scope_widget.h"

namespace asdex {

AsdexShell::AsdexShell(const QString& airport, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("nascope - ASDE-X - %1").arg(airport));
    resize(1280, 800);
    setCentralWidget(new renderer::AsdexScopeWidget(airport, this));
}

} // namespace asdex
