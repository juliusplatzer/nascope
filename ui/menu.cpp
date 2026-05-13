#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStyleFactory>
#include <QVBoxLayout>

#include "utils/resources.h"

#include <iostream>

namespace {

// With ~40px per item (28px min + padding + spacing), 5 items ≈ 200px popup.
constexpr int  kDropdownMaxVisible = 5;

// A dropdown's "state" drives its background:
//   past / current → black      rgb(33, 35, 41)
//   future         → mid-gray   rgb(104, 104, 112)
// Set via the "state" dynamic property; matched in the stylesheet.
constexpr char kStatePast[]    = "past";
constexpr char kStateCurrent[] = "current";
[[maybe_unused]] constexpr char kStateFuture[]  = "future";

QStringList loadAsdexAirports() {
    const QDir dir(utils::findProjectRelativeDir(QStringLiteral("resources/videomaps/asdex")));
    QStringList icaos;
    for (const QString& name : dir.entryList(QStringList{QStringLiteral("*.geojson.gz")},
                                             QDir::Files,
                                             QDir::Name)) {
        icaos << name.left(name.indexOf(QLatin1Char('.')));
    }
    return icaos;
}

QString executableSuffix() {
#ifdef Q_OS_WIN
    return QStringLiteral(".exe");
#else
    return QString();
#endif
}

QString findAsdexScopeExecutable() {
    const QString exeName = QStringLiteral("asdex_scope") + executableSuffix();
    const QDir appDir(QCoreApplication::applicationDirPath());

    const QStringList candidates = {
        appDir.filePath(QStringLiteral("asdex/%1").arg(exeName)),
        appDir.filePath(QStringLiteral("../asdex/%1").arg(exeName)),
        appDir.filePath(QStringLiteral("../../asdex/%1").arg(exeName)),
        QDir::current().filePath(QStringLiteral("build/asdex/%1").arg(exeName)),
    };

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) return QFileInfo(candidate).absoluteFilePath();
    }

    return exeName;
}

QLabel* makeLabel(const QString& text) {
    return new QLabel(text);
}

QString loadMenuStyleSheet() {
    QFile file(QStringLiteral(":/menu.css"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

void configureDropdown(QComboBox* combo, const char* state) {
    combo->setProperty("state", state);
    // Force a real QListView so stylesheet item padding applies.
    // Popup height is bounded by maxVisibleItems together with the
    // `combobox-popup: 0` property — native popups are disabled so
    // the widget can't paint gray slack above/below the list.
    combo->setView(new QListView(combo));
    combo->setMaxVisibleItems(kDropdownMaxVisible);
    combo->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

} // namespace

class Menu : public QDialog {
public:
    explicit Menu(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("nascope");
        setStyleSheet(loadMenuStyleSheet());

        auto* displayType = new QComboBox;
        displayType->addItems({"ASDE-X", "STARS"});
        displayType->setCurrentIndex(0);
        displayType->setEnabled(false);
        configureDropdown(displayType, kStatePast);

        facility_ = new QComboBox;
        facility_->addItems(loadAsdexAirports());
        configureDropdown(facility_, kStateCurrent);

        auto* confirm = new QPushButton("Confirm");
        auto* cancel  = new QPushButton("Cancel");
        confirm->setDefault(true);

        auto* actions = new QHBoxLayout;
        actions->addStretch();
        actions->addWidget(cancel);
        actions->addWidget(confirm);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(20, 20, 20, 16);
        root->addWidget(makeLabel("Display Type"));
        root->addWidget(displayType);
        root->addSpacing(15);
        root->addWidget(makeLabel("Facility"));
        root->addWidget(facility_);
        root->addStretch();
        root->addLayout(actions);

        connect(cancel,  &QPushButton::clicked, this, &QDialog::reject);
        connect(confirm, &QPushButton::clicked, this, &QDialog::accept);
    }

    QString selectedAirport() const { return facility_->currentText(); }

private:
    QComboBox* facility_ = nullptr;
};

int main(int argc, char** argv) {
    // Fusion ensures the stylesheet applies consistently (macOS native style ignores much of it).
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication app(argc, argv);

    Menu menu;
    if (menu.exec() != QDialog::Accepted) return 1;

    const QString airport = menu.selectedAirport();
    std::cout << airport.toStdString() << std::endl;

    if (QCoreApplication::arguments().contains(QStringLiteral("--select-only"))) return 0;

    const QString executable = findAsdexScopeExecutable();
    if (!QProcess::startDetached(executable, QStringList{airport}, QDir::currentPath())) {
        QMessageBox::critical(nullptr,
                              QStringLiteral("nascope"),
                              QStringLiteral("Could not launch ASDE-X renderer."));
        return 2;
    }

    return 0;
}
