#ifndef PANES_PANES_H_
#define PANES_PANES_H_

#include <QPointF>
#include <QSize>

namespace renderer {
class Renderer;
}

namespace panes {

struct Context {
    renderer::Renderer* renderer = nullptr;
    QSize framebufferSize;
    QSize logicalSize;
    double devicePixelRatio = 1.0;
    double nowSeconds = 0.0;
};

struct MouseState {
    QPointF logicalPos;
    QPointF framebufferPos;
    bool leftDown = false;
    bool middleDown = false;
    bool rightDown = false;
};

} // namespace panes

#endif  // PANES_PANES_H_
