#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QStyleFactory>
#include <QVBoxLayout>

#include <iostream>

namespace {

constexpr char kAsdexVideomapsDir[] = "resources/videomaps/asdex";
// With ~40px per item (28px min + padding + spacing), 5 items ≈ 200px popup.
constexpr int  kDropdownMaxVisible = 5;

// A dropdown's "state" drives its background:
//   past / current → black      rgb(33, 35, 41)
//   future         → mid-gray   rgb(104, 104, 112)
// Set via the "state" dynamic property; matched in the stylesheet.
constexpr char kStatePast[]    = "past";
constexpr char kStateCurrent[] = "current";
[[maybe_unused]] constexpr char kStateFuture[]  = "future";

constexpr char kStyleSheet[] = R"(
QDialog {
    background-color: rgb(55, 57, 68);
}

QLabel {
    color: rgb(220, 220, 220);
    font-weight: 600;
    font-size: 13px;
}

QComboBox {
    background-color: rgb(33, 35, 41);
    color: rgb(230, 230, 230);
    border: 1px solid rgb(159, 160, 162);
    border-radius: 5px;
    padding: 6px 10px;
    min-height: 20px;
    combobox-popup: 0;
    font-size: 13px;
}
QComboBox:disabled {
    color: rgb(160, 160, 160);
}
QComboBox[state="future"] {
    background-color: rgb(104, 104, 112);
}
QComboBox::drop-down {
    border: none;
    width: 24px;
    subcontrol-origin: padding;
    subcontrol-position: center right;
}
QComboBox::down-arrow {
    image: url(:/chevron.svg);
    width: 10px;
    height: 6px;
}

QComboBox QAbstractItemView {
    background-color: rgb(33, 35, 41);
    color: rgb(230, 230, 230);
    border: 1px solid rgb(159, 160, 162);
    border-radius: 5px;
    outline: 0;
    selection-background-color: rgb(70, 72, 82);
    selection-color: white;
    font-size: 13px;
}
QComboBox QAbstractItemView::item {
    min-height: 28px;
    padding: 0px 10px;
}

QPushButton {
    background-color: rgb(33, 35, 41);
    color: rgb(230, 230, 230);
    border: 1px solid rgb(159, 160, 162);
    border-radius: 5px;
    padding: 6px 16px;
    font-size: 13px;
}
QPushButton:hover    { background-color: rgb(45, 48, 56); }
QPushButton:default  { border-color: rgb(200, 200, 205); }
)";

QStringList loadAsdexAirports() {
    QDir dir(kAsdexVideomapsDir);
    QStringList icaos;
    for (const QString& name : dir.entryList(QStringList{"*.geojson.gz"}, QDir::Files, QDir::Name)) {
        icaos << name.left(name.indexOf('.'));
    }
    return icaos;
}

QLabel* makeLabel(const QString& text) {
    return new QLabel(text);
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
        setStyleSheet(kStyleSheet);

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
    std::cout << menu.selectedAirport().toStdString() << std::endl;
    return 0;
}
