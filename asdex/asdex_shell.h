#pragma once

#include <QMainWindow>
#include <QString>

namespace asdex {

class AsdexShell : public QMainWindow {
public:
    explicit AsdexShell(const QString& airport, QWidget* parent = nullptr);
};

} // namespace asdex
