#include "scope.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace asdex {

namespace {

constexpr double kViewportMargin = 0.04;  // 4 % padding around the videomap
constexpr double kMinZoom        = 0.2;
constexpr double kMaxZoom        = 50.0;
// One standard wheel notch = 120 angleDelta units = 1 step → 1.15× zoom.
// Qt also feeds angleDelta for trackpads, so this scales linearly across both.
constexpr double kZoomPerStep    = 1.15;

QColor backgroundFor(Mode m) {
    // sColorBackgroundDay / sColorBackgroundNight
    return (m == Mode::Day) ? QColor(0, 96, 120) : QColor(60, 60, 60);
}

/** Equirectangular projection with latitude cosine correction, fitted to size. */
QTransform geoToScreen(const QRectF& geo, const QSize& view) {
    if (geo.isEmpty() || view.isEmpty()) return {};

    const double cx     = geo.center().x();
    const double cy     = geo.center().y();
    const double cosLat = std::cos(cy * M_PI / 180.0);

    const double geoW = geo.width()  * cosLat;
    const double geoH = geo.height();

    const double availW = view.width()  * (1.0 - 2.0 * kViewportMargin);
    const double availH = view.height() * (1.0 - 2.0 * kViewportMargin);
    const double scale  = std::min(availW / geoW, availH / geoH);

    QTransform t;
    t.translate(view.width() / 2.0, view.height() / 2.0);
    t.scale(scale * cosLat, -scale);  // flip y so higher latitudes paint upward
    t.translate(-cx, -cy);
    return t;
}

} // namespace

Scope::Scope(VideoMap map, QWidget* parent) : QWidget(parent), map_(std::move(map)) {
    setWindowTitle(QStringLiteral("nascope — ASDE-X"));
    resize(1280, 800);
    setAutoFillBackground(true);
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    applyBackground();
}

void Scope::setMode(Mode m) {
    if (mode_ == m) return;
    mode_ = m;
    applyBackground();
    update();
}

void Scope::applyBackground() {
    QPalette pal = palette();
    pal.setColor(QPalette::Window, backgroundFor(mode_));
    setPalette(pal);
}

// ---- Paint ------------------------------------------------------------------

void Scope::paintEvent(QPaintEvent*) {
    if (!map_.isValid()) return;

    // Compose: screen = pan_ + zoom_ * baseFit(geo)
    //   Qt post-multiplies with .translate/.scale, so the sequence below
    //   builds (T_pan * S_zoom) in column-vector convention; the explicit
    //   right-multiply by `base` then produces (T_pan * S_zoom) * base,
    //   i.e. base is applied to the point first — correct.
    QTransform user;
    user.translate(pan_.x(), pan_.y());
    user.scale(zoom_, zoom_);
    const QTransform final = user * geoToScreen(map_.geoBounds(), size());

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(rect());  // belt-and-braces against off-screen path overflow
    map_.render(p, final, mode_);
}

// ---- Pan (right-click drag) -------------------------------------------------

void Scope::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::RightButton) {
        panning_    = true;
        lastPanPos_ = ev->position();
        setCursor(Qt::ClosedHandCursor);
        ev->accept();
        return;
    }
    QWidget::mousePressEvent(ev);
}

void Scope::mouseMoveEvent(QMouseEvent* ev) {
    if (!panning_) { QWidget::mouseMoveEvent(ev); return; }
    const QPointF now = ev->position();
    pan_       += (now - lastPanPos_);
    lastPanPos_ = now;
    ev->accept();
    update();
}

void Scope::mouseReleaseEvent(QMouseEvent* ev) {
    if (panning_ && ev->button() == Qt::RightButton) {
        panning_ = false;
        unsetCursor();
        ev->accept();
        return;
    }
    QWidget::mouseReleaseEvent(ev);
}

// ---- Zoom (wheel / trackpad scroll) ----------------------------------------

void Scope::wheelEvent(QWheelEvent* ev) {
    // Ignore macOS trackpad momentum: those events fire after the user's
    // fingers have left the pad and otherwise keep zooming silently.
    if (ev->phase() == Qt::ScrollMomentum) { ev->ignore(); return; }

    const int dy = ev->angleDelta().y();
    if (dy == 0) { ev->ignore(); return; }

    const double steps   = static_cast<double>(dy) / 120.0;
    const double newZoom = std::clamp(zoom_ * std::pow(kZoomPerStep, steps), kMinZoom, kMaxZoom);
    const double ratio   = newZoom / zoom_;
    if (std::abs(ratio - 1.0) < 1e-9) { ev->accept(); return; }

    // Keep the geo point under the cursor fixed under the cursor after zoom.
    const QPointF c = ev->position();
    pan_  = c - (c - pan_) * ratio;
    zoom_ = newZoom;

    ev->accept();
    update();
}

} // namespace asdex
