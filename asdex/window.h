#ifndef ASDEX_WINDOW_H_
#define ASDEX_WINDOW_H_

#include <QMainWindow>
#include <QString>

namespace asdex {

class AsdexShell : public QMainWindow {
public:
    explicit AsdexShell(const QString& airport, QWidget* parent = nullptr);
};

} // namespace asdex

#endif  // ASDEX_WINDOW_H_
