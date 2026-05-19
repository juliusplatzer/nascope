#ifndef PANES_DISPLAY_H_
#define PANES_DISPLAY_H_

#include <QOpenGLWidget>

namespace panes {

class Display : public QOpenGLWidget {
public:
    using QOpenGLWidget::QOpenGLWidget;
};

} // namespace panes

#endif  // PANES_DISPLAY_H_
